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

/*
 * 
 * \file config.c
 * \brief read configuration values from a config file
 */

#include "dbmail.h"

#define THIS_MODULE "config"

db_param_t _db_params;

/** dictionary which holds the configuration */
static GKeyFile *config_dict = NULL;
static int configured = 0;

/**
 * read the configuration file and stores the configuration
 * directives in an internal structure.
 */
int config_read(const char *config_filename)
{
	if (configured) return 0;
	assert(config_filename != NULL);
        config_dict = g_key_file_new();
	if (! g_key_file_load_from_file(config_dict, config_filename, G_KEY_FILE_NONE, NULL)) {
		g_key_file_free(config_dict);
                TRACE(TRACE_EMERG, "error reading config file %s", config_filename);
		_exit(1);
		return -1;
	}
	// silence the glib logger
	g_log_set_default_handler((GLogFunc)null_logger, NULL);
	configured = 1;
        return 0;
}

/**
 * free all memory related to config 
 */
void config_free(void) 
{
	if (!configured) return;
	g_key_file_free(config_dict);
	config_dict = NULL;
	configured = 0;
}

/* Return 1 if found, 0 if not. */
/* This function also strips any... # Trailing comments. */
/* value is not modified unless something is found. */
static int config_get_value_once(const field_t field_name,
		const char * const service_name,
		field_t value)
{
	char *dict_value;
	int retval = 0;

	assert(service_name);
	assert(config_dict);

	dict_value = g_key_file_get_value(config_dict, service_name, field_name, NULL);
        if (dict_value) {
		char *end;
		end = g_strstr_len(dict_value, FIELDSIZE, "#");
		if (end) *end = '\0';
		g_strstrip(dict_value);
                g_strlcpy(value, dict_value, FIELDSIZE);
		g_free(dict_value);
		retval = 1;
	}

	return retval;
}

/* FIXME: Always returns 0, which is dandy for debugging. */
int config_get_value(const field_t field_name,
                     const char * const service_name,
                     field_t value)
{
	char *key;
	gssize field_len;

	field_len = strlen(field_name);
	
	// First look in the SERVICE section.
	// For each attempt, try as-is, upper, lower.
	       
	key = NULL;
	if (config_get_value_once(field_name, service_name, value))
		goto config_get_value_done;
	
	key = g_ascii_strup(field_name, field_len);
	if (config_get_value_once(key, service_name, value))
		goto config_get_value_done;
	g_free(key);

	key = g_ascii_strdown(field_name, field_len);
	if (config_get_value_once(key, service_name, value))
		goto config_get_value_done;
	g_free(key);

	// if not found, get the DBMAIL section.
	// For each attempt, try as-is, upper, lower.
	       
	key = NULL;
	if (config_get_value_once(field_name, "DBMAIL", value))
		goto config_get_value_done;
	
	key = g_ascii_strup(field_name, field_len);
	if (config_get_value_once(key, "DBMAIL", value))
		goto config_get_value_done;
	g_free(key);

	key = g_ascii_strdown(field_name, field_len);
	if (config_get_value_once(key, "DBMAIL", value))
		goto config_get_value_done;
	g_free(key);
	
	/* give up */
        value[0] = '\0';
	return 0;

config_get_value_done:
	g_free(key);
	return 0;
}

void SetTraceLevel(const char *service_name)
{
	trace_t trace_stderr_int, trace_syslog_int;
	field_t trace_level, trace_syslog, trace_stderr, syslog_logging_levels, file_logging_levels;

	/* Warn about the deprecated "trace_level" config item,
	 * but we will use this value for trace_syslog if needed. */
	config_get_value("trace_level", service_name, trace_level);
	if (strlen(trace_level)) {
		TRACE(TRACE_ERR,
			"Config item TRACE_LEVEL is deprecated and ignored. "
			"Please use SYSLOG_LOGGING_LEVELS and FILE_LOGGING_LEVELS instead.");
	}

	config_get_value("syslog_logging_levels", service_name, syslog_logging_levels);
	config_get_value("file_logging_levels", service_name, file_logging_levels);

	if (strlen(syslog_logging_levels)) {
		trace_syslog_int = atoi(syslog_logging_levels);
	} else {
		config_get_value("trace_syslog", service_name, trace_syslog);
		if (strlen(trace_syslog)) {
			TRACE(TRACE_WARNING,
					"Config item TRACE_SYSLOG is deprecated. "
					"Please use SYSLOG_LOGGING_LEVELS and FILE_LOGGING_LEVELS instead.");

			int old_syslog_int = atoi(trace_syslog);
			switch(old_syslog_int) { // Convert old value to new system
				case 0:
					trace_syslog_int = TRACE_EMERG | TRACE_ALERT | TRACE_CRIT;
					break;
				case 1:
					trace_syslog_int = TRACE_EMERG | TRACE_ALERT | TRACE_CRIT | TRACE_ERR;
					break;
				case 2:
					trace_syslog_int = TRACE_EMERG | TRACE_ALERT | TRACE_CRIT | TRACE_ERR | TRACE_WARNING;
					break;
				case 3:
					trace_syslog_int = TRACE_EMERG | TRACE_ALERT | TRACE_CRIT | TRACE_ERR | TRACE_WARNING | TRACE_NOTICE;
					break;
				case 4:
					trace_syslog_int = TRACE_EMERG | TRACE_ALERT | TRACE_CRIT | TRACE_ERR | TRACE_WARNING | TRACE_NOTICE | TRACE_INFO;
					break;
				case 5:
				default:
					trace_syslog_int = TRACE_EMERG | TRACE_ALERT | TRACE_CRIT | TRACE_ERR | TRACE_WARNING | TRACE_NOTICE | TRACE_INFO | TRACE_DEBUG;
					break;
			}
		} else {
			trace_syslog_int = TRACE_EMERG | TRACE_ALERT | TRACE_CRIT | TRACE_ERR | TRACE_WARNING; // Use Default Levels
		}
	}

	if (strlen(file_logging_levels)) {
		trace_stderr_int = atoi(file_logging_levels);
	} else {
		config_get_value("trace_stderr", service_name, trace_stderr);
		if (strlen(trace_stderr)) {
			TRACE(TRACE_WARNING,
				"Config item TRACE_STDERR is deprecated. "
				"Please use SYSLOG_LOGGING_LEVELS and FILE_LOGGING_LEVELS instead.");

			int old_stderr_int = atoi(trace_stderr);
			switch(old_stderr_int) { // Convert old value to new system
				case 0:
					trace_stderr_int = TRACE_EMERG | TRACE_ALERT | TRACE_CRIT;
					break;
				case 1:
					trace_stderr_int = TRACE_EMERG | TRACE_ALERT | TRACE_CRIT | TRACE_ERR;
					break;
				case 2:
					trace_stderr_int = TRACE_EMERG | TRACE_ALERT | TRACE_CRIT | TRACE_ERR | TRACE_WARNING;
					break;
				case 3:
					trace_stderr_int = TRACE_EMERG | TRACE_ALERT | TRACE_CRIT | TRACE_ERR | TRACE_WARNING | TRACE_NOTICE;
					break;
				case 4:
					trace_stderr_int = TRACE_EMERG | TRACE_ALERT | TRACE_CRIT | TRACE_ERR | TRACE_WARNING | TRACE_NOTICE | TRACE_INFO;
					break;
				case 5:
				default:
					trace_stderr_int = TRACE_EMERG | TRACE_ALERT | TRACE_CRIT | TRACE_ERR | TRACE_WARNING | TRACE_NOTICE | TRACE_INFO | TRACE_DEBUG;
					break;
			}
		} else {
			trace_stderr_int = TRACE_EMERG | TRACE_ALERT | TRACE_CRIT; // Use Default Levels
		}
	}

	configure_debug(trace_syslog_int, trace_stderr_int);
}

void GetDBParams(void)
{
	field_t port_string, sock_string, serverid_string, query_time;
	field_t max_db_connections;

	if (config_get_value("driver", "DBMAIL", _db_params.driver) < 0)
		TRACE(TRACE_EMERG, "error getting config! [driver]");

	if (MATCH((const char *)_db_params.driver,"sqlite"))
		_db_params.db_driver = DM_DRIVER_SQLITE;
	else if (MATCH((const char *)_db_params.driver,"mysql"))
		_db_params.db_driver = DM_DRIVER_MYSQL;
	else if (MATCH((const char *)_db_params.driver,"postgresql"))
		_db_params.db_driver = DM_DRIVER_POSTGRESQL;
	else if (MATCH((const char *)_db_params.driver,"oracle"))
		_db_params.db_driver = DM_DRIVER_ORACLE;
	else
		TRACE(TRACE_EMERG,"driver not supported");

	if (config_get_value("authdriver", "DBMAIL", _db_params.authdriver) < 0)
		TRACE(TRACE_EMERG, "error getting config! [authdriver]");
	if (config_get_value("sortdriver", "DBMAIL", _db_params.sortdriver) < 0)
		TRACE(TRACE_EMERG, "error getting config! [sortdriver]");
	if (config_get_value("host", "DBMAIL", _db_params.host) < 0)
		TRACE(TRACE_EMERG, "error getting config! [host]");
	if (config_get_value("db", "DBMAIL", _db_params.db) < 0) 
		TRACE(TRACE_EMERG, "error getting config! [db]");
	if (config_get_value("user", "DBMAIL", _db_params.user) < 0) 
		TRACE(TRACE_EMERG, "error getting config! [user]");
	if (config_get_value("pass", "DBMAIL", _db_params.pass) < 0)
		TRACE(TRACE_EMERG, "error getting config! [pass]");
	if (config_get_value("sqlport", "DBMAIL", port_string) < 0)
		TRACE(TRACE_EMERG, "error getting config! [sqlpost]");
	if (config_get_value("sqlsocket", "DBMAIL", sock_string) < 0)
		TRACE(TRACE_EMERG, "error getting config! [sqlsocket]");
	if (config_get_value("serverid", "DBMAIL", serverid_string) < 0)
		TRACE(TRACE_EMERG, "error getting config! [serverid]");
	if (config_get_value("encoding", "DBMAIL", _db_params.encoding) < 0)
		TRACE(TRACE_EMERG, "error getting config! [encoding]");
	if (config_get_value("table_prefix", "DBMAIL", _db_params.pfx) < 0)
		TRACE(TRACE_EMERG, "error getting config! [table_prefix]");
	if (config_get_value("max_db_connections", "DBMAIL", max_db_connections) < 0)
		TRACE(TRACE_EMERG, "error getting config! [max_db_connections]");

	if (config_get_value("query_time_info", "DBMAIL", query_time) < 0)
		TRACE(TRACE_EMERG, "error getting config! [query_time_info]");
		if (strlen(query_time) != 0)
			_db_params.query_time_info = (unsigned int) strtoul(query_time, NULL, 10);
		else
			_db_params.query_time_info = 10;

	if (config_get_value("query_time_notice", "DBMAIL", query_time) < 0)
		TRACE(TRACE_EMERG, "error getting config! [query_time_notice]");
		if (strlen(query_time) != 0)
			_db_params.query_time_notice = (unsigned int) strtoul(query_time, NULL, 10);
		else
			_db_params.query_time_notice = 20;

	if (config_get_value("query_time_warning", "DBMAIL", query_time) < 0)
		TRACE(TRACE_EMERG, "error getting config! [query_time_warning]");
		if (strlen(query_time) != 0)
			_db_params.query_time_warning = (unsigned int) strtoul(query_time, NULL, 10);
		else
			_db_params.query_time_warning = 30;

	if (config_get_value("query_timeout", "DBMAIL", query_time) < 0)
		TRACE(TRACE_EMERG, "error getting config! [query_timeout]");
		if (strlen(query_time) != 0)
			_db_params.query_timeout = (unsigned int) strtoul(query_time, NULL, 10) * 1000;
		else
			_db_params.query_timeout = 300000;


	if (strcmp(_db_params.pfx, "\"\"") == 0) {
		/* FIXME: It appears that when the empty string is quoted
		 * that the quotes themselves are returned as the value. */
		g_strlcpy(_db_params.pfx, "", FIELDSIZE);
	} else if (strlen(_db_params.pfx) == 0) {
		/* If it's not "" but is zero length, set the default. */
		g_strlcpy(_db_params.pfx, DEFAULT_DBPFX, FIELDSIZE);
	}

	/* expand ~ in db name to HOME env variable */
	if ((strlen(_db_params.db) > 0 ) && (_db_params.db[0] == '~')) {
		char *homedir;
		field_t db;
		if ((homedir = getenv ("HOME")) == NULL)
			TRACE(TRACE_EMERG, "can't expand ~ in db name");
		g_snprintf(db, FIELDSIZE, "%s%s", homedir, &(_db_params.db[1]));
		g_strlcpy(_db_params.db, db, FIELDSIZE);
	}

	/* check if port_string holds a value */
	if (strlen(port_string) != 0) {
		errno = 0;
		_db_params.port =
		    (unsigned int) strtoul(port_string, NULL, 10);
		if (errno == EINVAL || errno == ERANGE)
			TRACE(TRACE_EMERG, "wrong value for sqlport in config file [%s]", strerror(errno));
	} else
		_db_params.port = 0;

	/* same for sock_string */
	if (strlen(sock_string) != 0)
		g_strlcpy(_db_params.sock, sock_string, FIELDSIZE);
	else
		_db_params.sock[0] = '\0';

	/* serverid */
	if (strlen(serverid_string) != 0) {
		_db_params.serverid = (unsigned int) strtol(serverid_string, NULL, 10);
		if (errno == EINVAL || errno == ERANGE)
			TRACE(TRACE_EMERG, "serverid invalid in config file");
	} else {
		_db_params.serverid = 1;
	}
	/* max_db_connections */
	if (strlen(max_db_connections) != 0) {
		_db_params.max_db_connections = (unsigned int) strtol(max_db_connections, NULL, 10);
		if (errno == EINVAL || errno == ERANGE)
			TRACE(TRACE_EMERG, "max_db_connnections invalid in config file");
	} else {
		_db_params.max_db_connections = 10;
	}

}

void config_get_logfiles(serverConfig_t *config, const char * const service)
{
	field_t val;

	/* logfile */
	config_get_value("logfile", service, val);
	if (! strlen(val))
		g_strlcpy(config->log,DEFAULT_LOG_FILE, FIELDSIZE);
	else
		g_strlcpy(config->log, val, FIELDSIZE);
	assert(config->log);

	/* errorlog */
	config_get_value("errorlog", service, val);
	if (! strlen(val))
		g_strlcpy(config->error_log,DEFAULT_ERROR_LOG, FIELDSIZE);
	else
		g_strlcpy(config->error_log, val, FIELDSIZE);
	assert(config->error_log);

	/* pid directory */
	config_get_value("pid_directory", service, val);
	if (! strlen(val))
		g_strlcpy(config->pid_dir, DEFAULT_PID_DIR, FIELDSIZE);
	else
		g_strlcpy(config->pid_dir, val, FIELDSIZE);
	assert(config->pid_dir);
}

char * config_get_pidfile(serverConfig_t *config, const char *name)
{
	char *res;
	GString *s;
	res = g_build_filename(config->pid_dir, name, NULL);
	s = g_string_new("");
	g_string_printf(s, "%s%s", res, DEFAULT_PID_EXT);
	g_free(res);
	res = s->str;
	g_string_free(s,FALSE);
	return res;
}

