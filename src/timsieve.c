/* 

 Copyright (C) 1999-2004 Aaron Stone aaron at serendipity dot cx

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

/* implementation for tims commands according to RFC 1081 */

#include "dbmail.h"
#define THIS_MODULE "timsieved"

#define STRT 1
#define AUTH 2
#define QUIT 3
#define MAX_ERRORS 3

/* allowed timsieve commands */
static const char *commands[] = {
	"LOGOUT", 
	"STARTTLS", 
	"CAPABILITY", 
	"LISTSCRIPTS",
	"AUTHENTICATE", 
	"DELETESCRIPT", 
	"GETSCRIPT", 
	"SETACTIVE",
	"HAVESPACE", 
	"PUTSCRIPT",
	NULL
};

typedef enum {
	TIMS_LOUT,
	TIMS_STLS,
	TIMS_CAPA,
	TIMS_LIST, // no args
	TIMS_AUTH,
	TIMS_DELS,
	TIMS_GETS,
	TIMS_SETS,
	TIMS_SPAC, // one arg
	TIMS_PUTS,
	TIMS_END
} command_id;



/* Defined in timsieved.c */
extern const char *sieve_extensions;
extern serverConfig_t *server_conf;

static int tims(ClientSession_t *session);
static int tims_tokenizer(ClientSession_t *session, char *buffer);

static void send_greeting(ClientSession_t *session)
{
	field_t banner;
	GETCONFIGVALUE("banner", "SIEVE", banner);
	if (! dm_db_ping()) {
		ci_write(session->ci, "BYE \"database has gone fishing\"\r\n");
		session->state = QUIT;
		return;
	}

	if (strlen(banner) > 0)
		ci_write(session->ci, "\"IMPLEMENTATION\" \"%s\"\r\n", banner);
	else
		ci_write(session->ci, "\"IMPLEMENTATION\" \"DBMail timsieved %s\"\r\n", VERSION);
	ci_write(session->ci, "\"SASL\" \"PLAIN\"\r\n");
	ci_write(session->ci, "\"SIEVE\" \"%s\"\r\n", sieve_extensions);
	ci_write(session->ci, "OK\r\n");
}

static void tims_handle_input(void *arg)
{
	int l = 0;
	char buffer[MAX_LINESIZE];	/* connection buffer */
	ClientSession_t *session = (ClientSession_t *)arg;

	ci_cork(session->ci);

	while (TRUE) {
		memset(buffer, 0, sizeof(buffer));
		l = ci_readln(session->ci, buffer);

		if (l == 0) break;

		if ((l = tims_tokenizer(session, buffer))) {
			if (l == -3) {
				client_session_bailout(&session);
				return;
			}
			if (tims(session) == -3) {
				client_session_bailout(&session);
				return;
			}
			client_session_reset_parser(session);
		}
	}

	if (session->state < QUIT)
		ci_uncork(session->ci);
}

void tims_cb_time(void * arg)
{
	ClientSession_t *session = (ClientSession_t *)arg;
	session->state = QUIT;
	ci_write(session->ci, "BYE \"Connection timed out.\"\r\n");
}

void tims_cb_write(void *arg)
{
	ClientSession_t *session = (ClientSession_t *)arg;
	TRACE(TRACE_DEBUG, "[%p] state: [%d]", session, session->state);

	switch(session->state) {
		case QUIT:
			client_session_bailout(&session);
			break;
		default:
			if (session->ci->write_buffer->len > session->ci->write_buffer_offset) {
				ci_write(session->ci,NULL);
				break;
			}
			session->handle_input(session);
			break;
	}
}

static void reset_callbacks(ClientSession_t *session)
{
        session->ci->cb_time = tims_cb_time;
        session->ci->cb_write = tims_cb_write;
	session->handle_input = tims_handle_input;

        UNBLOCK(session->ci->rx);
        UNBLOCK(session->ci->tx);

	ci_uncork(session->ci);
}

int tims_handle_connection(client_sock *c)
{
	ClientSession_t *session = client_session_new(c);
	session->state = STRT;
	client_session_set_timeout(session, server_conf->login_timeout);
	reset_callbacks(session);
	send_greeting(session);
	return 0;
}

int tims_error(ClientSession_t * session, const char *formatstring, ...)
{
	va_list ap, cp;
	char *s;

	if (session->error_count >= MAX_ERRORS) {
		ci_write(session->ci, "BYE \"Too many errors, closing connection.\"\r\n");
		session->SessionResult = 2;	/* possible flood */
		return -3;
	}

	va_start(ap, formatstring);
	va_copy(cp, ap);
	s = g_strdup_vprintf(formatstring, cp);
	va_end(cp);

	ci_write(session->ci, s);
	g_free(s);

	TRACE(TRACE_DEBUG, "an invalid command was issued");
	session->error_count++;
	return 1;
}


int tims_tokenizer(ClientSession_t *session, char *buffer)
{
	int command_type = 0;
	char *command, *value = NULL;

	if (! session->command_type) {

		command = buffer;

		strip_crlf(command);
		g_strstrip(command);
		if (! strlen(command)) return FALSE; /* ignore empty commands */

		value = strstr(command, " ");	/* look for the separator */

		if (value != NULL) {
			*value++ = '\0';	/* set a \0 on the command end and skip space */
			if (strlen(value) == 0)
				value = NULL;	/* no value specified */
		}

		for (command_type = TIMS_LOUT; command_type < TIMS_END; command_type++)
			if (strcasecmp(command, commands[command_type]) == 0)
				break;

		/* commands that are allowed to have no arguments */
		if ((value == NULL) && !(command_type <= TIMS_LIST) && (command_type < TIMS_END))
			return tims_error(session, "NO \"This command requires an argument.\"\r\n");

		TRACE (TRACE_DEBUG, "command [%s] value [%s]\n", command, value);
		session->command_type = command_type;
	}

	if (session->ci->rbuff_size) {
		size_t l = strlen(buffer);
		size_t n = min(session->ci->rbuff_size, l);
		g_string_append_len(session->rbuff, buffer, n);
		session->ci->rbuff_size -= n;
		if (! session->ci->rbuff_size) {
			session->args = g_list_append(session->args, g_strdup(session->rbuff->str));
			g_string_printf(session->rbuff,"%s","");
			session->parser_state = TRUE;
		}
		TRACE(TRACE_DEBUG, "state [%d], size [%ld]", session->parser_state, session->ci->rbuff_size);
		return session->parser_state;
	}

	if (value) {
		char *s = value;
		int i = 0;
		char p = 0, c = 0;
		gboolean inquote = FALSE;
		GString *t = g_string_new("");
		while ((c = s[i++])) {
			if (inquote) {
				if (c == '"' && p != '\\') {
					session->args = g_list_append(session->args, t->str);
					g_string_free(t, FALSE);
					t = g_string_new("");
					inquote = FALSE;
				} else {
					g_string_append_c(t,c);
				}
				p = c;
				continue;
			}
			if (c == '"' && p != '\\') {
				inquote = TRUE;
				p = c;
				continue;
			}
			if (c == ' ' && p != '\\') {
				p = c;
				continue;
			}
			if (c == '{') { // a literal...
				session->ci->rbuff_size = strtoull(s+i, NULL, 10);
				return FALSE;
			}
			g_string_append_c(t,c);
			p = c;
		}
		if (t->len) {
			session->args = g_list_append(session->args, t->str);
			g_string_free(t,FALSE);
		}
	} 
	session->parser_state = TRUE;
	
	session->args = g_list_first(session->args);
	while (session->args) {
		TRACE(TRACE_DEBUG,"arg: [%s]", (char *)session->args->data);			
		if (! g_list_next(session->args))
			break;
		session->args = g_list_next(session->args);
	}
	//session->args = g_list_append(session->args,g_strdup(value));
	TRACE(TRACE_DEBUG, "[%p] cmd [%d], state [%d] [%s]", session, session->command_type, session->parser_state, buffer);

	return session->parser_state;
}

int tims(ClientSession_t *session)
{
	/* returns values:
	 *  0 to quit
	 * -1 on failure
	 *  1 on success */
	
	char *arg;
	size_t scriptlen = 0;
	int ret;
	char *script = NULL, *scriptname = NULL;
	sort_result_t *sort_result = NULL;
	clientbase_t *ci = session->ci;

	TRACE(TRACE_DEBUG,"[%p] [%d][%s]", session, session->command_type, commands[session->command_type]);
	switch (session->command_type) {
	case TIMS_LOUT:
		ci_write(ci, "OK \"Bye.\"\r\n");
		session->state = QUIT;
		return 1;
		
	case TIMS_STLS:
		ci_write(ci, "NO\r\n");
		return 1;
		
	case TIMS_CAPA:
		send_greeting(session);
		return 1;
		
	case TIMS_AUTH:
		/* We currently only support plain authentication,
		 * which means that the command we accept will look
		 * like this: Authenticate "PLAIN" "base64-password"
		 * */
		session->args = g_list_first(session->args);
		if (! (session->args && session->args->data))
			return 1;

		arg = (char *)session->args->data;
		if (strcasecmp(arg, "PLAIN") == 0) {
			int i = 0;
			u64_t useridnr;
			if (! g_list_next(session->args))
				return tims_error(session, "NO \"Missing argument.\"\r\n");	
			session->args = g_list_next(session->args);
			arg = (char *)session->args->data;

			char **tmp64 = NULL;

			tmp64 = base64_decodev(arg);
			if (tmp64 == NULL)
				return tims_error(session, "NO \"SASL decode error.\"\r\n");
			for (i = 0; tmp64[i] != NULL; i++)
				;
			if (i < 3)
				tims_error(session, "NO \"Too few encoded SASL arguments.\"\r\n");

			/* The protocol specifies that the base64 encoding
			 * be made up of three parts: proxy, username, password
			 * Between them are NULLs, which are conveniently encoded
			 * by the base64 process... */
			if (auth_validate(ci, (const char *)tmp64[1], (const char *)tmp64[2], &useridnr) == 1) {
				ci_authlog_init(ci, THIS_MODULE, tmp64[1], AUTHLOG_ACT);

				ci_write(ci, "OK\r\n");
				session->state = AUTH;
				session->useridnr = useridnr;
				session->username = g_strdup(tmp64[1]);
				session->password = g_strdup(tmp64[2]);

				client_session_set_timeout(session, server_conf->timeout);

			} else {
				ci_authlog_init(ci, THIS_MODULE, tmp64[1], AUTHLOG_ERR);
				g_strfreev(tmp64);
				return tims_error(session, "NO \"Username or password incorrect.\"\r\n");
			}
			g_strfreev(tmp64);
		} else
			return tims_error(session, "NO \"Authentication scheme not supported.\"\r\n");

		return 1;

	case TIMS_PUTS:
		if (session->state != AUTH) {
			ci_write(ci, "NO \"Please authenticate first.\"\r\n");
			break;
		}
		
		session->args = g_list_first(session->args);
		if (! (session->args && session->args->data))
			return 1;

		scriptname = (char *)session->args->data;
		session->args = g_list_next(session->args);
		assert(session->args);

		script = (char *)session->args->data;

		scriptlen = strlen(script);
		TRACE(TRACE_INFO, "Client sending script of length [%ld]", scriptlen);
		if (scriptlen >= UINT_MAX)
			return tims_error(session, "NO \"Invalid script length.\"\r\n");

		if (dm_sievescript_quota_check(session->useridnr, scriptlen))
			return tims_error(session, "NO \"Script exceeds available space.\"\r\n");

		/* Store the script temporarily,
		 * validate it, then rename it. */
		if (dm_sievescript_add(session->useridnr, "@!temp-script!@", script)) {
			dm_sievescript_delete(session->useridnr, "@!temp-script!@");
			return tims_error(session, "NO \"Error inserting script.\"\r\n");
		}
		
		sort_result = sort_validate(session->useridnr, "@!temp-script!@");
		if (sort_result == NULL) {
			dm_sievescript_delete(session->useridnr, "@!temp-script!@");
			return tims_error(session, "NO \"Error inserting script.\"\r\n");
		} else if (sort_get_error(sort_result) > 0) {
			dm_sievescript_delete(session->useridnr, "@!temp-script!@");
			return tims_error(session, "NO \"Script error: %s.\"\r\n", sort_get_errormsg(sort_result));
		} 
		/* According to the draft RFC, a script with the same
		 * name as an existing script should [atomically] replace it. */
		if (dm_sievescript_rename(session->useridnr, "@!temp-script!@", scriptname))
			return tims_error(session, "NO \"Error inserting script.\"\r\n");

		ci_write(ci, "OK \"Script successfully received.\"\r\n");

		break;
		
	case TIMS_SETS:
		if (session->state != AUTH) {
			ci_write(ci, "NO \"Please authenticate first.\"\r\n");
			break;
		}
		

		scriptname = NULL;

		session->args = g_list_first(session->args);
		if ((session->args && session->args->data))
			scriptname = (char *)session->args->data;

		if (strlen(scriptname)) {
			if (! dm_sievescript_activate(session->useridnr, scriptname))
				ci_write(ci, "NO \"Error activating script.\"\r\n");
			else
				ci_write(ci, "OK \"Script activated.\"\r\n");
		} else {
			ret = dm_sievescript_get(session->useridnr, &scriptname);
			if (scriptname == NULL) {
				ci_write(ci, "OK \"No scripts are active at this time.\"\r\n");
			} else {
				if (! dm_sievescript_deactivate(session->useridnr, scriptname))
					ci_write(ci, "NO \"Error deactivating script.\"\r\n");
				else
					ci_write(ci, "OK \"All scripts deactivated.\"\r\n");
				g_free(scriptname);
			}
		}
		return 1;
		
	case TIMS_GETS:
		if (session->state != AUTH) {
			ci_write(ci, "NO \"Please authenticate first.\"\r\n");
			break;
		}
		
		session->args = g_list_first(session->args);
		if (! (session->args && session->args->data))
			return 1;

		scriptname = (char *)session->args->data;

		if (! strlen(scriptname))
			return tims_error(session, "NO \"Script name required.\"\r\n");

		ret = dm_sievescript_getbyname(session->useridnr, scriptname, &script);
		if (script == NULL) {
			return tims_error(session, "NO \"Script not found.\"\r\n");
		} else if (ret < 0) {
			g_free(script);
			return tims_error(session, "NO \"Internal error.\"\r\n");
		} else {
			ci_write(ci, "{%u+}\r\n", (unsigned int)strlen(script));
			ci_write(ci, "%s\r\n", script);
			ci_write(ci, "OK\r\n");
			g_free(script);
		}
		return 1;
		
	case TIMS_DELS:
		if (session->state != AUTH) {
			ci_write(ci, "NO \"Please authenticate first.\"\r\n");
			break;
		}
		
		session->args = g_list_first(session->args);
		if (! (session->args && session->args->data))
			return 1;

		scriptname = (char *)session->args->data;

		if (! strlen(scriptname))
			return tims_error(session, "NO \"Script name required.\"\r\n");

		if (! dm_sievescript_delete(session->useridnr, scriptname))
			return tims_error(session, "NO \"Error deleting script.\"\r\n");
		else
			ci_write(ci, "OK \"Script deleted.\"\r\n", scriptname);

		return 1;

	case TIMS_SPAC:
		if (session->state != AUTH) {
			ci_write(ci, "NO \"Please authenticate first.\"\r\n");
			break;
		}

		// Command is in format: HAVESPACE "scriptname" 12345
		// TODO: this is a fake
		if (dm_sievescript_quota_check(session->useridnr, 12345) == DM_SUCCESS)
			ci_write(ci, "OK (QUOTA)\r\n");
		else
			ci_write(ci, "NO (QUOTA) \"Quota exceeded\"\r\n");
		return 1;
		
	case TIMS_LIST:
		if (session->state != AUTH) {
			ci_write(ci, "NO \"Please authenticate first.\"\r\n");
			break;
		}
		
		GList *scriptlist = NULL;

		if (dm_sievescript_list (session->useridnr, &scriptlist) < 0) {
			ci_write(ci, "NO \"Internal error.\"\r\n");
		} else {
			if (g_list_length(scriptlist) == 0) {
				/* The command hasn't failed, but there aren't any scripts */
				ci_write(ci, "OK \"No scripts found.\"\r\n");
			} else {
				scriptlist = g_list_first(scriptlist);
				while (scriptlist) {
					sievescript_info_t *info = (sievescript_info_t *) scriptlist->data;
					ci_write(ci, "\"%s\"%s\r\n", info->name, (info-> active == 1 ?  " ACTIVE" : ""));
					if (! g_list_next(scriptlist)) break;
					scriptlist = g_list_next(scriptlist);
				}
				ci_write(ci, "OK\r\n");
			}
			g_list_destroy(scriptlist);
		}
		return 1;
		
	default:
		return tims_error(session, "NO \"What are you trying to say here?\"\r\n");
	}

	if (sort_result)
		sort_free_result(sort_result);
	
	return 1;
}
