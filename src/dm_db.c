/*  */
/*
  Copyright (C) 1999-2004 IC & S  dbmail@ic-s.nl
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
 * \file db.c
 * 
 */

#include "dbmail.h"
#include "dm_mailboxstate.h"

#define THIS_MODULE "db"
#define DEF_FRAGSIZE 64

// Flag order defined in dbmailtypes.h
static const char *db_flag_desc[] = {
	"seen_flag", "answered_flag", "deleted_flag", "flagged_flag", "draft_flag", "recent_flag" };
const char *imap_flag_desc[] = {
	"Seen", "Answered", "Deleted", "Flagged", "Draft", "Recent" };
const char *imap_flag_desc_escaped[] = {
	"\\Seen", "\\Answered", "\\Deleted", "\\Flagged", "\\Draft", "\\Recent" };

extern db_param_t _db_params;
#define DBPFX _db_params.pfx

/*
 * builds query values for matching mailbox names case insensitivity
 * supports utf7 
 * caller must free the return value.
 */
struct mailbox_match * mailbox_match_new(const char *mailbox)
{
	struct mailbox_match *res = g_new0(struct mailbox_match,1);
	char *sensitive, *insensitive;
	char ** tmparr;
	size_t i, len;
	int verbatim = 0, has_sensitive_part = 0;
	char p = 0, c = 0;

	tmparr = g_strsplit(mailbox, "_", -1);
	sensitive = g_strjoinv("\\_",tmparr);
	insensitive = g_strdup(sensitive);
	g_strfreev(tmparr);

 	len = strlen(sensitive);

	for (i = 0; i < len; i++) {
		c = sensitive[i];
		if (i>0)
			p = sensitive[i-1];

		switch (c) {
		case '&':
			verbatim = 1;
			has_sensitive_part = 1;
			break;
		case '-':
			verbatim = 0;
			break;
		}

		/* verbatim means that the case sensitive part must match
		 * and the case insensitive part matches anything,
		 * and vice versa.*/
		if (verbatim) {
			if (insensitive[i] != '\\')
				insensitive[i] = '_';
		} else {
			if (sensitive[i] != '\\')
				sensitive[i] = '_';
		}
	}

	if (has_sensitive_part) {
		res->insensitive = insensitive;
		res->sensitive = sensitive;
	} else {
		res->insensitive = insensitive;
		g_free(sensitive);
	}

	return res;
}

void mailbox_match_free(struct mailbox_match *m)
{
	if (! m) return;
	if (m->sensitive) g_free(m->sensitive);
	if (m->insensitive) g_free(m->insensitive);
	g_free(m); m=NULL;
}



/** list of tables used in dbmail */
#define DB_NTABLES 20
const char *DB_TABLENAMES[DB_NTABLES] = {
	"acl",
	"aliases",
	"envelope",
	"header",
	"headername",
	"headervalue",
	"keywords",
	"mailboxes",
	"messages",
	"mimeparts",
	"partlists",
	"pbsp",
	"physmessage",
	"referencesfield",
	"replycache",
	"sievescripts",
	"subscription",
	"usermap",
	"users"
};

#define REPLYCACHE_WIDTH 100

GTree * global_cache = NULL;
/////////////////////////////////////////////////////////////////////////////

/* globals for now... */
P pool = NULL;
U url = NULL;
int db_connected = 0; // 0 = not called, 1 = new url but not pool, 2 = new url and pool, but not tested, 3 = tested and ok

/* This is the first db_* call anybody should make. */
int db_connect(void)
{
	int sweepInterval = 60;
	C c;
	GString *dsn = g_string_new("");
	g_string_append_printf(dsn,"%s://",_db_params.driver);
	if (_db_params.host)
		g_string_append_printf(dsn,"%s", _db_params.host);
	if (_db_params.port)
		g_string_append_printf(dsn,":%u", _db_params.port);
	if (_db_params.db) {
		if (MATCH(_db_params.driver,"sqlite")) {

			/* expand ~ in db name to HOME env variable */
			if ((strlen(_db_params.db) > 0 ) && (_db_params.db[0] == '~')) {
				char *homedir;
				field_t db;
				if ((homedir = getenv ("HOME")) == NULL)
					TRACE(TRACE_EMERG, "can't expand ~ in db name");
				g_snprintf(db, FIELDSIZE, "%s%s", homedir, &(_db_params.db[1]));
				g_strlcpy(_db_params.db, db, FIELDSIZE);
			}

			g_string_append_printf(dsn,"%s", _db_params.db);
		} else {
			g_string_append_printf(dsn,"/%s", _db_params.db);
		}
	}
	if (_db_params.user && strlen((const char *)_db_params.user)) {
		g_string_append_printf(dsn,"?user=%s", _db_params.user);
		if (_db_params.pass && strlen((const char *)_db_params.pass)) 
			g_string_append_printf(dsn,"&password=%s", _db_params.pass);
		if (MATCH(_db_params.driver,"mysql")) {
			if (_db_params.encoding && strlen((const char *)_db_params.encoding))
				g_string_append_printf(dsn,"&charset=%s", _db_params.encoding);
		}
	}

	if (_db_params.sock && strlen((const char *)_db_params.sock))
		g_string_append_printf(dsn,"&unix-socket=%s", _db_params.sock);

	TRACE(TRACE_DATABASE, "db at url: [%s]", dsn->str);
	url = URL_new(dsn->str);
	db_connected = 1;
	g_string_free(dsn,TRUE);
	if (! (pool = ConnectionPool_new(url)))
		TRACE(TRACE_EMERG,"error creating database connection pool");
	db_connected = 2;
	
	if (_db_params.max_db_connections > 0) {
		if (_db_params.max_db_connections < (unsigned int)ConnectionPool_getInitialConnections(pool))
			ConnectionPool_setInitialConnections(pool, _db_params.max_db_connections);
		ConnectionPool_setMaxConnections(pool, _db_params.max_db_connections);
		TRACE(TRACE_INFO,"database connection pool created with maximum connections of [%d]", _db_params.max_db_connections);
	}

	ConnectionPool_setReaper(pool, sweepInterval);
	TRACE(TRACE_DATABASE, "run a database connection reaper thread every [%d] seconds", sweepInterval);

	ConnectionPool_start(pool);
	TRACE(TRACE_DATABASE, "database connection pool started with [%d] connections, max [%d]", 
		ConnectionPool_getInitialConnections(pool), ConnectionPool_getMaxConnections(pool));

	if (! (c = ConnectionPool_getConnection(pool))) {
		db_con_close(c);
		TRACE(TRACE_EMERG, "error getting a database connection from the pool");
		return -1;
	}
	db_connected = 3;
	db_con_close(c);

	return db_check_version();
}

/* But sometimes this gets called after help text or an
 * error but without a matching db_connect before it. */
int db_disconnect(void)
{
	if(db_connected >= 3) ConnectionPool_stop(pool);
	if(db_connected >= 2) ConnectionPool_free(&pool);
	if(db_connected >= 1) URL_free(&url);
	db_connected = 0;
	return 0;
}

C db_con_get(void)
{
	int i=0, k=0; C c;
	while (i++<30) {
		c = ConnectionPool_getConnection(pool);
		if (c) break;
		if((int)(i % 5)==0) {
			TRACE(TRACE_ALERT, "Thread is having trouble obtaining a database connection. Try [%d]", i);
			k = ConnectionPool_reapConnections(pool);
			TRACE(TRACE_INFO, "Database reaper closed [%d] stale connections", k);
		}
		sleep(1);
	}
	if (! c) {
		TRACE(TRACE_EMERG,"[%p] can't get a database connection from the pool! max [%d] size [%d] active [%d]", 
			pool,
			ConnectionPool_getMaxConnections(pool),
			ConnectionPool_size(pool),
			ConnectionPool_active(pool));
	}

	assert(c);
	Connection_setQueryTimeout(c, (int)_db_params.query_timeout);
	TRACE(TRACE_DATABASE,"[%p] connection from pool", c);
	return c;
}

gboolean dm_db_ping(void)
{
	C c; gboolean t;
	c = db_con_get();
	t = Connection_ping(c);
	db_con_close(c);

	if (!t) TRACE(TRACE_ERR,"database has gone away");

	return t;
}

void db_con_close(C c)
{
	TRACE(TRACE_DATABASE,"[%p] connection to pool", c);
	Connection_close(c);
	return;
}

void db_con_clear(C c)
{
	TRACE(TRACE_DATABASE,"[%p] connection cleared", c);
	Connection_clear(c);
	Connection_setQueryTimeout(c, (int)_db_params.query_timeout);
	return;
}

void log_query_time(char *query, struct timeval before, struct timeval after)
{
	double elapsed = ((double)after.tv_sec + ((double)after.tv_usec / 1000000)) - ((double)before.tv_sec + ((double)before.tv_usec / 1000000));
	TRACE(TRACE_DATABASE, "last query took [%.3f] seconds", elapsed);
	if (elapsed > (double)_db_params.query_time_warning)
		TRACE(TRACE_WARNING, "slow query [%s] took [%.3f] seconds", query, elapsed);
	else if (elapsed > (double)_db_params.query_time_notice)
		TRACE(TRACE_NOTICE, "slow query [%s] took [%.3f] seconds", query, elapsed);
	else if (elapsed > (double)_db_params.query_time_info)
		TRACE(TRACE_INFO, "slow query [%s] took [%.3f] seconds", query, elapsed);
	return;
}

gboolean db_exec(C c, const char *q, ...)
{
	struct timeval before, after;
	volatile gboolean result = FALSE;
	va_list ap, cp;
	char *query;

	va_start(ap, q);
	va_copy(cp, ap);
        query = g_strdup_vprintf(q, cp);
        va_end(cp);

	TRACE(TRACE_DATABASE,"[%p] [%s]", c, query);
	TRY
		gettimeofday(&before, NULL);
		Connection_execute(c, "%s", (const char *)query);
		gettimeofday(&after, NULL);
		result = TRUE;
	CATCH(SQLException)
		LOG_SQLERROR;
		TRACE(TRACE_ERR,"failed query [%s]", query);
	END_TRY;

	if (result) log_query_time(query, before, after);
	g_free(query);

	return result;
}

R db_query(C c, const char *q, ...)
{
	struct timeval before, after;
	R r = NULL;
	volatile gboolean result = FALSE;
	va_list ap, cp;
	char *query;

	va_start(ap, q);
	va_copy(cp, ap);
        query = g_strdup_vprintf(q, cp);
        va_end(cp);

	g_strstrip(query);

	TRACE(TRACE_DATABASE,"[%p] [%s]", c, query);
	TRY
		gettimeofday(&before, NULL);
		r = Connection_executeQuery(c, "%s", (const char *)query);
		gettimeofday(&after, NULL);
		result = TRUE;
	CATCH(SQLException)
		LOG_SQLERROR;
		TRACE(TRACE_ERR,"failed query [%s]", query);
	END_TRY;

	if (result) log_query_time(query, before, after);
	g_free(query);

	return r;
}



gboolean db_update(const char *q, ...)
{
	C c; volatile gboolean result = FALSE;
	va_list ap, cp;
	INIT_QUERY;

	va_start(ap, q);
	va_copy(cp, ap);
        vsnprintf(query, DEF_QUERYSIZE, q, cp);
        va_end(cp);

	c = db_con_get();
	TRY
		db_begin_transaction(c);
		result = db_exec(c, query);
		db_commit_transaction(c);
	CATCH(SQLException)
		LOG_SQLERROR;
		db_rollback_transaction(c);
	FINALLY
		db_con_close(c);
	END_TRY;

	return result;
}

S db_stmt_prepare(C c, const char *q, ...)
{
	va_list ap, cp;
	char *query;
	S s;

	va_start(ap, q);
	va_copy(cp, ap);
        query = g_strdup_vprintf(q, cp);
        va_end(cp);

	TRACE(TRACE_DATABASE,"[%p] [%s]", c, query);
	s = Connection_prepareStatement(c, "%s", (const char *)query);
	g_free(query);
	return s;
}

int db_stmt_set_str(S s, int index, const char *x)
{
	TRACE(TRACE_DATABASE,"[%p] %d:[%s]", s, index, x);
	PreparedStatement_setString(s, index, x);
	return TRUE;
}
int db_stmt_set_int(S s, int index, int x)
{
	TRACE(TRACE_DATABASE,"[%p] %d:[%d]", s, index, x);
	PreparedStatement_setInt(s, index, x);
	return TRUE;
}
int db_stmt_set_u64(S s, int index, u64_t x)
{	
	TRACE(TRACE_DATABASE,"[%p] %d:[%llu]", s, index, x);
	PreparedStatement_setLLong(s, index, (long long)x);
	return TRUE;
}
int db_stmt_set_blob(S s, int index, const void *x, int size)
{
	if (size > 200)
		TRACE(TRACE_DATABASE,"[%p] %d:[blob of length %d]", s, index, size);
	else
		TRACE(TRACE_DATABASE,"[%p] %d:[%s]", s, index, (char *)x);
	PreparedStatement_setBlob(s, index, x, size);
	return TRUE;
}
gboolean db_stmt_exec(S s)
{
	PreparedStatement_execute(s);
	return TRUE;
}
R db_stmt_query(S s)
{
	return PreparedStatement_executeQuery(s);
}
int db_result_next(R r)
{
	if (r)
		return ResultSet_next(r);
	else
		return FALSE;
}
unsigned db_num_fields(R r)
{
	return (unsigned)ResultSet_getColumnCount(r);
}
const char * db_result_get(R r, unsigned field)
{
	const char * val = ResultSet_getString(r, field+1);
	return val ? val : "";
}
int db_result_get_int(R r, unsigned field)
{
	return ResultSet_getInt(r, field+1);
}
int db_result_get_bool(R r, unsigned field)
{
	return (ResultSet_getInt(r, field+1) ? 1 : 0);
}
u64_t db_result_get_u64(R r, unsigned field)
{
	return (u64_t)(ResultSet_getLLong(r, field+1));
}
const void * db_result_get_blob(R r, unsigned field, int *size)
{
	const char * val = ResultSet_getBlob(r, field+1, size);
	if (!val) {
		*size = 0;
	}
	return val;
}

u64_t db_insert_result(C c, R r)
{
	u64_t id = 0;

	assert(r);
	db_result_next(r);

	// lastRowId is always zero for pgsql tables without OIDs
	// or possibly for sqlite after calling executeQuery but 
	// before calling db_result_next

	if ((id = (u64_t )Connection_lastRowId(c)) == 0) { // mysql
		// but if we're using 'RETURNING id' clauses on inserts
		// or we're using the sqlite backend, we can do this

		if ((id = (u64_t )Connection_lastRowId(c)) == 0) // sqlite
			id = db_result_get_u64(r, 0); // postgresql
	}
	assert(id);
	return id;
}

u64_t db_get_pk(C c, const char *table)
{
	R r;
	u64_t id = 0;
	r = db_query(c, "SELECT sq_%s%s.CURRVAL FROM DUAL", DBPFX, table);
	if (db_result_next(r))
		id = db_result_get_u64(r, 0);
	assert(id);
	return id;
}

int db_begin_transaction(C c)
{
	TRACE(TRACE_DATABASE,"BEGIN");
	Connection_beginTransaction(c);
	return DM_SUCCESS;
}

int db_commit_transaction(C c)
{
	TRACE(TRACE_DATABASE,"COMMIT");
	Connection_commit(c);
	return DM_SUCCESS;
}


int db_rollback_transaction(C c)
{
	TRACE(TRACE_DATABASE,"ROLLBACK");
	Connection_rollback(c);
	return DM_SUCCESS;
}

int db_savepoint(C UNUSED c, const char UNUSED *id)
{
	return 0;
}

int db_savepoint_rollback(C UNUSED c, const char UNUSED *id)
{
	return 0;
}

int db_do_cleanup(const char UNUSED **tables, int UNUSED num_tables)
{
	return 0;
}

static const char * db_get_sqlite_sql(sql_fragment_t frag)
{
	switch(frag) {
		case SQL_ENCODE_ESCAPE:
		case SQL_TO_CHAR:
		case SQL_STRCASE:
		case SQL_PARTIAL:
			return "%s";
		break;
		case SQL_TO_DATE:
			return "DATE(%s)";
		break;
		case SQL_TO_DATETIME:
			return "DATETIME(%s)";
		break;
		case SQL_TO_UNIXEPOCH:
			return "STRFTIME('%%s',%s)";
		break;
		case SQL_CURRENT_TIMESTAMP:
			return "STRFTIME('%%Y-%%m-%%d %%H:%%M:%%S','now','localtime')";
		break;
		case SQL_EXPIRE:
			return "DATETIME('now','-%d DAYS')";	
		break;
		case SQL_BINARY:
			return "";
		break;
		case SQL_SENSITIVE_LIKE:
			return "REGEXP";
		break;
		case SQL_INSENSITIVE_LIKE:
			return "LIKE";
		break;
		case SQL_IGNORE:
			return "OR IGNORE";
		break;
		case SQL_RETURNING:
		break;
		case SQL_TABLE_EXISTS:
			return "SELECT 1=1 FROM %s%s LIMIT 1 OFFSET 0";
		break;
		case SQL_ESCAPE_COLUMN:
			return "";
		break;
		case SQL_COMPARE_BLOB:
			return "%s=?";
		break;
	}
	return NULL;
}

static const char * db_get_mysql_sql(sql_fragment_t frag)
{
	switch(frag) {
		case SQL_TO_CHAR:
			return "DATE_FORMAT(%s, GET_FORMAT(DATETIME,'ISO'))";
		break;
		case SQL_TO_DATE:
			return "DATE(%s)";
		break;
		case SQL_TO_DATETIME:
			return "TIMESTAMP(%s)";
		break;
                case SQL_TO_UNIXEPOCH:
			return "UNIX_TIMESTAMP(%s)";
		break;
		case SQL_CURRENT_TIMESTAMP:
			return "NOW()";
		break;
		case SQL_EXPIRE:
			return "NOW() - INTERVAL %d DAY";
		break;
		case SQL_BINARY:
			return "BINARY";
		break;
		case SQL_SENSITIVE_LIKE:
			return "LIKE BINARY";
		break;
		case SQL_INSENSITIVE_LIKE:
			return "LIKE";
		break;
		case SQL_IGNORE:
			return "IGNORE";
		break;
		case SQL_STRCASE:
		case SQL_ENCODE_ESCAPE:
		case SQL_PARTIAL:
			return "%s";
		break;
		case SQL_RETURNING:
		break;
		case SQL_TABLE_EXISTS:
			return "SELECT 1=1 FROM %s%s LIMIT 1 OFFSET 0";
		break;
		case SQL_ESCAPE_COLUMN:
			return "`";
		break;
		case SQL_COMPARE_BLOB:
			return "%s=?";
		break;
	}
	return NULL;
}

static const char * db_get_pgsql_sql(sql_fragment_t frag)
{
	switch(frag) {
		case SQL_TO_CHAR:
			return "TO_CHAR(%s, 'YYYY-MM-DD HH24:MI:SS' )";
		break;
		case SQL_TO_DATE:
			return "TO_DATE(%s::text,'YYYY-MM-DD')";
		break;
		case SQL_TO_DATETIME:
			return "TO_TIMESTAMP(%s::text, 'YYYY-MM-DD HH24:MI:SS')";
		break;
		case SQL_TO_UNIXEPOCH:
			return "ROUND(DATE_PART('epoch',%s))";
		break;
		case SQL_CURRENT_TIMESTAMP:
			return "NOW()";
		break;
		case SQL_EXPIRE:
			return "NOW() - INTERVAL '%d DAY'";
		break;
		case SQL_IGNORE:
		case SQL_BINARY:
			return "";
		break;
		case SQL_SENSITIVE_LIKE:
			return "LIKE";
		break;
		case SQL_INSENSITIVE_LIKE:
			return "ILIKE";
		break;
		case SQL_ENCODE_ESCAPE:
			return "ENCODE(%s::bytea,'escape')";
		break;
		case SQL_STRCASE:
			return "LOWER(%s)";
		break;
		case SQL_PARTIAL:
			return "SUBSTRING(%s,0,255)";
		break;
		case SQL_RETURNING:
			return "RETURNING %s";
		break;
		case SQL_TABLE_EXISTS:
			return "SELECT 1=1 FROM %s%s LIMIT 1 OFFSET 0";
		break;
		case SQL_ESCAPE_COLUMN:
			return "\"";
		break;
		case SQL_COMPARE_BLOB:
			return "%s=?";
		break;
	}
	return NULL;
}

static const char * db_get_oracle_sql(sql_fragment_t frag)
{
	switch(frag) {
		case SQL_TO_CHAR:
			return "TO_CHAR(%s, 'YYYY-MM-DD HH24:MI:SS')";
		break;
		case SQL_TO_DATE:
			return "TO_DATE(%s, 'YYYY-MM-DD')";
		break;
		case SQL_TO_DATETIME:
			return "TO_DATE(%s, 'YYYY-MM-DD HH24:MI:SS')";
		break;
                case SQL_TO_UNIXEPOCH:
			return "DBMAIL_UTILS.UNIX_TIMESTAMP(%s)";
		break;
		case SQL_CURRENT_TIMESTAMP:
			return "SYSTIMESTAMP";
		break;
		case SQL_EXPIRE:
			return "SYSTIMESTAMP - NUMTODSINTERVAL(%d,'day')"; 
		break;
		case SQL_BINARY:
			return "";
		break;
		case SQL_SENSITIVE_LIKE:
			return "LIKE";
		break;
		case SQL_INSENSITIVE_LIKE:
			return "LIKE";
		break;
		case SQL_IGNORE:
			return "";
		break;
		case SQL_STRCASE:
		case SQL_ENCODE_ESCAPE:
		case SQL_PARTIAL:
			return "%s";
		break;
		case SQL_RETURNING:
		break;
		case SQL_TABLE_EXISTS:
			return "SELECT TABLE_NAME FROM USER_TABLES WHERE TABLE_NAME='%s%s'";
		break;
		case SQL_ESCAPE_COLUMN:
			return "\"";
		break;
		case SQL_COMPARE_BLOB:
			return "DBMS_LOB.COMPARE(%s,?) = 0";
		break;
	}
	return NULL;
}



const char * db_get_sql(sql_fragment_t frag)
{
	switch(_db_params.db_driver) {
		case DM_DRIVER_SQLITE:
			return db_get_sqlite_sql(frag);
		case DM_DRIVER_MYSQL:
			return db_get_mysql_sql(frag);
		case DM_DRIVER_POSTGRESQL:
			return db_get_pgsql_sql(frag);
		case DM_DRIVER_ORACLE:
			return db_get_oracle_sql(frag);

	}

	TRACE(TRACE_EMERG, "driver not in [sqlite|mysql|postgresql|oracle]");

	return NULL;
}

char *db_returning(const char *s)
{
	char *r = NULL, *t = NULL;

	if ((t = (char *)db_get_sql(SQL_RETURNING)))
		r = g_strdup_printf(t,s);
	else
		r = g_strdup("");
	return r;
}

/////////////////////////////////////////////////////////////////////////////


/*
 * check to make sure the database has been upgraded
 */
static void check_table_exists(C c, const char *table, const char *errormessage)
{
	if (! db_query(c, db_get_sql(SQL_TABLE_EXISTS), DBPFX, table))
		TRACE(TRACE_EMERG, "%s", errormessage);
}

int db_check_version(void)
{
	C c = db_con_get();
	volatile int ok = 0;
	TRY
		check_table_exists(c, "physmessage", "pre-2.0 database incompatible. You need to run the conversion script");
		check_table_exists(c, "headervalue", "2.0 database incompatible. You need to add the header tables.");
		check_table_exists(c, "envelope", "2.1+ database incompatible. You need to add the envelopes table and run dbmail-util -by");
		check_table_exists(c, "mimeparts", "3.x database incompatible.");
		check_table_exists(c, "header", "3.x database incompatible - single instance header storage missing.");
		ok = 1;
	CATCH(SQLException)
		LOG_SQLERROR;
	FINALLY
		db_con_close(c);
	END_TRY;

	if (ok)
		TRACE(TRACE_DEBUG,"Tables OK");
	else
		TRACE(TRACE_WARNING,"Schema version incompatible. Bailing out");

	return ok?DM_SUCCESS:DM_EQUERY;
}

/* test existence of usermap table */
int db_use_usermap(void)
{
	int use_usermap = TRUE;
	C c = db_con_get();
	TRY
		if (! db_query(c, db_get_sql(SQL_TABLE_EXISTS), DBPFX, "usermap"))
			use_usermap = FALSE;
	CATCH(SQLException)
		LOG_SQLERROR;
	FINALLY
		db_con_close(c);
	END_TRY;

	TRACE(TRACE_DEBUG, "%s usermap lookups", use_usermap ? "enabling" : "disabling" );
	return use_usermap;
}

int db_get_physmessage_id(u64_t message_idnr, u64_t * physmessage_id)
{
	C c; R r; volatile int t = DM_SUCCESS;
	assert(physmessage_id != NULL);
	*physmessage_id = 0;

	c = db_con_get();
	TRY
		r = db_query(c, "SELECT physmessage_id FROM %smessages WHERE message_idnr = %llu", 
				DBPFX, message_idnr);
		if (db_result_next(r))
			*physmessage_id = db_result_get_u64(r, 0);
	CATCH(SQLException)
		LOG_SQLERROR;
		t = DM_EQUERY;
	FINALLY
		db_con_close(c);
	END_TRY;

	if (! *physmessage_id) return DM_EGENERAL;

	return t;
}


/**
 * check if the user_idnr is the same as that of the DBMAIL_DELIVERY_USERNAME
 * \param user_idnr user idnr to check
 * \return
 *     - -1 on error
 *     -  0 of different user
 *     -  1 if same user (user_idnr belongs to DBMAIL_DELIVERY_USERNAME
 */

static int user_idnr_is_delivery_user_idnr(u64_t user_idnr)
{
	static int delivery_user_idnr_looked_up = 0;
	static u64_t delivery_user_idnr;
	static GStaticMutex mutex = G_STATIC_MUTEX_INIT;

	if (delivery_user_idnr_looked_up == 0) {
		u64_t idnr;
		TRACE(TRACE_DEBUG, "looking up user_idnr for [%s]", DBMAIL_DELIVERY_USERNAME);
		if (! auth_user_exists(DBMAIL_DELIVERY_USERNAME, &idnr)) {
			TRACE(TRACE_ERR, "error looking up user_idnr for %s", DBMAIL_DELIVERY_USERNAME);
			return DM_EQUERY;
		}
		g_static_mutex_lock(&mutex);
		delivery_user_idnr = idnr;
		delivery_user_idnr_looked_up = 1;
		g_static_mutex_unlock(&mutex);
	}
	
	if (delivery_user_idnr == user_idnr)
		return DM_EGENERAL;
	else
		return DM_SUCCESS;
}

#define NOT_DELIVERY_USER \
	int result; \
	if ((result = user_idnr_is_delivery_user_idnr(user_idnr)) == DM_EQUERY) return DM_EQUERY; \
	if (result == DM_EGENERAL) return DM_EGENERAL;


int dm_quota_user_get(u64_t user_idnr, u64_t *size)
{
	C c; R r;
	assert(size != NULL);

	c = db_con_get();
	TRY
		r = db_query(c, "SELECT curmail_size FROM %susers WHERE user_idnr = %llu", DBPFX, user_idnr);
		if (db_result_next(r))
			*size = db_result_get_u64(r, 0);
	CATCH(SQLException)
		LOG_SQLERROR;
	FINALLY	
		db_con_close(c);
	END_TRY;

	return DM_EGENERAL;
}

int dm_quota_user_set(u64_t user_idnr, u64_t size)
{
	NOT_DELIVERY_USER
	return db_update("UPDATE %susers SET curmail_size = %llu WHERE user_idnr = %llu", 
			DBPFX, size, user_idnr);
}
int dm_quota_user_inc(u64_t user_idnr, u64_t size)
{
	NOT_DELIVERY_USER
	return db_update("UPDATE %susers SET curmail_size = curmail_size + %llu WHERE user_idnr = %llu", 
			DBPFX, size, user_idnr);
}
int dm_quota_user_dec(u64_t user_idnr, u64_t size)
{
	NOT_DELIVERY_USER
	return db_update("UPDATE %susers SET curmail_size = CASE WHEN curmail_size >= %llu THEN curmail_size - %llu ELSE 0 END WHERE user_idnr = %llu", 
			DBPFX, size, size, user_idnr);
}

static int dm_quota_user_validate(u64_t user_idnr, u64_t msg_size)
{
	u64_t maxmail_size;
	C c; R r; volatile gboolean t = TRUE;

	if (auth_getmaxmailsize(user_idnr, &maxmail_size) == -1) {
		TRACE(TRACE_ERR, "auth_getmaxmailsize() failed\n");
		return DM_EQUERY;
	}

	if (maxmail_size <= 0)
		return TRUE;
 
	c = db_con_get();

	TRY
		r = db_query(c, "SELECT 1 FROM %susers WHERE user_idnr = %llu "
				"AND (curmail_size + %llu > %llu)", 
				DBPFX, user_idnr, msg_size, maxmail_size);
		if (! r)
			t = DM_EQUERY;
		else if (db_result_next(r))
			t = FALSE;
	CATCH(SQLException)
		LOG_SQLERROR;
	FINALLY
		db_con_close(c);
	END_TRY;

	return t;
}

int dm_quota_rebuild_user(u64_t user_idnr)
{
	C c; R r; volatile int t = DM_SUCCESS;
	u64_t quotum = 0;

	c = db_con_get();
	TRY
		r = db_query(c, "SELECT COALESCE(SUM(pm.messagesize),0) "
			 "FROM %sphysmessage pm, %smessages m, %smailboxes mb "
			 "WHERE m.physmessage_id = pm.id "
			 "AND m.mailbox_idnr = mb.mailbox_idnr "
			 "AND mb.owner_idnr = %llu " "AND m.status < %d",
			 DBPFX,DBPFX,DBPFX,user_idnr, MESSAGE_STATUS_DELETE);

		if (db_result_next(r))
			quotum = db_result_get_u64(r, 0);
		else
			TRACE(TRACE_WARNING, "SUM did not give result, assuming empty mailbox" );

	CATCH(SQLException)
		LOG_SQLERROR;
		t = DM_EQUERY;
	FINALLY
		db_con_close(c);
	END_TRY;

	if (t == DM_EQUERY) return t;

	TRACE(TRACE_DEBUG, "found quotum usage of [%llu] bytes", quotum);
	/* now insert the used quotum into the users table */
	if (! dm_quota_user_set(user_idnr, quotum)) 
		return DM_EQUERY;
	return DM_SUCCESS;
}

struct used_quota {
	u64_t user_id;
	u64_t curmail;
};

int dm_quota_rebuild()
{
	C c; R r;

	GList *quota = NULL;
	struct used_quota *q;
	int i = 0;
	int result;

	c = db_con_get();
	TRY
		r = db_query(c, "SELECT usr.user_idnr, SUM(pm.messagesize), usr.curmail_size FROM %susers usr "
				"LEFT JOIN %smailboxes mbx ON mbx.owner_idnr = usr.user_idnr "
				"LEFT JOIN %smessages msg ON msg.mailbox_idnr = mbx.mailbox_idnr "
				"LEFT JOIN %sphysmessage pm ON pm.id = msg.physmessage_id "
				"AND msg.status < %d "
				"GROUP BY usr.user_idnr, usr.curmail_size "
				"HAVING ((SUM(pm.messagesize) <> usr.curmail_size) OR "
				"(NOT (SUM(pm.messagesize) IS NOT NULL) "
				"AND usr.curmail_size <> 0))", DBPFX,DBPFX,
				DBPFX,DBPFX,MESSAGE_STATUS_DELETE);

		while (db_result_next(r)) {
			i++;
			q = g_new0(struct used_quota,1);
			q->user_id = db_result_get_u64(r, 0);
			q->curmail = db_result_get_u64(r, 1);
			quota = g_list_prepend(quota, q);
		}
	CATCH(SQLException)
		LOG_SQLERROR;
	FINALLY
		db_con_close(c);
	END_TRY;

	result = i;
	if (! i) {
		TRACE(TRACE_DEBUG, "quotum is already up to date");
		return DM_SUCCESS;
	}

	/* now update the used quotum for all users that need to be updated */
	quota = g_list_first(quota);
	while (quota) {
		q = (struct used_quota *)quota->data;	
		if (! dm_quota_user_set(q->user_id, q->curmail))
			result = DM_EQUERY;

		if (! g_list_next(quota)) break;
		quota = g_list_next(quota);
	}

	/* free allocated memory */
	g_list_destroy(quota);

	return result;
}



int db_get_notify_address(u64_t user_idnr, char **notify_address)
{
	C c; R r;
	const char *query_result = NULL;
	volatile int t = DM_EGENERAL;

	c = db_con_get();
	TRY
		r = db_query(c, "SELECT notify_address "
				"FROM %sauto_notifications WHERE user_idnr = %llu",
				DBPFX,user_idnr);
		if (db_result_next(r)) {
			query_result = db_result_get(r, 0);
			if (query_result && (strlen(query_result) > 0)) {
				*notify_address = g_strdup(query_result);
				TRACE(TRACE_DEBUG, "notify address [%s]", *notify_address);
			}
		}
		t = DM_SUCCESS;
	CATCH(SQLException)
		LOG_SQLERROR;
	FINALLY
		db_con_close(c);
	END_TRY;

	return t;
}

int db_get_reply_body(u64_t user_idnr, char **reply_body)
{
	C c; R r; S s;
	const char *query_result;
	volatile int t = DM_EGENERAL;
	*reply_body = NULL;

	c = db_con_get();
	TRY
		s = db_stmt_prepare(c, "SELECT reply_body FROM %sauto_replies "
				"WHERE user_idnr = ? "
				"AND %s BETWEEN start_date AND stop_date", 
				DBPFX,
				db_get_sql(SQL_CURRENT_TIMESTAMP));
		db_stmt_set_u64(s, 1, user_idnr);
		r = db_stmt_query(s);
		if (db_result_next(r)) {
			query_result = db_result_get(r, 0);
			if (query_result && (strlen(query_result)>0)) {
				*reply_body = g_strdup(query_result);
				TRACE(TRACE_DEBUG, "reply_body [%s]", *reply_body);
				t = DM_SUCCESS;
			}
		}
	CATCH(SQLException)
		LOG_SQLERROR;
	FINALLY
		db_con_close(c);
	END_TRY;
	return t;
}


u64_t db_get_useridnr(u64_t message_idnr)
{
	C c; R r;
	u64_t user_idnr = 0;
	c = db_con_get();
	TRY
		r = db_query(c, "SELECT %smailboxes.owner_idnr FROM %smailboxes, %smessages "
				"WHERE %smailboxes.mailbox_idnr = %smessages.mailbox_idnr "
				"AND %smessages.message_idnr = %llu", DBPFX,DBPFX,DBPFX,
				DBPFX,DBPFX,DBPFX,message_idnr);
		if (db_result_next(r))
			user_idnr = db_result_get_u64(r, 0);
	CATCH(SQLException)
		LOG_SQLERROR;
	FINALLY
		db_con_close(c);
	END_TRY;
	return user_idnr;
}



int db_log_ip(const char *ip)
{
	C c; R r; S s; volatile int t = DM_SUCCESS;
	u64_t id = 0;
	
	c = db_con_get();
	TRY
		db_begin_transaction(c);
		s = db_stmt_prepare(c, "SELECT idnr FROM %spbsp WHERE ipnumber = ?", DBPFX);
		db_stmt_set_str(s,1,ip);
		r = db_stmt_query(s);
		if (db_result_next(r))
			id = db_result_get_u64(r, 0);
		if (id) {
			/* this IP is already in the table, update the 'since' field */
			s = db_stmt_prepare(c, "UPDATE %spbsp SET since = %s WHERE idnr = ?", 
					DBPFX, db_get_sql(SQL_CURRENT_TIMESTAMP));
			db_stmt_set_u64(s,1,id);

			db_stmt_exec(s);
		} else {
			/* IP not in table, insert row */
			s = db_stmt_prepare(c, "INSERT INTO %spbsp (since, ipnumber) VALUES (%s, ?)", 
					DBPFX, db_get_sql(SQL_CURRENT_TIMESTAMP));
			db_stmt_set_str(s,1,ip);
			db_stmt_exec(s);
		}
		db_commit_transaction(c);
		TRACE(TRACE_DEBUG, "ip [%s] logged", ip);
	CATCH(SQLException)
		LOG_SQLERROR;
		db_rollback_transaction(c);
		t = DM_EQUERY;
	FINALLY
		db_con_close(c);
	END_TRY;

	return t;
}

int db_cleanup(void)
{
	return db_do_cleanup(DB_TABLENAMES, DB_NTABLES);
}

int db_empty_mailbox(u64_t user_idnr)
{
	C c; R r; volatile int t = DM_SUCCESS;
	GList *mboxids = NULL;
	u64_t *id;
	unsigned i = 0;
	int result = 0;

	c = db_con_get();

	TRY
		r = db_query(c, "SELECT mailbox_idnr FROM %smailboxes WHERE owner_idnr=%llu", 
				DBPFX, user_idnr);
		while (db_result_next(r)) {
			i++;
			id = g_new0(u64_t, 1);
			*id = db_result_get_u64(r, 0);
			mboxids = g_list_prepend(mboxids,id);
		}
	CATCH(SQLException)
		LOG_SQLERROR;
		t = DM_EQUERY;
		g_list_free(mboxids);
	FINALLY;
		db_con_close(c);
	END_TRY;

	if (i == 0) {
		TRACE(TRACE_WARNING, "user [%llu] does not have any mailboxes?", user_idnr);
		return t;
	}

	mboxids = g_list_first(mboxids);
	while (mboxids) {
		id = mboxids->data;
		if (db_delete_mailbox(*id, 1, 1)) {
			TRACE(TRACE_ERR, "error emptying mailbox [%llu]", *id);
			result = -1;
			break;
		}
		if (! g_list_next(mboxids)) break;
		mboxids = g_list_next(mboxids);
	}

	g_list_destroy(mboxids);
	
	return result;
}

int db_icheck_physmessages(gboolean cleanup)
{
	C c; R r; volatile int t = DM_SUCCESS;
	GList *ids = NULL;

	c = db_con_get();
	TRY
		r = db_query(c, "SELECT p.id FROM %sphysmessage p LEFT JOIN %smessages m ON p.id = m.physmessage_id "
				"WHERE m.physmessage_id IS NULL", DBPFX, DBPFX);
		while(db_result_next(r)) {
			u64_t *id = g_new0(u64_t, 1);
			*id = db_result_get_u64(r, 0);
			ids = g_list_prepend(ids, id);
		}
		if (cleanup) {
			while(ids) {
				db_begin_transaction(c);
				db_exec(c, "DELETE FROM %sphysmessage WHERE id = %llu", DBPFX, *(u64_t *)ids->data);
				db_commit_transaction(c);
				if (! g_list_next(ids)) break;
				ids = g_list_next(ids);
			}
		}
		t = g_list_length(ids);
		g_list_destroy(ids);
	CATCH(SQLException)
		LOG_SQLERROR;
		db_rollback_transaction(c);
		t = DM_EQUERY;
	FINALLY
		db_con_close(c);
	END_TRY;

	return t;
}

int db_icheck_partlists(gboolean cleanup)
{
	C c; R r; volatile int t = DM_SUCCESS;
	GList *ids = NULL;

	c = db_con_get();
	TRY
		r = db_query(c, "SELECT COUNT(*), l.physmessage_id FROM %spartlists l LEFT JOIN %sphysmessage p ON p.id = l.physmessage_id "
				"WHERE p.id IS NULL GROUP BY l.physmessage_id", DBPFX, DBPFX);

		while(db_result_next(r)) {
			u64_t *id = g_new0(u64_t, 1);
			*id = db_result_get_u64(r, 0);
			ids = g_list_prepend(ids, id);
		}
		if (cleanup) {
			while(ids) {
				db_begin_transaction(c);
				db_exec(c, "DELETE FROM %spartlists WHERE physmessage_id = %llu", DBPFX, *(u64_t *)ids->data);
				db_commit_transaction(c);
				if (! g_list_next(ids)) break;
				ids = g_list_next(ids);
			}
		}
		t = g_list_length(ids);
		g_list_destroy(ids);
	CATCH(SQLException)
		LOG_SQLERROR;
		db_rollback_transaction(c);
		t = DM_EQUERY;
	FINALLY
		db_con_close(c);
	END_TRY;

	return t;
}

int db_icheck_mimeparts(gboolean cleanup)
{
	C c; R r; volatile int t = DM_SUCCESS;
	GList *ids = NULL;

	c = db_con_get();
	TRY
		r = db_query(c, "SELECT p.id FROM %smimeparts p LEFT JOIN %spartlists l ON p.id = l.part_id "
				"WHERE l.part_id IS NULL", DBPFX, DBPFX);
		while(db_result_next(r)) {
			u64_t *id = g_new0(u64_t, 1);
			*id = db_result_get_u64(r, 0);
			ids = g_list_prepend(ids, id);
		}
		if (cleanup) {
			while(ids) {
				db_begin_transaction(c);
				db_exec(c, "DELETE FROM %smimeparts WHERE id = %llu", DBPFX, *(u64_t *)ids->data);
				db_commit_transaction(c);
				if (! g_list_next(ids)) break;
				ids = g_list_next(ids);
			}
		}
		t = g_list_length(ids);
		g_list_destroy(ids);
	CATCH(SQLException)
		LOG_SQLERROR;
		db_rollback_transaction(c);
		t = DM_EQUERY;
	FINALLY
		db_con_close(c);
	END_TRY;

	return t;
}

int db_icheck_rfcsize(GList  **lost)
{
	C c; R r; volatile int t = DM_SUCCESS;
	u64_t *id;
	
	c = db_con_get();
	TRY
		r = db_query(c, "SELECT id FROM %sphysmessage WHERE rfcsize=0", DBPFX);
		while (db_result_next(r)) {
			id = g_new0(u64_t,1);
			*id = db_result_get_u64(r, 0);
			*(GList **)lost = g_list_prepend(*(GList **)lost, id);
		}
	CATCH(SQLException)
		LOG_SQLERROR;
		t = DM_EQUERY;
	FINALLY
		db_con_close(c);
	END_TRY;

	return t;
}

int db_update_rfcsize(GList *lost) 
{
	C c;
	u64_t *pmsid;
	DbmailMessage *msg;
	if (! lost)
		return DM_SUCCESS;

	lost = g_list_first(lost);
	
	c = db_con_get();
	while(lost) {
		pmsid = (u64_t *)lost->data;
		
		if (! (msg = dbmail_message_new())) {
			db_con_close(c);
			return DM_EQUERY;
		}

		if (! (msg = dbmail_message_retrieve(msg, *pmsid, DBMAIL_MESSAGE_FILTER_FULL))) {
			TRACE(TRACE_WARNING, "error retrieving physmessage: [%llu]", *pmsid);
			fprintf(stderr,"E");
		} else {
			TRY
				db_begin_transaction(c);
				db_exec(c, "UPDATE %sphysmessage SET rfcsize = %llu WHERE id = %llu", 
					DBPFX, (u64_t)dbmail_message_get_size(msg,TRUE), *pmsid);
				db_commit_transaction(c);
				fprintf(stderr,".");
			CATCH(SQLException)
				db_rollback_transaction(c);
				fprintf(stderr,"E");
			END_TRY;
		}
		dbmail_message_free(msg);
		if (! g_list_next(lost)) break;
		lost = g_list_next(lost);
	}

	db_con_close(c);

	return DM_SUCCESS;
}

int db_set_headercache(GList *lost)
{
	u64_t pmsgid;
	u64_t *id;
	DbmailMessage *msg;
	if (! lost)
		return DM_SUCCESS;

	lost = g_list_first(lost);
	while (lost) {
		id = (u64_t *)lost->data;
		pmsgid = *id;
		
		msg = dbmail_message_new();
		if (! msg) return DM_EQUERY;

		if (! (msg = dbmail_message_retrieve(msg, pmsgid, DBMAIL_MESSAGE_FILTER_HEAD))) {
			TRACE(TRACE_WARNING, "error retrieving physmessage: [%llu]", pmsgid);
			fprintf(stderr,"E");
		} else {
			if (dbmail_message_cache_headers(msg) != 0) {
				TRACE(TRACE_WARNING,"error caching headers for physmessage: [%llu]", 
					pmsgid);
				fprintf(stderr,"E");
			} else {
				fprintf(stderr,".");
			}
			dbmail_message_free(msg);
		}
		if (! g_list_next(lost)) break;
		lost = g_list_next(lost);
	}
	return DM_SUCCESS;
}

		
int db_icheck_headercache(GList **lost)
{
	C c; R r; volatile int t = DM_SUCCESS;
	u64_t *id;

	c = db_con_get();
	TRY
		r = db_query(c, "SELECT p.id "
			"FROM %sphysmessage p "
			"LEFT JOIN %sheader h ON p.id = h.physmessage_id "
			"WHERE h.physmessage_id IS NULL", DBPFX, DBPFX);
		while (db_result_next(r)) {
			id = g_new0(u64_t,1);
			*id = db_result_get_u64(r, 0);
			*(GList **)lost = g_list_prepend(*(GList **)lost,id);
		}
	CATCH(SQLException)
		LOG_SQLERROR;
		t = DM_EQUERY;
	FINALLY
		db_con_close(c);
	END_TRY;

	return t;
}

int db_set_envelope(GList *lost)
{
	u64_t pmsgid;
	u64_t *id;
	DbmailMessage *msg;
	if (! lost)
		return DM_SUCCESS;

	lost = g_list_first(lost);
	while (lost) {
		id = (u64_t *)lost->data;
		pmsgid = *id;
		
		msg = dbmail_message_new();
		if (! msg)
			return DM_EQUERY;

		if (! (msg = dbmail_message_retrieve(msg, pmsgid, DBMAIL_MESSAGE_FILTER_HEAD))) {
			TRACE(TRACE_WARNING,"error retrieving physmessage: [%llu]", pmsgid);
			fprintf(stderr,"E");
		} else {
			dbmail_message_cache_envelope(msg);
			fprintf(stderr,".");
		}
		dbmail_message_free(msg);
		if (! g_list_next(lost)) break;
		lost = g_list_next(lost);
	}
	return DM_SUCCESS;
}

		
int db_icheck_envelope(GList **lost)
{
	C c; R r; volatile int t = DM_SUCCESS;
	u64_t *id;

	c = db_con_get();
	TRY
		r = db_query(c, "SELECT p.id FROM %sphysmessage p LEFT JOIN %senvelope e "
			"ON p.id = e.physmessage_id WHERE e.physmessage_id IS NULL", DBPFX, DBPFX);
		while (db_result_next(r)) {
			id = g_new0(u64_t,1);
			*id = db_result_get_u64(r, 0);
			*(GList **)lost = g_list_prepend(*(GList **)lost,id);
		}
	CATCH(SQLException)
		LOG_SQLERROR;
		t = DM_EQUERY;
	FINALLY
		db_con_close(c);
	END_TRY;

	return t;
}


int db_set_message_status(u64_t message_idnr, MessageStatus_t status)
{
	return db_update("UPDATE %smessages SET status = %d WHERE message_idnr = %llu", 
			DBPFX, status, message_idnr);
}

int db_delete_message(u64_t message_idnr)
{
	return db_update("DELETE FROM %smessages WHERE message_idnr = %llu", 
			DBPFX, message_idnr);
}

static int mailbox_delete(u64_t mailbox_idnr)
{
	return db_update("DELETE FROM %smailboxes WHERE mailbox_idnr = %llu", 
			DBPFX, mailbox_idnr);
}

static int mailbox_empty(u64_t mailbox_idnr)
{
	return db_update("DELETE FROM %smessages WHERE mailbox_idnr = %llu", 
			DBPFX, mailbox_idnr);
}

/** get the total size of messages in a mailbox. Does not work recursively! */
int db_get_mailbox_size(u64_t mailbox_idnr, int only_deleted, u64_t * mailbox_size)
{
	C c; R r = NULL; volatile int t = DM_SUCCESS;
	assert(mailbox_size != NULL);
	*mailbox_size = 0;

	c = db_con_get();
	TRY
		r = db_query(c, "SELECT COALESCE(SUM(pm.messagesize),0) FROM %smessages msg, %sphysmessage pm "
				"WHERE msg.physmessage_id = pm.id AND msg.mailbox_idnr = %llu "
				"AND msg.status < %d %s", DBPFX,DBPFX, mailbox_idnr, MESSAGE_STATUS_DELETE,
				only_deleted?"AND msg.deleted_flag = 1":"");
		if (db_result_next(r))
			*mailbox_size = db_result_get_u64(r, 0);
	CATCH(SQLException)
		; // pass
	FINALLY
		db_con_close(c);
	END_TRY;
	
	return t;
}

int db_delete_mailbox(u64_t mailbox_idnr, int only_empty, int update_curmail_size)
{
	u64_t user_idnr = 0;
	int result;
	u64_t mailbox_size = 0;

	TRACE(TRACE_DEBUG,"mailbox_idnr [%llu] only_empty [%d] update_curmail_size [%d]", mailbox_idnr, only_empty, update_curmail_size);
	/* get the user_idnr of the owner of the mailbox */
	result = db_get_mailbox_owner(mailbox_idnr, &user_idnr);
	if (result == DM_EQUERY) {
		TRACE(TRACE_ERR, "cannot find owner of mailbox for mailbox [%llu]", mailbox_idnr);
		return DM_EQUERY;
	}
	if (result == 0) {
		TRACE(TRACE_ERR, "unable to find owner of mailbox [%llu]", mailbox_idnr);
		return DM_EGENERAL;
	}

	if (update_curmail_size) {
		if (db_get_mailbox_size(mailbox_idnr, 0, &mailbox_size) == DM_EQUERY)
			return DM_EQUERY;
	}

	if (! mailbox_is_writable(mailbox_idnr))
		return DM_EGENERAL;

	if (! mailbox_empty(mailbox_idnr))
		return DM_EGENERAL;

	if (! only_empty) {
		if (! mailbox_delete(mailbox_idnr))
			return DM_EGENERAL;
	}

	/* calculate the new quotum */
	if (! update_curmail_size)
		return DM_SUCCESS;

	if (! dm_quota_user_dec(user_idnr, mailbox_size))
		return DM_EQUERY;
	return DM_SUCCESS;
}

char * db_get_message_lines(u64_t message_idnr, long lines)
{
	DbmailMessage *msg;
	
	u64_t physmessage_id = 0;
	char *c, *raw = NULL, *hdr = NULL, *buf = NULL;
	GString *s, *t;
	int pos = 0;
	long n = 0;
	
	TRACE(TRACE_DEBUG, "request for [%ld] lines", lines);

	/* first find the physmessage_id */
	if (db_get_physmessage_id(message_idnr, &physmessage_id) != DM_SUCCESS)
		return NULL;

	msg = dbmail_message_new();
	msg = dbmail_message_retrieve(msg, physmessage_id, DBMAIL_MESSAGE_FILTER_FULL);
	hdr = dbmail_message_hdrs_to_string(msg);
	buf = dbmail_message_body_to_string(msg);
	dbmail_message_free(msg);

	/* always send all headers */
	s = g_string_new(hdr);
	g_free(hdr);

	/* send requested body lines */	
	if (buf) {
		t = g_string_new(buf);
		g_free(buf);
	} else {
		t = g_string_new("");
	}

	raw = t->str;
	
	if (lines >=0) {
		while (raw[pos] && n < lines) {
			if (raw[pos] == '\n') n++;
			pos++;
		}
		t = g_string_truncate(t,pos);
	}

	g_string_append(s, t->str);
	g_string_free(t, TRUE);

	c = get_crlf_encoded_dots(s->str);
	g_string_free(s,TRUE);
	return c;
}

int db_update_pop(ClientSession_t * session_ptr)
{
	C c; volatile int t = DM_SUCCESS;
	GList *messagelst = NULL;
	u64_t user_idnr = 0;
	INIT_QUERY;

	/* get first element in list */

	c = db_con_get();
	TRY
		messagelst = g_list_first(session_ptr->messagelst);
		while (messagelst) {
			/* check if they need an update in the database */
			struct message *msg = (struct message *)messagelst->data;
			if (msg->virtual_messagestatus != msg->messagestatus) {
				/* use one message to get the user_idnr that goes with the messages */
				if (user_idnr == 0) user_idnr = db_get_useridnr(msg->realmessageid);

				/* yes they need an update, do the query */
				db_exec(c, "UPDATE %smessages set status=%d WHERE message_idnr=%llu AND status < %d",
						DBPFX, msg->virtual_messagestatus, msg->realmessageid, 
						MESSAGE_STATUS_DELETE);
			}

			if (! g_list_next(messagelst)) break;
			messagelst = g_list_next(messagelst);
		}
	CATCH(SQLException)
		LOG_SQLERROR;
		t = DM_EQUERY;
	FINALLY
		db_con_close(c);
	END_TRY;

	if (t == DM_EQUERY) return t;

	/* because the status of some messages might have changed (for instance
	 * to status >= MESSAGE_STATUS_DELETE, the quotum has to be 
	 * recalculated */
	if (user_idnr != 0) {
		if (dm_quota_rebuild_user(user_idnr) == -1) {
			TRACE(TRACE_ERR, "Could not calculate quotum used for user [%llu]", user_idnr);
			return DM_EQUERY;
		}
	}
	return DM_SUCCESS;
}

static int db_findmailbox_owner(const char *name, u64_t owner_idnr,
			 u64_t * mailbox_idnr)
{
	C c; R r; volatile int t = DM_SUCCESS;
	struct mailbox_match *mailbox_like = NULL;
	S stmt;
	GString *qs;
	int p;

	assert(mailbox_idnr);
	*mailbox_idnr = 0;

	c = db_con_get();

	mailbox_like = mailbox_match_new(name); 
	qs = g_string_new("");
	g_string_printf(qs, "SELECT mailbox_idnr FROM %smailboxes WHERE owner_idnr = ? ", DBPFX);

	if (mailbox_like->insensitive)
		g_string_append_printf(qs, "AND name %s ? ", db_get_sql(SQL_INSENSITIVE_LIKE));
	if (mailbox_like->sensitive)
		g_string_append_printf(qs, "AND name %s ? ", db_get_sql(SQL_SENSITIVE_LIKE));

	p=1;
	TRY
		stmt = db_stmt_prepare(c, qs->str);
		db_stmt_set_u64(stmt, p++, owner_idnr);
		if (mailbox_like->insensitive)
			db_stmt_set_str(stmt, p++, mailbox_like->insensitive);
		if (mailbox_like->sensitive)
			db_stmt_set_str(stmt, p++, mailbox_like->sensitive);

		r = db_stmt_query(stmt);
		if (db_result_next(r))
			*mailbox_idnr = db_result_get_u64(r, 0);

	CATCH(SQLException)
		LOG_SQLERROR;
		t = DM_EQUERY;
	FINALLY
		db_con_close(c);
		mailbox_match_free(mailbox_like);
		g_string_free(qs, TRUE);
	END_TRY;

	if (t == DM_EQUERY) return FALSE;
	if (*mailbox_idnr == 0) return FALSE;

	return TRUE;
}


int db_findmailbox(const char *fq_name, u64_t owner_idnr, u64_t * mailbox_idnr)
{
	char *mbox, *namespace, *username;
	char *simple_name;
	int i, result;
	size_t l;

	assert(mailbox_idnr != NULL);
	*mailbox_idnr = 0;

	mbox = g_strdup(fq_name);
	
	/* remove trailing '/' if present */
	l = strlen(mbox);
	while (--l > 0 && mbox[l] == '/')
		mbox[l] = '\0';

	/* remove leading '/' if present */
	for (i = 0; mbox[i] && mbox[i] == '/'; i++);
	memmove(&mbox[0], &mbox[i], (strlen(mbox) - i) * sizeof(char));

	TRACE(TRACE_DEBUG, "looking for mailbox with FQN [%s].", mbox);

	simple_name = mailbox_remove_namespace(mbox, &namespace, &username);

	if (!simple_name) {
		g_free(mbox);
		TRACE(TRACE_NOTICE, "Could not remove mailbox namespace.");
		return FALSE;
	}

	if (username) {
		TRACE(TRACE_DEBUG, "finding user with name [%s].", username);
		if (! auth_user_exists(username, &owner_idnr)) {
			TRACE(TRACE_INFO, "user [%s] not found.", username);
			g_free(mbox);
			g_free(username);
			return FALSE;
		}
	}

	if (! (result = db_findmailbox_owner(simple_name, owner_idnr, mailbox_idnr)))
		TRACE(TRACE_INFO, "no mailbox [%s] for owner [%llu]", simple_name, owner_idnr);

	g_free(mbox);
	g_free(username);
	return result;
}

static int mailboxes_by_regex(u64_t user_idnr, int only_subscribed, const char * pattern, GList ** mailboxes)
{
	C c; R r; volatile int t = DM_SUCCESS;
	u64_t search_user_idnr = user_idnr;
	char *spattern;
	char *namespace, *username;
	struct mailbox_match *mailbox_like = NULL;
	GString *qs;
	int n_rows = 0;
	S stmt;
	int prml;
	
	assert(mailboxes != NULL);
	*mailboxes = NULL;

	/* If the pattern begins with a #Users or #Public, pull that off and 
	 * find the new user_idnr whose mailboxes we're searching in. */
	spattern = mailbox_remove_namespace((char *)pattern, &namespace, &username);
	if (!spattern) {
		TRACE(TRACE_NOTICE, "invalid mailbox search pattern [%s]", pattern);
		g_free(username);
		return DM_SUCCESS;
	}
	if (username) {
		/* Replace the value of search_user_idnr with the namespace user. */
		if (! auth_user_exists(username, &search_user_idnr)) {
			TRACE(TRACE_NOTICE, "cannot search namespace because user [%s] does not exist", username);
			g_free(username);
			return DM_SUCCESS;
		}
		TRACE(TRACE_DEBUG, "searching namespace [%s] for user [%s] with pattern [%s]",
			namespace, username, spattern);
		g_free(username);
	}

	/* If there's neither % nor *, don't match on mailbox name. */
	if ( (! strchr(spattern, '%')) && (! strchr(spattern,'*')) )
		mailbox_like = mailbox_match_new(spattern);

	qs = g_string_new("");
	g_string_printf(qs,
			"SELECT distinct(mbx.name), mbx.mailbox_idnr, mbx.owner_idnr "
			"FROM %smailboxes mbx "
			"LEFT JOIN %sacl acl ON mbx.mailbox_idnr = acl.mailbox_id "
			"LEFT JOIN %susers usr ON acl.user_id = usr.user_idnr ",
			DBPFX, DBPFX, DBPFX);

	if (only_subscribed)
		g_string_append_printf(qs, 
				"LEFT JOIN %ssubscription sub ON sub.mailbox_id = mbx.mailbox_idnr "
				"WHERE ( sub.user_id=? ) ", DBPFX);
	else
		g_string_append_printf(qs, 
				"WHERE 1=1 ");

	g_string_append_printf(qs, 
			"AND ((mbx.owner_idnr=?) "
			"%s (acl.user_id=? AND acl.lookup_flag=1) "
			"OR (usr.userid=? AND acl.lookup_flag=1)) ",
			search_user_idnr==user_idnr?"OR":"AND"
	);

	if (mailbox_like && mailbox_like->insensitive)
		g_string_append_printf(qs, " AND mbx.name %s ? ", db_get_sql(SQL_INSENSITIVE_LIKE));
	if (mailbox_like && mailbox_like->sensitive)
		g_string_append_printf(qs, " AND mbx.name %s ? ", db_get_sql(SQL_SENSITIVE_LIKE));

	c = db_con_get();
	TRY
		stmt = db_stmt_prepare(c, qs->str);
		prml = 1;

		if (only_subscribed)
			db_stmt_set_u64(stmt, prml++, user_idnr);

		db_stmt_set_u64(stmt, prml++, search_user_idnr);
		db_stmt_set_u64(stmt, prml++, user_idnr);
		db_stmt_set_str(stmt, prml++, DBMAIL_ACL_ANYONE_USER);

		if (mailbox_like && mailbox_like->insensitive)
			db_stmt_set_str(stmt, prml++, mailbox_like->insensitive);
		if (mailbox_like && mailbox_like->sensitive)
			db_stmt_set_str(stmt, prml++, mailbox_like->sensitive);

		r = db_stmt_query(stmt);
		while (db_result_next(r)) {
			n_rows++;
			char *mailbox_name;
			char *simple_mailbox_name = g_strdup(db_result_get(r, 0));
			u64_t mailbox_idnr = db_result_get_u64(r, 1);
			u64_t owner_idnr = db_result_get_u64(r,2);

			/* add possible namespace prefix to mailbox_name */
			mailbox_name = mailbox_add_namespace(simple_mailbox_name, owner_idnr, user_idnr);
			TRACE(TRACE_DEBUG, "adding namespace prefix to [%s] got [%s]", simple_mailbox_name, mailbox_name);
			if (mailbox_name) {
				u64_t *id = g_new0(u64_t,1);
				*id = mailbox_idnr;
				*(GList **)mailboxes = g_list_prepend(*(GList **)mailboxes, id);
			}
			g_free(simple_mailbox_name);
			g_free(mailbox_name);
		}
	CATCH(SQLException)
		LOG_SQLERROR;
		t = DM_EQUERY;
	FINALLY
		db_con_close(c);
	END_TRY;

	g_string_free(qs, TRUE);

	if (mailbox_like) mailbox_match_free(mailbox_like);
	if (t == DM_EQUERY) return t;
	if (n_rows == 0) return DM_SUCCESS;

	*(GList **)mailboxes = g_list_reverse(*(GList **)mailboxes);

	return DM_EGENERAL;
}

int db_findmailbox_by_regex(u64_t owner_idnr, const char *pattern, GList ** children, int only_subscribed)
{
	*children = NULL;

	/* list normal mailboxes */
	if (mailboxes_by_regex(owner_idnr, only_subscribed, pattern, children) < 0) {
		TRACE(TRACE_ERR, "error listing mailboxes");
		return DM_EQUERY;
	}

	if (g_list_length(*children) == 0) {
		TRACE(TRACE_INFO, "did not find any mailboxes that "
		      "match pattern. returning 0, nchildren = 0");
		return DM_SUCCESS;
	}

	/* store matches */
	TRACE(TRACE_INFO, "found [%d] mailboxes for [%s]", g_list_length(*children), pattern);
	return DM_SUCCESS;
}

int mailbox_is_writable(u64_t mailbox_idnr)
{
	int result = TRUE;
	MailboxState_T M = MailboxState_new(mailbox_idnr);
	if (MailboxState_getPermission(M) != IMAPPERM_READWRITE) {
		TRACE(TRACE_INFO, "read-only mailbox");
		result = FALSE;
	}
	MailboxState_free(&M);
	return result;

}

GList * db_imap_split_mailbox(const char *mailbox, u64_t owner_idnr, const char ** errmsg)
{
	assert(mailbox);
	assert(errmsg);

	GList *mailboxes = NULL;
	char *namespace, *username, *cpy, **chunks = NULL;
	char *simple_name;
	int i, is_users = 0, is_public = 0;
	u64_t mboxid, public;

	/* Scratch space as we build the mailbox names. */
	cpy = g_new0(char, strlen(mailbox) + 1);

	simple_name = mailbox_remove_namespace((char *)mailbox, &namespace, &username);

	if (username) {
		TRACE(TRACE_DEBUG, "finding user with name [%s].", username);
		if (! auth_user_exists(username, &owner_idnr)) {
			TRACE(TRACE_INFO, "user [%s] not found.", username);
			goto egeneral;
		}
	}

	if (namespace) {
		if (strcasecmp(namespace, NAMESPACE_USER) == 0) {
			is_users = 1;
		} else if (strcasecmp(namespace, NAMESPACE_PUBLIC) == 0) {
			is_public = 1;
		}
	}

	TRACE(TRACE_DEBUG, "Splitting mailbox [%s] simple name [%s] namespace [%s] username [%s]",
		mailbox, simple_name, namespace, username);

	/* split up the name  */
	if (! (chunks = g_strsplit(simple_name, MAILBOX_SEPARATOR, 0))) {
		TRACE(TRACE_ERR, "could not create chunks");
		*errmsg = "Server ran out of memory";
		goto egeneral;
	}

	if (chunks[0] == NULL) {
		*errmsg = "Invalid mailbox name specified";
		goto egeneral;
	}

	for (i = 0; chunks[i]; i++) {

		/* Indicates a // in the mailbox name. */
		if ((! (strlen(chunks[i])) && chunks[i+1])) {
			*errmsg = "Invalid mailbox name specified";
			goto egeneral;
		}
		if (! (strlen(chunks[i]))) // trailing / in name
			break;

		if (i == 0) {
			if (strcasecmp(chunks[0], "inbox") == 0) {
				/* Make inbox uppercase */
				strcpy(chunks[0], "INBOX");
			}
			/* The current chunk goes into the name. */
			strcat(cpy, chunks[0]);
		} else {
			/* The current chunk goes into the name. */
			strcat(cpy, MAILBOX_SEPARATOR);
			strcat(cpy, chunks[i]);
		}

		TRACE(TRACE_DEBUG, "Preparing mailbox [%s]", cpy);

		/* Only the PUBLIC user is allowed to own #Public itself. */
		if (i == 0 && is_public) {
			if (! auth_user_exists(PUBLIC_FOLDER_USER, &public)) {
				*errmsg = "Public user required for #Public folder access.";
				goto egeneral;
			}
			db_findmailbox(cpy, public, &mboxid);

		} else {
			db_findmailbox(cpy, owner_idnr, &mboxid);
		}

		/* Prepend a mailbox struct onto the list. */
		MailboxState_T M = MailboxState_new(mboxid);
		MailboxState_setName(M, cpy);
		MailboxState_setIsUsers(M, is_users);
		MailboxState_setIsPublic(M, is_public);

		/* Only the PUBLIC user is allowed to own #Public folders. */
		if (is_public) {
			MailboxState_setOwner(M, public);
		} else {
			MailboxState_setOwner(M, owner_idnr);
		}

		mailboxes = g_list_prepend(mailboxes, M);
	}

	/* We built the path with prepends,
	 * so we have to reverse it now. */
	mailboxes = g_list_reverse(mailboxes);
	*errmsg = "Everything is peachy keen";

	g_strfreev(chunks);
	g_free(username);
	g_free(cpy);
 
	return mailboxes;

egeneral:
	mailboxes = g_list_first(mailboxes);
	while (mailboxes) {
		MailboxState_T M = (MailboxState_T)mailboxes->data;
		MailboxState_free(&M);
		if (! g_list_next(mailboxes)) break;
		mailboxes = g_list_next(mailboxes);
	}
	g_list_free(g_list_first(mailboxes));
	g_strfreev(chunks);
	g_free(username);
	g_free(cpy);
	return NULL;
}

/** Create a mailbox, recursively creating its parents.
 * \param mailbox Name of the mailbox to create
 * \param owner_idnr Owner of the mailbox
 * \param mailbox_idnr Fills the pointer with the mailbox id
 * \param message Returns a static pointer to the return message
 * \return
 *   DM_SUCCESS Everything's good
 *   DM_EGENERAL Cannot create mailbox
 *   DM_EQUERY Database error
 */
int db_mailbox_create_with_parents(const char * mailbox, mailbox_source_t source,
		u64_t owner_idnr, u64_t * mailbox_idnr, const char * * message)
{
	int skip_and_free = DM_SUCCESS;
	u64_t created_mboxid = 0;
	int result;
	GList *mailbox_list = NULL, *mailbox_item = NULL;

	assert(mailbox);
	assert(mailbox_idnr);
	assert(message);
	
	TRACE(TRACE_INFO, "Creating mailbox [%s] source [%d] for user [%llu]",
			mailbox, source, owner_idnr);

	/* Check if new name is valid. */
	if (!checkmailboxname(mailbox)) {
		*message = "New mailbox name contains invalid characters";
		TRACE(TRACE_NOTICE, "New mailbox name contains invalid characters. Aborting create.");
	        return DM_EGENERAL;
        }

	/* Check if mailbox already exists. */
	if (db_findmailbox(mailbox, owner_idnr, mailbox_idnr)) {
		*message = "Mailbox already exists";
		TRACE(TRACE_ERR, "Asked to create mailbox [%s] which already exists. Aborting create.", mailbox);
		return DM_EGENERAL;
	}

	if ((mailbox_list = db_imap_split_mailbox(mailbox, owner_idnr, message)) == NULL) {
		TRACE(TRACE_ERR, "db_imap_split_mailbox returned with error");
		// Message pointer was set by db_imap_split_mailbox
		return DM_EGENERAL;
	}

	if (source == BOX_BRUTEFORCE) {
		TRACE(TRACE_INFO, "Mailbox requested with BRUTEFORCE creation status; "
			"pretending that all permissions have been granted to create it.");
	}

	mailbox_item = g_list_first(mailbox_list);
	while (mailbox_item) {
		MailboxState_T M = (MailboxState_T)mailbox_item->data;

		/* Needs to be created. */
		if (MailboxState_getId(M) == 0) {
			if (MailboxState_isUsers(M) && MailboxState_getOwner(M) != owner_idnr) {
				*message = "Top-level mailboxes may not be created for others under #Users";
				skip_and_free = DM_EGENERAL;
			} else {
				u64_t this_owner_idnr;

				/* Only the PUBLIC user is allowed to own #Public. */
				if (MailboxState_isPublic(M)) {
					this_owner_idnr = MailboxState_getOwner(M);
				} else {
					this_owner_idnr = owner_idnr;
				}

				/* Create it! */
				result = db_createmailbox(MailboxState_getName(M), this_owner_idnr, &created_mboxid);

				if (result == DM_EGENERAL) {
					*message = "General error while creating";
					skip_and_free = DM_EGENERAL;
				} else if (result == DM_EQUERY) {
					*message = "Database error while creating";
					skip_and_free = DM_EQUERY;
				} else {
					/* Subscribe to the newly created mailbox. */
					if (! db_subscribe(created_mboxid, owner_idnr)) {
						*message = "General error while subscribing";
						skip_and_free = DM_EGENERAL;
					}
					MailboxState_setPermission(M, IMAPPERM_READWRITE);
				}

				/* If the PUBLIC user owns it, then the current user needs ACLs. */
				if (MailboxState_isPublic(M)) {
					result = acl_set_rights(owner_idnr, created_mboxid, "lrswipkxteacd");
					if (result == DM_EQUERY) {
						*message = "Database error while setting rights";
						skip_and_free = DM_EQUERY;
					}
				}
			}

			if (!skip_and_free) {
				*message = "Folder created";
				MailboxState_setId(M, created_mboxid);
			}
		}

		if (skip_and_free)
			break;

		if (source != BOX_BRUTEFORCE) {
			TRACE(TRACE_DEBUG, "Checking if we have the right to "
				"create mailboxes under mailbox [%llu]", MailboxState_getId(M));

			/* Mailbox does exist, failure if no_inferiors flag set. */
			result = db_noinferiors(MailboxState_getId(M));
			if (result == DM_EGENERAL) {
				*message = "Mailbox cannot have inferior names";
				skip_and_free = DM_EGENERAL;
			} else if (result == DM_EQUERY) {
				*message = "Internal database error while checking inferiors";
				skip_and_free = DM_EQUERY;
			}

			/* Mailbox does exist, failure if ACLs disallow CREATE. */
			result = acl_has_right(M, owner_idnr, ACL_RIGHT_CREATE);
			if (result == 0) {
				*message = "Permission to create mailbox denied";
				skip_and_free = DM_EGENERAL;
			} else if (result < 0) {
				*message = "Internal database error while checking ACL";
				skip_and_free = DM_EQUERY;
			}
		}

		if (skip_and_free) break;

		if (! g_list_next(mailbox_item)) break;
		mailbox_item = g_list_next(mailbox_item);
	}

	mailbox_item = g_list_first(mailbox_list);
	while (mailbox_item) {
		MailboxState_T M = (MailboxState_T)mailbox_item->data;
		MailboxState_free(&M);
		if (! g_list_next(mailbox_item)) break;
		mailbox_item = g_list_next(mailbox_item);
	}
	g_list_free(g_list_first(mailbox_list));

	*mailbox_idnr = created_mboxid;
	return skip_and_free;
}

int db_createmailbox(const char * name, u64_t owner_idnr, u64_t * mailbox_idnr)
{
	char *simple_name;
	char *frag;
	assert(mailbox_idnr != NULL);
	*mailbox_idnr = 0;
	volatile int result = DM_SUCCESS;
	C c; R r; S s;
	INIT_QUERY;

	if (auth_requires_shadow_user()) {
		TRACE(TRACE_DEBUG, "creating shadow user for [%llu]",
				owner_idnr);
		if ((db_user_find_create(owner_idnr) < 0)) {
			TRACE(TRACE_ERR, "unable to find or create sql shadow account for useridnr [%llu]", 
					owner_idnr);
			return DM_EQUERY;
		}
	}

	/* remove namespace information from mailbox name */
	if (!(simple_name = mailbox_remove_namespace((char *)name, NULL, NULL))) {
		TRACE(TRACE_NOTICE, "Could not remove mailbox namespace.");
		return DM_EGENERAL;
	}

	frag = db_returning("mailbox_idnr");
	snprintf(query, DEF_QUERYSIZE,
		 "INSERT INTO %smailboxes (name, owner_idnr,permission)"
		 " VALUES (?, ?, %d) %s", DBPFX,
		 IMAPPERM_READWRITE, frag);
	g_free(frag);

	c = db_con_get();
	TRY
		db_begin_transaction(c);
		s = db_stmt_prepare(c,query);
		db_stmt_set_str(s,1,simple_name);
		db_stmt_set_u64(s,2,owner_idnr);
		
		if (_db_params.db_driver == DM_DRIVER_ORACLE) {
			db_stmt_exec(s);
			*mailbox_idnr = db_get_pk(c, "mailboxes");
		} else {
			r = db_stmt_query(s);
			*mailbox_idnr = db_insert_result(c, r);
		}
		db_commit_transaction(c);
		TRACE(TRACE_DEBUG, "created mailbox with idnr [%llu] for user [%llu]",
				*mailbox_idnr, owner_idnr);
	CATCH(SQLException)
		LOG_SQLERROR;
		db_rollback_transaction(c);
		result = DM_EQUERY;
	FINALLY
		db_con_close(c);
	END_TRY;

	return result;
}


int db_mailbox_set_permission(u64_t mailbox_id, int permission)
{
	return db_update("UPDATE %smailboxes SET permission=%d WHERE mailbox_idnr=%llu", 
			DBPFX, permission, mailbox_id);
}


/* Called from:
 * dbmail-message.c (dbmail_message_store -> _message_insert) (always INBOX)
 * modules/authldap.c (creates shadow INBOX) (always INBOX)
 * sort.c (delivers to a mailbox) (performs own ACL checking)
 *
 * Ok, this can very possibly return mailboxes owned by someone else;
 * so the caller must be wary to perform additional ACL checking.
 * Why? Sieve script:
 *   fileinto "#Users/joeschmoe/INBOX";
 * Simple as that.
 */
int db_find_create_mailbox(const char *name, mailbox_source_t source,
		u64_t owner_idnr, u64_t * mailbox_idnr)
{
	u64_t mboxidnr;
	const char *message;

	assert(mailbox_idnr != NULL);
	*mailbox_idnr = 0;
	
	/* Did we fail to find the mailbox? */
	if (! db_findmailbox(name, owner_idnr, &mboxidnr)) {
		/* Who specified this mailbox? */
		if (source == BOX_COMMANDLINE
		 || source == BOX_BRUTEFORCE
		 || source == BOX_SORTING
		 || source == BOX_DEFAULT) {
			/* Did we fail to create the mailbox? */
			if (db_mailbox_create_with_parents(name, source, owner_idnr, &mboxidnr, &message) != DM_SUCCESS) {
				TRACE(TRACE_ERR, "could not create mailbox [%s] because [%s]",
						name, message);
				return DM_EQUERY;
			}
			TRACE(TRACE_DEBUG, "mailbox [%s] created on the fly", 
					name);
			// Subscription now occurs in db_mailbox_create_with_parents
		} else {
			/* The mailbox was specified by an untrusted
			 * source, such as the address part, and will
			 * not be autocreated. */
			return db_find_create_mailbox("INBOX", BOX_DEFAULT,
					owner_idnr, mailbox_idnr);
		}

	}
	TRACE(TRACE_DEBUG, "mailbox [%s] found", name);

	*mailbox_idnr = mboxidnr;
	return DM_SUCCESS;
}


int db_listmailboxchildren(u64_t mailbox_idnr, u64_t user_idnr, GList ** children)
{
	struct mailbox_match *mailbox_like = NULL;
	C c; R r; S s; volatile int t = DM_SUCCESS;
	GString *qs;
	int prml;

	*children = NULL;

	/* retrieve the name of this mailbox */
	c = db_con_get();
	TRY
		r = db_query(c, "SELECT name FROM %smailboxes WHERE mailbox_idnr=%llu AND owner_idnr=%llu",
				DBPFX, mailbox_idnr, user_idnr);
		if (db_result_next(r)) {
			char *pattern = g_strdup_printf("%s/%%", db_result_get(r,0));
			mailbox_like = mailbox_match_new(pattern);
			g_free(pattern);
		}
	CATCH(SQLException)
		LOG_SQLERROR;
		t = DM_EQUERY;
	FINALLY
		db_con_clear(c);
	END_TRY;

	if (t == DM_EQUERY) {
		if (mailbox_like) 
			mailbox_match_free(mailbox_like);
		db_con_close(c);
		return t;
	}

	t = DM_SUCCESS;
	qs = g_string_new("");
	g_string_printf(qs, "SELECT mailbox_idnr FROM %smailboxes WHERE owner_idnr = ? ", DBPFX);
	if (mailbox_like && mailbox_like->insensitive)
		g_string_append_printf(qs, " AND name %s ? ", db_get_sql(SQL_INSENSITIVE_LIKE));
	if (mailbox_like && mailbox_like->sensitive)
		g_string_append_printf(qs, " AND name %s ? ", db_get_sql(SQL_SENSITIVE_LIKE));
	
	TRY
		s = db_stmt_prepare(c, qs->str);
		prml = 1;
		db_stmt_set_u64(s, prml++, user_idnr);
		if (mailbox_like && mailbox_like->insensitive)
			db_stmt_set_str(s, prml++, mailbox_like->insensitive);
		if (mailbox_like && mailbox_like->sensitive)
			db_stmt_set_str(s, prml++, mailbox_like->sensitive);

		/* now find the children */
		r = db_stmt_query(s);
		while (db_result_next(r)) {
			u64_t *id = g_new0(u64_t,1);
			*id = db_result_get_u64(r,0);
			*(GList **)children = g_list_prepend(*(GList **)children, id);
		}
	CATCH(SQLException)
		LOG_SQLERROR;
		t = DM_EQUERY;
	FINALLY
		db_con_close(c);
	END_TRY;

	if (mailbox_like) mailbox_match_free(mailbox_like);
	g_string_free(qs, TRUE);
	return t;
}

int db_isselectable(u64_t mailbox_idnr)
{
	C c; R r; volatile int t = FALSE;

	c = db_con_get();
	TRY
		r = db_query(c, "SELECT no_select FROM %smailboxes WHERE mailbox_idnr = %llu", 
				DBPFX, mailbox_idnr);
		if (db_result_next(r)) 
			t = db_result_get_bool(r, 0);
	CATCH(SQLException)
		LOG_SQLERROR;
		t = DM_EQUERY;
	FINALLY
		db_con_close(c);
	END_TRY;

	if (t == DM_EQUERY) return t;

	return t ? FALSE : TRUE;
}

int db_noinferiors(u64_t mailbox_idnr)
{
	C c; R r; volatile int t = FALSE;

	c = db_con_get();
	TRY

		r = db_query(c, "SELECT no_inferiors FROM %smailboxes WHERE mailbox_idnr=%llu", 
				DBPFX, mailbox_idnr);
		if (db_result_next(r))
			t = db_result_get_bool(r, 0);
	CATCH(SQLException)
		LOG_SQLERROR;
		t = DM_EQUERY;
	FINALLY
		db_con_close(c);
	END_TRY;

	return t;
}

int db_movemsg(u64_t mailbox_to, u64_t mailbox_from)
{
	C c; volatile int t = DM_SUCCESS;
	c = db_con_get();
	TRY
		db_exec(c, "UPDATE %smessages SET mailbox_idnr=%llu WHERE mailbox_idnr=%llu", 
				DBPFX, mailbox_to, mailbox_from);
	CATCH(SQLException)
		LOG_SQLERROR;
		t = DM_EQUERY;
	FINALLY
		db_con_close(c);
	END_TRY;

	if (t == DM_EQUERY) return t;

	db_mailbox_seq_update(mailbox_to);
	db_mailbox_seq_update(mailbox_from);

	return DM_SUCCESS;		/* success */
}

#define EXPIRE_DAYS 3
int db_mailbox_has_message_id(u64_t mailbox_idnr, const char *messageid)
{
	int rows = 0;
	C c; R r; S s;
	char expire[DEF_FRAGSIZE], partial[DEF_FRAGSIZE];
	INIT_QUERY;

	memset(expire,0,sizeof(expire));
	memset(partial,0,sizeof(partial));

	g_return_val_if_fail(messageid!=NULL,0);

	snprintf(expire, DEF_FRAGSIZE, db_get_sql(SQL_EXPIRE), EXPIRE_DAYS);
	snprintf(partial, DEF_FRAGSIZE, db_get_sql(SQL_PARTIAL), "v.headervalue");
	snprintf(query, DEF_QUERYSIZE,
		"SELECT m.message_idnr "
		"FROM %smessages m "
		"LEFT JOIN %sphysmessage p ON m.physmessage_id=p.id "
		"LEFT JOIN %sheader h ON p.id=h.physmessage_id "
		"LEFT JOIN %sheadername n ON h.headername_id=n.id "
		"LEFT JOIN %sheadervalue v ON h.headervalue_id=v.id "
		"WHERE m.mailbox_idnr=? "
		"AND n.headername IN ('resent-message-id','message-id') "
		"AND %s=? "
		"AND p.internal_date > %s", DBPFX, DBPFX, DBPFX, DBPFX, DBPFX,
		partial, expire);
	
	c = db_con_get();
	TRY
		s = db_stmt_prepare(c, query);
		db_stmt_set_u64(s, 1, mailbox_idnr);
		db_stmt_set_str(s, 2, messageid);

		r = db_stmt_query(s);
		while (db_result_next(r))
			rows++;
	CATCH(SQLException)
		LOG_SQLERROR;
	FINALLY
		db_con_close(c);
	END_TRY;
		
	return rows;
}

static u64_t message_get_size(u64_t message_idnr)
{
	C c; R r;
	u64_t size = 0;

	c = db_con_get();
	TRY
		r = db_query(c, "SELECT pm.messagesize FROM %sphysmessage pm, %smessages msg "
				"WHERE pm.id = msg.physmessage_id "
				"AND message_idnr = %llu",DBPFX,DBPFX, message_idnr);
		if (db_result_next(r))
			size = db_result_get_u64(r, 0);
	CATCH(SQLException)
		LOG_SQLERROR;
	FINALLY
		db_con_close(c);
	END_TRY;

	return size;
}

int db_copymsg(u64_t msg_idnr, u64_t mailbox_to, u64_t user_idnr,
	       u64_t * newmsg_idnr, gboolean recent)
{
	C c; R r;
	u64_t msgsize;
	char *frag;
	int valid=FALSE;
	char unique_id[UID_SIZE];

	/* Get the size of the message to be copied. */
	if (! (msgsize = message_get_size(msg_idnr))) {
		TRACE(TRACE_ERR, "error getting size for message [%llu]", msg_idnr);
		return DM_EQUERY;
	}

	/* Check to see if the user has room for the message. */
	if ((valid = dm_quota_user_validate(user_idnr, msgsize)) == DM_EQUERY)
		return DM_EQUERY;

	if (! valid) {
		TRACE(TRACE_INFO, "user [%llu] would exceed quotum", user_idnr);
		return -2;
	}

	/* Copy the message table entry of the message. */
	frag = db_returning("message_idnr");
	memset(unique_id,0,sizeof(unique_id));

	c = db_con_get();
	TRY
		db_begin_transaction(c);
		create_unique_id(unique_id, msg_idnr);
		if (_db_params.db_driver == DM_DRIVER_ORACLE) {
			db_exec(c, "INSERT INTO %smessages ("
				"mailbox_idnr,physmessage_id,seen_flag,answered_flag,deleted_flag,flagged_flag,recent_flag,draft_flag,unique_id,status)"
				" SELECT %llu,physmessage_id,seen_flag,answered_flag,deleted_flag,flagged_flag,%d,draft_flag,'%s',status"
				" FROM %smessages WHERE message_idnr = %llu %s",DBPFX, mailbox_to, recent, unique_id, DBPFX, msg_idnr, frag);
			*newmsg_idnr = db_get_pk(c, "messages");
		} else {
			r = db_query(c, "INSERT INTO %smessages ("
				"mailbox_idnr,physmessage_id,seen_flag,answered_flag,deleted_flag,flagged_flag,recent_flag,draft_flag,unique_id,status)"
				" SELECT %llu,physmessage_id,seen_flag,answered_flag,deleted_flag,flagged_flag,%d,draft_flag,'%s',status"
				" FROM %smessages WHERE message_idnr = %llu %s",DBPFX, mailbox_to, recent, unique_id, DBPFX, msg_idnr, frag);
			*newmsg_idnr = db_insert_result(c, r);
		}
		db_commit_transaction(c);
	CATCH(SQLException)
		LOG_SQLERROR;
		db_rollback_transaction(c);
	FINALLY
		db_con_close(c);
	END_TRY;

	g_free(frag);

	db_mailbox_seq_update(mailbox_to);

	/* Copy the message keywords */
	c = db_con_get();
	TRY
		db_begin_transaction(c);
		db_exec(c, "INSERT INTO %skeywords (message_idnr, keyword) "
			"SELECT %llu,keyword from %skeywords WHERE message_idnr=%llu", 
			DBPFX, *newmsg_idnr, DBPFX, msg_idnr);
		db_commit_transaction(c);
	CATCH(SQLException)
		LOG_SQLERROR;
		db_rollback_transaction(c);
	FINALLY
		db_con_close(c);
	END_TRY;

	/* update quotum */
	if (! dm_quota_user_inc(user_idnr, msgsize))
		return DM_EQUERY;

	return DM_EGENERAL;
}

int db_getmailboxname(u64_t mailbox_idnr, u64_t user_idnr, char *name)
{
	C c; R r;
	char *tmp_name = NULL, *tmp_fq_name;
	int result;
	size_t tmp_fq_name_len;
	u64_t owner_idnr;
	INIT_QUERY;

	result = db_get_mailbox_owner(mailbox_idnr, &owner_idnr);
	if (result <= 0) {
		TRACE(TRACE_ERR, "error checking ownership of mailbox");
		return DM_EQUERY;
	}

	c = db_con_get();
	TRY
		r = db_query(c, "SELECT name FROM %smailboxes WHERE mailbox_idnr=%llu",
				DBPFX, mailbox_idnr);
		if (db_result_next(r))
			tmp_name = g_strdup(db_result_get(r, 0));
	CATCH(SQLException)
		LOG_SQLERROR;
	FINALLY
		db_con_close(c);
	END_TRY;

	tmp_fq_name = mailbox_add_namespace(tmp_name, owner_idnr, user_idnr);
	g_free(tmp_name);

	if (!tmp_fq_name) {
		TRACE(TRACE_ERR, "error getting fully qualified mailbox name");
		return DM_EQUERY;
	}
	tmp_fq_name_len = strlen(tmp_fq_name);
	if (tmp_fq_name_len >= IMAP_MAX_MAILBOX_NAMELEN)
		tmp_fq_name_len = IMAP_MAX_MAILBOX_NAMELEN - 1;
	strncpy(name, tmp_fq_name, tmp_fq_name_len);
	name[tmp_fq_name_len] = '\0';
	g_free(tmp_fq_name);
	return DM_SUCCESS;
}

int db_setmailboxname(u64_t mailbox_idnr, const char *name)
{
	C c; S s; volatile int t = DM_SUCCESS;

	c = db_con_get();
	TRY
		s = db_stmt_prepare(c, "UPDATE %smailboxes SET name = ? WHERE mailbox_idnr = ?", DBPFX);
		db_stmt_set_str(s,1,name);
		db_stmt_set_u64(s,2,mailbox_idnr);
		db_stmt_exec(s);
	CATCH(SQLException)
		LOG_SQLERROR;
		t = DM_EQUERY;
	FINALLY
		db_con_close(c);
	END_TRY;

	return t;
}


int db_subscribe(u64_t mailbox_idnr, u64_t user_idnr)
{
	C c; S s; R r; volatile int t = TRUE;
	c = db_con_get();
	TRY
		db_begin_transaction(c);
		s = db_stmt_prepare(c, "SELECT * FROM %ssubscription WHERE user_id=? and mailbox_id=?", DBPFX);
		db_stmt_set_u64(s,1,user_idnr);
		db_stmt_set_u64(s,2,mailbox_idnr);
		r = db_stmt_query(s);
		if (! db_result_next(r)) {
			s = db_stmt_prepare(c, "INSERT INTO %ssubscription (user_id, mailbox_id) VALUES (?, ?)", DBPFX);
			db_stmt_set_u64(s,1,user_idnr);
			db_stmt_set_u64(s,2,mailbox_idnr);
			t = db_stmt_exec(s);
		}
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

int db_unsubscribe(u64_t mailbox_idnr, u64_t user_idnr)
{
	return db_update("DELETE FROM %ssubscription WHERE user_id=%llu AND mailbox_id=%llu", DBPFX, user_idnr, mailbox_idnr);
}

int db_get_msgflag(const char *flag_name, u64_t msg_idnr)
{
	C c; R r;
	char the_flag_name[DEF_QUERYSIZE / 2];	/* should be sufficient ;) */
	int val = 0;

	/* determine flag */
	if (strcasecmp(flag_name, "seen") == 0)
		snprintf(the_flag_name, DEF_QUERYSIZE / 2, "seen_flag");
	else if (strcasecmp(flag_name, "deleted") == 0)
		snprintf(the_flag_name, DEF_QUERYSIZE / 2, "deleted_flag");
	else if (strcasecmp(flag_name, "answered") == 0)
		snprintf(the_flag_name, DEF_QUERYSIZE / 2, "answered_flag");
	else if (strcasecmp(flag_name, "flagged") == 0)
		snprintf(the_flag_name, DEF_QUERYSIZE / 2, "flagged_flag");
	else if (strcasecmp(flag_name, "recent") == 0)
		snprintf(the_flag_name, DEF_QUERYSIZE / 2, "recent_flag");
	else if (strcasecmp(flag_name, "draft") == 0)
		snprintf(the_flag_name, DEF_QUERYSIZE / 2, "draft_flag");
	else
		return DM_SUCCESS;	/* non-existent flag is not set */

	c = db_con_get();
	TRY
		r = db_query(c, "SELECT %s FROM %smessages WHERE message_idnr=%llu AND status < %d ",
				the_flag_name, DBPFX, msg_idnr, MESSAGE_STATUS_DELETE);
		if (db_result_next(r))
			val = db_result_get_int(r, 0);
	CATCH(SQLException)
		LOG_SQLERROR;
	FINALLY
		db_con_close(c);
	END_TRY;

	return val;
}

static void db_set_msgkeywords(C c, u64_t msg_idnr, GList *keywords, int action_type, MessageInfo *msginfo)
{
	S s;
	INIT_QUERY;

	if (action_type == IMAPFA_REMOVE) {
		s = db_stmt_prepare(c, "DELETE FROM %skeywords WHERE message_idnr=? AND keyword=?",
				DBPFX);
		db_stmt_set_u64(s,1,msg_idnr);

		keywords = g_list_first(keywords);
		while (keywords) {
			db_stmt_set_str(s,2,(char *)keywords->data);
			db_stmt_exec(s);

			if (! g_list_next(keywords)) break;
			keywords = g_list_next(keywords);
		}
	}

	else if (action_type == IMAPFA_ADD || action_type == IMAPFA_REPLACE) {
		const char *ignore = db_get_sql(SQL_IGNORE);
		if (action_type == IMAPFA_REPLACE) {
			s = db_stmt_prepare(c, "DELETE FROM %skeywords WHERE message_idnr=?", DBPFX);
			db_stmt_set_u64(s, 1, msg_idnr);
			db_stmt_exec(s);
		}


		keywords = g_list_first(keywords);
		while (keywords) {
			if ((! msginfo) || (! g_list_find_custom(msginfo->keywords, 
							(char *)keywords->data, 
							(GCompareFunc)g_ascii_strcasecmp))) {

				if (action_type == IMAPFA_ADD) { // avoid duplicate key errors in case of concurrent inserts
					s = db_stmt_prepare(c, "DELETE FROM %skeywords WHERE message_idnr=? AND keyword=?", DBPFX);
					db_stmt_set_u64(s, 1, msg_idnr);
					db_stmt_set_str(s, 2, (char *)keywords->data);
					db_stmt_exec(s);
				}

				s = db_stmt_prepare(c, "INSERT %s INTO %skeywords (message_idnr,keyword) VALUES (?, ?)", 
						ignore, DBPFX);
				db_stmt_set_u64(s, 1, msg_idnr);
				db_stmt_set_str(s, 2, (char *)keywords->data);
				db_stmt_exec(s);
			}
			if (! g_list_next(keywords)) break;
			keywords = g_list_next(keywords);
		}
	}
}

int db_set_msgflag(u64_t msg_idnr, int *flags, GList *keywords, int action_type, MessageInfo *msginfo)
{
	C c; int t = DM_SUCCESS;
	size_t i, pos = 0;
	volatile int seen = 0;
	INIT_QUERY;

	memset(query,0,DEF_QUERYSIZE);
	pos += snprintf(query, DEF_QUERYSIZE, "UPDATE %smessages SET ", DBPFX);

	for (i = 0; i < IMAP_NFLAGS; i++) {
		if (flags[i])
			TRACE(TRACE_DEBUG,"set %s", db_flag_desc[i]);

		switch (action_type) {
		case IMAPFA_ADD:
			if (flags[i]) {
				if (msginfo && msginfo->flags) msginfo->flags[i] = 1;
				pos += snprintf(query + pos, DEF_QUERYSIZE - pos, "%s%s=1", seen?",":"", db_flag_desc[i]); 
				seen++;
			}
			break;
		case IMAPFA_REMOVE:
			if (flags[i]) {
				if (msginfo && msginfo->flags) msginfo->flags[i] = 0;
				pos += snprintf(query + pos, DEF_QUERYSIZE - pos, "%s%s=0", seen?",":"", db_flag_desc[i]); 
				seen++;
			}
			break;

		case IMAPFA_REPLACE:
			if (flags[i]) {
				if (msginfo && msginfo->flags) msginfo->flags[i] = 1;
				pos += snprintf(query + pos, DEF_QUERYSIZE - pos, "%s%s=1", seen?",":"", db_flag_desc[i]); 
			} else if (i != IMAP_FLAG_RECENT) {
				if (msginfo && msginfo->flags) msginfo->flags[i] = 0;
				pos += snprintf(query + pos, DEF_QUERYSIZE - pos, "%s%s=0", seen?",":"", db_flag_desc[i]); 
			}
			seen++;
			break;
		}
	}

	snprintf(query + pos, DEF_QUERYSIZE - pos,
			" WHERE message_idnr = %llu AND status < %d",
			msg_idnr, MESSAGE_STATUS_DELETE);

	c = db_con_get();
	TRY
		db_begin_transaction(c);
		if (seen) db_exec(c, query);
		db_set_msgkeywords(c, msg_idnr, keywords, action_type, msginfo);
		db_commit_transaction(c);
	CATCH(SQLException)
		LOG_SQLERROR;
		t = DM_EQUERY;
	FINALLY
		db_con_close(c);
	END_TRY;

	if (t == DM_EQUERY) return t;

	return DM_SUCCESS;
}

static int db_acl_has_acl(u64_t userid, u64_t mboxid)
{
	C c; R r; volatile int t = FALSE;

	c = db_con_get();
	TRY
		r = db_query(c, "SELECT user_id, mailbox_id FROM %sacl WHERE user_id = %llu AND mailbox_id = %llu",DBPFX, userid, mboxid);
		if (db_result_next(r))
			t = TRUE;
	CATCH(SQLException)
		LOG_SQLERROR;
		t = DM_EQUERY;
	FINALLY
		db_con_close(c);
	END_TRY;

	return t;
}

static int db_acl_create_acl(u64_t userid, u64_t mboxid)
{	
	return db_update("INSERT INTO %sacl (user_id, mailbox_id) VALUES (%llu, %llu)",DBPFX, userid, mboxid);
}

int db_acl_set_right(u64_t userid, u64_t mboxid, const char *right_flag,
		     int set)
{
	int result;

	assert(set == 0 || set == 1);

	TRACE(TRACE_DEBUG, "Setting ACL for user [%llu], mailbox [%llu].",
		userid, mboxid);

	result = db_user_is_mailbox_owner(userid, mboxid);
	if (result < 0) {
		TRACE(TRACE_ERR, "error checking ownership of mailbox.");
		return DM_EQUERY;
	}
	if (result == TRUE)
		return DM_SUCCESS;

	// if necessary, create ACL for user, mailbox
	result = db_acl_has_acl(userid, mboxid);
	if (result < 0) {
		TRACE(TRACE_ERR, "Error finding acl for user "
		      "[%llu], mailbox [%llu]",
		      userid, mboxid);
		return DM_EQUERY;
	}

	if (result == FALSE) {
		if (db_acl_create_acl(userid, mboxid) == -1) {
			TRACE(TRACE_ERR, "Error creating ACL for "
			      "user [%llu], mailbox [%llu]",
			      userid, mboxid);
			return DM_EQUERY;
		}
	}

	return db_update("UPDATE %sacl SET %s = %i WHERE user_id = %llu AND mailbox_id = %llu",DBPFX, right_flag, set, userid, mboxid);
}

int db_acl_delete_acl(u64_t userid, u64_t mboxid)
{
	return db_update("DELETE FROM %sacl WHERE user_id = %llu AND mailbox_id = %llu",DBPFX, userid, mboxid);
}

int db_acl_get_identifier(u64_t mboxid, GList **identifier_list)
{
	C c; R r; volatile int t = TRUE;

	c = db_con_get();
	TRY
		r = db_query(c, "SELECT %susers.userid FROM %susers, %sacl "
				"WHERE %sacl.mailbox_id = %llu "
				"AND %susers.user_idnr = %sacl.user_id",DBPFX,DBPFX,DBPFX,
				DBPFX,mboxid,DBPFX,DBPFX);
		while (db_result_next(r))
			*(GList **)identifier_list = g_list_prepend(*(GList **)identifier_list, g_strdup(db_result_get(r, 0)));
	CATCH(SQLException)
		LOG_SQLERROR;
		t = DM_EQUERY;
	FINALLY
		db_con_close(c);
	END_TRY;

	return t;
}

int db_get_mailbox_owner(u64_t mboxid, u64_t * owner_id)
{
	C c; R r; volatile int t = FALSE;
	assert(owner_id != NULL);
	*owner_id = 0;

	c = db_con_get();
	TRY
		r = db_query(c, "SELECT owner_idnr FROM %smailboxes WHERE mailbox_idnr = %llu", DBPFX, mboxid);
		if (db_result_next(r))
			*owner_id = db_result_get_u64(r, 0);
	CATCH(SQLException)
		LOG_SQLERROR;
		t = DM_EQUERY;
	FINALLY
		db_con_close(c);
	END_TRY;
	
	if (t == DM_EQUERY) return t;
	if (*owner_id == 0) return FALSE;

	return TRUE;
}

int db_user_is_mailbox_owner(u64_t userid, u64_t mboxid)
{
	C c; R r; volatile int t = FALSE;

	c = db_con_get();
	TRY
		r = db_query(c, "SELECT mailbox_idnr FROM %smailboxes WHERE mailbox_idnr = %llu AND owner_idnr = %llu", DBPFX, mboxid, userid);
		if (db_result_next(r)) t = TRUE;
	CATCH(SQLException)
		LOG_SQLERROR;
		t = DM_EQUERY;
	FINALLY
		db_con_close(c);
	END_TRY;

	return t;
}

int date2char_str(const char *column, field_t *frag)
{
	assert(frag);
	memset(frag, 0, sizeof(field_t));
	snprintf((char *)frag, sizeof(field_t), db_get_sql(SQL_TO_CHAR), column);
	return 0;
}

int char2date_str(const char *date, field_t *frag)
{
	char *qs;

	assert(frag);
	memset(frag, 0, sizeof(field_t));

	qs = g_strdup_printf("'%s'", date);
	snprintf((char *)frag, sizeof(field_t), db_get_sql(SQL_TO_DATETIME), qs);
	g_free(qs);

	return 0;
}

int db_usermap_resolve(clientbase_t *ci, const char *username, char *real_username)
{
	char clientsock[DM_SOCKADDR_LEN];
	const char *userid = NULL, *sockok = NULL, *sockno = NULL, *login = NULL;
	unsigned row = 0;
	int result = TRUE;
	int score, bestscore = -1;
	char *bestlogin = NULL, *bestuserid = NULL;
	C c; R r; S s;
	INIT_QUERY;

	memset(clientsock,0,DM_SOCKADDR_LEN);
	
	TRACE(TRACE_DEBUG,"checking userid [%s] in usermap", username);
	
	if (ci->tx==0) {
		strncpy(clientsock,"",1);
	} else {
		snprintf(clientsock, DM_SOCKADDR_LEN, "inet:%s:%s", ci->dst_ip, ci->dst_port);
		TRACE(TRACE_DEBUG, "client on inet socket [%s]", clientsock);
	}
	
	/* user_idnr not found, so try to get it from the usermap */
	snprintf(query, DEF_QUERYSIZE, "SELECT login, sock_allow, sock_deny, userid FROM %susermap "
			"WHERE login in (?,'ANY') "
			"ORDER BY sock_allow, sock_deny", 
			DBPFX);

	c = db_con_get();
	TRY
		s = db_stmt_prepare(c, query);
		db_stmt_set_str(s,1,username);

		r = db_stmt_query(s);
		/* find the best match on the usermap table */
		while (db_result_next(r)) {
			row++;
			login = db_result_get(r,0);
			sockok = db_result_get(r,1);
			sockno = db_result_get(r,2);
			userid = db_result_get(r,3);
			result = dm_sock_compare(clientsock, "", sockno);
			/* any match on sockno will be fatal */
			if (! result) break;

			score = dm_sock_score(clientsock, sockok);
			if (score > bestscore) {
				bestlogin = (char *)login;
				bestuserid = (char *)userid;
				bestscore = score;
			}
		}
	CATCH(SQLException)
		LOG_SQLERROR;
	FINALLY
		db_con_close(c);
	END_TRY;

	if (! result) {
		TRACE(TRACE_DEBUG,"access denied");
		return result;
	}

	if (! row) {
		/* user does not exist */
		TRACE(TRACE_DEBUG, "login [%s] not found in usermap", username);
		return DM_SUCCESS;
	}


	TRACE(TRACE_DEBUG, "bestscore [%d]", bestscore);
	if (bestscore == 0)
		return DM_SUCCESS; // no match at all.

	if (bestscore < 0)
		return DM_EGENERAL;
	
	/* use the best matching sockok */
	login = (const char *)bestlogin;
	userid = (const char *)bestuserid;

	TRACE(TRACE_DEBUG,"best match: [%s] -> [%s]", login, userid);

	if ((strncmp(login,"ANY",3)==0)) {
		if (dm_valid_format(userid)==0)
			snprintf(real_username,DM_USERNAME_LEN,userid,username);
		else
			return DM_EQUERY;
	} else {
		strncpy(real_username, userid, DM_USERNAME_LEN);
	}
	
	TRACE(TRACE_DEBUG,"[%s] maps to [%s]", username, real_username);

	return DM_SUCCESS;

}
int db_user_exists(const char *username, u64_t * user_idnr) 
{
	C c; R r; S s;

	assert(username);
	assert(user_idnr);
	*user_idnr = 0;
	
	c = db_con_get();
	TRY
                if(chkmc()){
                        char keyuser[KEY_SIZE];
                        char *ret;
			snprintf(keyuser,(8+(strlen(username)+1)),"dbmidnr_%s",username);
			ret = getmc(keyuser);
			
			if(ret != NULL)
				*user_idnr = atoi(ret);
			
			printf("db_user_exists: username %s user_idnr: %d\n", username,*user_idnr);
			if(*user_idnr == NULL|| *user_idnr == 0){
				printf("not in memcache\n");
				s = db_stmt_prepare(c, "SELECT user_idnr FROM %susers WHERE lower(userid) = lower(?)", DBPFX);
				db_stmt_set_str(s,1,username);
				r = db_stmt_query(s);
				if (db_result_next(r)){
					*user_idnr = db_result_get_u64(r, 0);
					setmc(keyuser,db_result_get(r,0));
					printf("user_idnr: %d \n Setting idnr in memcache: %s %d\n", user_idnr,keyuser,*user_idnr);
				}
			}
		}else{
			s = db_stmt_prepare(c, "SELECT user_idnr FROM %susers WHERE lower(userid) = lower(?)", DBPFX);
                                db_stmt_set_str(s,1,username);
                                r = db_stmt_query(s);
                                if (db_result_next(r))
                                        *user_idnr = db_result_get_u64(r, 0);
                }	
	CATCH(SQLException)
		LOG_SQLERROR;
	FINALLY
		db_con_close(c);
	END_TRY;

	return (*user_idnr) ? 1 : 0;
}

int db_user_create_shadow(const char *username, u64_t * user_idnr)
{
	return db_user_create(username, "UNUSED", "md5", 0xffff, 0, user_idnr);
}

int db_user_create(const char *username, const char *password, const char *enctype,
		 u64_t clientid, u64_t maxmail, u64_t * user_idnr) 
{
	INIT_QUERY;
	C c; R r; S s; volatile int t = FALSE;
	char *encoding = NULL, *frag;
	u64_t id, existing_user_idnr = 0;

	assert(user_idnr != NULL);

	if (db_user_exists(username, &existing_user_idnr))
		return TRUE;

	if (strlen(password) >= (DEF_QUERYSIZE/2)) {
		TRACE(TRACE_ERR, "password length is insane");
		return DM_EQUERY;
	}

	encoding = g_strdup(enctype ? enctype : "");

	c = db_con_get();

	t = TRUE;
	memset(query,0,DEF_QUERYSIZE);
	TRY
		db_begin_transaction(c);
		frag = db_returning("user_idnr");
		if (*user_idnr==0) {
			snprintf(query, DEF_QUERYSIZE, "INSERT INTO %susers "
				"(userid,passwd,client_idnr,maxmail_size,"
				"encryption_type) VALUES "
				"(?,?,?,?,?) %s",
				DBPFX, frag);
			s = db_stmt_prepare(c, query);
			db_stmt_set_str(s, 1, username);
			db_stmt_set_str(s, 2, password);
			db_stmt_set_u64(s, 3, clientid);
			db_stmt_set_u64(s, 4, maxmail);
			db_stmt_set_str(s, 5, encoding);
		} else {
			snprintf(query, DEF_QUERYSIZE, "INSERT INTO %susers "
				"(userid,user_idnr,passwd,client_idnr,maxmail_size,"
				"encryption_type) VALUES "
				"(?,?,?,?,?,?) %s",
				DBPFX, frag);
			s = db_stmt_prepare(c, query);
			db_stmt_set_str(s, 1, username);
			db_stmt_set_u64(s, 2, *user_idnr);
			db_stmt_set_str(s, 3, password);
			db_stmt_set_u64(s, 4, clientid);
			db_stmt_set_u64(s, 5, maxmail);
			db_stmt_set_str(s, 6, encoding);
		}
		g_free(frag);
		if (_db_params.db_driver == DM_DRIVER_ORACLE) {
			db_stmt_exec(s);
			id = db_get_pk(c, "users");
		} else {
			r = db_stmt_query(s);
			id = db_insert_result(c, r);
		}
		if (*user_idnr == 0) *user_idnr = id;
		db_commit_transaction(c);
		if(chkmc()){
		        char keyuser[KEY_SIZE];
                        char keypass[KEY_SIZE];
			char keyencode[KEY_SIZE];
			snprintf(keyuser,(8+(strlen(username)+1)),"dbmidnr_%s",username);
			snprintf(keypass,(8+(strlen(password)+1)),"dbmpass_%d", id);
			snprintf(keyencode,(8+(strlen(encoding)+1)),"dbmencode_%d",encoding);
			char keys[2]= {keyuser,keypass,keyencode};
			char idstr[5];
			sprintf(idstr,"%d",id);
			char values[2]={idstr,password,encoding};
			printf("db_user_create: setmcmulti now\n");
			int i=0;
			for(i=0;i<3;i++){
				setmc(keys[i], values[i]);
			}
		}
	CATCH(SQLException)
		LOG_SQLERROR;
		db_rollback_transaction(c);
		t = DM_EQUERY;
	FINALLY
		db_con_close(c);
	END_TRY;

	g_free(encoding);
	if (t == TRUE)
		TRACE(TRACE_DEBUG, "create shadow account userid [%s], user_idnr [%llu]", username, *user_idnr);

	return t;
}

int db_change_mailboxsize(u64_t user_idnr, u64_t new_size)
{
	return db_update("UPDATE %susers SET maxmail_size = %llu WHERE user_idnr = %llu", DBPFX, new_size, user_idnr);
}

int db_user_delete(const char * username)
{
	C c; S s; volatile int t = FALSE;
	c = db_con_get();
	TRY
		db_begin_transaction(c);
		s = db_stmt_prepare(c, "DELETE FROM %susers WHERE userid = ?", DBPFX);
		db_stmt_set_str(s, 1, username);
		t = db_stmt_exec(s);
		db_commit_transaction(c);
	CATCH(SQLException)
		LOG_SQLERROR;
		db_rollback_transaction(c);
	FINALLY
		db_con_close(c);
	END_TRY;
	return t;
}

int db_user_rename(u64_t user_idnr, const char *new_name) 
{
	C c; S s; volatile gboolean t = FALSE;
	c = db_con_get();
	TRY
		db_begin_transaction(c);
		s = db_stmt_prepare(c, "UPDATE %susers SET userid = ? WHERE user_idnr= ?", DBPFX);
		db_stmt_set_str(s, 1, new_name);
		db_stmt_set_u64(s, 2, user_idnr);
		t = db_stmt_exec(s);
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

int db_user_find_create(u64_t user_idnr)
{
	char *username;
	u64_t idnr;
	int result;

	assert(user_idnr > 0);
	
	TRACE(TRACE_DEBUG,"user_idnr [%llu]", user_idnr);

	if ((result = user_idnr_is_delivery_user_idnr(user_idnr)))
		return result;
	
	if (! (username = auth_get_userid(user_idnr))) 
		return DM_EQUERY;
	
	TRACE(TRACE_DEBUG,"found username for user_idnr [%llu -> %s]",
			user_idnr, username);
	
	if ((db_user_exists(username, &idnr) < 0)) {
		g_free(username);
		return DM_EQUERY;
	}

	if ((idnr > 0) && (idnr != user_idnr)) {
		TRACE(TRACE_ERR, "user_idnr for sql shadow account "
				"differs from user_idnr [%llu != %llu]",
				idnr, user_idnr);
		g_free(username);
		return DM_EQUERY;
	}
	
	if (idnr == user_idnr) {
		TRACE(TRACE_DEBUG, "shadow entry exists and valid");
		g_free(username);
		return DM_EGENERAL;
	}

	result = db_user_create_shadow(username, &user_idnr);
	g_free(username);
	return result;
}

int db_replycache_register(const char *to, const char *from, const char *handle)
{
	char *tmp_to = NULL;
	char *tmp_from = NULL;
	char *tmp_handle = NULL;
	C c; R r; S s; volatile int t = FALSE;
	INIT_QUERY;

	tmp_to = g_strndup(to, REPLYCACHE_WIDTH);
	tmp_from = g_strndup(from, REPLYCACHE_WIDTH);
	tmp_handle = g_strndup(handle, REPLYCACHE_WIDTH);

	snprintf(query, DEF_QUERYSIZE, "SELECT lastseen FROM %sreplycache "
			"WHERE to_addr = ? AND from_addr = ? AND handle = ? ", DBPFX);

	c = db_con_get();
	TRY
		s = db_stmt_prepare(c, query);
		db_stmt_set_str(s, 1, tmp_to);
		db_stmt_set_str(s, 2, tmp_from);
		db_stmt_set_str(s, 3, tmp_handle);

		r = db_stmt_query(s);
		if (db_result_next(r)) 
			t = TRUE;
	CATCH(SQLException)
		LOG_SQLERROR;
		t = DM_EQUERY;
	END_TRY;

	if (t == DM_EQUERY) {
		db_con_close(c);
		return t;
	}
	
	memset(query,0,DEF_QUERYSIZE);
	if (t) {
		snprintf(query, DEF_QUERYSIZE,
			 "UPDATE %sreplycache SET lastseen = %s "
			 "WHERE to_addr = ? AND from_addr = ? "
			 "AND handle = ?",
			 DBPFX, db_get_sql(SQL_CURRENT_TIMESTAMP));
	} else {
		snprintf(query, DEF_QUERYSIZE,
			 "INSERT INTO %sreplycache (to_addr, from_addr, handle, lastseen) "
			 "VALUES (?,?,?, %s)",
			 DBPFX, db_get_sql(SQL_CURRENT_TIMESTAMP));
	}

	t = FALSE;
	db_con_clear(c);
	TRY
		db_begin_transaction(c);
		s = db_stmt_prepare(c, query);
		db_stmt_set_str(s, 1, tmp_to);
		db_stmt_set_str(s, 2, tmp_from);
		db_stmt_set_str(s, 3, tmp_handle);
		t = db_stmt_exec(s);
		db_commit_transaction(c);
	CATCH(SQLException)
		LOG_SQLERROR;
		db_rollback_transaction(c);
		t = DM_EQUERY;
	FINALLY
		db_con_close(c);
		g_free(tmp_to);
		g_free(tmp_from);
		g_free(tmp_handle);
	END_TRY;

	return t;
}

int db_replycache_unregister(const char *to, const char *from, const char *handle)
{
	C c; S s; volatile gboolean t = FALSE;
	INIT_QUERY;

	snprintf(query, DEF_QUERYSIZE,
			"DELETE FROM %sreplycache "
			"WHERE to_addr = ? "
			"AND from_addr = ? "
			"AND handle    = ? ",
			DBPFX);

	c = db_con_get();
	TRY
		db_begin_transaction(c);
		s = db_stmt_prepare(c, query);
		db_stmt_set_str(s, 1, to);
		db_stmt_set_str(s, 2, from);
		db_stmt_set_str(s, 3, handle);
		t = db_stmt_exec(s);
		db_commit_transaction(c);
	CATCH(SQLException)
		LOG_SQLERROR;
	FINALLY
		db_con_close(c);
	END_TRY;

	return t;
}


// 
// Returns FALSE if the (to, from) pair hasn't been seen in days.
//
int db_replycache_validate(const char *to, const char *from,
		const char *handle, int days)
{
	GString *tmp = g_string_new("");
	C c; R r; S s; volatile int t = FALSE;
	INIT_QUERY;

	g_string_printf(tmp, db_get_sql(SQL_EXPIRE), days);

	snprintf(query, DEF_QUERYSIZE,
			"SELECT lastseen FROM %sreplycache "
			"WHERE to_addr = ? AND from_addr = ? "
			"AND handle = ? AND lastseen > (%s)",
			DBPFX, tmp->str);

	g_string_free(tmp, TRUE);

	c = db_con_get();
	TRY
		s = db_stmt_prepare(c, query);
		db_stmt_set_str(s, 1, to);
		db_stmt_set_str(s, 2, from);
		db_stmt_set_str(s, 3, handle);

		r = db_stmt_query(s);
		if (db_result_next(r))
			t = TRUE;
	CATCH(SQLException)
		LOG_SQLERROR;
		t = DM_EQUERY;
	FINALLY
		db_con_close(c);
	END_TRY;

	return t;
}

int db_user_log_login(u64_t user_idnr)
{
	timestring_t timestring;
	create_current_timestring(&timestring);
	return db_update("UPDATE %susers SET last_login = '%s' WHERE user_idnr = %llu",DBPFX, timestring, user_idnr);
}

int db_mailbox_seq_update(u64_t mailbox_id)
{
	return db_update("UPDATE %s %smailboxes SET seq=seq+1 WHERE mailbox_idnr=%llu", 
		db_get_sql(SQL_IGNORE), DBPFX, mailbox_id);
}

int db_rehash_store(void)
{
	GList *ids = NULL;
	C c; S s; R r; volatile int t = FALSE;
	const char *buf;
	char *hash;

	c = db_con_get();
	TRY
		r = db_query(c, "SELECT id FROM %smimeparts", DBPFX);
		while (db_result_next(r)) {
			u64_t *id = g_new0(u64_t,1);
			*id = db_result_get_u64(r, 0);
			ids = g_list_prepend(ids, id);
		}
	CATCH(SQLException)
		LOG_SQLERROR;
		t = DM_EQUERY;
	END_TRY;

	if (t == DM_EQUERY) {
		db_con_close(c);
		return t;
	}

	db_con_clear(c);
	
	t = FALSE;
	ids = g_list_first(ids);
	TRY
		db_begin_transaction(c);
		while (ids) {
			u64_t *id = ids->data;

			db_con_clear(c);
			s = db_stmt_prepare(c, "SELECT data FROM %smimeparts WHERE id=?", DBPFX);
			db_stmt_set_u64(s,1, *id);
			r = db_stmt_query(s);
			db_result_next(r);
			buf = db_result_get(r, 0);
			hash = dm_get_hash_for_string(buf);

			db_con_clear(c);
			s = db_stmt_prepare(c, "UPDATE %smimeparts SET hash=? WHERE id=?", DBPFX);
			db_stmt_set_str(s, 1, hash);
			db_stmt_set_u64(s, 2, *id);
			db_stmt_exec(s);

			g_free(hash);
			if (! g_list_next(ids)) break;
			ids = g_list_next(ids);
		}
		db_commit_transaction(c);
	CATCH(SQLException)
		LOG_SQLERROR;
		db_rollback_transaction(c);
		t = DM_EQUERY;
	FINALLY
		db_con_close(c);
	END_TRY;

	g_list_destroy(ids);

	return t;
}

int db_append_msg(const char *msgdata, u64_t mailbox_idnr, u64_t user_idnr,
		char* internal_date, u64_t * msg_idnr, gboolean recent)
{
        DbmailMessage *message;
	int result;
	char *date = NULL;
	GString *msgdata_string;

	if (! mailbox_is_writable(mailbox_idnr)) return DM_EQUERY;

	msgdata_string = g_string_new(msgdata);

        message = dbmail_message_new();
        message = dbmail_message_init_with_string(message, msgdata_string);
	dbmail_message_set_internal_date(message, (char *)internal_date);
	g_string_free(msgdata_string, TRUE); 
        
        if (dbmail_message_store(message) < 0) {
		dbmail_message_free(message);
		return DM_EQUERY;
	}

	result = db_copymsg(message->id, mailbox_idnr, user_idnr, msg_idnr, recent);
	db_delete_message(message->id);
        dbmail_message_free(message);
	
        switch (result) {
            case -2:
                    TRACE(TRACE_DEBUG, "error copying message to user [%llu],"
                            "maxmail exceeded", user_idnr);
                    return -2;
            case -1:
                    TRACE(TRACE_ERR, "error copying message to user [%llu]", 
                            user_idnr);
                    return -1;
        }
                
        TRACE(TRACE_NOTICE, "message id=%llu is inserted", *msg_idnr);
        
        return (db_set_message_status(*msg_idnr, MESSAGE_STATUS_SEEN)?FALSE:TRUE);
}

