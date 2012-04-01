/*
 Copyright (c) 2004-2011 NFG Net Facilities Group BV support@nfg.nl

  This program is free software; you can redistribute it and/or 
  modify it under the terms of the GNU General Public License 
  as published by the Free Software Foundation; either 
  version 2 of the License, or (at your option) any later 
  version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/

/**
 * \file dbmail-message.c
 *
 * implements DbmailMessage object
 */

#include "dbmail.h"

extern db_param_t _db_params;
#define DBPFX _db_params.pfx
#define DBMAIL_TEMPMBOX "INBOX"
#define THIS_MODULE "message"

/*
 * used for debugging message de/re-construction
 */
//#define dprint(fmt, args...) TRACE(TRACE_DEBUG, fmt, ##args); printf(fmt, ##args)

#ifndef dprint
#define dprint(fmt, args...) 
#endif

/*
 * _register_header
 *
 * register a message header in a ghashtable dictionary
 *
 */
static void _register_header(const char *header, const char *value, gpointer user_data);
static gboolean _header_cache(const char *header, const char *value, gpointer user_data);

static gboolean _header_insert(u64_t physmessage_id, u64_t headername_id, u64_t headervalue_id);
static int _header_name_get_id(const DbmailMessage *self, const char *header, u64_t *id);
static int _header_value_get_id(const char *value, const char *sortfield, const char *datefield, u64_t *id);

static DbmailMessage * _retrieve(DbmailMessage *self, const char *query_template);
static void _map_headers(DbmailMessage *self);
static int _message_insert(DbmailMessage *self, 
		u64_t user_idnr, 
		const char *mailbox, 
		const char *unique_id); 


/* general mime utils (missing from gmime?) */

unsigned find_end_of_header(const char *h)
{
	gchar c, p1 = 0, p2 = 0;
	unsigned i = 0;
	size_t l;

	assert(h);

	l  = strlen(h);

	while (h++ && i<=l) {
		i++;
		c = *h;
		if (c == '\n' && ((p1 == '\n') || (p1 == '\r' && p2 == '\n'))) {
			if (l > i) 
				i++;
			break;
		}
		p2 = p1;
		p1 = c;
	}
	return i;
}


gchar * g_mime_object_get_body(const GMimeObject *object)
{
	gchar *s = NULL, *b = NULL;
        unsigned i;
	size_t l;
	
	g_return_val_if_fail(object != NULL, NULL);

	s = g_mime_object_to_string(GMIME_OBJECT(object));
	assert(s);

	i = find_end_of_header(s);
	if (i >= strlen(s)) {
		g_free(s);
		return g_strdup("");
	}
	
	b = s+i;
	l = strlen(b);
	memmove(s,b,l);
	s[l] = '\0';
	s = g_realloc(s, l+1);

	return s;
}

static u64_t blob_exists(const char *buf, const char *hash)
{
	volatile u64_t id = 0;
	volatile u64_t id_old = 0;
	size_t l;
	assert(buf);
	C c; S s; R r;
	char blob_cmp[DEF_FRAGSIZE];
	memset(blob_cmp, 0, sizeof(blob_cmp));

	l = strlen(buf);
	c = db_con_get();
	TRY
		if (_db_params.db_driver == DM_DRIVER_ORACLE  && l > DM_ORA_MAX_BYTES_LOB_CMP) {
			db_begin_transaction(c);
			/** XXX Due to specific Oracle behavior and limitation of
			 * libzdb methods we can't perform direct comparision lob data
			 * with some constant more then 4000 chars. So the only way to
			 * avoid data duplication - insert record and check if it alread
			 * exists in table. If it exists - rollback the transaction */
			s = db_stmt_prepare(c, "INSERT INTO %smimeparts (hash, data, %ssize%s) VALUES (?, ?, ?)", 
				DBPFX, db_get_sql(SQL_ESCAPE_COLUMN), db_get_sql(SQL_ESCAPE_COLUMN));
			db_stmt_set_str(s, 1, hash);
			db_stmt_set_blob(s, 2, buf, l);
			db_stmt_set_int(s, 3, l);
			db_stmt_exec(s);
			id = db_get_pk(c, "mimeparts");
			s = db_stmt_prepare(c, "SELECT a.id, b.id FROM dbmail_mimeparts a INNER JOIN " 
					"%smimeparts b ON a.hash=b.hash AND DBMS_LOB.COMPARE(a.data, b.data) = 0 " 
					" AND a.id<>b.id AND b.id=?", DBPFX);
			db_stmt_set_u64(s,1,l);
			r = db_stmt_query(s);
			if (db_result_next(r))
				id_old = db_result_get_u64(r,0);			
			if (id_old) {
				//  BLOB already exists - rollback insert
				id = id_old;
				db_rollback_transaction(c);
			} else {
				db_commit_transaction(c);
			}
		} else {
			snprintf(blob_cmp, DEF_FRAGSIZE, db_get_sql(SQL_COMPARE_BLOB), "data");
			s = db_stmt_prepare(c,"SELECT id FROM %smimeparts WHERE hash=? AND %ssize%s=? AND %s", 
					DBPFX,db_get_sql(SQL_ESCAPE_COLUMN), db_get_sql(SQL_ESCAPE_COLUMN),
					blob_cmp);
			db_stmt_set_str(s,1,hash);
			db_stmt_set_u64(s,2,l);
			db_stmt_set_blob(s,3,buf,l);
			r = db_stmt_query(s);
			if (db_result_next(r))
				id = db_result_get_u64(r,0);
		}
	CATCH(SQLException)
		LOG_SQLERROR;
		if (_db_params.db_driver == DM_DRIVER_ORACLE) 
			db_rollback_transaction(c);
	FINALLY
		db_con_close(c);
	END_TRY;

	return id;
}

static u64_t blob_insert(const char *buf, const char *hash)
{
	C c; R r; S s;
	size_t l;
	volatile u64_t id = 0;
	char *frag = db_returning("id");

	assert(buf);
	l = strlen(buf);

	c = db_con_get();
	TRY
		db_begin_transaction(c);
		s = db_stmt_prepare(c, "INSERT INTO %smimeparts (hash, data, %ssize%s) VALUES (?, ?, ?) %s", 
				DBPFX, db_get_sql(SQL_ESCAPE_COLUMN), db_get_sql(SQL_ESCAPE_COLUMN), frag);
		db_stmt_set_str(s, 1, hash);
		db_stmt_set_blob(s, 2, buf, l);
		db_stmt_set_int(s, 3, l);
		if (_db_params.db_driver == DM_DRIVER_ORACLE) {
			db_stmt_exec(s);
			id = db_get_pk(c, "mimeparts");
		} else {
			r = db_stmt_query(s);
			id = db_insert_result(c,r);
		}
		db_commit_transaction(c);
	CATCH(SQLException)
		LOG_SQLERROR;
		db_rollback_transaction(c);
	FINALLY
		db_con_close(c);
	END_TRY;

	TRACE(TRACE_DEBUG,"inserted id [%llu]", id);
	g_free(frag);

	return id;
}

static int register_blob(DbmailMessage *m, u64_t id, gboolean is_header)
{
	C c; volatile gboolean t = FALSE;
	c = db_con_get();
	TRY
		db_begin_transaction(c);
		t = db_exec(c, "INSERT INTO %spartlists (physmessage_id, is_header, part_key, part_depth, part_order, part_id) "
				"VALUES (%llu,%d,%d,%d,%d,%llu)", DBPFX,
				dbmail_message_get_physid(m), is_header, m->part_key, m->part_depth, m->part_order, id);	
		db_commit_transaction(c);
	CATCH(SQLException)
		LOG_SQLERROR;
		db_rollback_transaction(c);
	FINALLY
		db_con_close(c);
	END_TRY;

	return t;
}

static u64_t blob_store(const char *buf)
{
	u64_t id;
	char *hash;

	if (! buf) return 0;

	hash = dm_get_hash_for_string(buf);

	if (! hash) return 0;

	// store this message fragment
	if ((id = blob_exists(buf, (const char *)hash)) != 0) {
		g_free(hash);
		return id;
	}

	if ((id = blob_insert(buf, (const char *)hash)) != 0) {
		g_free(hash);
		return id;
	}

	g_free(hash);
	
	return 0;
}

static int store_blob(DbmailMessage *m, const char *buf, gboolean is_header)
{
	u64_t id;

	if (! buf) return 0;

	if (is_header) {
		m->part_key++;
		m->part_order=0;
	}

	dprint("<blob is_header=\"%d\" part_depth=\"%d\" part_key=\"%d\" part_order=\"%d\">\n%s\n</blob>\n", 
			is_header, m->part_depth, m->part_key, m->part_order, buf);

	if (! (id = blob_store(buf)))
		return DM_EQUERY;

	// register this message fragment
	if (! register_blob(m, id, is_header))
		return DM_EQUERY;

	m->part_order++;

	return 0;

}

static GMimeContentType *find_type(const char *s)
{
	GMimeContentType *type = NULL;
	GString *header;
	char *rest, *h = NULL;
	int i=0;

	rest = g_strcasestr(s, "\nContent-type: ");
	if (! rest) {
		if ((g_strncasecmp(s, "Content-type: ", 14)) == 0)
			rest = (char *)s;
	}
	if (! rest) return NULL;

	header = g_string_new("");

	i = 0;
	while (rest[i]) {
		if (rest[i] == ':') break;
		i++;
	}
	i++;

	while (rest[i]) {
		if (((rest[i] == '\n') || (rest[i] == '\r')) && (!isspace(rest[i+1]))) {
			break;
		}
		g_string_append_c(header,rest[i++]);
	}
	h = header->str;
	g_strstrip(h);
	if (strlen(h))
		type = g_mime_content_type_new_from_string(h);
	g_string_free(header,TRUE);
	return type;
}

static char * find_boundary(const char *s)
{
	gchar *boundary;
	GMimeContentType *type = find_type(s);
	boundary = g_strdup(g_mime_content_type_get_parameter(type,"boundary"));
	g_object_unref(type);
	return boundary;
}


static DbmailMessage * _mime_retrieve(DbmailMessage *self)
{
	C c; R r;
	char *str = NULL, *internal_date = NULL;
	char *boundary = NULL;
	GMimeContentType *mimetype = NULL;
	char **blist = g_new0(char *,128);
	int prevdepth, depth = 0, order, row = 0, key = 1;
	volatile int t = FALSE;
	gboolean got_boundary = FALSE, prev_boundary = FALSE, is_header = TRUE, prev_header, finalized=FALSE;
	gboolean prev_is_message = FALSE, is_message = FALSE;
	GString *m = NULL, *n = NULL;
	const void *blob;
	field_t frag;

	assert(dbmail_message_get_physid(self));
	date2char_str("ph.internal_date", &frag);
	n = g_string_new("");
	g_string_printf(n,db_get_sql(SQL_ENCODE_ESCAPE), "data");

	c = db_con_get();
	TRY
		r = db_query(c, "SELECT l.part_key,l.part_depth,l.part_order,l.is_header,%s,%s "
			"FROM %smimeparts p "
			"JOIN %spartlists l ON p.id = l.part_id "
			"JOIN %sphysmessage ph ON ph.id = l.physmessage_id "
			"WHERE l.physmessage_id = %llu ORDER BY l.part_key,l.part_order ASC", 
			frag, n->str, DBPFX, DBPFX, DBPFX, dbmail_message_get_physid(self));
		
		m = g_string_new("");

		row = 0;
		while (db_result_next(r)) {
			int l;

			prevdepth	= depth;
			prev_header	= is_header;
			key		= db_result_get_int(r,0);
			depth		= db_result_get_int(r,1);
			order		= db_result_get_int(r,2);
			is_header	= db_result_get_bool(r,3);
			if (row == 0) 	internal_date = g_strdup(db_result_get(r,4));
			blob		= db_result_get_blob(r,5,&l);

			str 		= g_new0(char,l+1);
			str		= strncpy(str,blob,l);
			str[l]		= 0;
			if (is_header) {
				prev_boundary = got_boundary;
				prev_is_message = is_message;
				if ((mimetype = find_type(str))) {
					is_message = g_mime_content_type_is_type(mimetype, "message", "rfc822");
					g_object_unref(mimetype);
				}
			}

			got_boundary = FALSE;

			if (is_header && ((boundary = find_boundary(str)) != NULL)) {
				got_boundary = TRUE;
				dprint("<boundary depth=\"%d\">%s</boundary>\n", depth, boundary);
				if (blist[depth]) g_free(blist[depth]);
				blist[depth] = boundary;
			}

			if (prevdepth > depth && blist[depth]) {
				dprint("\n--%s at %d--\n", blist[depth], depth);
				g_string_append_printf(m, "\n--%s--\n", blist[depth]);
				g_free(blist[depth]);
				blist[depth] = NULL;
				finalized=TRUE;
			}

			if (depth>0 && blist[depth-1])
				boundary = (char *)blist[depth-1];

			if (is_header && (!prev_header || prev_boundary || (prev_header && depth>0 && !prev_is_message))) {
				dprint("\n--%s\n", boundary);
				g_string_append_printf(m, "\n--%s\n", boundary);
			}

			g_string_append(m, str);
			dprint("<part is_header=\"%d\" depth=\"%d\" key=\"%d\" order=\"%d\">\n%s\n</part>\n", 
				is_header, depth, key, order, str);

			if (is_header)
				g_string_append_printf(m,"\n");
			
			g_free(str);
			row++;
		}
	CATCH(SQLException)
		LOG_SQLERROR;
		t = DM_EQUERY;
	FINALLY
		db_con_close(c);
	END_TRY;

	if ((row == 0) || (t == DM_EQUERY)) return NULL;

	if (row > 2 && boundary && !finalized) {
		dprint("\n--%s-- final\n", boundary);
		g_string_append_printf(m, "\n--%s--\n", boundary);
		finalized=1;
	}

	if (row > 2 && depth > 0 && boundary && blist[0] && !finalized) {
		if (strcmp(blist[0],boundary)!=0) {
			dprint("\n--%s-- final\n", blist[0]);
			g_string_append_printf(m, "\n--%s--\n\n", blist[0]);
		} else
			g_string_append_printf(m, "\n");
	}
	
	self = dbmail_message_init_with_string(self,m);
	dbmail_message_set_internal_date(self, internal_date);
	g_free(internal_date);
	g_string_free(m,TRUE);
	g_string_free(n,TRUE);
	g_strfreev(blist);
	return self;
}

static gboolean store_mime_object(GMimeObject *parent, GMimeObject *object, DbmailMessage *m);

static int store_head(GMimeObject *object, DbmailMessage *m)
{
	int r;
	char *head = g_mime_object_get_headers(object);
	r = store_blob(m, head, 1);
	g_free(head);
	return r;
}

static int store_body(GMimeObject *object, DbmailMessage *m)
{
	int r;
	char *text = g_mime_object_get_body(object);
	if (! text) return 0;
	r = store_blob(m, text, 0);
	g_free(text);
	return r;
}

static gboolean store_mime_text(GMimeObject *object, DbmailMessage *m, gboolean skiphead)
{
	g_return_val_if_fail(GMIME_IS_OBJECT(object), TRUE);
	if (! skiphead && store_head(object, m) < 0) return TRUE;
	if(store_body(object, m) < 0) return TRUE;
	return FALSE;
}

static gboolean store_mime_multipart(GMimeObject *object, DbmailMessage *m, const GMimeContentType *content_type, gboolean skiphead)
{
	const char *boundary;
	int n, i, c;

	g_return_val_if_fail(GMIME_IS_OBJECT(object), TRUE);

	boundary = g_mime_content_type_get_parameter(GMIME_CONTENT_TYPE(content_type),"boundary");

	if (! skiphead && store_head(object,m) < 0) return TRUE;

	if (g_mime_content_type_is_type(GMIME_CONTENT_TYPE(content_type), "multipart", "*") &&
			store_blob(m, g_mime_multipart_get_preface((GMimeMultipart *)object), 0) < 0) return TRUE;

	if (boundary) {
		m->part_depth++;
		n = m->part_order;
		m->part_order=0;
	}

	c = g_mime_multipart_get_count((GMimeMultipart *)object);
	for (i=0; i<c; i++) {
		GMimeObject *part = g_mime_multipart_get_part((GMimeMultipart *)object, i);
		if (store_mime_object(object, part, m)) return TRUE;
	}

	if (boundary) {
		n++;
		m->part_depth--;
		m->part_order=n;
	}

	if (g_mime_content_type_is_type(GMIME_CONTENT_TYPE(content_type), "multipart", "*") &&
			store_blob(m, g_mime_multipart_get_postface((GMimeMultipart *)object), 0) < 0) return TRUE;


	return FALSE;
}

static gboolean store_mime_message(GMimeObject * object, DbmailMessage *m, gboolean skiphead)
{
	gboolean r;
	GMimeMessage *m2;

	if (! skiphead && store_head(object, m) < 0) return TRUE;

	m2 = g_mime_message_part_get_message(GMIME_MESSAGE_PART(object));

	if (GMIME_IS_MESSAGE(m2))
		r = store_mime_object(object, GMIME_OBJECT(m2), m);
	else // fall-back
		r = store_mime_text(object, m, TRUE);

	return r;
	
}

gboolean store_mime_object(GMimeObject *parent, GMimeObject *object, DbmailMessage *m)
{
	GMimeContentType *content_type;
	GMimeObject *mime_part;
	gboolean r = FALSE;
	gboolean skiphead = FALSE;

	TRACE(TRACE_DEBUG, "parent [%p], object [%p], message->content [%p]", parent, object, m->content);
	g_return_val_if_fail(GMIME_IS_OBJECT(object), TRUE);

	if (GMIME_IS_MESSAGE(object)) {
		dprint("\n<message>\n");

		if(store_head(object,m) < 0) return TRUE;

		// we need to skip the first (post-rfc822) mime-headers
		// of the mime_part because they are already included as
		// part of the rfc822 headers
		skiphead = TRUE;

		g_mime_header_list_set_stream (GMIME_MESSAGE(object)->mime_part->headers, NULL);
		mime_part = g_mime_message_get_mime_part((GMimeMessage *)object);
	} else
		mime_part = object;

	content_type = g_mime_object_get_content_type(mime_part);

	if (g_mime_content_type_is_type(content_type, "multipart", "*")) {
		r = store_mime_multipart((GMimeObject *)mime_part, m, content_type, skiphead);

	} else if (g_mime_content_type_is_type(content_type, "message","*")) {
		r = store_mime_message((GMimeObject *)mime_part, m, skiphead);

	} else if (g_mime_content_type_is_type(content_type, "text","*")) {
		if (GMIME_IS_MESSAGE(object)) {
			if(store_body(object,m) < 0) r = TRUE;
		} else {
			r = store_mime_text((GMimeObject *)mime_part, m, skiphead);
		}

	} else {
		r = store_mime_text((GMimeObject *)mime_part, m, skiphead);
	}

	if (GMIME_IS_MESSAGE(object)) {
		dprint("\n</message>\n");
	}

	return r;
}


gboolean dm_message_store(DbmailMessage *m)
{
	return store_mime_object(NULL, (GMimeObject *)m->content, m);
}


/* Useful for debugging. Uncomment if/when needed.
 *//*
static void dump_to_file(const char *filename, const char *buf)
{
	gint se;
	g_assert(filename);
	FILE *f = fopen(filename,"a");
	if (! f) {
		se=errno;
		TRACE(TRACE_DEBUG,"opening dumpfile failed [%s]", strerror(se));
		errno=se;
		return;
	}
	fprintf(f,"%s",buf);
	fclose(f);
}
*/


/*  \brief create a new empty DbmailMessage struct
 *  \return the DbmailMessage
 */

DbmailMessage * dbmail_message_new(void)
{
	DbmailMessage *self = g_new0(DbmailMessage,1);
	
	self->internal_date = time(NULL);
	self->envelope_recipient = g_string_new("");

	/* provide quick case-insensitive header name searches */
	self->header_name = g_tree_new((GCompareFunc)g_ascii_strcasecmp);

	/* provide quick case-sensitive header value searches */
	self->header_value = g_tree_new((GCompareFunc)strcmp);
	
	/* internal cache: header_dict[headername.name] = headername.id */
	self->header_dict = g_hash_table_new_full((GHashFunc)g_str_hash,
			(GEqualFunc)g_str_equal, (GDestroyNotify)g_free, (GDestroyNotify)g_free);
	
	dbmail_message_set_class(self, DBMAIL_MESSAGE);
	
	return self;
}

void dbmail_message_free(DbmailMessage *self)
{
	if (! self)
		return;

	if (self->headers) {
		g_relation_destroy(self->headers);
		self->headers = NULL;
	}
	if (self->content) {
		g_object_unref(self->content);
		self->content = NULL;
	}
	if (self->raw_content) {
		g_free(self->raw_content);
		self->raw_content = NULL;
	}
	if (self->charset) {
		g_free(self->charset);
		self->charset = NULL;
	}

	g_string_free(self->envelope_recipient,TRUE);
	g_hash_table_destroy(self->header_dict);
	g_tree_destroy(self->header_name);
	g_tree_destroy(self->header_value);
	
	if (self->tmp) fclose(self->tmp);
	self->id=0;
	g_free(self);
	self = NULL;
}


/* \brief set the type flag for this DbmailMessage
 * \param the DbmailMessage on which to set the flag
 * \param type flag is either DBMAIL_MESSAGE or DBMAIL_MESSAGE_PART
 * \return non-zero in case of error
 */
int dbmail_message_set_class(DbmailMessage *self, int klass)
{
	switch (klass) {
		case DBMAIL_MESSAGE:
		case DBMAIL_MESSAGE_PART:
			self->klass = klass;
			break;
		default:
			return 1;
			break;
	}		
	return 0;
			
}

/* \brief accessor for the type flag
 * \return the flag
 */
int dbmail_message_get_class(const DbmailMessage *self)
{
	return self->klass;
}

/* \brief initialize a previously created DbmailMessage using a GString
 * \param the empty DbmailMessage
 * \param GString *content contains the raw message
 * \return the filled DbmailMessage
 */
DbmailMessage * dbmail_message_init_with_string(DbmailMessage *self, const GString *str)
{
	GMimeObject *content;
	GMimeParser *parser;
	gchar *from = NULL;
	GMimeStream *stream;

	assert(self->content == NULL);

	stream = g_mime_stream_mem_new_with_buffer(str->str, str->len);
	parser = g_mime_parser_new_with_stream(stream);
	g_object_unref(stream);

	if (strncmp(str->str, "From ", 5) == 0) {
		/* don't use gmime's from scanner since body lines may begin with 'From ' */
		char *end;
		if ((end = g_strstr_len(str->str, 80, "\n"))) {
			size_t l = end - str->str;
			from = g_strndup(str->str, l);
			TRACE(TRACE_DEBUG, "From_ [%s]", from);
		}
	}

	content = GMIME_OBJECT(g_mime_parser_construct_message(parser));
	if (content) {
		dbmail_message_set_class(self, DBMAIL_MESSAGE);
		self->content = content;
		self->raw_content = dbmail_message_to_string(self);
		if (from) {
			dbmail_message_set_internal_date(self, from);
		}
		g_object_unref(parser);
	} else {
		content = GMIME_OBJECT(g_mime_parser_construct_part(parser));
		if (content) {
			dbmail_message_set_class(self, DBMAIL_MESSAGE_PART);
			self->content = content;
			self->raw_content = dbmail_message_to_string(self);
			g_object_unref(parser);
		}
	}

	if (from) g_free(from);
	_map_headers(self);
	return self;
}

static gboolean g_str_case_equal(gconstpointer a, gconstpointer b)
{
       return MATCH((const char *)a,(const char *)b);
}

static void _map_headers(DbmailMessage *self) 
{
	GMimeObject *part;
	assert(self->content);
	if (self->headers) g_relation_destroy(self->headers);

	self->headers = g_relation_new(2);

	g_relation_index(self->headers, 0, (GHashFunc)g_str_hash, (GEqualFunc)g_str_case_equal);
	g_relation_index(self->headers, 1, (GHashFunc)g_str_hash, (GEqualFunc)g_str_case_equal);

	if (GMIME_IS_MESSAGE(self->content)) {
		char *type = NULL;

		// gmime doesn't consider the content-type header to be a message-header so extract 
		// and register it separately
		part = g_mime_message_get_mime_part(GMIME_MESSAGE(self->content));
		if ((type = (char *)g_mime_object_get_header(part,"Content-Type"))!=NULL)
			_register_header("Content-Type",type, (gpointer)self);
	}

	g_mime_header_list_foreach(g_mime_object_get_header_list(GMIME_OBJECT(self->content)), _register_header, self);
}

static void _register_header(const char *header, const char *value, gpointer user_data)
{
	const char *hname, *hvalue;
	DbmailMessage *m = (DbmailMessage *)user_data;

	assert(header);
	assert(value);
	assert(m);

	if (! (hname = g_tree_lookup(m->header_name,header))) {
		g_tree_insert(m->header_name,(gpointer)header,(gpointer)header);
		hname = header;
	}

	if (! (hvalue = g_tree_lookup(m->header_value,value))) {
		g_tree_insert(m->header_value,(gpointer)value,(gpointer)value);
		hvalue = value;
	}
	
	if (m->headers && (! g_relation_exists(m->headers, hname, hvalue)))
		g_relation_insert(m->headers, hname, hvalue);
}

void dbmail_message_set_physid(DbmailMessage *self, u64_t physid)
{
	self->id = physid;
	self->physid = physid;
}

u64_t dbmail_message_get_physid(const DbmailMessage *self)
{
	return self->physid;
}

void dbmail_message_set_internal_date(DbmailMessage *self, char *internal_date)
{
	self->internal_date = time(NULL);
	if (internal_date && strlen(internal_date)) {
		time_t dt;
	        if ((dt = g_mime_utils_header_decode_date(
						internal_date,
						&(self->internal_date_gmtoff)))) {
			self->internal_date = dt;
		}
		TRACE(TRACE_DEBUG, "internal_date [%s] [%ld] offset [%d]",
				internal_date,
				self->internal_date,
				self->internal_date_gmtoff);
	}
}

/* thisyear is a workaround for some broken gmime version. */
gchar * dbmail_message_get_internal_date(const DbmailMessage *self, int thisyear)
{
	char *res;
	struct tm gmt;
	assert(self->internal_date);
	
	memset(&gmt,'\0', sizeof(struct tm));
	gmtime_r(&self->internal_date, &gmt);

	/* override if the date is not sane */
	if (thisyear && ((gmt.tm_year + 1900) > (thisyear + 1)))
		gmt.tm_year = thisyear - 1900;

	res = g_new0(char, TIMESTRING_SIZE+1);
	strftime(res, TIMESTRING_SIZE, "%Y-%m-%d %T", &gmt);

	return res;
}

void dbmail_message_set_envelope_recipient(DbmailMessage *self, const char *envelope_recipient)
{
	if (envelope_recipient)
		g_string_printf(self->envelope_recipient,"%s", envelope_recipient);
}

gchar * dbmail_message_get_envelope_recipient(const DbmailMessage *self)
{
	if (self->envelope_recipient->len > 0)
		return self->envelope_recipient->str;
	return NULL;
}

void dbmail_message_set_header(DbmailMessage *self, const char *header, const char *value)
{
	g_mime_object_set_header(GMIME_OBJECT(self->content), header, value);
	if (self->headers) _map_headers(self);
	if (self->raw_content) 
		g_free(self->raw_content);
	self->raw_content = dbmail_message_to_string(self);
}

const gchar * dbmail_message_get_header(const DbmailMessage *self, const char *header)
{
	return g_mime_object_get_header(GMIME_OBJECT(self->content), header);
}

GTuples * dbmail_message_get_header_repeated(const DbmailMessage *self, const char *header)
{
	const char *hname;
	if (! (hname = g_tree_lookup(self->header_name,header)))
		hname = header;
	return g_relation_select(self->headers, hname, 0);
}

GList * dbmail_message_get_header_addresses(DbmailMessage *message, const char *field_name)
{
	InternetAddressList *ialist;
	InternetAddress *ia;
	GList *result = NULL;
	const char *field_value;
	int i,j = 0;

	if (!message || !field_name) {
		TRACE(TRACE_WARNING, "received a NULL argument, this is a bug");
		return NULL; 
	}

	if ((field_value = dbmail_message_get_header(message, field_name)) == NULL)
		return NULL;
	
	TRACE(TRACE_INFO, "mail address parser looking at field [%s] with value [%s]", field_name, field_value);
	
	if ((ialist = internet_address_list_parse_string(field_value)) == NULL) {
		TRACE(TRACE_NOTICE, "mail address parser error parsing header field");
		return NULL;
	}

	i = internet_address_list_length(ialist);
	for (j=0; j<i; j++) {
		const char *a;
		ia = internet_address_list_get_address(ialist, j);
		if ((a = internet_address_mailbox_get_addr((InternetAddressMailbox *)ia)) != NULL) {;
			TRACE(TRACE_DEBUG, "mail address parser found [%s]", a);
			result = g_list_append(result, g_strdup(a));
		}
	}
	g_object_unref(ialist);

	TRACE(TRACE_DEBUG, "mail address parser found [%d] email addresses", g_list_length(result));

	return result;
}
char * dbmail_message_get_charset(DbmailMessage *self)
{
	assert(self && self->content);
	if (! self->charset)
		self->charset = message_get_charset((GMimeMessage *)self->content);
	return self->charset;
}

/* dump message(parts) to char ptrs */
gchar * dbmail_message_to_string(const DbmailMessage *self) 
{
	assert(self && self->content);
	return g_mime_object_to_string(GMIME_OBJECT(self->content));
}
gchar * dbmail_message_body_to_string(const DbmailMessage *self)
{
	assert(self && self->content);
	return g_mime_object_get_body(GMIME_OBJECT(self->content));
}

gchar * dbmail_message_hdrs_to_string(const DbmailMessage *self)
{
	gchar *h;
	unsigned i = 0;

	h = dbmail_message_to_string(self);
	i = find_end_of_header(h);
	h[i] = '\0';
	h = g_realloc(h, i+1);

	return h;
}

size_t dbmail_message_get_size(const DbmailMessage *self, gboolean crlf)
{
	char *s; size_t r;

	s = self->raw_content;

	r = strlen(s);

        if (crlf) {
		int i = 0;
		char curr = 0, prev = 0;
		while (s[i]) {
			curr = s[i];
			if (ISLF(curr) && (! ISCR(prev)))
				r++;
			prev = curr;
			i++;
		}
	}

	return r;
}

static DbmailMessage * _retrieve(DbmailMessage *self, const char *query_template)
{
	int l, row = 0;
	GString *m;
	INIT_QUERY;
	C c; R r;
	DbmailMessage *store;
	field_t frag;
	char *internal_date = NULL;
	gconstpointer blob;
	
	assert(dbmail_message_get_physid(self));
	
	store = self;

	if ((self = _mime_retrieve(self)))
		return self;

	/* 
	 * _mime_retrieve failed. Fall back to messageblks
	 * interface
	 */
	self = store;

	date2char_str("p.internal_date", &frag);
	snprintf(query, DEF_QUERYSIZE, query_template, frag, DBPFX, DBPFX, dbmail_message_get_physid(self));

	c = db_con_get();
	if (! (r = db_query(c, query))) {
		db_con_close(c);
		return NULL;
	}
	
	row = 0;
	m = g_string_new("");
	while (db_result_next(r)) {
		blob = db_result_get_blob(r,0,&l);
		char *str = g_new0(char,l+1);
		str = strncpy(str, blob, l);

		if (row == 0) internal_date = g_strdup(db_result_get(r,2));

		g_string_append_printf(m, "%s", str);
		g_free(str);
		row++;
	}
	db_con_close(c);
	
	self = dbmail_message_init_with_string(self,m);
	dbmail_message_set_internal_date(self, internal_date);

	if (internal_date) g_free(internal_date);
	g_string_free(m,TRUE);

	return self;
}

/*
 *
 * retrieve the message headers as a single mime object
 *
 */
static DbmailMessage * _fetch_head(DbmailMessage *self)
{
	const char *query_template = 	"SELECT b.messageblk, b.is_header, %s "
		"FROM %smessageblks b "
		"JOIN %sphysmessage p ON b.physmessage_id=p.id "
		"WHERE b.physmessage_id = %llu "
		"AND b.is_header = '1'";
	return _retrieve(self, query_template);

}

/*
 *
 * retrieve the full message
 *
 */
static DbmailMessage * _fetch_full(DbmailMessage *self) 
{
	const char *query_template = "SELECT b.messageblk, b.is_header, %s "
		"FROM %smessageblks b "
		"JOIN %sphysmessage p ON b.physmessage_id=p.id "
		"WHERE b.physmessage_id = %llu "
		"ORDER BY b.messageblk_idnr";
	return _retrieve(self, query_template);
}

/* \brief retrieve message
 * \param empty DbmailMessage
 * \param physmessage_id
 * \param filter (header-only or full message)
 * \return filled DbmailMessage
 */
DbmailMessage * dbmail_message_retrieve(DbmailMessage *self, u64_t physid, int filter)
{
	assert(physid);
	
	dbmail_message_set_physid(self, physid);
	
	switch (filter) {
		case DBMAIL_MESSAGE_FILTER_HEAD:
			self = _fetch_head(self);
			break;

		case DBMAIL_MESSAGE_FILTER_BODY:
		case DBMAIL_MESSAGE_FILTER_FULL:
			self = _fetch_full(self);
			break;
	}
	

	if ((!self) || (! self->content)) {
		TRACE(TRACE_ERR, "retrieval failed for physid [%llu]", physid);
		return NULL;
	}

	return self;
}


/* \brief store a temporary copy of a message.
 * \param 	filled DbmailMessage
 * \return 
 *     - -1 on error
 *     -  1 on success
 */
static int _update_message(DbmailMessage *self)
{
	u64_t size    = (u64_t)dbmail_message_get_size(self,FALSE);
	u64_t rfcsize = (u64_t)dbmail_message_get_size(self,TRUE);

	if (! db_update("UPDATE %sphysmessage SET messagesize = %llu, rfcsize = %llu WHERE id = %llu", 
			DBPFX, size, rfcsize, self->physid))
		return DM_EQUERY;

	if (! db_update("UPDATE %smessages SET status = %d WHERE message_idnr = %llu", 
			DBPFX, MESSAGE_STATUS_NEW, self->id))
		return DM_EQUERY;

	if (! dm_quota_user_inc(db_get_useridnr(self->id), size))
		return DM_EQUERY;

	return DM_SUCCESS;
}


int dbmail_message_store(DbmailMessage *self)
{
	u64_t user_idnr;
	char unique_id[UID_SIZE];
	int res = 0, i = 1, retry = 10, delay = 200;
	int step = 0;
	
	if (! auth_user_exists(DBMAIL_DELIVERY_USERNAME, &user_idnr)) {
		TRACE(TRACE_ERR, "unable to find user_idnr for user [%s]. Make sure this system user is in the database!", DBMAIL_DELIVERY_USERNAME);
		return DM_EQUERY;
	}
	
	create_unique_id(unique_id, user_idnr);

	while (i++ < retry) {
		if (step == 0) {
			/* create a message record */
			if(_message_insert(self, user_idnr, DBMAIL_TEMPMBOX, unique_id) < 0) {
				usleep(delay*i);
				continue;
			}
			step++;
		}
		if (step == 1) {
			/* update message meta-data and owner quota */
			if ((res = _update_message(self) < 0)) {
				usleep(delay*i);
				continue;
			}
			step++;
		}

		if (step == 2) {
			/* store the message mime-parts */
			if ((res = dm_message_store(self))) {
				TRACE(TRACE_WARNING,"Failed to store mimeparts");
				usleep(delay*i);
				continue;
			}
			step++;
		}

		if (step == 3) {
			/* store message headers */
			if ((res = dbmail_message_cache_headers(self)) < 0) {
				usleep(delay*i);
				continue;
			}

			dbmail_message_cache_envelope(self);

			step++;
		}
		
		/* ready */
		break;
	}

	return res;
}

static void insert_physmessage(DbmailMessage *self, C c)
{
	R r;
	char *internal_date = NULL, *frag;
	int thisyear;
	volatile u64_t id = 0;
	struct timeval tv;
	struct tm gmt;

	/* get the messages date, but override it if it's from the future */
	gettimeofday(&tv, NULL);
	localtime_r(&tv.tv_sec, &gmt);
	thisyear = gmt.tm_year + 1900;
	internal_date = dbmail_message_get_internal_date(self, thisyear);

	frag = db_returning("id");

	if (internal_date != NULL) {
		field_t to_date_str;
		char2date_str(internal_date, &to_date_str);
		g_free(internal_date);
		if (_db_params.db_driver == DM_DRIVER_ORACLE) 
			db_exec(c, "INSERT INTO %sphysmessage (internal_date) VALUES (%s) %s",
					DBPFX, &to_date_str, frag);
		else 
			r = db_query(c, "INSERT INTO %sphysmessage (internal_date) VALUES (%s) %s",
					DBPFX, &to_date_str, frag);
	} else {
		if (_db_params.db_driver == DM_DRIVER_ORACLE) 
			db_exec(c, "INSERT INTO %sphysmessage (internal_date) VALUES (%s) %s",
					DBPFX, db_get_sql(SQL_CURRENT_TIMESTAMP), frag);
		else
			r = db_query(c, "INSERT INTO %sphysmessage (internal_date) VALUES (%s) %s",
					DBPFX, db_get_sql(SQL_CURRENT_TIMESTAMP), frag);
	}

	g_free(frag);	

	if (_db_params.db_driver == DM_DRIVER_ORACLE)
		id = db_get_pk(c, "physmessage");
	else
		id = db_insert_result(c, r);

	if (! id) {
		TRACE(TRACE_ERR,"no physmessage_id [%llu]", id);
	} else {
		dbmail_message_set_physid(self, id);
		TRACE(TRACE_DEBUG,"new physmessage_id [%llu]", id);
	}
}

int _message_insert(DbmailMessage *self, 
		u64_t user_idnr, 
		const char *mailbox, 
		const char *unique_id)
{
	u64_t mailboxid;
	char *frag = NULL;
	C c; R r;
	volatile int t = 0;

	assert(unique_id);
	assert(mailbox);

	if (db_find_create_mailbox(mailbox, BOX_DEFAULT, user_idnr, &mailboxid) == -1)
		return -1;
	
	if (mailboxid == 0) {
		TRACE(TRACE_ERR, "mailbox [%s] could not be found!", mailbox);
		return -1;
	}

	/* insert a new physmessage entry */
	
	/* now insert an entry into the messages table */
	c = db_con_get();
	TRY
		db_begin_transaction(c);
		insert_physmessage(self, c);

		if (_db_params.db_driver == DM_DRIVER_ORACLE) {
				db_exec(c, "INSERT INTO "
						"%smessages(mailbox_idnr, physmessage_id, unique_id,"
						"recent_flag, status) "
						"VALUES (%llu, %llu, '%s', 1, %d)",
						DBPFX, mailboxid, dbmail_message_get_physid(self), unique_id,
						MESSAGE_STATUS_INSERT);
				self->id = db_get_pk(c, "messages");
		} else {
				frag = db_returning("message_idnr");
				r = db_query(c, "INSERT INTO "
						"%smessages(mailbox_idnr, physmessage_id, unique_id,"
						"recent_flag, status) "
						"VALUES (%llu, %llu, '%s', 1, %d) %s",
						DBPFX, mailboxid, dbmail_message_get_physid(self), unique_id,
						MESSAGE_STATUS_INSERT, frag);
				g_free(frag);
				self->id = db_insert_result(c, r);
		}
		TRACE(TRACE_DEBUG,"new message_idnr [%llu]", self->id);

		t = DM_SUCCESS;
		db_commit_transaction(c);
	CATCH(SQLException)
		LOG_SQLERROR;
		db_rollback_transaction(c);
		t = DM_EQUERY;
	FINALLY
		db_con_close(c);
	END_TRY;

	return t;
}

#define CACHE_WIDTH 255

void _message_cache_envelope_date(const DbmailMessage *self)
{
	time_t date = self->internal_date;
	char *value, *datefield, *sortfield;
	u64_t headervalue_id;
	u64_t headername_id;

	value = g_mime_utils_header_format_date(
			self->internal_date, 
			self->internal_date_gmtoff);

	sortfield = g_new0(char, CACHE_WIDTH+1);
	strftime(sortfield, CACHE_WIDTH, "%Y-%m-%d %H:%M:%S", gmtime(&date));

	if (self->internal_date_gmtoff)
		date += (self->internal_date_gmtoff * 36);

	datefield = g_new0(gchar, 20);
	strftime(datefield, 20, "%Y-%m-%d", gmtime(&date));

	_header_name_get_id(self, "Date", &headername_id);
	_header_value_get_id(value, sortfield, datefield, &headervalue_id);

	if (headervalue_id && headername_id)
		_header_insert(self->physid, headername_id, headervalue_id);

	g_free(value);
	g_free(sortfield);
	g_free(datefield);
}

int dbmail_message_cache_headers(const DbmailMessage *self)
{
	assert(self);
	assert(self->physid);

	if (! GMIME_IS_MESSAGE(self->content)) {
		TRACE(TRACE_ERR,"self->content is not a message");
		return -1;
	}

	/* 
	 * store all headers as-is, plus separate copies for
	 * searching and sorting
	 * 
	 * */
	g_tree_foreach(self->header_name,
			(GTraverseFunc)_header_cache, (gpointer)self);

	/* 
	 * if there is no Date: header, store the envelope's date
	 * 
	 * */
	if (! dbmail_message_get_header(self, "Date"))
		_message_cache_envelope_date(self);
	
	/* 
	 * not all messages have a references field or a in-reply-to field 
	 *
	 * */
	dbmail_message_cache_referencesfield(self);

	return DM_SUCCESS;
}



static int _header_name_get_id(const DbmailMessage *self, const char *header, u64_t *id)
{
	u64_t *tmp = NULL;
	gpointer cacheid;
	gchar *case_header, *safe_header, *frag;
	C c; R r; S s;
	volatile int t = FALSE;

	// rfc822 headernames are case-insensitive
	safe_header = g_ascii_strdown(header,-1);
	if ((cacheid = g_hash_table_lookup(self->header_dict, (gconstpointer)safe_header)) != NULL) {
		*id = *(u64_t *)cacheid;
		g_free(safe_header);
		return 1;
	}

	case_header = g_strdup_printf(db_get_sql(SQL_STRCASE),"headername");
	tmp = g_new0(u64_t,1);

	c = db_con_get();

	TRY
		db_begin_transaction(c);
		*tmp = 0;
		s = db_stmt_prepare(c, "SELECT id FROM %sheadername WHERE %s=?", DBPFX, case_header);
		db_stmt_set_str(s,1,safe_header);
		r = db_stmt_query(s);

		if (db_result_next(r)) {
			*tmp = db_result_get_u64(r,0);
		} else {
			db_con_clear(c);

			frag = db_returning("id");
			s = db_stmt_prepare(c, "INSERT %s INTO %sheadername (headername) VALUES (?) %s",
					db_get_sql(SQL_IGNORE), DBPFX, frag);
			g_free(frag);

			db_stmt_set_str(s,1,safe_header);

			if (_db_params.db_driver == DM_DRIVER_ORACLE) {
				db_stmt_exec(s);
				*tmp = db_get_pk(c, "headername");
			} else {
				r = db_stmt_query(s);
				*tmp = db_insert_result(c, r);
			}
		}
		t = TRUE;
		db_commit_transaction(c);

	CATCH(SQLException)
		LOG_SQLERROR;
		db_rollback_transaction(c);
		t = DM_EQUERY;
	FINALLY
		db_con_close(c);
	END_TRY;

	g_free(case_header);

	if (t == DM_EQUERY) {
		g_free(safe_header);
		g_free(tmp);
		return t;
	}

	*id = *tmp;
	g_hash_table_insert(self->header_dict, (gpointer)(safe_header), (gpointer)(tmp));
	return 1;
}

static u64_t _header_value_exists(C c, const char *value, const char *hash)
{
	R r; S s;
	u64_t id = 0;
	char blob_cmp[DEF_FRAGSIZE];
	memset(blob_cmp, 0, sizeof(blob_cmp));

	if (_db_params.db_driver == DM_DRIVER_ORACLE && strlen(value) > DM_ORA_MAX_BYTES_LOB_CMP) {
		/** Value greater then DM_ORA_MAX_BYTES_LOB_CMP will cause SQL exception */
		return 0;
	}
	db_con_clear(c);
	snprintf(blob_cmp, DEF_FRAGSIZE, db_get_sql(SQL_COMPARE_BLOB), "headervalue");

	s = db_stmt_prepare(c, "SELECT id FROM %sheadervalue WHERE hash=? AND %s", DBPFX, blob_cmp);
	db_stmt_set_str(s, 1, hash);
	db_stmt_set_blob(s, 2, value, strlen(value));

	r = db_stmt_query(s);
	if (db_result_next(r))
		id = db_result_get_u64(r,0);

	return id;

}

static u64_t _header_value_insert(C c, const char *value, const char *sortfield, const char *datefield, const char *hash)
{
	R r; S s;
	u64_t id = 0;
	char *frag;

	db_con_clear(c);

	frag = db_returning("id");
	if (datefield)
		s = db_stmt_prepare(c, "INSERT INTO %sheadervalue (hash, headervalue, sortfield, datefield) VALUES (?,?,?,?) %s", DBPFX, frag);
	else
		s = db_stmt_prepare(c, "INSERT INTO %sheadervalue (hash, headervalue, sortfield) VALUES (?,?,?) %s", DBPFX, frag);
	g_free(frag);

	db_stmt_set_str(s, 1, hash);
	db_stmt_set_blob(s, 2, value, strlen(value));
	db_stmt_set_str(s, 3, sortfield);
	if (datefield)
		db_stmt_set_str(s, 4, datefield);

	if (_db_params.db_driver == DM_DRIVER_ORACLE) {
		db_stmt_exec(s);
		id = db_get_pk(c, "headervalue");
	} else {
		r = db_stmt_query(s);
		id = db_insert_result(c, r);
	}
	TRACE(TRACE_DATABASE,"new headervalue.id [%llu]", id);

	return id;
}

static int _header_value_get_id(const char *value, const char *sortfield, const char *datefield, u64_t *id)
{
	u64_t tmp = 0;
	char *hash;

	C c;
	hash = dm_get_hash_for_string(value);
	if (! hash) return FALSE;

	c = db_con_get();
	TRY
		db_begin_transaction(c);
		if ((tmp = _header_value_exists(c, value, (const char *)hash)) != 0)
			*id = tmp;
		else if ((tmp = _header_value_insert(c, value, sortfield, datefield, (const char *)hash)) != 0)
			*id = tmp;
		db_commit_transaction(c);
	CATCH(SQLException)
		LOG_SQLERROR;
		db_rollback_transaction(c);
		*id = 0;
	FINALLY
		db_con_close(c);
	END_TRY;

	g_free(hash);

	return TRUE;
}

static gboolean _header_insert(u64_t physmessage_id, u64_t headername_id, u64_t headervalue_id)
{

	C c; S s; volatile gboolean t = TRUE;

	c = db_con_get();
	db_con_clear(c);
	TRY
		db_begin_transaction(c);
		s = db_stmt_prepare(c, "INSERT INTO %sheader (physmessage_id, headername_id, headervalue_id) VALUES (?,?,?)", DBPFX);
		db_stmt_set_u64(s, 1, physmessage_id);
		db_stmt_set_u64(s, 2, headername_id);
		db_stmt_set_u64(s, 3, headervalue_id);
		db_stmt_exec(s);
		db_commit_transaction(c);
	CATCH(SQLException)
		LOG_SQLERROR;
		db_rollback_transaction(c);
		t = FALSE;
	FINALLY
		db_con_close(c);
	END_TRY;
	
	return t;
}

static GString * _header_addresses(InternetAddressList *ialist)
{
	int i,j;
	InternetAddress *ia;
	GString *store = g_string_new("");

	i = internet_address_list_length(ialist);
	for (j=0; j<i; j++) {
		ia = internet_address_list_get_address(ialist, j);
		if(ia == NULL) break;

		if (internet_address_group_get_members((InternetAddressGroup *)ia)) {

			if (j>0) g_string_append(store, " ");

			GString *group;
			g_string_append_printf(store, "%s:", internet_address_get_name(ia));
			group = _header_addresses(internet_address_group_get_members((InternetAddressGroup *)ia));
			if (group->len > 0)
				g_string_append_printf(store, " %s", group->str);
			g_string_free(group, TRUE);
			g_string_append(store, ";");
		} else {

			if (j>0)
				g_string_append(store, ", ");

			const char *name = internet_address_get_name(ia);
			const char *addr = internet_address_mailbox_get_addr((InternetAddressMailbox *)ia);

			if (name)
				g_string_append_printf(store, "%s ", name);
			if (addr)
				g_string_append_printf(store, "%s%s%s", 
						name?"<":"", 
						addr,
						name?">":"");
		}
	}
	return store;
}

static gboolean _header_cache(const char UNUSED *key, const char *header, gpointer user_data)
{
	u64_t headername_id;
	u64_t headervalue_id;
	DbmailMessage *self = (DbmailMessage *)user_data;
	GTuples *values;
	unsigned char *raw;
	unsigned i;
	time_t date;
	volatile gboolean isaddr = 0, isdate = 0, issubject = 0;
	const char *charset = dbmail_message_get_charset(self);
	gchar *sortfield = NULL, *datefield = NULL;
	InternetAddressList *emaillist;
	InternetAddress *ia;

	/* skip headernames with spaces like From_ */
	if (strchr(header, ' '))
		return FALSE;

	TRACE(TRACE_DEBUG,"headername [%s]", header);

	if ((_header_name_get_id(self, header, &headername_id) < 0))
		return TRUE;

	if (g_ascii_strcasecmp(header,"From")==0)
		isaddr=1;
	else if (g_ascii_strcasecmp(header,"To")==0)
		isaddr=1;
	else if (g_ascii_strcasecmp(header,"Reply-to")==0)
		isaddr=1;
	else if (g_ascii_strcasecmp(header,"Cc")==0)
		isaddr=1;
	else if (g_ascii_strcasecmp(header,"Bcc")==0)
		isaddr=1;
	else if (g_ascii_strcasecmp(header,"Return-path")==0)
		isaddr=1;
	else if (g_ascii_strcasecmp(header,"Subject")==0)
		issubject=1;
	else if (g_ascii_strcasecmp(header,"Date")==0)
		isdate=1;

	values = g_relation_select(self->headers,header,0);

	for (i=0; i<values->len;i++) {
		char *value = NULL;
		raw = (unsigned char *)g_tuples_index(values,i,1);
		TRACE(TRACE_DEBUG,"raw header value [%s]", raw);

		value = dbmail_iconv_decode_field((const char *)raw, charset, isaddr);

		if ((! value) || (strlen(value) == 0)) {
			if (value) g_free(value);
			continue;
		}

		// Generate additional fields for SORT optimization
		if(isaddr) {
			GString *store;
			int i,j=0;
			emaillist = internet_address_list_parse_string(value);
			store = _header_addresses(emaillist);

			i = internet_address_list_length(emaillist);
			for (j=0; j<i; j++) {
	                        ia = internet_address_list_get_address(emaillist, j);
				if(ia == NULL) break;


				if(sortfield == NULL) {
					// Only the first email recipient is to be used for sorting - so save it now.
					const char *addr;
				       
					if (internet_address_group_get_members((InternetAddressGroup *)ia)) {
						addr = internet_address_get_name(ia);
						sortfield = g_strndup(addr ? addr : "", CACHE_WIDTH);
					} else {
						addr = internet_address_mailbox_get_addr((InternetAddressMailbox *)ia);
						gchar **parts = g_strsplit(addr, "@",2);
						sortfield = g_strndup(parts[0]?parts[0]:"", CACHE_WIDTH);
						g_strfreev(parts);
					}
				}
			}
			g_object_unref(emaillist);
			g_free(value);

			value = store->str;
			g_string_free(store, FALSE);
		}

		if(issubject) {
			char *t = dm_base_subject(value);
			sortfield = dbmail_iconv_str_to_db(t, charset);
			g_free(t);
		}

		if(isdate) {
			int offset;
			date = g_mime_utils_header_decode_date(value,&offset);
			sortfield = g_new0(char, CACHE_WIDTH+1);
			strftime(sortfield, CACHE_WIDTH, "%Y-%m-%d %H:%M:%S", gmtime(&date));

			date += (offset * 36); // +0200 -> offset 200
			datefield = g_new0(gchar,20);
			strftime(datefield,20,"%Y-%m-%d", gmtime(&date));

			TRACE(TRACE_DEBUG,"Date is [%s] offset [%d], datefield [%s]",
					value, offset, datefield);
		}

		if (! sortfield)
			sortfield = g_strndup(value, CACHE_WIDTH);

		if (strlen(sortfield) > CACHE_WIDTH)
			sortfield[CACHE_WIDTH-1] = '\0';

		/* Fetch header value id if exists, else insert, and return new id */
		_header_value_get_id(value, sortfield, datefield, &headervalue_id);

		g_free(value);

		/* Insert relation between physmessage, header name and header value */
		if (headervalue_id)
			_header_insert(self->physid, headername_id, headervalue_id);
		else
			TRACE(TRACE_INFO, "error inserting headervalue. skipping.");

		headervalue_id=0;

		g_free(sortfield); sortfield = NULL;
		g_free(datefield); datefield = NULL;
		emaillist=NULL;
		date=0;
	}
	
	g_tuples_destroy(values);
	return FALSE;
}

static void insert_field_cache(u64_t physid, const char *field, const char *value)
{
	gchar *clean_value;
	C c; S s;

	g_return_if_fail(value != NULL);

	/* field values are truncated to 255 bytes */
	clean_value = g_strndup(value,CACHE_WIDTH);

	c = db_con_get();
	TRY
		db_begin_transaction(c);
		s = db_stmt_prepare(c,"INSERT INTO %s%sfield (physmessage_id, %sfield) VALUES (?,?)", DBPFX, field, field);
		db_stmt_set_u64(s, 1, physid);
		db_stmt_set_str(s, 2, clean_value);
		db_stmt_exec(s);
		db_commit_transaction(c);
	CATCH(SQLException)
		LOG_SQLERROR;
		db_rollback_transaction(c);
		TRACE(TRACE_ERR, "insert %sfield failed [%s]", field, value);
	FINALLY
		db_con_close(c);
	END_TRY;
	g_free(clean_value);
}

#define DM_ADDRESS_TYPE_TO "To"
#define DM_ADDRESS_TYPE_CC "Cc"
#define DM_ADDRESS_TYPE_FROM "From"
#define DM_ADDRESS_TYPE_REPL "Reply-to"

void dbmail_message_cache_referencesfield(const DbmailMessage *self)
{
	GMimeReferences *refs, *head;
	GTree *tree;
	const char *referencesfield, *inreplytofield;
	char *field;

	referencesfield = (char *)dbmail_message_get_header(self,"References");
	inreplytofield = (char *)dbmail_message_get_header(self,"In-Reply-To");

	// Some clients will put parent in the in-reply-to header only and the grandparents and older in references
	field = g_strconcat(referencesfield, " ", inreplytofield, NULL);
	refs = g_mime_references_decode(field);
	g_free(field);

	if (! refs) {
		TRACE(TRACE_DEBUG, "reference_decode failed [%llu]", self->physid);
		return;
	}
	
	head = refs;
	tree = g_tree_new_full((GCompareDataFunc)dm_strcmpdata, NULL, NULL, NULL);
	
	while (refs->msgid) {
		if (! g_tree_lookup(tree,refs->msgid)) {
			insert_field_cache(self->physid, "references", refs->msgid);
			g_tree_insert(tree,refs->msgid,refs->msgid);
		}
		if (refs->next == NULL)
			break;
		refs = refs->next;
	}

	g_tree_destroy(tree);
	g_mime_references_clear(&head);
}
	
void dbmail_message_cache_envelope(const DbmailMessage *self)
{
	char *envelope = NULL;
	C c; S s;

	envelope = imap_get_envelope(GMIME_MESSAGE(self->content));

	c = db_con_get();
	TRY
		db_begin_transaction(c);
		s = db_stmt_prepare(c, "INSERT INTO %senvelope (physmessage_id, envelope) VALUES (?,?)", DBPFX);
		db_stmt_set_u64(s, 1, self->physid);
		db_stmt_set_str(s, 2, envelope);
		db_stmt_exec(s);
		db_commit_transaction(c);
	CATCH(SQLException)
		LOG_SQLERROR;
		db_rollback_transaction(c);
		TRACE(TRACE_ERR, "insert envelope failed [%s]", envelope);
	FINALLY
		db_con_close(c);
	END_TRY;

	g_free(envelope);
	envelope = NULL;
}

// 
// construct a new message where only sender, recipient, subject and 
// a body are known. The body can be any kind of charset. Make sure
// it's not pre-encoded (base64, quopri)
//
// TODO: support text/html

DbmailMessage * dbmail_message_construct(DbmailMessage *self, 
		const gchar *to, const gchar *from, 
		const gchar *subject, const gchar *body)
{
	GMimeMessage *message;
	GMimePart *mime_part;
	GMimeDataWrapper *content;
	GMimeStream *stream, *fstream;
	GMimeContentType *mime_type;
	GMimeContentEncoding encoding = GMIME_CONTENT_ENCODING_DEFAULT;
	GMimeFilter *filter = NULL;

	// FIXME: this could easily be expanded to allow appending
	// a new sub-part to an existing mime-part. But for now let's
	// require self to be a pristine (empty) DbmailMessage.
	g_return_val_if_fail(self->content==NULL, self);
	
	message = g_mime_message_new(TRUE);

	// determine the optimal encoding type for the body: how would gmime
	// encode this string. This will return either base64 or quopri.
	if (g_mime_utils_text_is_8bit((unsigned char *)body, strlen(body)))
		encoding = g_mime_utils_best_encoding((unsigned char *)body, strlen(body));

	// set basic headers
	TRACE(TRACE_DEBUG, "from: [%s] to: [%s] subject: [%s] body: [%s]", from, to, subject, body);
	g_mime_message_set_sender(message, from);
	g_mime_message_set_subject(message, subject);
	g_mime_message_add_recipient(message, GMIME_RECIPIENT_TYPE_TO, NULL, to);

	// construct mime-part
	mime_part = g_mime_part_new();
	
	// setup a stream-filter
	stream = g_mime_stream_mem_new();
	fstream = g_mime_stream_filter_new(stream);
	
	switch(encoding) {
		case GMIME_CONTENT_ENCODING_BASE64:
			filter = g_mime_filter_basic_new(GMIME_CONTENT_ENCODING_BASE64, TRUE);
			g_mime_stream_filter_add((GMimeStreamFilter *)fstream, filter);
			g_object_unref(filter);
			break;
		case GMIME_CONTENT_ENCODING_QUOTEDPRINTABLE:
			filter = g_mime_filter_basic_new(GMIME_CONTENT_ENCODING_QUOTEDPRINTABLE, TRUE);
			g_mime_stream_filter_add((GMimeStreamFilter *)fstream, filter);
			g_object_unref(filter);
			break;
		default:
			break;
	}
	
	// fill the stream and thus the mime-part
	g_mime_stream_write_string(fstream,body);
	g_object_unref(fstream);

	content = g_mime_data_wrapper_new_with_stream(stream, encoding);
	g_object_unref(stream);
	g_mime_part_set_content_object(mime_part, content);
	
	
	// Content-Type
	mime_type = g_mime_content_type_new("text","plain");
	g_mime_object_set_content_type((GMimeObject *)mime_part, mime_type);
	// We originally tried to use g_mime_charset_best to pick a charset,
	// but it regularly failed to choose utf-8 when utf-8 data was given to it.
	g_mime_object_set_content_type_parameter((GMimeObject *)mime_part, "charset", "utf-8");

	// Content-Transfer-Encoding
	switch(encoding) {
		case GMIME_CONTENT_ENCODING_BASE64:
			g_mime_object_set_header(GMIME_OBJECT(mime_part),"Content-Transfer-Encoding", "base64");
			break;
		case GMIME_CONTENT_ENCODING_QUOTEDPRINTABLE:
			g_mime_object_set_header(GMIME_OBJECT(mime_part),"Content-Transfer-Encoding", "quoted-printable");
			break;
		default:
			g_mime_object_set_header(GMIME_OBJECT(mime_part),"Content-Transfer-Encoding", "7bit");
			break;
	}

	// attach the mime-part to the mime-message
	g_mime_message_set_mime_part(message, (GMimeObject *)mime_part);
	g_object_unref(mime_part);

	// attach the message to the DbmailMessage struct
	self->content = (GMimeObject *)message;
	self->raw_content = dbmail_message_to_string(self);

	// cleanup
	return self;
}

static int get_mailbox_from_filters(DbmailMessage *message, u64_t useridnr, const char *mailbox, char *into, size_t into_n)
{
	int t = FALSE;
	u64_t anyone = 0;
	C c; R r;
			
	TRACE(TRACE_INFO, "default mailbox [%s]", mailbox);
	
	if (mailbox != NULL) return t;

	if (! auth_user_exists(DBMAIL_ACL_ANYONE_USER, &anyone))
		return t;

	c = db_con_get();

	TRY
		r = db_query(c, "SELECT f.mailbox,f.headername,f.headervalue FROM %sfilters f "
			"JOIN %sheadername n ON f.headername=n.headername "
			"JOIN %sheader h ON h.headername_id = n.id "
			"join %sheadervalue v on v.id=h.headervalue_id "
			"WHERE v.headervalue %s f.headervalue "
			"AND h.physmessage_id=%llu "
			"AND f.user_id in (%llu,%llu)", 
			DBPFX, DBPFX, DBPFX, DBPFX,
			db_get_sql(SQL_INSENSITIVE_LIKE),
			dbmail_message_get_physid(message), anyone, useridnr);
		if (db_result_next(r)) {
			const char *hn, *hv;
			strncpy(into, db_result_get(r,0), into_n);
			hn = db_result_get(r,1);
			hv = db_result_get(r,2);
			TRACE(TRACE_DEBUG, "match [%s: %s] file-into mailbox [%s]", hn, hv, into);
			t = TRUE;
		}
	
	CATCH(SQLException)
		LOG_SQLERROR;
	FINALLY
		db_con_close(c);
	END_TRY;

	return t;
}

/* Figure out where to deliver the message, then deliver it.
 * */
dsn_class_t sort_and_deliver(DbmailMessage *message,
		const char *destination, u64_t useridnr,
		const char *mailbox, mailbox_source_t source)
{
	int cancelkeep = 0;
	int reject = 0;
	dsn_class_t ret;
	field_t val;
	char *subaddress = NULL;

	/* Catch the brute force delivery right away.
	 * We skip the Sieve scripts, and down the call
	 * chain we don't check permissions on the mailbox. */
	if (source == BOX_BRUTEFORCE) {
		TRACE(TRACE_NOTICE, "Beginning brute force delivery for user [%llu] to mailbox [%s].",
				useridnr, mailbox);
		return sort_deliver_to_mailbox(message, useridnr, mailbox, source, NULL);
	}

	/* This is the only condition when called from pipe.c, actually. */
	if (! mailbox) {
		char into[1024];
		
		memset(into,0,sizeof(into));

		if (! (get_mailbox_from_filters(message, useridnr, mailbox, into, sizeof(into)))) {				
			mailbox = "INBOX";
			source = BOX_DEFAULT;
		} else {
			mailbox = into;
		}
	}

	TRACE(TRACE_INFO, "Destination [%s] useridnr [%llu], mailbox [%s], source [%d]",
			destination, useridnr, mailbox, source);
	
	/* Subaddress. */
	config_get_value("SUBADDRESS", "DELIVERY", val);
	if (strcasecmp(val, "yes") == 0) {
		int res;
		size_t sublen, subpos;
		res = find_bounded((char *)destination, '+', '@', &subaddress, &sublen, &subpos);
		if (res > 0 && sublen > 0) {
			/* We'll free this towards the end of the function. */
			mailbox = subaddress;
			source = BOX_ADDRESSPART;
			TRACE(TRACE_INFO, "Setting BOX_ADDRESSPART mailbox to [%s]", mailbox);
		}
	}

	/* Give Sieve access to the envelope recipient. */
	dbmail_message_set_envelope_recipient(message, destination);

	/* Sieve. */
	config_get_value("SIEVE", "DELIVERY", val);
	if (strcasecmp(val, "yes") == 0 && dm_sievescript_isactive(useridnr)) {
		TRACE(TRACE_INFO, "Calling for a Sieve sort");
		sort_result_t *sort_result = sort_process(useridnr, message, mailbox);
		if (sort_result) {
			cancelkeep = sort_get_cancelkeep(sort_result);
			reject = sort_get_reject(sort_result);
			sort_free_result(sort_result);
		}
	}

	/* Sieve actions:
	 * (m = must implement, s = should implement, e = extension)
	 * m Keep - implicit default action.
	 * m Discard - requires us to skip the default action.
	 * m Redirect - add to the forwarding list.
	 * s Fileinto - change the destination mailbox.
	 * s Reject - nope, sorry. we killed bounce().
	 * e Vacation - share with the auto reply code.
	 */

	if (cancelkeep) {
		// The implicit keep has been cancelled.
		// This may necessarily imply that the message
		// is being discarded -- dropped flat on the floor.
		ret = DSN_CLASS_OK;
		TRACE(TRACE_INFO, "Keep was cancelled. Message may be discarded.");
	} else {
		ret = sort_deliver_to_mailbox(message, useridnr, mailbox, source, NULL);
		TRACE(TRACE_INFO, "Keep was not cancelled. Message will be delivered by default.");
	}

	/* Might have been allocated by the subaddress calculation. NULL otherwise. */
	g_free(subaddress);

	/* Reject probably implies cancelkeep,
	 * but we'll not assume that and instead
	 * just test this as a separate block. */
	if (reject) {
		TRACE(TRACE_INFO, "Message will be rejected.");
		ret = DSN_CLASS_FAIL;
	}

	return ret;
}

dsn_class_t sort_deliver_to_mailbox(DbmailMessage *message,
		u64_t useridnr, const char *mailbox, mailbox_source_t source,
		int *msgflags)
{
	u64_t mboxidnr, newmsgidnr;
	field_t val;
	size_t msgsize = (u64_t)dbmail_message_get_size(message, FALSE);

	TRACE(TRACE_INFO,"useridnr [%llu] mailbox [%s]", useridnr, mailbox);

	if (db_find_create_mailbox(mailbox, source, useridnr, &mboxidnr) != 0) {
		TRACE(TRACE_ERR, "mailbox [%s] not found", mailbox);
		return DSN_CLASS_FAIL;
	}

	if (source == BOX_BRUTEFORCE) {
		TRACE(TRACE_INFO, "Brute force delivery; skipping ACL checks on mailbox.");
	} else {
		// Check ACL's on the mailbox. It must be read-write,
		// it must not be no_select, and it may require an ACL for
		// the user whose Sieve script this is, since it's possible that
		// we've looked up a #Public or a #Users mailbox.
		int permission;
		TRACE(TRACE_DEBUG, "Checking if we have the right to post incoming messages");
        
		MailboxState_T S = MailboxState_new(mboxidnr);
		permission = acl_has_right(S, useridnr, ACL_RIGHT_POST);
		MailboxState_free(&S);
		
		switch (permission) {
		case -1:
			TRACE(TRACE_NOTICE, "error retrieving right for [%llu] to deliver mail to [%s]",
					useridnr, mailbox);
			return DSN_CLASS_TEMP;
		case 0:
			// No right.
			TRACE(TRACE_NOTICE, "user [%llu] does not have right to deliver mail to [%s]",
					useridnr, mailbox);
			// Switch to INBOX.
			if (strcmp(mailbox, "INBOX") == 0) {
				// Except if we've already been down this path.
				TRACE(TRACE_NOTICE, "already tried to deliver to INBOX");
				return DSN_CLASS_FAIL;
			}
			return sort_deliver_to_mailbox(message, useridnr, "INBOX", BOX_DEFAULT, msgflags);
		case 1:
			// Has right.
			TRACE(TRACE_INFO, "user [%llu] has right to deliver mail to [%s]",
					useridnr, mailbox);
			break;
		default:
			TRACE(TRACE_ERR, "invalid return value from acl_has_right");
			return DSN_CLASS_FAIL;
		}
	}

	// if the mailbox already holds this message we're done
	GETCONFIGVALUE("suppress_duplicates", "DELIVERY", val);
	if (strcasecmp(val,"yes")==0) {
		const char *messageid = dbmail_message_get_header(message, "message-id");
		if ( messageid && ((db_mailbox_has_message_id(mboxidnr, messageid)) > 0) ) {
			TRACE(TRACE_INFO, "suppress_duplicate: [%s]", messageid);
			return DSN_CLASS_OK;
		}
	}

	// Ok, we have the ACL right, time to deliver the message.
	switch (db_copymsg(message->id, mboxidnr, useridnr, &newmsgidnr, TRUE)) {
	case -2:
		TRACE(TRACE_ERR, "error copying message to user [%llu],"
				"maxmail exceeded", useridnr);
		return DSN_CLASS_QUOTA;
	case -1:
		TRACE(TRACE_ERR, "error copying message to user [%llu]", 
				useridnr);
		return DSN_CLASS_TEMP;
	default:
		TRACE(TRACE_NOTICE, "message id=%llu, size=%zd is inserted", 
				newmsgidnr, msgsize);
		if (msgflags) {
			TRACE(TRACE_NOTICE, "message id=%llu, setting imap flags", 
				newmsgidnr);
			db_set_msgflag(newmsgidnr, msgflags, NULL, IMAPFA_ADD, NULL);
			db_mailbox_seq_update(mboxidnr);
		}
		message->id = newmsgidnr;
		return DSN_CLASS_OK;
	}
}

static int parse_and_escape(const char *in, char **out)
{
	InternetAddressList *ialist;
	InternetAddress *ia;
	const char *addr;

	TRACE(TRACE_DEBUG, "parsing address [%s]", in);
	ialist = internet_address_list_parse_string(in);
	if (!ialist) {
                TRACE(TRACE_NOTICE, "unable to parse email address [%s]", in);
                return -1;
	}

        ia = internet_address_list_get_address(ialist,0);
	addr = internet_address_mailbox_get_addr((InternetAddressMailbox *)ia);
        if (!ia || !addr) {
		TRACE(TRACE_NOTICE, "unable to parse email address [%s]", in);
		return -1;
	}

	if (! (*out = dm_shellesc(addr))) {
		TRACE(TRACE_ERR, "out of memory calling dm_shellesc");
		return -1;
	}

	return 0;
}
/* Sends a message. */
int send_mail(DbmailMessage *message,
		const char *to, const char *from,
		const char *preoutput,
		enum sendwhat sendwhat, char *sendmail_external)
{
	FILE *mailpipe = NULL;
	char *escaped_to = NULL;
	char *escaped_from = NULL;
	char *message_string = NULL;
	char *sendmail_command = NULL;
	field_t sendmail, postmaster;
	int result;

	if (!from || strlen(from) < 1) {
		if (config_get_value("POSTMASTER", "DBMAIL", postmaster) < 0) {
			TRACE(TRACE_NOTICE, "no config value for POSTMASTER");
		}
		if (strlen(postmaster))
			from = postmaster;
		else
			from = DEFAULT_POSTMASTER;
	}

	if (config_get_value("SENDMAIL", "DBMAIL", sendmail) < 0) {
		TRACE(TRACE_ERR, "error getting value for SENDMAIL in DBMAIL section of dbmail.conf.");
		return -1;
	}

	if (strlen(sendmail) < 1) {
		TRACE(TRACE_ERR, "SENDMAIL not set in DBMAIL section of dbmail.conf.");
		return -1;
	}

	if (!sendmail_external) {
		if (parse_and_escape(to, &escaped_to) < 0) {
			TRACE(TRACE_NOTICE, "could not prepare 'to' address.");
			return 1;
		}
		if (parse_and_escape(from, &escaped_from) < 0) {
			g_free(escaped_to);
			TRACE(TRACE_NOTICE, "could not prepare 'from' address.");
			return 1;
		}
		sendmail_command = g_strconcat(sendmail, " -i -f ", escaped_from, " ", escaped_to, NULL);
		g_free(escaped_to);
		g_free(escaped_from);
		if (!sendmail_command) {
			TRACE(TRACE_ERR, "out of memory calling g_strconcat");
			return -1;
		}
	} else {
		sendmail_command = sendmail_external;
	}

	TRACE(TRACE_INFO, "opening pipe to [%s]", sendmail_command);

	if (!(mailpipe = popen(sendmail_command, "w"))) {
		TRACE(TRACE_ERR, "could not open pipe to sendmail");
		g_free(sendmail_command);
		return 1;
	}

	TRACE(TRACE_DEBUG, "pipe opened");

	switch (sendwhat) {
	case SENDRAW:
		// This is a hack so forwards can give a From line.
		if (preoutput)
			fprintf(mailpipe, "%s\n", preoutput);
		// fall-through
	case SENDMESSAGE:
		message_string = dbmail_message_to_string(message);
		fprintf(mailpipe, "%s", message_string);
		g_free(message_string);
		break;
	default:
		TRACE(TRACE_ERR, "invalid sendwhat in call to send_mail: [%d]", sendwhat);
		break;
	}

	result = pclose(mailpipe);
	TRACE(TRACE_DEBUG, "pipe closed");

	/* Adapted from the Linux waitpid 2 man page. */
	if (WIFEXITED(result)) {
		result = WEXITSTATUS(result);
		TRACE(TRACE_INFO, "sendmail exited normally");
	} else if (WIFSIGNALED(result)) {
		result = WTERMSIG(result);
		TRACE(TRACE_INFO, "sendmail was terminated by signal");
	} else if (WIFSTOPPED(result)) {
		result = WSTOPSIG(result);
		TRACE(TRACE_INFO, "sendmail was stopped by signal");
	}

	if (result != 0) {
		TRACE(TRACE_ERR, "sendmail error return value was [%d]", result);

		if (!sendmail_external)
			g_free(sendmail_command);
		return 1;
	}

	if (!sendmail_external)
		g_free(sendmail_command);
	return 0;
} 

static int valid_sender(const char *addr) 
{
	int ret = 1;
	char *testaddr;
	testaddr = g_ascii_strdown(addr, -1);
	if (strstr(testaddr, "mailer-daemon@"))
		ret = 0;
	if (strstr(testaddr, "daemon@"))
		ret = 0;
	if (strstr(testaddr, "postmaster@"))
		ret = 0;
	g_free(testaddr);
	return ret;
}

#define REPLY_DAYS 7

static int check_destination(DbmailMessage *message, GList *aliases)
{
	GList *to, *cc, *recipients = NULL;
	to = dbmail_message_get_header_addresses(message, "To");
	cc = dbmail_message_get_header_addresses(message, "Cc");

	recipients = g_list_concat(to, cc);

	while (recipients) {
		char *addr = (char *)recipients->data;

		if (addr) {
			aliases = g_list_first(aliases);
			while (aliases) {
				char *alias = (char *)aliases->data;
				if (MATCH(alias, addr)) {
					TRACE(TRACE_DEBUG, "valid alias found as recipient [%s]", alias);
					return TRUE;
				}
				if (! g_list_next(aliases)) break;
				aliases = g_list_next(aliases);
			}
		}
		if (! g_list_next(recipients)) break;
		recipients = g_list_next(recipients);
	}

	g_list_destroy(recipients);
	return FALSE;
}


static int send_reply(DbmailMessage *message, const char *body, GList *aliases)
{
	const char *from, *to, *subject;
	const char *x_dbmail_reply;
	char *handle;
	int result;

	x_dbmail_reply = dbmail_message_get_header(message, "X-Dbmail-Reply");
	if (x_dbmail_reply) {
		TRACE(TRACE_INFO, "reply loop detected [%s]", x_dbmail_reply);
		return 0;
	}

	if (! check_destination(message, aliases)) {
		TRACE(TRACE_INFO, "no valid destination ");
		return 0;
	}

	subject = dbmail_message_get_header(message, "Subject");
	from = dbmail_message_get_header(message, "Delivered-To");

	if (!from)
		from = message->envelope_recipient->str;
	if (!from)
		from = ""; // send_mail will change this to DEFAULT_POSTMASTER

	to = dbmail_message_get_header(message, "Reply-To");
	if (!to)
		to = dbmail_message_get_header(message, "Return-Path");
	if (!to) {
		TRACE(TRACE_ERR, "no address to send to");
		return 0;
	}
	if (!valid_sender(to)) {
		TRACE(TRACE_DEBUG, "sender invalid. skip auto-reply.");
		return 0;
	}

	handle = dm_md5((const char * const)body);

	if (db_replycache_validate(to, from, handle, REPLY_DAYS) != DM_SUCCESS) {
		g_free(handle);
		TRACE(TRACE_DEBUG, "skip auto-reply");
		return 0;
	}

	char *newsubject = g_strconcat("Re: ", subject, NULL);

	DbmailMessage *new_message = dbmail_message_new();
	new_message = dbmail_message_construct(new_message, to, from, newsubject, body);
	dbmail_message_set_header(new_message, "X-DBMail-Reply", from);
	dbmail_message_set_header(new_message, "Precedence", "bulk");
	dbmail_message_set_header(new_message, "Auto-Submitted", "auto-replied");

	result = send_mail(new_message, to, from, NULL, SENDMESSAGE, SENDMAIL);

	if (result == 0) {
		db_replycache_register(to, from, handle);
	}

	g_free(handle);
	g_free(newsubject);
	dbmail_message_free(new_message);

	return result;
}

/*          
 *           * Send an automatic notification.
 *            */         
static int send_notification(DbmailMessage *message UNUSED, const char *to)
{           
	field_t from = "";
	field_t subject = "";
	int result;

	if (config_get_value("POSTMASTER", "DBMAIL", from) < 0) {
		TRACE(TRACE_INFO, "no config value for POSTMASTER");
	}   

	if (config_get_value("AUTO_NOTIFY_SENDER", "DELIVERY", from) < 0) {
		TRACE(TRACE_INFO, "no config value for AUTO_NOTIFY_SENDER");
	}   

	if (config_get_value("AUTO_NOTIFY_SUBJECT", "DELIVERY", subject) < 0) {
		TRACE(TRACE_INFO, "no config value for AUTO_NOTIFY_SUBJECT");
	}   

	if (strlen(from) < 1)
		g_strlcpy(from, AUTO_NOTIFY_SENDER, FIELDSIZE);

	if (strlen(subject) < 1)
		g_strlcpy(subject, AUTO_NOTIFY_SUBJECT, FIELDSIZE);

	DbmailMessage *new_message = dbmail_message_new();
	new_message = dbmail_message_construct(new_message, to, from, subject, "");

	result = send_mail(new_message, to, from, NULL, SENDMESSAGE, SENDMAIL);

	dbmail_message_free(new_message);

	return result;
}           


/* Yeah, RAN. That's Reply And Notify ;-) */
static int execute_auto_ran(DbmailMessage *message, u64_t useridnr)
{
	field_t val;
	int do_auto_notify = 0, do_auto_reply = 0;
	char *reply_body = NULL;
	char *notify_address = NULL;

	/* message has been successfully inserted, perform auto-notification & auto-reply */
	if (config_get_value("AUTO_NOTIFY", "DELIVERY", val) < 0) {
		TRACE(TRACE_ERR, "error getting config value for AUTO_NOTIFY");
		return -1;
	}

	if (strcasecmp(val, "yes") == 0)
		do_auto_notify = 1;

	if (config_get_value("AUTO_REPLY", "DELIVERY", val) < 0) {
		TRACE(TRACE_ERR, "error getting config value for AUTO_REPLY");
		return -1;
	}

	if (strcasecmp(val, "yes") == 0)
		do_auto_reply = 1;

	if (do_auto_notify) {
		TRACE(TRACE_DEBUG, "starting auto-notification procedure");

		if (db_get_notify_address(useridnr, &notify_address) != 0)
			TRACE(TRACE_ERR, "error fetching notification address");
		else {
			if (notify_address == NULL)
				TRACE(TRACE_DEBUG, "no notification address specified, skipping");
			else {
				TRACE(TRACE_DEBUG, "sending notification to [%s]", notify_address);
				if (send_notification(message, notify_address) < 0) {
					TRACE(TRACE_ERR, "error in call to send_notification.");
					g_free(notify_address);
					return -1;
				}
				g_free(notify_address);
			}
		}
	}

	if (do_auto_reply) {
		TRACE(TRACE_DEBUG, "starting auto-reply procedure");

		if (db_get_reply_body(useridnr, &reply_body) != 0)
			TRACE(TRACE_DEBUG, "no reply body found");
		else {
			if (reply_body == NULL || reply_body[0] == '\0')
				TRACE(TRACE_DEBUG, "no reply body specified, skipping");
			else {
				GList *aliases = auth_get_user_aliases(useridnr);
				if (send_reply(message, reply_body, aliases) < 0) {
					TRACE(TRACE_ERR, "error in call to send_reply");
					g_free(reply_body);
					return -1;
				}
				g_free(reply_body);
				
			}
		}
	}

	return 0;
}



int send_forward_list(DbmailMessage *message, GList *targets, const char *from)
{
	int result = 0;
	field_t postmaster;

	if (!from) {
		if (config_get_value("POSTMASTER", "DBMAIL", postmaster) < 0)
			TRACE(TRACE_NOTICE, "no config value for POSTMASTER");
		if (strlen(postmaster))
			from = postmaster;
		else
			from = DEFAULT_POSTMASTER;
	}
	targets = g_list_first(targets);
	TRACE(TRACE_INFO, "delivering to [%u] external addresses", g_list_length(targets));
	while (targets) {
		char *to = (char *)targets->data;

		if (!to || strlen(to) < 1) {
			TRACE(TRACE_ERR, "forwarding address is zero length, message not forwarded.");
		} else {
			if (to[0] == '!') {
				// The forward is a command to execute.
				// Prepend an mbox From line.
				char timestr[50];
				time_t td;
				struct tm tm;
				char *fromline;
                        
				time(&td);		/* get time */
				tm = *localtime(&td);	/* get components */
				strftime(timestr, sizeof(timestr), "%a %b %e %H:%M:%S %Y", &tm);
                        
				TRACE(TRACE_DEBUG, "prepending mbox style From header to pipe returnpath: %s", from);
                        
				/* Format: From<space>address<space><space>Date */
				fromline = g_strconcat("From ", from, "  ", timestr, NULL);

				result |= send_mail(message, "", "", fromline, SENDRAW, to+1);
				g_free(fromline);
			} else if (to[0] == '|') {
				// The forward is a command to execute.
				result |= send_mail(message, "", "", NULL, SENDRAW, to+1);

			} else {
				// The forward is an email address.
				result |= send_mail(message, to, from, NULL, SENDRAW, SENDMAIL);
			}
		}
		if (! g_list_next(targets))
			break;
		targets = g_list_next(targets);

	}

	return result;
}


/* Here's the real *meat* of this source file!
 *
 * Function: insert_messages()
 * What we get:
 *   - The message 
 *   - A list of destinations
 *
 * What we do:
 *   - Store the message
 *   - Process the destination addresses into lists:
 *     - Local useridnr's
 *     - External forwards
 *     - No such user bounces
 *   - Store the local useridnr's
 *     - Run the message through each user's sorting rules
 *     - Potentially alter the delivery:
 *       - Different mailbox
 *       - Bounce
 *       - Reply with vacation message
 *       - Forward to another address
 *     - Check the user's quota before delivering
 *       - Do this *after* their sorting rules, since the
 *         sorting rules might not store the message anyways
 *   - Send out the no such user bounces
 *   - Send out the external forwards
 *   - Delete the temporary message from the database
 * What we return:
 *   - 0 on success
 *   - -1 on full failure
 */

int insert_messages(DbmailMessage *message, GList *dsnusers)
{
	u64_t tmpid;
	int result=0;

 	delivery_status_t final_dsn;

	/* first start a new database transaction */

	if ((result = dbmail_message_store(message)) == DM_EQUERY) {
		TRACE(TRACE_ERR,"storing message failed");
		return result;
	} 

	TRACE(TRACE_DEBUG, "temporary msgidnr is [%llu]", message->id);

	tmpid = message->id; // for later removal

	// TODO: Run a Sieve script associated with the internal delivery user.
	// Code would go here, after we've stored the message 
	// before we've started delivering it

	/* Loop through the users list. */
	dsnusers = g_list_first(dsnusers);
	while (dsnusers) {
		
		GList *userids;

		int has_2 = 0, has_4 = 0, has_5 = 0, has_5_2 = 0;
		
		deliver_to_user_t *delivery = (deliver_to_user_t *) dsnusers->data;
		
		/* Each user may have a list of user_idnr's for local
		 * delivery. */
		userids = g_list_first(delivery->userids);
		while (userids) {
			u64_t *useridnr = (u64_t *) userids->data;

			TRACE(TRACE_DEBUG, "calling sort_and_deliver for useridnr [%llu]", *useridnr);
			switch (sort_and_deliver(message, delivery->address, *useridnr, delivery->mailbox, delivery->source)) {
			case DSN_CLASS_OK:
				TRACE(TRACE_INFO, "successful sort_and_deliver for useridnr [%llu]", *useridnr);
				has_2 = 1;
				break;
			case DSN_CLASS_FAIL:
				TRACE(TRACE_ERR, "permanent failure sort_and_deliver for useridnr [%llu]", *useridnr);
				has_5 = 1;
				break;
			case DSN_CLASS_QUOTA:
				TRACE(TRACE_NOTICE, "mailbox over quota, message rejected for useridnr [%llu]", *useridnr);
				has_5_2 = 1;
				break;
			case DSN_CLASS_TEMP:
			default:
				TRACE(TRACE_ERR, "unknown temporary failure in sort_and_deliver for useridnr [%llu]", *useridnr);
				has_4 = 1;
				break;
			}

			/* Automatic reply and notification */
			if (execute_auto_ran(message, *useridnr) < 0) {
				TRACE(TRACE_ERR, "error in execute_auto_ran(), but continuing delivery normally.");
			}   

			if (! g_list_next(userids))
				break;
			userids = g_list_next(userids);

		}

		final_dsn.class = dsnuser_worstcase_int(has_2, has_4, has_5, has_5_2);
		switch (final_dsn.class) {
		case DSN_CLASS_OK:
			/* Success. Address related. Valid. */
			set_dsn(&delivery->dsn, DSN_CLASS_OK, 1, 5);
			break;
		case DSN_CLASS_TEMP:
			/* sort_and_deliver returns TEMP is useridnr is 0, aka,
			 * if nothing was delivered at all, or for any other failures. */	

			/* If there's a problem with the delivery address, but
			 * there are proper forwarding addresses, we're OK. */
			if (g_list_length(delivery->forwards) > 0) {
				/* Success. Address related. Valid. */
				set_dsn(&delivery->dsn, DSN_CLASS_OK, 1, 5);
				break;
			}
			/* Fall through to FAIL. */
		case DSN_CLASS_FAIL:
			/* Permanent failure. Address related. Does not exist. */
			set_dsn(&delivery->dsn, DSN_CLASS_FAIL, 1, 1);
			break;
		case DSN_CLASS_QUOTA:
			/* Permanent failure. Mailbox related. Over quota limit. */
			set_dsn(&delivery->dsn, DSN_CLASS_FAIL, 2, 2);
			break;
		case DSN_CLASS_NONE:
			/* Leave the DSN status at whatever dsnuser_resolve set it at. */
			break;
		}

		TRACE(TRACE_DEBUG, "deliver [%u] messages to external addresses", g_list_length(delivery->forwards));

		/* Each user may also have a list of external forwarding addresses. */
		if (g_list_length(delivery->forwards) > 0) {

			TRACE(TRACE_DEBUG, "delivering to external addresses");
			const char *from = dbmail_message_get_header(message, "Return-Path");

			/* Forward using the temporary stored message. */
			if (send_forward_list(message, delivery->forwards, from)) {
				/* If forward fails, tell the sender that we're
				 * having a transient error. They'll resend. */
				TRACE(TRACE_NOTICE, "forwaring failed, reporting transient error.");
				set_dsn(&delivery->dsn, DSN_CLASS_TEMP, 1, 1);
			}
		}
		if (! g_list_next(dsnusers))
			break;
		dsnusers = g_list_next(dsnusers);

	}

	/* Always delete the temporary message, even if the delivery failed.
	 * It is the MTA's job to requeue or bounce the message,
	 * and our job to keep a tidy database ;-) */
	if (! db_delete_message(tmpid)) 
		TRACE(TRACE_ERR, "failed to delete temporary message [%llu]", message->id);
	TRACE(TRACE_DEBUG, "temporary message deleted from database. Done.");

	return 0;
}

