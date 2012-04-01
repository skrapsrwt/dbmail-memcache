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

/* 
 * 
 * dbmail-imapsession.c
 *
 * IMAP-server utility functions implementations
 */

#include "dbmail.h"
#include "dm_cache.h"

#define THIS_MODULE "imap"
#define BUFLEN 2048
#define SEND_BUF_SIZE 8192
#define MAX_ARGS 512
#define IDLE_TIMEOUT 30

extern db_param_t _db_params;
#define DBPFX _db_params.pfx

gboolean imap_feature_idle_status = FALSE;

extern const char *month_desc[];
extern const int month_len[];
extern const char *imap_flag_desc[];
extern const char *imap_flag_desc_escaped[];
extern volatile sig_atomic_t alarm_occured;

extern int selfpipe[2];
extern GAsyncQueue *queue;
extern serverConfig_t *server_conf;

/*
 * send_data()
 *
 * sends cnt bytes from a MEM structure to a FILE stream
 * uses a simple buffering system
 */
static void send_data(ImapSession *self, Mem_T M, int cnt)
{
	char buf[SEND_BUF_SIZE];
	size_t l;
	int got = 0, want = cnt;

	assert(M);
	TRACE(TRACE_DEBUG,"[%p] C [%p] M [%p] cnt [%d]", self, self->cache, M, cnt);
	while (cnt >= SEND_BUF_SIZE) {
		memset(buf,0,sizeof(buf));
		l = Mem_read(M, buf, SEND_BUF_SIZE-1);
		if (l>0) dbmail_imap_session_buff_printf(self, "%s", buf);
		cnt -= l;
	}

	if (cnt > 0) {
		memset(buf,0,sizeof(buf));
		l = Mem_read(M, buf, cnt);
		if (l>0) dbmail_imap_session_buff_printf(self, "%s", buf);
		cnt -= l;
	}
	got = want - cnt;
	if (got != want) TRACE(TRACE_EMERG,"[%p] want [%d] <> got [%d]", self, want, got);
}

static void mailboxstate_destroy(MailboxState_T M)
{
	MailboxState_free(&M);
}


/* 
 * initializer and accessors for ImapSession
 */
ImapSession * dbmail_imap_session_new(void)
{
	ImapSession * self;

	self = g_new0(ImapSession,1);
	self->args = g_new0(char *, MAX_ARGS);
	self->buff = g_string_new("");
	self->fi = g_new0(fetch_items_t,1);
	self->capa = Capa_new();
	self->preauth_capa = Capa_new();
	Capa_remove(self->preauth_capa, "ACL");
	Capa_remove(self->preauth_capa, "RIGHTS=texk");
	Capa_remove(self->preauth_capa, "NAMESPACE");
	Capa_remove(self->preauth_capa, "CHILDREN");
	Capa_remove(self->preauth_capa, "SORT");
	Capa_remove(self->preauth_capa, "QUOTA");
	Capa_remove(self->preauth_capa, "THREAD=ORDEREDSUBJECT");
	Capa_remove(self->preauth_capa, "UNSELECT");
	Capa_remove(self->preauth_capa, "IDLE");
	if (MATCH(_db_params.authdriver, "LDAP")) {
		Capa_remove(self->capa, "AUTH=CRAM-MD5");
		Capa_remove(self->preauth_capa, "AUTH=CRAM-MD5");
	}
	self->physids = g_tree_new_full((GCompareDataFunc)ucmpdata,NULL,(GDestroyNotify)g_free,(GDestroyNotify)g_free);
	self->mbxinfo = g_tree_new_full((GCompareDataFunc)ucmpdata,NULL,(GDestroyNotify)g_free,(GDestroyNotify)mailboxstate_destroy);

	self->cache = Cache_new();
	assert(self->cache);
 
	TRACE(TRACE_DEBUG,"imap session [%p] created", self);
	return self;
}

static u64_t dbmail_imap_session_message_load(ImapSession *self, int filter)
{
	u64_t *physid = NULL;

	TRACE(TRACE_DEBUG,"[%llu]", self->msg_idnr);

	if (! (physid = g_tree_lookup(self->physids, &(self->msg_idnr)))) {
		u64_t *uid;
		physid = g_new0(u64_t,1);
			
		if ((db_get_physmessage_id(self->msg_idnr, physid)) != DM_SUCCESS) {
			TRACE(TRACE_ERR,"can't find physmessage_id for message_idnr [%llu]", self->msg_idnr);
			g_free(physid);
			return 0;
		}
		uid = g_new0(u64_t, 1);
		*uid = self->msg_idnr;
		g_tree_insert(self->physids, uid, physid);
	}
		
	if (self->message && GMIME_IS_MESSAGE(self->message->content)) {
		if (*physid != self->message->id) {
			dbmail_message_free(self->message);
			self->message = NULL;
		}
	}

	assert(physid);

	if (! self->message) {
		DbmailMessage *msg = dbmail_message_new();
		if ((msg = dbmail_message_retrieve(msg, *physid, filter)) != NULL)
			self->message = msg;
	}

	if (! self->message) {
		TRACE(TRACE_ERR,"message retrieval failed");
		return 0;
	}

	return Cache_update(self->cache, self->message, filter);
}


    
ImapSession * dbmail_imap_session_set_tag(ImapSession * self, char * tag)
{
	if (self->tag) g_free(self->tag);
	self->tag = g_strdup(tag);
	return self;
}

ImapSession * dbmail_imap_session_set_command(ImapSession * self, char * command)
{
	if (self->command) g_free(self->command);
	self->command = g_ascii_strup(command,-1);
	return self;
}


void dbmail_imap_session_delete(ImapSession ** s)
{
	ImapSession *self = *s;

	TRACE(TRACE_DEBUG, "[%p]", self);
	Cache_free(&self->cache);
	Capa_free(&self->preauth_capa);
	Capa_free(&self->capa);

	if (self->ci) {
		ci_close(self->ci);
		self->ci = NULL;
	}

	dbmail_imap_session_args_free(self, TRUE);
	dbmail_imap_session_fetch_free(self);

	if (self->tag) {
		g_free(self->tag);
		self->tag = NULL;
	}
	if (self->command) {
		g_free(self->command);
		self->command = NULL;
	}
	if (self->mailbox) {
		dbmail_mailbox_free(self->mailbox);
		self->mailbox = NULL;
	}
	if (self->mbxinfo) {
		g_tree_destroy(self->mbxinfo);
		self->mbxinfo = NULL;
	}
	if (self->message) {
		dbmail_message_free(self->message);
		self->message = NULL;
	}
	if (self->physids) {
		g_tree_destroy(self->physids);
		self->physids = NULL;
	}
	
	g_string_free(self->buff,TRUE);
	g_free(self);
	self = NULL;
}

void dbmail_imap_session_fetch_free(ImapSession *self) 
{
	if (self->envelopes) {
		g_tree_destroy(self->envelopes);
		self->envelopes = NULL;
	}
	if (self->ids) {
		g_tree_destroy(self->ids);
		self->ids = NULL;
	}
	if (self->ids_list) {
		g_list_free(g_list_first(self->ids_list));
		self->ids_list = NULL;
	}
	if (self->fi) {
		dbmail_imap_session_bodyfetch_free(self);
		g_free(self->fi);
		self->fi = NULL;
	}
}

void dbmail_imap_session_args_free(ImapSession *self, gboolean all)
{
	int i;
	for (i = 0; i < MAX_ARGS && self->args[i]; i++) {
		if (self->args[i]) g_free(self->args[i]);
		self->args[i] = NULL;
	}
	self->args_idx = 0;

	if (all) g_free(self->args);
}

/*************************************************************************************
 *
 *
 * imap utilities using ImapSession
 *
 *
 ************************************************************************************/
static int _imap_session_fetch_parse_partspec(ImapSession *self)
{
	/* check for a partspecifier */
	/* first check if there is a partspecifier (numbers & dots) */
	int indigit = 0;
	unsigned int j = 0;
	char *token, *nexttoken;

	token=self->args[self->args_idx];
	nexttoken=self->args[self->args_idx+1];

	TRACE(TRACE_DEBUG,"token [%s], nexttoken [%s]", token, nexttoken);

	for (j = 0; token[j]; j++) {
		if (isdigit(token[j])) {
			indigit = 1;
			continue;
		} else if (token[j] == '.') {
			if (!indigit) return -2; /* error, single dot specified */
			indigit = 0;
			continue;
		} else
			break;	/* other char found */
	}
	
	if (j > 0) {
		if (indigit && token[j]) return -2;		/* error DONE */
		/* partspecifier present, save it */
		if (j >= IMAP_MAX_PARTSPEC_LEN) return -2;	/* error DONE */
		dbmail_imap_session_bodyfetch_set_partspec(self, token, j);
	}

	char *partspec = &token[j];
	int shouldclose = 0;

	if (MATCH(partspec, "header.fields")) {
		dbmail_imap_session_bodyfetch_set_itemtype(self, BFIT_HEADER_FIELDS);
	} else if (MATCH(partspec, "header.fields.not")) {
		dbmail_imap_session_bodyfetch_set_itemtype(self, BFIT_HEADER_FIELDS_NOT);
	} else if (MATCH(partspec, "text")) {
		self->fi->msgparse_needed=1;
		dbmail_imap_session_bodyfetch_set_itemtype(self, BFIT_TEXT);
		shouldclose = 1;
	} else if (MATCH(partspec, "header")) {
		self->fi->msgparse_needed=1;
		dbmail_imap_session_bodyfetch_set_itemtype(self, BFIT_HEADER);
		shouldclose = 1;
	} else if (MATCH(partspec, "mime")) {
		self->fi->msgparse_needed=1;
		if (j == 0) return -2;				/* error DONE */
		dbmail_imap_session_bodyfetch_set_itemtype(self, BFIT_MIME);
		shouldclose = 1;
	} else if (token[j] == '\0') {
		self->fi->msgparse_needed=1;
		dbmail_imap_session_bodyfetch_set_itemtype(self, BFIT_TEXT_SILENT);
		shouldclose = 1;
	} else {
		return -2;					/* error DONE */
	}
	if (shouldclose) {
		if (! MATCH(nexttoken, "]")) return -2;		/* error DONE */
	} else {
		self->args_idx++;	/* should be at '(' now */
		token = self->args[self->args_idx];
		nexttoken = self->args[self->args_idx+1];

		if (! MATCH(token,"(")) return -2;		/* error DONE */

		self->args_idx++;	/* at first item of field list now, remember idx */
		dbmail_imap_session_bodyfetch_set_argstart(self); 
		/* walk on untill list terminates (and it does 'cause parentheses are matched) */
		while (! MATCH(self->args[self->args_idx],")") ) self->args_idx++;

		token = self->args[self->args_idx];
		nexttoken = self->args[self->args_idx+1];
		
		TRACE(TRACE_DEBUG,"token [%s], nexttoken [%s]", token, nexttoken);

		dbmail_imap_session_bodyfetch_set_argcnt(self);

		if (dbmail_imap_session_bodyfetch_get_last_argcnt(self) == 0 || ! MATCH(nexttoken,"]") )
			return -2;				/* error DONE */
	}

	return 0;
}

static int _imap_session_fetch_parse_octet_range(ImapSession *self) 
{
	/* check if octet start/cnt is specified */
	int delimpos;
	unsigned int j = 0;
	
	char *token = self->args[self->args_idx];
	
	if (! token) return 0;
	
	TRACE(TRACE_DEBUG,"[%p] parse token [%s]", self, token);

	if (token[0] == '<') {

		/* check argument */
		if (token[strlen(token) - 1] != '>') return -2;	/* error DONE */
		delimpos = -1;
		for (j = 1; j < strlen(token) - 1; j++) {
			if (token[j] == '.') {
				if (delimpos != -1) 
					return -2;
				delimpos = j;
			} else if (!isdigit (token[j]))
				return -2;
		}
		if (delimpos == -1 || delimpos == 1 || delimpos == (int) (strlen(token) - 2))
			return -2;	/* no delimiter found or at first/last pos OR invalid args DONE */

		/* read the numbers */
		token[strlen(token) - 1] = '\0';
		token[delimpos] = '\0';
		self->fi->msgparse_needed=1;
		dbmail_imap_session_bodyfetch_set_octetstart(self, strtoll(&token[1], NULL, 10));
		dbmail_imap_session_bodyfetch_set_octetcnt(self,strtoll(&token [delimpos + 1], NULL, 10));

		/* restore argument */
		token[delimpos] = '.';
		token[strlen(token) - 1] = '>';
	} else {
		self->args_idx--;
		return self->args_idx;
	}

	self->args_idx++;
	return 0;	/* DONE */
}

/*
 * dbmail_imap_session_fetch_parse_args()
 *
 * retrieves next item to be fetched from an argument list starting at the given
 * index. The update index is returned being -1 on 'no-more' and -2 on error.
 * arglist is supposed to be formatted according to build_args_array()
 *
 */
int dbmail_imap_session_fetch_parse_args(ImapSession * self)
{
	int ispeek = 0;
	
	if (!self->args[self->args_idx]) return -1;	/* no more */
	if (self->args[self->args_idx][0] == '(') self->args_idx++;
	if (!self->args[self->args_idx]) return -2;	/* error */
	
	char *token = NULL, *nexttoken = NULL;
	
	token = self->args[self->args_idx];
	nexttoken = self->args[self->args_idx+1];

	TRACE(TRACE_DEBUG,"[%p] parse args[%llu] = [%s]", self, self->args_idx, token);

	if (MATCH(token,"flags")) {
		self->fi->getFlags = 1;
	} else if (MATCH(token,"internaldate")) {
		self->fi->getInternalDate=1;
	} else if (MATCH(token,"uid")) {
		self->fi->getUID=1;
	} else if (MATCH(token,"rfc822.size")) {
		self->fi->getSize = 1;
	} else if (MATCH(token,"fast")) {
		self->fi->getInternalDate = 1;
		self->fi->getFlags = 1;
		self->fi->getSize = 1;
	} else if (MATCH(token,"rfc822")) {
		self->fi->msgparse_needed=1;
		self->fi->getRFC822=1;
	} else if (MATCH(token,"rfc822.header")) {
		self->fi->msgparse_needed=1;
		self->fi->getRFC822Header = 1;
	} else if (MATCH(token,"rfc822.peek")) {
		self->fi->msgparse_needed=1;
		self->fi->getRFC822Peek = 1;
	} else if (MATCH(token,"rfc822.text")) {
		self->fi->msgparse_needed=1;
		self->fi->getRFC822Text = 1;
	
	} else if (MATCH(token,"body") || MATCH(token,"body.peek")) {
		
		/* setting self->fi->msgparse_needed deferred to fetch_parse_partspec 
		 * since we don't need to parse just to retrieve headers */
		
		dbmail_imap_session_bodyfetch_new(self);

		if (MATCH(token,"body.peek")) ispeek=1;
		
		nexttoken = (char *)self->args[self->args_idx+1];
		
		if (! nexttoken || ! MATCH(nexttoken,"[")) {
			if (ispeek) return -2;	/* error DONE */
			self->fi->msgparse_needed = 1;
			self->fi->getMIME_IMB_noextension = 1;	/* just BODY specified */
		} else {
			int res = 0;
			/* now read the argument list to body */
			self->args_idx++;	/* now pointing at '[' (not the last arg, parentheses are matched) */
			self->args_idx++;	/* now pointing at what should be the item type */

			token = (char *)self->args[self->args_idx];
			nexttoken = (char *)self->args[self->args_idx+1];

			TRACE(TRACE_DEBUG,"[%p] token [%s], nexttoken [%s]", self, token, nexttoken);

			if (MATCH(token,"]")) {
				if (ispeek) {
					self->fi->msgparse_needed = 1;
					self->fi->getBodyTotalPeek = 1;
				} else {
					self->fi->msgparse_needed = 1;
					self->fi->getBodyTotal = 1;
				}
				self->args_idx++;				
				res = _imap_session_fetch_parse_octet_range(self);
				if (res == -2)
					TRACE(TRACE_DEBUG,"[%p] fetch_parse_octet_range return with error", self);
				return res;
					
			}
			
			if (ispeek) self->fi->noseen = 1;

			if (_imap_session_fetch_parse_partspec(self) < 0) {
				TRACE(TRACE_DEBUG,"[%p] fetch_parse_partspec return with error", self);
				return -2;
			}
			
			self->args_idx++; // idx points to ']' now
			self->args_idx++; // idx points to octet range now 
			res = _imap_session_fetch_parse_octet_range(self);
			if (res == -2)
				TRACE(TRACE_DEBUG,"[%p] fetch_parse_octet_range return with error", self);
			return res;
		}
	} else if (MATCH(token,"all")) {		
		self->fi->msgparse_needed=1; // because of getEnvelope
		self->fi->getInternalDate = 1;
		self->fi->getEnvelope = 1;
		self->fi->getFlags = 1;
		self->fi->getSize = 1;
	} else if (MATCH(token,"full")) {
		self->fi->msgparse_needed=1;
		self->fi->getInternalDate = 1;
		self->fi->getEnvelope = 1;
		self->fi->getMIME_IMB_noextension = 1;
		self->fi->getFlags = 1;
		self->fi->getSize = 1;
	} else if (MATCH(token,"bodystructure")) {
		self->fi->msgparse_needed=1;
		self->fi->getMIME_IMB = 1;
	} else if (MATCH(token,"envelope")) {
		self->fi->getEnvelope = 1;
	} else {			
		if ((! nexttoken) && (strcmp(token,")") == 0)) return -1;
		TRACE(TRACE_INFO,"[%p] error [%s]", self, token);
		return -2;	/* DONE */
	}

	return 1; //theres more...
}

#define SEND_SPACE if (self->fi->isfirstfetchout) \
				self->fi->isfirstfetchout = 0; \
			else \
				dbmail_imap_session_buff_printf(self, " ")

#define QUERY_BATCHSIZE 2000

void _send_headers(ImapSession *self, const body_fetch_t *bodyfetch, gboolean not)
{
	long long cnt = 0;
	gchar *tmp;
	gchar *s;
	GString *ts;

	dbmail_imap_session_buff_printf(self,"HEADER.FIELDS%s %s] ", not ? ".NOT" : "", bodyfetch->hdrplist);

	if (! (s = g_tree_lookup(bodyfetch->headers, &(self->msg_idnr)))) {
		dbmail_imap_session_buff_printf(self, "{2}\r\n\r\n");
		return;
	}

	TRACE(TRACE_DEBUG,"[%p] [%s] [%s]", self, bodyfetch->hdrplist, s);

	ts = g_string_new(s);
	tmp = get_crlf_encoded(ts->str);
	cnt = strlen(tmp);

	if (bodyfetch->octetcnt > 0) {
		char *p = tmp;
		if (bodyfetch->octetstart > 0 && bodyfetch->octetstart < (guint64)cnt) {
			p += bodyfetch->octetstart;
			cnt -= bodyfetch->octetstart;
		}
		
		if ((guint64)cnt > bodyfetch->octetcnt) {
			p[bodyfetch->octetcnt] = '\0';
			cnt = bodyfetch->octetcnt;
		}
		
		dbmail_imap_session_buff_printf(self, "<%llu> {%llu}\r\n%s\r\n", 
				bodyfetch->octetstart, cnt+2, p);
	} else {
		dbmail_imap_session_buff_printf(self, "{%llu}\r\n%s\r\n", cnt+2, tmp);
	}

	g_string_free(ts,TRUE);
	g_free(tmp);
}


/* get headers or not */
static void _fetch_headers(ImapSession *self, body_fetch_t *bodyfetch, gboolean not)
{
	C c; R r; volatile int t = FALSE;
	GString *q;
	gchar *fld, *val, *old, *new = NULL;
	u64_t *mid;
	u64_t id;
	GList *last;
	int k;
	char range[DEF_FRAGSIZE];
	memset(range,0,DEF_FRAGSIZE);

	if (! bodyfetch->headers) {
		TRACE(TRACE_DEBUG, "[%p] init bodyfetch->headers", self);
		bodyfetch->headers = g_tree_new_full((GCompareDataFunc)ucmpdata,NULL,(GDestroyNotify)g_free,(GDestroyNotify)g_free);
		self->ceiling = 0;
		self->hi = 0;
		self->lo = 0;
	}

	if (! bodyfetch->hdrnames) {

		GList *tlist = NULL;
		GString *h = NULL;

		for (k = 0; k < bodyfetch->argcnt; k++) 
			tlist = g_list_append(tlist, g_strdup(self->args[k + bodyfetch->argstart]));

		bodyfetch->hdrplist = dbmail_imap_plist_as_string(tlist);
		h = g_list_join((GList *)tlist,"','");
		g_list_destroy(tlist);

		h = g_string_ascii_down(h);

		bodyfetch->hdrnames = h->str;

		g_string_free(h,FALSE);
	}

	TRACE(TRACE_DEBUG,"[%p] for %llu [%s]", self, self->msg_idnr, bodyfetch->hdrplist);

	// did we prefetch this message already?
	if (self->msg_idnr <= self->ceiling) {
		_send_headers(self, bodyfetch, not);
		return;
	}

	// let's fetch the required message and prefetch a batch if needed.
	
	if (! (last = g_list_nth(self->ids_list, self->lo+(u64_t)QUERY_BATCHSIZE)))
		last = g_list_last(self->ids_list);
	self->hi = *(u64_t *)last->data;

	if (self->msg_idnr == self->hi)
		snprintf(range,DEF_FRAGSIZE,"= %llu", self->msg_idnr);
	else
		snprintf(range,DEF_FRAGSIZE,"BETWEEN %llu AND %llu", self->msg_idnr, self->hi);

	TRACE(TRACE_DEBUG,"[%p] prefetch %llu:%llu ceiling %llu [%s]", self, self->msg_idnr, self->hi, self->ceiling, bodyfetch->hdrplist);
	q = g_string_new("");
	g_string_printf(q,"SELECT m.message_idnr, n.headername, v.headervalue "
			"FROM %sheader h "
			"LEFT JOIN %smessages m ON h.physmessage_id=m.physmessage_id "
			"LEFT JOIN %sheadername n ON h.headername_id=n.id "
			"LEFT JOIN %sheadervalue v ON h.headervalue_id=v.id "
			"WHERE m.mailbox_idnr = %llu "
			"AND m.message_idnr %s "
			"AND lower(n.headername) %s IN ('%s')",
			DBPFX, DBPFX, DBPFX, DBPFX,
			self->mailbox->id, range, 
			not?"NOT":"", bodyfetch->hdrnames);

	c = db_con_get();	
	TRY
		r = db_query(c, q->str);
		while (db_result_next(r)) {
			int l;	
			const void *blob;
			char *str = NULL;

			id = db_result_get_u64(r, 0);
			
			if (! g_tree_lookup(self->ids,&id))
				continue;
			
			mid = g_new0(u64_t,1);
			*mid = id;
			
			fld = (char *)db_result_get(r, 1);
			blob = db_result_get_blob(r, 2, &l);
			str = g_new0(char,l+1);
			str = strncpy(str,blob,l);
			str[l]= 0;
			val = dbmail_iconv_db_to_utf7(str);
			g_free(str);
			if (! val) {
				TRACE(TRACE_DEBUG, "[%p] [%llu] no headervalue [%s]", self, id, fld);
			} else {
				old = g_tree_lookup(bodyfetch->headers, (gconstpointer)mid);
				new = g_strdup_printf("%s%s: %s\n", old?old:"", fld, val);
				g_free(val);
				g_tree_insert(bodyfetch->headers,mid,new);
			}
			
		}
	CATCH(SQLException)
		LOG_SQLERROR;
		t = DM_EQUERY;
	FINALLY
		db_con_close(c);
	END_TRY;

	g_string_free(q,TRUE);
	if (t == DM_EQUERY) return;
	
	self->lo += QUERY_BATCHSIZE;
	self->ceiling = self->hi;

	_send_headers(self, bodyfetch, not);

	return;
}

static u64_t get_dumpsize(body_fetch_t *bodyfetch, gsize dumpsize) 
{
	if (bodyfetch->octetstart > dumpsize)
		return 0;
	if ((bodyfetch->octetstart + dumpsize) > bodyfetch->octetcnt)
		return bodyfetch->octetcnt;
	return (dumpsize - bodyfetch->octetstart);
}

static void _imap_send_part(ImapSession *self, GMimeObject *part, body_fetch_t *bodyfetch, const char *type)
{
	TRACE(TRACE_DEBUG,"[%p] type [%s]", self, type);
	if ( !part ) { 
		dbmail_imap_session_buff_printf(self, "] NIL");
	} else {
		char *tmp = imap_get_logical_part(part,type);
		GString *str = g_string_new(tmp);
		g_free(tmp);

		if (str->len < 1) {
			dbmail_imap_session_buff_printf(self, "] NIL");
		} else {
			gsize cnt = 0;
			if (bodyfetch->octetcnt > 0) {
				cnt = get_dumpsize(bodyfetch, str->len);
				dbmail_imap_session_buff_printf(self, "]<%llu> {%lu}\r\n", bodyfetch->octetstart, cnt);
				g_string_erase(str,0,bodyfetch->octetstart);
				g_string_truncate(str,cnt);
			} else {
				dbmail_imap_session_buff_printf(self, "] {%lu}\r\n", str->len);
			}
			dbmail_imap_session_buff_printf(self,"%s", str->str);
		}
		g_string_free(str,TRUE);
	}
}


static int _imap_show_body_section(body_fetch_t *bodyfetch, gpointer data) 
{
	GMimeObject *part = NULL;
	gboolean condition = FALSE;
	ImapSession *self = (ImapSession *)data;
	
	if (bodyfetch->itemtype < 0) return 0;
	
	TRACE(TRACE_DEBUG,"[%p] itemtype [%d] partspec [%s]", self, bodyfetch->itemtype, bodyfetch->partspec);
	
	if (self->fi->msgparse_needed) {

		if (! dbmail_imap_session_message_load(self, DBMAIL_MESSAGE_FILTER_FULL))
			return 0;

		if (bodyfetch->partspec[0]) {
			if (bodyfetch->partspec[0] == '0') {
				dbmail_imap_session_buff_printf(self, "\r\n%s BAD protocol error\r\n", self->tag);
				TRACE(TRACE_ERR, "[%p] PROTOCOL ERROR", self);
				return 1;
			}
			part = imap_get_partspec(GMIME_OBJECT((self->message)->content), bodyfetch->partspec);
		} else {
			part = GMIME_OBJECT((self->message)->content);
		}
	}

	SEND_SPACE;

	if (! self->fi->noseen) self->fi->setseen = 1;
	dbmail_imap_session_buff_printf(self, "BODY[%s", bodyfetch->partspec);

	switch (bodyfetch->itemtype) {

		case BFIT_TEXT:
			dbmail_imap_session_buff_printf(self, "TEXT");
			// fall-through
		case BFIT_TEXT_SILENT:
			_imap_send_part(self, part, bodyfetch, "TEXT");
			break;
		case BFIT_HEADER:
			dbmail_imap_session_buff_printf(self, "HEADER");
			_imap_send_part(self, part, bodyfetch, "HEADER");
			break;
		case BFIT_MIME:
			dbmail_imap_session_buff_printf(self, "MIME");
			_imap_send_part(self, part, bodyfetch, "MIME");
			break;
		case BFIT_HEADER_FIELDS_NOT:
			condition=TRUE;
		case BFIT_HEADER_FIELDS:
			_fetch_headers(self, bodyfetch, condition);
			break;
		default:
			dbmail_imap_session_buff_clear(self);
			dbmail_imap_session_buff_printf(self, "\r\n* BYE internal server error\r\n");
			return -1;

	}
	return 0;
}

/* get envelopes */
static void _fetch_envelopes(ImapSession *self)
{
	C c; R r; volatile int t = FALSE;
	GString *q;
	gchar *s;
	u64_t *mid;
	u64_t id;
	char range[DEF_FRAGSIZE];
	GList *last;
	memset(range,0,DEF_FRAGSIZE);

	if (! self->envelopes) {
		self->envelopes = g_tree_new_full((GCompareDataFunc)ucmpdata,NULL,(GDestroyNotify)g_free,(GDestroyNotify)g_free);
		self->lo = 0;
		self->hi = 0;
	}

	if ((s = g_tree_lookup(self->envelopes, &(self->msg_idnr))) != NULL) {
		dbmail_imap_session_buff_printf(self, "ENVELOPE %s", s?s:"");
		return;
	}

	TRACE(TRACE_DEBUG,"[%p] lo: %llu", self, self->lo);

	if (! (last = g_list_nth(self->ids_list, self->lo+(u64_t)QUERY_BATCHSIZE)))
		last = g_list_last(self->ids_list);
	self->hi = *(u64_t *)last->data;

	if (self->msg_idnr == self->hi)
		snprintf(range,DEF_FRAGSIZE,"= %llu", self->msg_idnr);
	else
		snprintf(range,DEF_FRAGSIZE,"BETWEEN %llu AND %llu", self->msg_idnr, self->hi);

        q = g_string_new("");
	g_string_printf(q,"SELECT message_idnr,envelope "
			"FROM %senvelope e "
			"LEFT JOIN %smessages m USING (physmessage_id) "
			"WHERE m.mailbox_idnr = %llu "
			"AND message_idnr %s",
			DBPFX, DBPFX,  
			self->mailbox->id, range);
	c = db_con_get();
	TRY
		r = db_query(c, q->str);
		while (db_result_next(r)) {
			id = db_result_get_u64(r, 0);
			
			if (! g_tree_lookup(self->ids,&id))
				continue;
			
			mid = g_new0(u64_t,1);
			*mid = id;
			
			g_tree_insert(self->envelopes,mid,g_strdup(ResultSet_getString(r, 2)));
		}
	CATCH(SQLException)
		LOG_SQLERROR;
		t = DM_EQUERY;
	FINALLY
		db_con_close(c);
		g_string_free(q,TRUE);
	END_TRY;

	if (t == DM_EQUERY) return;

	self->lo += QUERY_BATCHSIZE;

	s = g_tree_lookup(self->envelopes, &(self->msg_idnr));
	dbmail_imap_session_buff_printf(self, "ENVELOPE %s", s?s:"");
}

static void _imap_show_body_sections(ImapSession *self) 
{
	dbmail_imap_session_bodyfetch_rewind(self);
	g_list_foreach(self->fi->bodyfetch,(GFunc)_imap_show_body_section, (gpointer)self);
	dbmail_imap_session_bodyfetch_rewind(self);
}

static int _fetch_get_items(ImapSession *self, u64_t *uid)
{
	int result;
	u64_t actual_cnt, tmpdumpsize;
	gchar *s = NULL;
	u64_t *id = uid;
	gboolean reportflags = FALSE;

	MessageInfo *msginfo = g_tree_lookup(MailboxState_getMsginfo(self->mailbox->mbstate), uid);

	if (! msginfo) {
		TRACE(TRACE_INFO, "[%p] failed to lookup msginfo struct for message [%llu]", self, *uid);
		return 0;
	}
	
	id = g_tree_lookup(MailboxState_getIds(self->mailbox->mbstate),uid);

	g_return_val_if_fail(id,-1);

	dbmail_imap_session_buff_printf(self, "* %llu FETCH (", *id);

	self->msg_idnr = *uid;
	self->fi->isfirstfetchout = 1;

	if (self->fi->msgparse_needed) {
		if (! (dbmail_imap_session_message_load(self, DBMAIL_MESSAGE_FILTER_FULL))) {
			dbmail_imap_session_buff_clear(self);
			dbmail_imap_session_buff_printf(self, "\r\n* BYE error loading message\r\n");
			return -1;
		}
	}

	if (self->fi->getInternalDate) {
		SEND_SPACE;
		char *s =date_sql2imap(msginfo->internaldate);
		dbmail_imap_session_buff_printf(self, "INTERNALDATE \"%s\"", s);
		g_free(s);
	}
	if (self->fi->getSize) {
		u64_t rfcsize = msginfo->rfcsize;
		SEND_SPACE;

		/* FIXME: this is an override to prevent mismatch between reported
		 * rfc822.size and the actual number of octets in the message. 
		 * There is a heisenbug in the message code that leads to these
		 * mismatches during message storage that is only exposed when a 
		 * message is retrieved again.
		 * see bug #797
		 */
		if (self->fi->msgparse_needed && self->cache)
			rfcsize = Cache_get_size(self->cache);
		dbmail_imap_session_buff_printf(self, "RFC822.SIZE %llu", rfcsize);
	}
	if (self->fi->getFlags) {
		SEND_SPACE;

		GList *sublist = MailboxState_message_flags(self->mailbox->mbstate, msginfo);
		s = dbmail_imap_plist_as_string(sublist);
		g_list_destroy(sublist);
		dbmail_imap_session_buff_printf(self,"FLAGS %s",s);
		g_free(s);
	}
	if (self->fi->getUID) {
		SEND_SPACE;
		dbmail_imap_session_buff_printf(self, "UID %llu", msginfo->uid);
	}
	if (self->fi->getMIME_IMB) {
		SEND_SPACE;
		if ((s = imap_get_structure(GMIME_MESSAGE((self->message)->content), 1))==NULL) {
			dbmail_imap_session_buff_clear(self);
			dbmail_imap_session_buff_printf(self, "\r\n* BYE error fetching body structure\r\n");
			return -1;
		}
		dbmail_imap_session_buff_printf(self, "BODYSTRUCTURE %s", s);
		g_free(s);
	}

	if (self->fi->getMIME_IMB_noextension) {
		SEND_SPACE;
		if ((s = imap_get_structure(GMIME_MESSAGE((self->message)->content), 0))==NULL) {
			dbmail_imap_session_buff_clear(self);
			dbmail_imap_session_buff_printf(self, "\r\n* BYE error fetching body\r\n");
			return -1;
		}
		dbmail_imap_session_buff_printf(self, "BODY %s",s);
		g_free(s);
	}

	if (self->fi->getEnvelope) {
		SEND_SPACE;
		_fetch_envelopes(self);
	}

	if (self->fi->getRFC822 || self->fi->getRFC822Peek) {
		SEND_SPACE;
		dbmail_imap_session_buff_printf(self, "RFC822 {%llu}\r\n", Cache_get_size(self->cache) );
		send_data(self, Cache_get_memdump(self->cache), Cache_get_size(self->cache) );
		if (self->fi->getRFC822)
			self->fi->setseen = 1;

	}

	if (self->fi->getBodyTotal || self->fi->getBodyTotalPeek) {
		SEND_SPACE;
		if (dbmail_imap_session_bodyfetch_get_last_octetcnt(self) == 0) {
			dbmail_imap_session_buff_printf(self, "BODY[] {%llu}\r\n", Cache_get_size(self->cache) );
			send_data(self, Cache_get_memdump(self->cache), Cache_get_size(self->cache) );
		} else {
			u64_t size = Cache_get_size(self->cache);
			Mem_T M = Cache_get_memdump(self->cache);
			Mem_seek(M, dbmail_imap_session_bodyfetch_get_last_octetstart(self), SEEK_SET);
			actual_cnt = (dbmail_imap_session_bodyfetch_get_last_octetcnt(self) >
			     (((long long)size) - dbmail_imap_session_bodyfetch_get_last_octetstart(self)))
			    ? (((long long)size) - dbmail_imap_session_bodyfetch_get_last_octetstart(self)) 
			    : dbmail_imap_session_bodyfetch_get_last_octetcnt(self);

			dbmail_imap_session_buff_printf(self, "BODY[]<%llu> {%llu}\r\n", 
					dbmail_imap_session_bodyfetch_get_last_octetstart(self), actual_cnt);
			send_data(self, M, actual_cnt);
		}
		if (self->fi->getBodyTotal)
			self->fi->setseen = 1;
	}

	if (self->fi->getRFC822Header) {
		SEND_SPACE;
		tmpdumpsize = dbmail_imap_session_message_load(self,DBMAIL_MESSAGE_FILTER_HEAD);
		dbmail_imap_session_buff_printf(self, "RFC822.HEADER {%llu}\r\n", tmpdumpsize);
		send_data(self, Cache_get_tmpdump(self->cache), tmpdumpsize);
	}

	if (self->fi->getRFC822Text) {
		SEND_SPACE;
		tmpdumpsize = dbmail_imap_session_message_load(self,DBMAIL_MESSAGE_FILTER_BODY);
		dbmail_imap_session_buff_printf(self, "RFC822.TEXT {%llu}\r\n", tmpdumpsize);
		send_data(self, Cache_get_tmpdump(self->cache), tmpdumpsize);
		self->fi->setseen = 1;
	}

	_imap_show_body_sections(self);

	/* set \Seen flag if necessary; note the absence of an error-check 
	 * for db_get_msgflag()!
	 */
	int setSeenSet[IMAP_NFLAGS] = { 1, 0, 0, 0, 0, 0 };
	if (self->fi->setseen && db_get_msgflag("seen", self->msg_idnr) != 1) {
		/* only if the user has an ACL which grants
		   him rights to set the flag should the
		   flag be set! */
		result = acl_has_right(self->mailbox->mbstate, self->userid, ACL_RIGHT_SEEN);
		if (result == -1) {
			dbmail_imap_session_buff_clear(self);
			dbmail_imap_session_buff_printf(self, "\r\n* BYE internal dbase error\r\n");
			return -1;
		}
		
		if (result == 1) {
			reportflags = TRUE;
			result = db_set_msgflag(self->msg_idnr, setSeenSet, NULL, IMAPFA_ADD, msginfo);
			if (result == -1) {
				dbmail_imap_session_buff_clear(self);
				dbmail_imap_session_buff_printf(self, "\r\n* BYE internal dbase error\r\n");
				return -1;
			}
			db_mailbox_seq_update(MailboxState_getId(self->mailbox->mbstate));
		}

		self->fi->getFlags = 1;
	}

	dbmail_imap_session_buff_printf(self, ")\r\n");

	if (reportflags) {
		char *t = NULL;
		GList *sublist = NULL;
		if (self->use_uid)
			t = g_strdup_printf("UID %llu ", *uid);
		
		sublist = MailboxState_message_flags(self->mailbox->mbstate, msginfo);
		s = dbmail_imap_plist_as_string(sublist);
		g_list_destroy(sublist);

		dbmail_imap_session_buff_printf(self,"* %llu FETCH (%sFLAGS %s)\r\n", *id, t?t:"", s);
		if (t) g_free(t);
		g_free(s);
	}

	return 0;
}

static gboolean _do_fetch(u64_t *uid, gpointer UNUSED value, ImapSession *self)
{
	/* go fetch the items */
	if (_fetch_get_items(self,uid) < 0) {
		TRACE(TRACE_ERR, "[%p] _fetch_get_items returned with error", self);
		dbmail_imap_session_buff_clear(self);
		self->error = TRUE;
		return TRUE;
	}

	return FALSE;
}

int dbmail_imap_session_fetch_get_items(ImapSession *self)
{
	if (! self->ids)
		TRACE(TRACE_INFO, "[%p] self->ids is NULL", self);
	else {
		self->error = FALSE;
		g_tree_foreach(self->ids, (GTraverseFunc) _do_fetch, self);
		dbmail_imap_session_buff_flush(self);
		if (self->error) return -1;
	}
	return 0;
	
}


int client_is_authenticated(ImapSession * self)
{
	return (self->state != CLIENTSTATE_NON_AUTHENTICATED);
}

/*
 * check_state_and_args()
 *
 * checks if the user is in the right state & the numbers of arguments;
 * a state of -1 specifies any state
 * arguments can be grouped by means of parentheses
 *
 * returns 1 on succes, 0 on failure
 */
int check_state_and_args(ImapSession * self, int minargs, int maxargs, clientstate_t state)
{
	int i;

	if (self->state == CLIENTSTATE_ERROR) return 0;

	/* check state */
	if (state != CLIENTSTATE_ANY) {
		if (self->state != state) {
			if (!  (state == CLIENTSTATE_AUTHENTICATED && self->state == CLIENTSTATE_SELECTED)) {
				dbmail_imap_session_buff_printf(self, "%s BAD %s command received in invalid state [%d] != [%d]\r\n", 
					self->tag, self->command, self->state, state);
				return 0;
			}
		}
	}

	/* check args */
	for (i = 0; i < minargs; i++) {
		if (!self->args[self->args_idx+i]) {
			/* error: need more args */
			dbmail_imap_session_buff_printf(self, "%s BAD missing argument%s to %s\r\n", self->tag, (minargs == 1) ? "" : "(s)", 
					self->command);
			return 0;
		}
	}

	for (i = 0; self->args[self->args_idx+i]; i++);

	if (maxargs && (i > maxargs)) {
		/* error: too many args */
		dbmail_imap_session_buff_printf(self, "%s BAD too many arguments to %s\r\n", 
				self->tag, self->command);
		return 0;
	}

	/* succes */
	return 1;
}

void dbmail_imap_session_buff_clear(ImapSession *self)
{
	self->buff = g_string_truncate(self->buff, 0);
	g_string_maybe_shrink(self->buff);
}	

void dbmail_imap_session_buff_flush(ImapSession *self)
{
	dm_thread_data *D;
	if (self->state >= CLIENTSTATE_LOGOUT) return;
	if (self->buff->len < 1) return;

	D = g_new0(dm_thread_data,1);
	D->session = self;
	D->data = (gpointer)self->buff->str;
	D->cb_leave = dm_thread_data_sendmessage;

	g_string_free(self->buff, FALSE);
	self->buff = g_string_new("");

        g_async_queue_push(queue, (gpointer)D);
        if (selfpipe[1] > -1)
		if (write(selfpipe[1], "Q", 1) != 1) { /* ignore */; } 
}

#define IMAP_BUF_SIZE 4096

int dbmail_imap_session_buff_printf(ImapSession * self, char * message, ...)
{
        va_list ap, cp;
        size_t j = 0, l;

        assert(message);
        j = self->buff->len;

        va_start(ap, message);
	va_copy(cp, ap);
        g_string_append_vprintf(self->buff, message, cp);
        va_end(cp);

        l = self->buff->len;

	if (l >= IMAP_BUF_SIZE) dbmail_imap_session_buff_flush(self);

        return (int)(l-j);
}

int dbmail_imap_session_handle_auth(ImapSession * self, const char * username, const char * password)
{
	u64_t userid = 0;
	
	int valid = auth_validate(self->ci, username, password, &userid);
	
	if (self->ci->auth)
		username = Cram_getUsername(self->ci->auth);

	TRACE(TRACE_DEBUG, "[%p] trying to validate user [%s]", self, username);

	
	switch(valid) {
		case -1: /* a db-error occurred */
			dbmail_imap_session_buff_printf(self, "* BYE internal db error validating user\r\n");
			return -1;

		case 0:
			sleep(2);	/* security */
			ci_authlog_init(self->ci, THIS_MODULE, username, AUTHLOG_ERR);
			if (self->ci->auth) { // CRAM-MD5 auth failed
				char *enctype = NULL;
				if (userid) enctype = auth_getencryption(userid);
				if ((! enctype) || (! MATCH(enctype,""))) {
					Capa_remove(self->capa,"AUTH=CRAM-MD5");
					dbmail_imap_session_buff_printf(self, "* CAPABILITY %s\r\n", Capa_as_string(self->capa));
				}
				if (enctype) g_free(enctype);
			}
			dbmail_imap_session_buff_printf(self, "%s NO login rejected\r\n", self->tag);
			TRACE(TRACE_NOTICE, "[%p] login rejected: user [%s] from [%s:%s]", self, username, 
				self->ci->src_ip, self->ci->src_port);
			return 1;

		case 1:
			self->userid = userid;
			ci_authlog_init(self->ci, THIS_MODULE, username, AUTHLOG_ACT);
			TRACE(TRACE_NOTICE, "[%p] login accepted: user [%s] from [%s:%s]", self, username, 
				self->ci->src_ip, self->ci->src_port);
			break;

		default:
			TRACE(TRACE_ERR, "[%p] auth_validate returned [%d]", self, valid);
			return -1;
	}

	dbmail_imap_session_set_state(self,CLIENTSTATE_AUTHENTICATED);

	return 0;

}

int dbmail_imap_session_prompt(ImapSession * self, char * prompt)
{
	char *prompt64, *promptcat;
	
	g_return_val_if_fail(prompt != NULL, -1);
	
	/* base64 encoding increases string length by about 40%. */
	promptcat = g_strdup_printf("%s\r\n", prompt);
	prompt64 = (char *)g_base64_encode((const guchar *)promptcat, strlen(promptcat));
	dbmail_imap_session_buff_printf(self, "+ %s\r\n", prompt64);
	dbmail_imap_session_buff_flush(self);
	
	g_free(prompt64);
	g_free(promptcat);
	
	return 0;
}

int dbmail_imap_session_mailbox_get_selectable(ImapSession * self, u64_t idnr)
{
	/* check if mailbox is selectable */
	int selectable;
	selectable = db_isselectable(idnr);
	if (selectable == -1) {
		dbmail_imap_session_buff_printf(self, "* BYE internal dbase error\r\n");
		return -1;
	}
	if (selectable == FALSE) {
		dbmail_imap_session_buff_printf(self, "%s NO specified mailbox is not selectable\r\n", self->tag);
		return 1;
	}
	return 0;
}

static void notify_fetch(ImapSession *self, MailboxState_T N, u64_t *uid)
{
	u64_t *msn;

	GList *ol = NULL, *nl = NULL;
	char *oldflags = NULL, *newflags = NULL;
	MessageInfo *old = NULL, *new = NULL;
	MailboxState_T M = self->mailbox->mbstate;
	GTree *old_recent;

	assert(uid);

	if (! (MailboxState_getMsginfo(N) && *uid && (new = g_tree_lookup(MailboxState_getMsginfo(N), uid))))
		return;

	if (! (msn = g_tree_lookup(MailboxState_getIds(M), uid)))
		return;

	old_recent = MailboxState_steal_recent(M);
	MailboxState_merge_recent(N, old_recent);

	// FETCH
	if ((old = g_tree_lookup(MailboxState_getMsginfo(M), uid)))
		ol = MailboxState_message_flags(M, old);
	oldflags = dbmail_imap_plist_as_string(ol);

	nl = MailboxState_message_flags(N, new);
	newflags = dbmail_imap_plist_as_string(nl);

	TRACE(TRACE_DEBUG, "oldflags [%s] newflags [%s]", oldflags, newflags);

	g_list_destroy(ol);
	g_list_destroy(nl);

	if (oldflags && (! MATCH(oldflags, newflags))) {
		char *t = NULL;
		if (self->use_uid) t = g_strdup_printf(" UID %llu", *uid);
		dbmail_imap_session_buff_printf(self, "* %llu FETCH (FLAGS %s%s)\r\n", 
				*msn, newflags, t?t:"");

		if (t) g_free(t);
	}

	if (oldflags) g_free(oldflags);
	g_free(newflags);
}

static gboolean notify_expunge(ImapSession *self, u64_t *uid)
{
	u64_t *msn = NULL, m = 0;

	if (! (msn = g_tree_lookup(MailboxState_getIds(self->mailbox->mbstate), uid))) {
		TRACE(TRACE_DEBUG,"[%p] can't find uid [%llu]", self, *uid);
		return TRUE;
	}

	switch (self->command_type) {
		case IMAP_COMM_FETCH:
		case IMAP_COMM_STORE:
		case IMAP_COMM_SEARCH:
			break;
		default:
			m = *msn;
			if (MailboxState_removeUid(self->mailbox->mbstate, *uid) == DM_SUCCESS)
				dbmail_imap_session_buff_printf(self, "* %llu EXPUNGE\r\n", m);
			else
				return TRUE;
		break;
	}

	return FALSE;
}

static void mailbox_notify_expunge(ImapSession *self, MailboxState_T N)
{
	u64_t *uid, *msn;
	MailboxState_T M;
	GList *ids;
	if (! N) return;

	M = self->mailbox->mbstate;

	ids  = g_tree_keys(MailboxState_getIds(M));
	ids = g_list_reverse(ids);

	// send expunge updates
	
	if (ids) {
		uid = (u64_t *)ids->data;
		msn = g_tree_lookup(MailboxState_getIds(self->mailbox->mbstate), uid);
		if (msn && (*msn > MailboxState_getExists(N))) {
			TRACE(TRACE_DEBUG,"exists new [%d] old: [%d]", MailboxState_getExists(N), MailboxState_getExists(M)); 
			dbmail_imap_session_buff_printf(self, "* %d EXISTS\r\n", MailboxState_getExists(M));
		}
	}

	while (ids) {
		uid = (u64_t *)ids->data;
		if (! g_tree_lookup(MailboxState_getIds(N), uid)) {
			notify_expunge(self, uid);
		}

		if (! g_list_next(ids)) break;
		ids = g_list_next(ids);
	}
	ids = g_list_first(ids);
	g_list_free(ids);
}

static void mailbox_notify_fetch(ImapSession *self, MailboxState_T N)
{
	u64_t *uid, *id;
	GList *ids;
	if (! N) return;

	// send fetch updates
	ids = g_tree_keys(MailboxState_getIds(self->mailbox->mbstate));
	ids = g_list_first(ids);
	while (ids) {
		uid = (u64_t *)ids->data;
		notify_fetch(self, N, uid);
		if (! g_list_next(ids)) break;
		ids = g_list_next(ids);
	}
	g_list_free(g_list_first(ids));

	// switch active mailbox view
	self->mailbox->mbstate = N;
	id = g_new0(u64_t,1);
	*id = MailboxState_getId(N);
	g_tree_replace(self->mbxinfo, id, N);

	MailboxState_flush_recent(N);
}

int dbmail_imap_session_mailbox_status(ImapSession * self, gboolean update)
{
	/* 
		C: a047 NOOP
		S: * 22 EXPUNGE
		S: * 23 EXISTS
		S: * 3 RECENT
		S: * 14 FETCH (FLAGS (\Seen \Deleted))
	*/

	MailboxState_T M, N = NULL;
	gboolean showexists = FALSE, showrecent = FALSE, showflags = FALSE;
	unsigned oldexists;

	if (self->state != CLIENTSTATE_SELECTED) return FALSE;

	if (update) {
		unsigned oldseq;
		u64_t olduidnext;
		char *oldflags, *newflags;

		M = self->mailbox->mbstate;
		oldseq = MailboxState_getSeq(M);
		oldflags = MailboxState_flags(M);
		oldexists = MailboxState_getExists(M);
		olduidnext = MailboxState_getUidnext(M);

                // re-read flags and counters
		N = MailboxState_new(self->mailbox->id);

		if (oldseq != MailboxState_getSeq(N)) {
			// rebuild uid/msn trees
			// ATTN: new messages shouldn't be visible in any way to a 
			// client session until it has been announced with EXISTS

			// EXISTS response may never decrease
			if ((MailboxState_getUidnext(N) > olduidnext)) {
				if (MailboxState_getExists(N) > oldexists)
					showexists = TRUE;
			}

			if (MailboxState_getRecent(N))
				showrecent = TRUE;

			newflags = MailboxState_flags(N);
			if (! MATCH(newflags,oldflags))
				showflags = TRUE;
			g_free(newflags);
		}
		g_free(oldflags);
	}

	// command specific overrides
	switch (self->command_type) {
		case IMAP_COMM_EXAMINE:
		case IMAP_COMM_SELECT:
		case IMAP_COMM_SEARCH:
		case IMAP_COMM_SORT:
			showexists = showrecent = TRUE;
		break;

		default:
			// ok show them if needed
		break;
	}

	// never decrease without first sending expunge !!
	if (N) {
		if (MailboxState_getExists(N) > MailboxState_getExists(M))
			showexists = TRUE;

		if (showexists && MailboxState_getExists(N)) 
			dbmail_imap_session_buff_printf(self, "* %u EXISTS\r\n", MailboxState_getExists(N));

		if (showrecent && MailboxState_getRecent(N))
			dbmail_imap_session_buff_printf(self, "* %u RECENT\r\n", MailboxState_getRecent(N));

		mailbox_notify_expunge(self, N);

		if (showflags) {
			char *flags = MailboxState_flags(N);
			dbmail_imap_session_buff_printf(self, "* FLAGS (%s)\r\n", flags);
			dbmail_imap_session_buff_printf(self, "* OK [PERMANENTFLAGS (%s \\*)] Flags allowed.\r\n", flags);
			g_free(flags);
		}

		mailbox_notify_fetch(self, N);
	}

	return 0;
}

MailboxState_T dbmail_imap_session_mbxinfo_lookup(ImapSession *self, u64_t mailbox_id)
{
	MailboxState_T M = NULL;
	u64_t *id;
	int reload = FALSE;

	switch (self->command_type) {
		case IMAP_COMM_SELECT:
		case IMAP_COMM_EXAMINE:
			reload = TRUE;
			break;
	}
	
	TRACE(TRACE_DEBUG, "[%p] mailbox_id [%llu]", self, mailbox_id);

	if (reload) {
		id = g_new0(u64_t,1);
		*id = mailbox_id;
		M = MailboxState_new(mailbox_id);
		g_tree_replace(self->mbxinfo, id, M);
	} else if (self->mailbox && self->mailbox->mbstate && (MailboxState_getId(self->mailbox->mbstate) == mailbox_id)) {
		// selected state
		M = self->mailbox->mbstate;
	} else {
		M = (MailboxState_T)g_tree_lookup(self->mbxinfo, &mailbox_id);
		if (! M) {
			id = g_new0(u64_t,1);
			*id = mailbox_id;
			M = MailboxState_new(mailbox_id);
			g_tree_replace(self->mbxinfo, id, M);
		}
	}

	assert(M);

	return M;
}

int dbmail_imap_session_set_state(ImapSession *self, clientstate_t state)
{
	TRACE(TRACE_DEBUG,"state [%d]", state);
	if (self->state == state)
		return 0;

	switch (state) {
		case CLIENTSTATE_ERROR:
			assert(self->ci);
			if (self->ci->wev) event_del(self->ci->wev);
			// fall-through...

		case CLIENTSTATE_LOGOUT:
			assert(self->ci);
			if (self->ci->rev) event_del(self->ci->rev);
			break;

		case CLIENTSTATE_AUTHENTICATED:
			// change from login_timeout to main timeout
			assert(self->ci);
			TRACE(TRACE_DEBUG,"[%p] set timeout to [%d]", self, server_conf->timeout);
			self->ci->timeout->tv_sec = server_conf->timeout; 
			Capa_remove(self->capa, "AUTH=login");
			Capa_remove(self->capa, "AUTH=CRAM-MD5");

			break;

		default:
			break;
	}

	TRACE(TRACE_DEBUG,"[%p] state [%d]->[%d]", self, self->state, state);
	self->state = state;

	return 0;
}

static gboolean _do_expunge(u64_t *id, ImapSession *self)
{
	MessageInfo *msginfo = g_tree_lookup(MailboxState_getMsginfo(self->mailbox->mbstate), id);
	assert(msginfo);

	if (! msginfo->flags[IMAP_FLAG_DELETED]) return FALSE;

	if (db_exec(self->c, "UPDATE %smessages SET status=%d WHERE message_idnr=%llu ", DBPFX, MESSAGE_STATUS_DELETE, *id) == DM_EQUERY)
		return TRUE;

	return notify_expunge(self, id);
}

int dbmail_imap_session_mailbox_expunge(ImapSession *self)
{
	u64_t mailbox_size;
	int i;
	GList *ids;
	MailboxState_T M = self->mailbox->mbstate;

	if (! (i = g_tree_nnodes(MailboxState_getMsginfo(M))))
		return DM_SUCCESS;

	if (db_get_mailbox_size(self->mailbox->id, 1, &mailbox_size) == DM_EQUERY)
		return DM_EQUERY;

	ids = g_tree_keys(MailboxState_getMsginfo(M));
	ids = g_list_reverse(ids);

	self->c = db_con_get();
	db_begin_transaction(self->c);
	g_list_foreach(ids, (GFunc) _do_expunge, self);
	db_commit_transaction(self->c);
	db_con_close(self->c);
	self->c = NULL;

	ids = g_list_first(ids);
	g_list_free(ids);

	if (i > g_tree_nnodes(MailboxState_getMsginfo(M))) {
		db_mailbox_seq_update(self->mailbox->id);
		if (! dm_quota_user_dec(self->userid, mailbox_size))
			return DM_EQUERY;
	}

	return 0;
}

/*****************************************************************************
 *
 *
 *   bodyfetch
 *
 *
 ****************************************************************************/
void dbmail_imap_session_bodyfetch_new(ImapSession *self) 
{

	assert(self->fi);
	body_fetch_t *bodyfetch = g_new0(body_fetch_t, 1);
	bodyfetch->itemtype = -1;
	self->fi->bodyfetch = g_list_append(self->fi->bodyfetch, bodyfetch);
}

void dbmail_imap_session_bodyfetch_rewind(ImapSession *self) 
{
	assert(self->fi);
	self->fi->bodyfetch = g_list_first(self->fi->bodyfetch);
}

static void _body_fetch_free(body_fetch_t *bodyfetch, gpointer UNUSED data)
{
	if (! bodyfetch) return;
	if (bodyfetch->hdrnames) g_free(bodyfetch->hdrnames);
	if (bodyfetch->hdrplist) g_free(bodyfetch->hdrplist);
	if (bodyfetch->headers) {
		g_tree_destroy(bodyfetch->headers);
		bodyfetch->headers = NULL;
	}
	g_free(bodyfetch);
	bodyfetch = NULL;
}

void dbmail_imap_session_bodyfetch_free(ImapSession *self) 
{
	assert(self->fi);
	if (! self->fi->bodyfetch) return;
	self->fi->bodyfetch = g_list_first(self->fi->bodyfetch);
	g_list_foreach(self->fi->bodyfetch, (GFunc)_body_fetch_free, NULL);
	g_list_free(g_list_first(self->fi->bodyfetch));
	self->fi->bodyfetch = NULL;

}

body_fetch_t * dbmail_imap_session_bodyfetch_get_last(ImapSession *self) 
{
	assert(self->fi);
	if (! self->fi->bodyfetch) dbmail_imap_session_bodyfetch_new(self);
	
	self->fi->bodyfetch = g_list_last(self->fi->bodyfetch);
	return (body_fetch_t *)self->fi->bodyfetch->data;
}

int dbmail_imap_session_bodyfetch_set_partspec(ImapSession *self, char *partspec, int length) 
{
	assert(self->fi);
	body_fetch_t *bodyfetch = dbmail_imap_session_bodyfetch_get_last(self);
	memset(bodyfetch->partspec,'\0',IMAP_MAX_PARTSPEC_LEN);
	memcpy(bodyfetch->partspec,partspec,length);
	return 0;
}
char *dbmail_imap_session_bodyfetch_get_last_partspec(ImapSession *self) 
{
	assert(self->fi);
	body_fetch_t *bodyfetch = dbmail_imap_session_bodyfetch_get_last(self);
	return bodyfetch->partspec;
}

int dbmail_imap_session_bodyfetch_set_itemtype(ImapSession *self, int itemtype) 
{
	assert(self->fi);
	body_fetch_t *bodyfetch = dbmail_imap_session_bodyfetch_get_last(self);
	bodyfetch->itemtype = itemtype;
	return 0;
}
int dbmail_imap_session_bodyfetch_get_last_itemtype(ImapSession *self) 
{
	assert(self->fi);
	body_fetch_t *bodyfetch = dbmail_imap_session_bodyfetch_get_last(self);
	return bodyfetch->itemtype;
}
int dbmail_imap_session_bodyfetch_set_argstart(ImapSession *self) 
{
	assert(self->fi);
	body_fetch_t *bodyfetch = dbmail_imap_session_bodyfetch_get_last(self);
	bodyfetch->argstart = self->args_idx;
	return bodyfetch->argstart;
}
int dbmail_imap_session_bodyfetch_get_last_argstart(ImapSession *self) 
{
	assert(self->fi);
	body_fetch_t *bodyfetch = dbmail_imap_session_bodyfetch_get_last(self);
	return bodyfetch->argstart;
}
int dbmail_imap_session_bodyfetch_set_argcnt(ImapSession *self) 
{
	assert(self->fi);
	body_fetch_t *bodyfetch = dbmail_imap_session_bodyfetch_get_last(self);
	bodyfetch->argcnt = self->args_idx - bodyfetch->argstart;
	return bodyfetch->argcnt;
}
int dbmail_imap_session_bodyfetch_get_last_argcnt(ImapSession *self) 
{
	assert(self->fi);
	body_fetch_t *bodyfetch = dbmail_imap_session_bodyfetch_get_last(self);
	return bodyfetch->argcnt;
}
int dbmail_imap_session_bodyfetch_set_octetstart(ImapSession *self, guint64 octet)
{
	assert(self->fi);
	body_fetch_t *bodyfetch = dbmail_imap_session_bodyfetch_get_last(self);
	bodyfetch->octetstart = octet;
	return 0;
}
guint64 dbmail_imap_session_bodyfetch_get_last_octetstart(ImapSession *self)
{
	assert(self->fi);
	body_fetch_t *bodyfetch = dbmail_imap_session_bodyfetch_get_last(self);
	return bodyfetch->octetstart;
}
int dbmail_imap_session_bodyfetch_set_octetcnt(ImapSession *self, guint64 octet)
{
	assert(self->fi);
	body_fetch_t *bodyfetch = dbmail_imap_session_bodyfetch_get_last(self);
	bodyfetch->octetcnt = octet;
	return 0;
}
guint64 dbmail_imap_session_bodyfetch_get_last_octetcnt(ImapSession *self)
{
	assert(self->fi);
	body_fetch_t *bodyfetch = dbmail_imap_session_bodyfetch_get_last(self);
	return bodyfetch->octetcnt;
}


/* local defines */
#define NORMPAR 1
#define SQUAREPAR 2
#define NOPAR 0

/*
 * imap4_tokenizer_main()
 *
 * imap command tokenizer
 *
 * builds an dimensional array of strings containing arguments based upon 
 * strings on cmd line specified by {##}\0
 * (\r\n had been removed from string)
 * 
 * normal/square parentheses have special meaning:
 * '(body [all header])' will result in the following array:
 * [0] = '('
 * [1] = 'body'
 * [2] = '['
 * [3] = 'all'
 * [4] = 'header'
 * [5] = ']'
 * [6] = ')'
 *
 * quoted strings are those enclosed by double quotation marks and returned as a single argument
 * WITHOUT the enclosing quotation marks
 *
 * parentheses loose their special meaning if inside (double)quotation marks;
 * data should be 'clarified' (see clarify_data() function below)
 *
 * The returned array will be NULL-terminated.
 * Will return NULL upon errors.
 */

int imap4_tokenizer_main(ImapSession *self, const char *buffer)
{
	int inquote = 0, quotestart = 0;
	int nnorm = 0, nsquare = 0, paridx = 0, argstart = 0;
	unsigned int i = 0;
	size_t max;
	char parlist[MAX_LINESIZE];
	char *s, *lastchar;

	assert(buffer);

	/* Check for zero length input */
	if (! strlen(buffer)) goto finalize;

	s = (char *)buffer;

	max = strlen(s);

	TRACE(TRACE_DEBUG,"[%p] tokenize [%lu/%lu] [%s]", self, max, self->ci->rbuff_size, buffer);

	assert(max <= MAX_LINESIZE);

	/* find the arguments */
	paridx = 0;
	parlist[paridx] = NOPAR;

	inquote = 0;

	// if we're not fetching string-literals it's safe to strip NL
	if (self->ci->rbuff_size) {
		assert(max <= self->ci->rbuff_size);

		if (! self->args[self->args_idx])
			self->args[self->args_idx] = g_new0(gchar, self->ci->rbuff_size+1);

		strncat(self->args[self->args_idx], buffer, max);
		self->ci->rbuff_size -= max;
		if (self->ci->rbuff_size == 0) {
			self->args_idx++; // move on to next token
			TRACE(TRACE_DEBUG, "string literal complete. last-char [%d]", s[max-1]);
			if (MATCH(self->command,"APPEND")) {
				TRACE(TRACE_DEBUG,"break out to finalize");
				goto finalize;
			}

		}

		return 0;

	} else {
		g_strchomp(s); 
		max = strlen(s);
	}

	if (self->args[0]) {
		if (MATCH(self->args[0],"LOGIN")) {
			size_t len;
			if (self->args_idx == 2) {
				/* decode and store the password */
				self->args[self->args_idx++] = dm_base64_decode(s, &len);
				goto finalize; // done
			} else if (self->args_idx == 1) {
				/* decode and store the username */
				self->args[self->args_idx++] = dm_base64_decode(s, &len);
				/* ask for password */
				dbmail_imap_session_prompt(self,"password");
				return 0;
			}
		} else if (MATCH(self->args[0],"CRAM-MD5")) {
			if (self->args_idx == 1) {
				/* decode and store the response */
				if (! Cram_decode(self->ci->auth, s)) {
					Cram_free(&self->ci->auth);
					return -1;
				}
				self->args_idx++;
				goto finalize; // done
			}

		}
	}

	for (i = 0; (i < max) && s[i] && (self->args_idx < MAX_ARGS - 1); i++) {
		/* check quotes */
		if ((s[i] == '"') && ((i > 0 && s[i - 1] != '\\') || i == 0)) {
			if (inquote) {
				/* quotation end, treat quoted string as argument */
				self->args[self->args_idx] = g_new0(char,(i - quotestart));
				memcpy((void *) self->args[self->args_idx], (void *) &s[quotestart + 1], i - quotestart - 1);
				self->args[self->args_idx][i - quotestart - 1] = '\0';

				self->args_idx++;
				inquote = 0;
			} else {
				inquote = 1;
				quotestart = i;
			}
			continue;
		}

		if (inquote) continue;

		//if strchr("[]()",s[i]) {
		if (s[i] == '(' || s[i] == ')' || s[i] == '[' || s[i] == ']') {
			switch (s[i]) {
				/* check parenthese structure */
				case ')':
					
				if (paridx < 0 || parlist[paridx] != NORMPAR)
					paridx = -1;
				else {
					nnorm--;
					paridx--;
				}

				break;

				case ']':
				
				if (paridx < 0 || parlist[paridx] != SQUAREPAR)
					paridx = -1;
				else {
					paridx--;
					nsquare--;
				}

				break;

				case '(':
				
				parlist[++paridx] = NORMPAR;
				nnorm++;
				
				break;

				case '[':
				
				parlist[++paridx] = SQUAREPAR;
				nsquare++;

				break;
			}

			if (paridx < 0) return -1; /* error in parenthesis structure */
				
			/* add this parenthesis to the arg list and continue */
			self->args[self->args_idx] = g_new0(char,2);
			self->args[self->args_idx][0] = s[i];
			self->args[self->args_idx][1] = '\0';

			self->args_idx++;
			continue;
		}

		if (s[i] == ' ') continue;

		/* check for {number}\0 */
		if (s[i] == '{') {
			unsigned long int octets = strtoul(&s[i + 1], &lastchar, 10);

			/* only continue if the number is followed by '}\0' */
			if (
					(*lastchar == '}' && *(lastchar + 1) == '\0') ||
					(*lastchar == '+' && *(lastchar + 1) == '}' && *(lastchar + 2) == '\0')
			   ) {
				self->ci->rbuff_size = octets;
				dbmail_imap_session_buff_printf(self, "+ OK\r\n");
				dbmail_imap_session_buff_flush(self);
				return 0;
			}
		}
		/* at an argument start now, walk on until next delimiter
		 * and save argument 
		 */
		for (argstart = i; i < strlen(s) && !strchr(" []()", s[i]); i++) {
			if (s[i] == '"') {
				if (s[i - 1] == '\\')
					continue;
				else
					break;
			}
		}

		self->args[self->args_idx] = g_new0(char,(i - argstart + 1));
		memcpy((void *) self->args[self->args_idx], (void *) &s[argstart], i - argstart);
		self->args[self->args_idx][i - argstart] = '\0';
		self->args_idx++;
		i--;		/* walked one too far */
	}

	if (paridx != 0) return -1; /* error in parenthesis structure */
		
finalize:
	if (self->args_idx == 1) {
		if (MATCH(self->args[0],"LOGIN")) {
			TRACE(TRACE_DEBUG, "[%p] prompt for authenticate tokens", self);

			/* ask for username */
			dbmail_imap_session_prompt(self,"username");
			return 0;
		} else if (MATCH(self->args[0],"CRAM-MD5")) {
			const gchar *s;
			gchar *t;
			self->ci->auth = Cram_new();
			s = Cram_getChallenge(self->ci->auth);
			t = (char *)g_base64_encode((const guchar *)s, strlen(s));
			dbmail_imap_session_buff_printf(self, "+ %s\r\n", t);
			dbmail_imap_session_buff_flush(self);
			g_free(t);
	
			return 0;
		}
	}


	TRACE(TRACE_DEBUG, "[%p] tag: [%s], command: [%s], [%llu] args", self, self->tag, self->command, self->args_idx);
	self->args[self->args_idx] = NULL;	/* terminate */

//#if 1
	for (i = 0; i<=self->args_idx && self->args[i]; i++) { 
		TRACE(TRACE_DEBUG, "[%p] arg[%d]: '%s'\n", self, i, self->args[i]); 
	}
//#endif
	self->args_idx = 0;

	return 1;
}

#undef NOPAR
#undef NORMPAR
#undef RIGHTPAR



