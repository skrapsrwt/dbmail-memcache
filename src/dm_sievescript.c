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
 * \file dm_sievescript.c
 * 
 */

#include "dbmail.h"
#define THIS_MODULE "sievescript"
#define DBPFX _db_params.pfx

extern db_param_t _db_params;

int dm_sievescript_getbyname(u64_t user_idnr, char *scriptname, char **script)
{
	C c; R r; S s; volatile int t = FALSE;
	assert(scriptname);
				
	c = db_con_get();
	TRY
		s = db_stmt_prepare(c, "SELECT script FROM %ssievescripts WHERE owner_idnr = ? AND name = ?", DBPFX);
		db_stmt_set_u64(s, 1, user_idnr);
		db_stmt_set_str(s, 2, scriptname);

		r = db_stmt_query(s);
		if (db_result_next(r))
			*script = g_strdup(db_result_get(r,0));
	CATCH(SQLException)
		LOG_SQLERROR;
		t = DM_EQUERY;
	FINALLY
		db_con_close(c);
	END_TRY;

	return t;
}

int dm_sievescript_isactive(u64_t user_idnr)
{
	return dm_sievescript_isactive_byname(user_idnr, NULL);
}

int dm_sievescript_isactive_byname(u64_t user_idnr, const char *scriptname)
{
	C c; R r; S s; volatile int t = TRUE;

	c = db_con_get();
	TRY
		if (scriptname) {
			s = db_stmt_prepare(c, "SELECT name FROM %ssievescripts WHERE owner_idnr = ? AND active = 1 AND name = ?", DBPFX);
			db_stmt_set_u64(s, 1, user_idnr);
			db_stmt_set_str(s, 2, scriptname);
		} else {
			s = db_stmt_prepare(c, "SELECT name FROM %ssievescripts WHERE owner_idnr = ? AND active = 1", DBPFX);
			db_stmt_set_u64(s, 1, user_idnr);
		}

		r = db_stmt_query(s);
		if (! db_result_next(r)) t = FALSE;

	CATCH(SQLException)
		LOG_SQLERROR;
		t = DM_EQUERY;
	FINALLY
		db_con_close(c);
	END_TRY;

	return t;
}

int dm_sievescript_get(u64_t user_idnr, char **scriptname)
{
	C c; R r; volatile int t = FALSE;
	assert(scriptname);
	*scriptname = NULL;

	c = db_con_get();
	TRY
		r = db_query(c, "SELECT name from %ssievescripts where owner_idnr = %llu and active = 1", DBPFX, user_idnr);
		if (db_result_next(r))
			 *scriptname = g_strdup(db_result_get(r,0));

	CATCH(SQLException)
		LOG_SQLERROR;
		t = DM_EQUERY;
	FINALLY
		db_con_close(c);
	END_TRY;

	return t;
}

int dm_sievescript_list(u64_t user_idnr, GList **scriptlist)
{
	C c; R r; volatile int t = FALSE;

	c = db_con_get();
	TRY
		r = db_query(c,"SELECT name,active FROM %ssievescripts WHERE owner_idnr = %llu", DBPFX,user_idnr);
		while (db_result_next(r)) {
			sievescript_info_t *info = g_new0(sievescript_info_t,1);
			strncpy(info->name, db_result_get(r,0), sizeof(info->name));   
			info->active = db_result_get_int(r,1);
			*(GList **)scriptlist = g_list_prepend(*(GList **)scriptlist, info);
		}
	CATCH(SQLException)
		LOG_SQLERROR;
		t = DM_EQUERY;
	FINALLY
		db_con_close(c);
	END_TRY;

	return t;
}

int dm_sievescript_rename(u64_t user_idnr, char *scriptname, char *newname)
{
	int active = 0;
	C c; R r; S s; volatile int t = FALSE;
	assert(scriptname);

	/* 
	 * According to the draft RFC, a script with the same
	 * name as an existing script should *atomically* replace it.
	 */
	c = db_con_get();
	TRY
		db_begin_transaction(c);

		s = db_stmt_prepare(c,"SELECT active FROM %ssievescripts WHERE owner_idnr = ? AND name = ?", DBPFX);
		db_stmt_set_u64(s,1, user_idnr);
		db_stmt_set_str(s,2, newname);
		r = db_stmt_query(s);

		if (db_result_next(r)) {
			active = db_result_get_int(r,0);

			db_con_clear(c);

			s = db_stmt_prepare(c, "DELETE FROM %ssievescripts WHERE owner_idnr = ? AND name = ?", DBPFX);
			db_stmt_set_u64(s, 1, user_idnr);
			db_stmt_set_str(s, 2, newname);
			db_stmt_exec(s);
		}

		db_con_clear(c);

		s = db_stmt_prepare(c, "UPDATE %ssievescripts SET name = ?, active = ? WHERE owner_idnr = ? AND name = ?", DBPFX);
		db_stmt_set_str(s, 1, newname);
		db_stmt_set_int(s, 2, active);
		db_stmt_set_u64(s, 3, user_idnr);
		db_stmt_set_str(s, 4, scriptname);
		db_stmt_exec(s);

		t = db_commit_transaction(c);

	CATCH(SQLException)
		LOG_SQLERROR;
		t = DM_EQUERY;
		db_rollback_transaction(c);
	FINALLY
		db_con_close(c);
	END_TRY;

	return t;
}

int dm_sievescript_add(u64_t user_idnr, char *scriptname, char *script)
{
	C c; R r; S s; volatile int t = FALSE;
	assert(scriptname);

	c = db_con_get();
	TRY
		db_begin_transaction(c);

		s = db_stmt_prepare(c,"SELECT COUNT(*) FROM %ssievescripts WHERE owner_idnr = ? AND name = ?", DBPFX);
		db_stmt_set_u64(s, 1, user_idnr);
		db_stmt_set_str(s, 2, scriptname);
		
		r = db_stmt_query(s);
		if (db_result_next(r)) {
			
			db_con_clear(c);

			s = db_stmt_prepare(c,"DELETE FROM %ssievescripts WHERE owner_idnr = ? AND name = ?", DBPFX);
			db_stmt_set_u64(s, 1, user_idnr);
			db_stmt_set_str(s, 2, scriptname);

			db_stmt_exec(s);
		}
		
		db_con_clear(c);

		s = db_stmt_prepare(c,"INSERT INTO %ssievescripts (owner_idnr, name, script, active) VALUES (?,?,?,1)", DBPFX);
		db_stmt_set_u64(s, 1, user_idnr);
		db_stmt_set_str(s, 2, scriptname);
		db_stmt_set_blob(s, 3, script, strlen(script));
		db_stmt_exec(s);

		t = db_commit_transaction(c);

	CATCH(SQLException)
		LOG_SQLERROR;
		db_rollback_transaction(c);
		t = DM_EQUERY;
	FINALLY
		db_con_close(c);
	END_TRY;

	return t;
}

int dm_sievescript_deactivate(u64_t user_idnr, char *scriptname)
{
	C c; S s; volatile gboolean t = FALSE;
	assert(scriptname);

	c = db_con_get();
	TRY
		s = db_stmt_prepare(c, "UPDATE %ssievescripts set active = 0 where owner_idnr = ? and name = ?", DBPFX);
		db_stmt_set_u64(s, 1, user_idnr);
		db_stmt_set_str(s, 2, scriptname);
		t = db_stmt_exec(s);
	CATCH(SQLException)
		LOG_SQLERROR;
	FINALLY
		db_con_close(c);
	END_TRY;

	return t;
}

int dm_sievescript_activate(u64_t user_idnr, char *scriptname)
{
	C c; S s; volatile gboolean t = FALSE;
	assert(scriptname);

	c = db_con_get();
	TRY
		db_begin_transaction(c);

		s = db_stmt_prepare(c,"UPDATE %ssievescripts SET active = 0 WHERE owner_idnr = ? ", DBPFX);
		db_stmt_set_u64(s, 1, user_idnr);
		db_stmt_exec(s);

		db_con_clear(c);

		s = db_stmt_prepare(c,"UPDATE %ssievescripts SET active = 1 WHERE owner_idnr = ? AND name = ?", DBPFX);
		db_stmt_set_u64(s, 1, user_idnr);
		db_stmt_set_str(s, 2, scriptname);
		db_stmt_exec(s);

		db_commit_transaction(c);
		t = TRUE;
	CATCH(SQLException)
		LOG_SQLERROR;
		db_rollback_transaction(c);
	FINALLY
		db_con_close(c);
	END_TRY;

	return t;
}

int dm_sievescript_delete(u64_t user_idnr, char *scriptname)
{
	C c; S s; volatile gboolean t = FALSE;
	assert(scriptname);

	c = db_con_get();
	TRY
		s = db_stmt_prepare(c,"DELETE FROM %ssievescripts WHERE owner_idnr = ? AND name = ?", DBPFX);
		db_stmt_set_u64(s, 1, user_idnr);
		db_stmt_set_str(s, 2, scriptname);
		t = db_stmt_exec(s);
	CATCH(SQLException)
		LOG_SQLERROR;
	FINALLY
		db_con_close(c);
	END_TRY;

	return t;
}

int dm_sievescript_quota_check(u64_t user_idnr, u64_t scriptlen)
{
	/* TODO function dm_sievescript_quota_check */
	TRACE(TRACE_DEBUG, "checking %llu sievescript quota with %llu" , user_idnr, scriptlen);
	return DM_SUCCESS;
}

int dm_sievescript_quota_set(u64_t user_idnr, u64_t quotasize)
{
	/* TODO function dm_sievescript_quota_set */
	TRACE(TRACE_DEBUG, "setting %llu sievescript quota with %llu" , user_idnr, quotasize);
	return DM_SUCCESS;
}

int dm_sievescript_quota_get(u64_t user_idnr, u64_t * quotasize)
{
	/* TODO function dm_sievescript_quota_get */
	TRACE(TRACE_DEBUG, "getting sievescript quota for %llu" , user_idnr);
	*quotasize = 0;
	return DM_SUCCESS;
}


