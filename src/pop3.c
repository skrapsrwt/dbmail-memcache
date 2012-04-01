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

/* implementation for pop3 commands according to RFC 1081 */

#include "dbmail.h"
#define THIS_MODULE "pop3"

#define MAX_ERRORS 3

/* allowed pop3 commands */
const char *commands[] = {
	"quit", /**< POP3_QUIT */
	"user", /**< POP3_USER */
	"pass", /**< POP3_PASS */
	"stat", /**< POP3_STAT */
	"list", /**< POP3_LIST */
	"retr", /**< POP3_RETR */
	"dele", /**< POP3_DELE */
	"noop", /**< POP3_NOOP */
	"last", /**< POP3_LAST */
	"rset", /**< POP3_RSET */
	"uidl", /**< POP3_UIDL */
	"apop", /**< POP3_APOP */
	"auth", /**< POP3_AUTH */
	"top", /**< POP3_TOP */
	"capa", /**< POP3_CAPA */
	"stls"  /**< POP3_STLS */
};

const char ValidNetworkChars[] =
    "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789"
    ",'\"?_.!|@#$%^&*()-+=~[]{}<>:;\\/ '";

extern serverConfig_t *server_conf;
extern int pop_before_smtp;

static int pop3(ClientSession_t *session, const char *buffer);

static void send_greeting(ClientSession_t *session)
{
	field_t banner;
	GETCONFIGVALUE("banner", "POP", banner);
	if (! dm_db_ping()) {
		ci_write(session->ci, "+ERR database has gone fishing\r\n");
		session->state = CLIENTSTATE_QUIT;
		return;
	}

	if (strlen(banner) > 0) {
		ci_write(session->ci, "+OK %s %s\r\n", banner, session->apop_stamp);
	} else {
		ci_write(session->ci, "+OK DBMAIL pop3 server ready to rock %s\r\n", session->apop_stamp);
	}
}

extern db_param_t _db_params;
#define DBPFX _db_params.pfx

static int db_createsession(u64_t user_idnr, ClientSession_t * session_ptr)
{
	C c; R r; volatile int t = DM_SUCCESS;
	struct message *tmpmessage;
	int message_counter = 0;
	const char *query_result;
	u64_t mailbox_idnr;
	INIT_QUERY;

	if (db_find_create_mailbox("INBOX", BOX_DEFAULT, user_idnr, &mailbox_idnr) < 0) {
		TRACE(TRACE_NOTICE, "find_create INBOX for user [%llu] failed, exiting..", user_idnr);
		return DM_EQUERY;
	}

	g_return_val_if_fail(mailbox_idnr > 0, DM_EQUERY);

	/* query is < MESSAGE_STATUS_DELETE  because we don't want deleted 
	 * messages
	 */
	snprintf(query, DEF_QUERYSIZE,
		 "SELECT pm.messagesize, msg.message_idnr, msg.status, "
		 "msg.unique_id FROM %smessages msg, %sphysmessage pm "
		 "WHERE msg.mailbox_idnr = %llu "
		 "AND msg.status < %d "
		 "AND msg.physmessage_id = pm.id "
		 "ORDER BY msg.message_idnr ASC",DBPFX,DBPFX,
		 mailbox_idnr, MESSAGE_STATUS_DELETE);

	c = db_con_get();
	TRY
		r = db_query(c, query);

		session_ptr->totalmessages = 0;
		session_ptr->totalsize = 0;

		/* messagecounter is total message, +1 tot end at message 1 */
		message_counter = 1;

		/* filling the list */
		TRACE(TRACE_DEBUG, "adding items to list");
		while (db_result_next(r)) {
			tmpmessage = g_new0(struct message,1);
			/* message size */
			tmpmessage->msize = db_result_get_u64(r,0);
			/* real message id */
			tmpmessage->realmessageid = db_result_get_u64(r,1);
			/* message status */
			tmpmessage->messagestatus = db_result_get_u64(r,2);
			/* virtual message status */
			tmpmessage->virtual_messagestatus = tmpmessage->messagestatus;
			/* unique id */
			query_result = db_result_get(r,3);
			if (query_result)
				strncpy(tmpmessage->uidl, query_result, UID_SIZE);

			session_ptr->totalmessages++;
			session_ptr->totalsize += tmpmessage->msize;
			tmpmessage->messageid = (u64_t) message_counter;

			session_ptr->messagelst = g_list_prepend(session_ptr->messagelst, tmpmessage);

			message_counter++;
		}
	CATCH(SQLException)
		LOG_SQLERROR;
		t = DM_EQUERY;
	FINALLY
		db_con_close(c);
	END_TRY;

	if (t == DM_EQUERY) return t;
	if (message_counter == 1) {
		/* there are no messages for this user */
		return DM_EGENERAL;
	}

	/* descending list */
	session_ptr->messagelst = g_list_reverse(session_ptr->messagelst);

	TRACE(TRACE_DEBUG, "adding succesful");

	/* setting all virtual values */
	session_ptr->virtual_totalmessages = session_ptr->totalmessages;
	session_ptr->virtual_totalsize = session_ptr->totalsize;

	return DM_EGENERAL;
}

static void db_session_cleanup(ClientSession_t * session_ptr)
{
	/* cleanups a session 
	   removes a list and all references */
	session_ptr->totalsize = 0;
	session_ptr->virtual_totalsize = 0;
	session_ptr->totalmessages = 0;
	session_ptr->virtual_totalmessages = 0;
	g_list_destroy(session_ptr->messagelst);
}


static void pop3_close(ClientSession_t *session)
{
	clientbase_t *ci = session->ci;
	TRACE(TRACE_DEBUG,"[%p] sessionResult [%d]", session, session->SessionResult);

	session->state = CLIENTSTATE_QUIT;
	if (session->username != NULL && (session->was_apop || session->password != NULL)) {

		switch (session->SessionResult) {
		case 0:
			TRACE(TRACE_NOTICE, "user %s logging out [messages=%llu, octets=%llu]", 
					session->username, 
					session->virtual_totalmessages, 
					session->virtual_totalsize);

			/* if everything went well, write down everything and do a cleanup */
			if (db_update_pop(session) == DM_SUCCESS)
				ci_write(ci, "+OK see ya later\r\n");
			else
				ci_write(ci, "-ERR some deleted messages not removed\r\n");
			break;

		case 1:
			ci_write(ci, "-ERR I'm leaving, you're too slow\r\n");
			TRACE(TRACE_ERR, "client timed out, connection closed");
			break;

		case 2:
			TRACE(TRACE_ERR, "alert! possible flood attempt, closing connection");
			break;

		case 3:
			TRACE(TRACE_ERR, "authorization layer failure");
			break;
		case 4:
			TRACE(TRACE_ERR, "storage layer failure");
			break;
		}
	} else {
		ci_write(ci, "+OK see ya later\r\n");
	}
}


/* the default pop3 read handler */

static void pop3_handle_input(void *arg)
{
	char buffer[MAX_LINESIZE];	/* connection buffer */
	ClientSession_t *session = (ClientSession_t *)arg;

	if (session->ci->write_buffer->len) {
		ci_write(session->ci, NULL);
		return;
	}

	memset(buffer, 0, sizeof(buffer));
	if (ci_readln(session->ci, buffer) == 0)
		return;

	pop3(session, buffer);
}

void pop3_cb_write(void *arg)
{
	ClientSession_t *session = (ClientSession_t *)arg;
	TRACE(TRACE_DEBUG, "[%p] state: [%d]", session, session->state);

	switch (session->state) {
		case CLIENTSTATE_QUIT:
			db_session_cleanup(session);
			client_session_bailout(&session);
			break;
		default:
			ci_write_cb(session->ci);
			session->handle_input(session);
			break;
	}
}

void pop3_cb_time(void * arg)
{
	ClientSession_t *session = (ClientSession_t *)arg;
	session->state = CLIENTSTATE_QUIT;
	ci_write(session->ci, "-ERR I'm leaving, you're too slow\r\n");
}

static void reset_callbacks(ClientSession_t *session)
{
        session->ci->cb_time = pop3_cb_time;
        session->ci->cb_write = pop3_cb_write;
	session->handle_input = pop3_handle_input;

        UNBLOCK(session->ci->rx);
        UNBLOCK(session->ci->tx);

	ci_uncork(session->ci);
}

int pop3_handle_connection(client_sock *c)
{
	ClientSession_t *session = client_session_new(c);
	session->state = CLIENTSTATE_INITIAL_CONNECT;
	client_session_set_timeout(session, server_conf->login_timeout);
        reset_callbacks(session);
        send_greeting(session);
	return 0;
}

int pop3_error(ClientSession_t * session, const char *formatstring, ...)
{
	va_list ap, cp;
	char *s;
	clientbase_t *ci = session->ci;

	if (session->error_count >= MAX_ERRORS) {
		ci_write(ci, "-ERR too many errors\r\n");
		client_session_bailout(&session);
		return -3;
	} else {
		va_start(ap, formatstring);
		va_copy(cp, ap);
		s = g_strdup_vprintf(formatstring, cp);
		va_end(cp);
		ci_write(ci, s);
		g_free(s);
	}

	TRACE(TRACE_DEBUG, "an invalid command was issued");
	session->error_count++;
	return 1;
}

static int _pop3_session_authenticated(ClientSession_t *session, int user_idnr)
{
	clientbase_t *ci = session->ci;
	int result = 0;
	session->state = CLIENTSTATE_AUTHENTICATED;
	ci_authlog_init(ci, THIS_MODULE, (const char *)session->username, AUTHLOG_ACT);
	client_session_set_timeout(session, server_conf->timeout);

	/* user seems to be valid, let's build a session */
	TRACE(TRACE_DEBUG, "validation OK, building a session for user [%s]", 
			session->username);

	/* if pop_before_smtp is active, log this ip */
	if (pop_before_smtp)
		db_log_ip(ci->src_ip);

	result = db_createsession(user_idnr, session);
	if (result == 1) {
		ci_write(ci, "+OK %s has %llu messages (%llu octets)\r\n", 
				session->username, 
				session->virtual_totalmessages, 
				session->virtual_totalsize);
		TRACE(TRACE_NOTICE, "user %s logged in [messages=%llu, octets=%llu]", 
				session->username, 
				session->virtual_totalmessages, 
				session->virtual_totalsize);
	} else
		session->SessionResult = 4;	/* Database error. */

	return result;
}

int pop3(ClientSession_t *session, const char *buffer)
{
	/* returns a 0  on a quit
	 *           -1  on a failure
	 *            1  on a success 
	 */
	char *command, *value, *searchptr, *enctype, *s;
	Pop3Cmd_t cmdtype;
	int found = 0, indx = 0, validate_result;
	u64_t result, top_lines, top_messageid, user_idnr;
	unsigned char *md5_apop_he;
	struct message *msg;
	clientbase_t *ci = session->ci;

	s = (char *)buffer;

	strip_crlf(s);
	g_strstrip(s);

	/* check for command issued */
	while (strchr(ValidNetworkChars, s[indx++]))
		;

	TRACE(TRACE_DEBUG, "incoming buffer: [%s]", s);
	if (! strlen(s)) return 1;

	command = s;

	value = strstr(command, " ");	/* look for the separator */

	if (value != NULL) {
		*value = '\0';	/* set a \0 on the command end */
		value++;	/* skip space */

		if (strlen(value) == 0)
			value = NULL;	/* no value specified */
		else {
			TRACE(TRACE_DEBUG, "state[%d], command issued :cmd [%s], value [%s]\n", session->state, command, value);
		}
	}

	/* find command that was issued */
	for (cmdtype = POP3_QUIT; cmdtype < POP3_FAIL; cmdtype++)
		if (strcasecmp(command, commands[cmdtype]) == 0) {
			session->was_apop = 1;
			break;
		}

	TRACE(TRACE_DEBUG, "command looked up as commandtype %d", 
			cmdtype);

	/* commands that are allowed to have no arguments */
	if (value == NULL) {
		switch (cmdtype) {
			case POP3_QUIT:
			case POP3_LIST:
			case POP3_STAT:
			case POP3_RSET:
			case POP3_NOOP:
			case POP3_LAST:
			case POP3_UIDL:
			case POP3_AUTH:
			case POP3_CAPA:
			case POP3_STLS:
				break;
			default:
				return pop3_error(session, "-ERR your command does not compute\r\n");
			break;
		}
	}

	switch (cmdtype) {
		
	case POP3_QUIT:
		/* We return 0 here, and then pop3_handle_connection cleans up
		 * the connection, commits all changes, and sends the final
		 * "OK" message indicating that QUIT has completed. */
		session->state = CLIENTSTATE_LOGOUT;
		session->SessionResult = 0;
		pop3_close(session);
		return 0;
		
	case POP3_STLS:
		if (session->state != CLIENTSTATE_INITIAL_CONNECT)
			return pop3_error(session, "-ERR wrong command mode\r\n");
		if (! server_conf->ssl)
			return pop3_error(session, "-ERR server error\r\n");

		if (session->ci->ssl_state)
			return pop3_error(session, "-ERR TLS already active\r\n");
		ci_write(session->ci, "+OK Begin TLS now\r\n");
		if (ci_starttls(session->ci) < 0) return 0;
		return 1;

	case POP3_USER:
		if (session->state != CLIENTSTATE_INITIAL_CONNECT)
			return pop3_error(session, "-ERR wrong command mode\r\n");

		if (session->username != NULL) {
			/* reset username */
			g_free(session->username);
			session->username = NULL;
		}

		if (session->username == NULL) {
			/* create memspace for username */
			session->username = g_new0(char,strlen(value) + 1);
			strncpy(session->username, value, strlen(value) + 1);
		}

		ci_write(ci, "+OK Password required for %s\r\n", session->username);
		return 1;

	case POP3_PASS:
		if (session->state != CLIENTSTATE_INITIAL_CONNECT)
			return pop3_error(session, "-ERR wrong command mode\r\n");

		if (session->password != NULL) {
			g_free(session->password);
			session->password = NULL;
		}

		if (session->password == NULL) {
			/* create memspace for password */
			session->password = g_new0(char,strlen(value) + 1);
			strncpy(session->password, value, strlen(value) + 1);
		}

		/* check in authorization layer if these credentials are correct */
		validate_result = auth_validate(ci, (const char *)session->username, (const char *)session->password, &result);

		switch (validate_result) {
		case -1:
			session->SessionResult = 3;
			return -1;
		case 0:
			ci_authlog_init(ci, THIS_MODULE, (const char *)session->username, AUTHLOG_ERR);
			TRACE(TRACE_ERR, "user [%s] coming from [%s] tried to login with wrong password", 
				session->username, ci->src_ip);

			g_free(session->username);
			session->username = NULL;

			g_free(session->password);
			session->password = NULL;

			return pop3_error(session, "-ERR username/password incorrect\r\n");
		default:
			return _pop3_session_authenticated(session, result);
		}

		return 1;

	case POP3_LIST:
		if (session->state != CLIENTSTATE_AUTHENTICATED)
			return pop3_error(session, "-ERR wrong command mode\r\n");

		session->messagelst = g_list_first(session->messagelst);

		if (value != NULL) {
			/* they're asking for a specific message */
			while (session->messagelst) {
				msg = (struct message *)session->messagelst->data;
				if (msg->messageid == strtoull(value,NULL, 10) && msg->virtual_messagestatus < MESSAGE_STATUS_DELETE) {
					ci_write(ci, "+OK %llu %llu\r\n", msg->messageid,msg->msize);
					found = 1;
				}
				if (! g_list_next(session->messagelst))
					break;
				session->messagelst = g_list_next(session->messagelst);
			}
			if (!found)
				return pop3_error(session, "-ERR [%s] no such message\r\n", value);
			else
				return 1;
		}

		/* just drop the list */
		ci_write(ci, "+OK %llu messages (%llu octets)\r\n", session->virtual_totalmessages, session->virtual_totalsize);

		if (session->virtual_totalmessages > 0) {
			/* traversing list */
			while (session->messagelst) {
				msg = (struct message *)session->messagelst->data;
				if (msg->virtual_messagestatus < MESSAGE_STATUS_DELETE)
					ci_write(ci, "%llu %llu\r\n", msg->messageid,msg->msize);
				if (! g_list_next(session->messagelst))
					break;
				session->messagelst = g_list_next(session->messagelst);
			}
		}
		ci_write(ci, ".\r\n");
		return 1;

	case POP3_STAT:
		if (session->state != CLIENTSTATE_AUTHENTICATED)
			return pop3_error(session, "-ERR wrong command mode\r\n");

		ci_write(ci, "+OK %llu %llu\r\n", 
				session->virtual_totalmessages, 
				session->virtual_totalsize);

		return 1;

	case POP3_RETR:
		TRACE(TRACE_DEBUG, "RETR command, retrieving message");

		if (session->state != CLIENTSTATE_AUTHENTICATED)
			return pop3_error(session, "-ERR wrong command mode\r\n");

		session->messagelst = g_list_first(session->messagelst);

		/* selecting a message */
		TRACE(TRACE_DEBUG, "RETR command, selecting message");
		
		while (session->messagelst) {
			msg = (struct message *) session->messagelst->data;
			if (msg->messageid == strtoull(value, NULL, 10) && msg->virtual_messagestatus < MESSAGE_STATUS_DELETE) {	/* message is not deleted */
				char *s = NULL;
				msg->virtual_messagestatus = MESSAGE_STATUS_SEEN;
				if (! (s = db_get_message_lines(msg->realmessageid, -2)))
					return -1;
				ci_write(ci, "+OK %llu octets\r\n%s", (u64_t)strlen(s), s);
				ci_write(ci, "\r\n.\r\n");
				g_free(s);
				return 1;
			}
			if (! g_list_next(session->messagelst))
				break;
			session->messagelst = g_list_next(session->messagelst);
		}
		return pop3_error(session, "-ERR [%s] no such message\r\n", value);

	case POP3_DELE:
		if (session->state != CLIENTSTATE_AUTHENTICATED)
			return pop3_error(session, "-ERR wrong command mode\r\n");

		session->messagelst = g_list_first(session->messagelst);

		/* selecting a message */
		while (session->messagelst) {
			msg = (struct message *)session->messagelst->data;
			if (msg->messageid == strtoull(value, NULL, 10) && msg->virtual_messagestatus < MESSAGE_STATUS_DELETE) {	/* message is not deleted */
				msg->virtual_messagestatus = MESSAGE_STATUS_DELETE;
				session->virtual_totalsize -= msg->msize;
				session->virtual_totalmessages -= 1;

				ci_write(ci, "+OK message %llu deleted\r\n", msg->messageid);
				return 1;
			}
			if (! g_list_next(session->messagelst))
				break;
			session->messagelst = g_list_next(session->messagelst);
		}
		return pop3_error(session, "-ERR [%s] no such message\r\n", value);

	case POP3_RSET:
		if (session->state != CLIENTSTATE_AUTHENTICATED)
			return pop3_error(session, "-ERR wrong command mode\r\n");

		session->messagelst = g_list_first(session->messagelst);

		session->virtual_totalsize = session->totalsize;
		session->virtual_totalmessages = session->totalmessages;

		while (session->messagelst) {
			msg = (struct message *)session->messagelst->data;
			msg->virtual_messagestatus = msg->messagestatus;

			if (! g_list_next(session->messagelst))
				break;
			session->messagelst = g_list_next(session->messagelst);
		}

		ci_write(ci, "+OK %llu messages (%llu octets)\r\n", session->virtual_totalmessages, session->virtual_totalsize);

		return 1;

	case POP3_LAST:
		if (session->state != CLIENTSTATE_AUTHENTICATED)
			return pop3_error(session, "-ERR wrong command mode\r\n");

		session->messagelst = g_list_first(session->messagelst);

		while (session->messagelst) {
			msg = (struct message *)session->messagelst->data;
			if (msg->virtual_messagestatus == MESSAGE_STATUS_NEW) {
				/* we need the last message that has been accessed */
				ci_write(ci, "+OK %llu\r\n", msg->messageid - 1);
				return 1;
			}
			if (! g_list_next(session->messagelst))
				break;
			session->messagelst = g_list_next(session->messagelst);
		}

		/* all old messages */
		ci_write(ci, "+OK %llu\r\n", session->virtual_totalmessages);

		return 1;

	case POP3_NOOP:
		if (session->state != CLIENTSTATE_AUTHENTICATED)
			return pop3_error(session, "-ERR wrong command mode\r\n");

		ci_write(ci, "+OK\r\n");
		return 1;

	case POP3_UIDL:
		if (session->state != CLIENTSTATE_AUTHENTICATED)
			return pop3_error(session, "-ERR wrong command mode\r\n");

		session->messagelst = g_list_first(session->messagelst);

		if (value != NULL) {
			/* they're asking for a specific message */
			while (session->messagelst) {
				msg = (struct message *)session->messagelst->data;
				if (msg->messageid == strtoull(value,NULL, 10) && msg->virtual_messagestatus < MESSAGE_STATUS_DELETE) {
					ci_write(ci, "+OK %llu %s\r\n", msg->messageid,msg->uidl);
					found = 1;
				}

				if (! g_list_next(session->messagelst))
					break;
				session->messagelst = g_list_next(session->messagelst);
			}
			if (!found)
				return pop3_error(session, "-ERR [%s] no such message\r\n", value);
			else
				return 1;
		}

		/* just drop the list */
		ci_write(ci, "+OK Some very unique numbers for you\r\n");

		if (session->virtual_totalmessages > 0) {
			/* traversing list */
			while (session->messagelst) {
				msg = (struct message *)session->messagelst->data; 
				if (msg->virtual_messagestatus < MESSAGE_STATUS_DELETE)
					ci_write(ci, "%llu %s\r\n", msg->messageid, msg->uidl);

				if (! g_list_next(session->messagelst))
					break;
				session->messagelst = g_list_next(session->messagelst);
			}
		}

		ci_write(ci, ".\r\n");

		return 1;

	case POP3_APOP:
		if (session->state != CLIENTSTATE_INITIAL_CONNECT)
			return pop3_error(session, "-ERR wrong command mode\r\n");

		/* find out where the md5 hash starts */
		searchptr = strstr(value, " ");

		if (searchptr == NULL) 
			return pop3_error(session, "-ERR your command does not compute\r\n");

		/* skip the space */
		searchptr = searchptr + 1;

		/* value should now be the username */
		value[searchptr - value - 1] = '\0';

		if (strlen(searchptr) != 32)
			return pop3_error(session, "-ERR not a valid md5 hash\r\n");

		md5_apop_he = g_new0(unsigned char,strlen(searchptr) + 1);
		session->username = g_new0(char,strlen(value) + 1);

		strncpy((char *)md5_apop_he, searchptr, strlen(searchptr) + 1);
		strncpy(session->username, value, strlen(value) + 1);

		/*
		 * check the encryption used for this user
		 * note that if the user does not exist it is not noted
		 * by db_getencryption()
		 */
		if (! auth_user_exists(session->username, &user_idnr)) {
			TRACE(TRACE_ERR, "error finding if user exists. username = [%s]", 
					session->username);
			return -1;
		}
		enctype = auth_getencryption(user_idnr);
		if (strcasecmp(enctype, "") != 0) {
			/* it should be clear text */
			g_free(enctype);
			g_free(md5_apop_he);
			g_free(session->username);
			session->username = NULL;
			md5_apop_he = NULL;
			return pop3_error(session, "-ERR APOP command is not supported for this user\r\n");
		}

		TRACE(TRACE_DEBUG, "APOP auth, username [%s], md5_hash [%s]", session->username, md5_apop_he);

		result = auth_md5_validate(ci,session->username, md5_apop_he, session->apop_stamp);

		g_free(md5_apop_he);
		md5_apop_he = 0;

		switch (result) {
		case -1:
			session->SessionResult = 3;
			return -1;
		case 0:
			TRACE(TRACE_ERR, "user [%s] tried to login with wrong password", session->username);

			g_free(session->username);
			session->username = NULL;

			g_free(session->password);
			session->password = NULL;

			return pop3_error(session, "-ERR authentication attempt is invalid\r\n");

		default:
			return _pop3_session_authenticated(session, result);
		}
		return 1;

	case POP3_AUTH:
		if (session->state != CLIENTSTATE_INITIAL_CONNECT)
			return pop3_error(session, "-ERR wrong command mode\r\n");
		return pop3_error(session, "-ERR no AUTH mechanisms supported\r\n");

	case POP3_TOP:
		if (session->state != CLIENTSTATE_AUTHENTICATED)
			return pop3_error(session, "-ERR wrong command mode\r\n");

		/* find out how many lines they want */
		searchptr = strstr(value, " ");

		/* insufficient parameters */
		if (searchptr == NULL)
			return pop3_error(session, "-ERR your command does not compute\r\n");

		/* skip the space */
		searchptr = searchptr + 1;

		/* value should now be the the message that needs to be retrieved */
		value[searchptr - value - 1] = '\0';

		/* check if searchptr or value are negative. If so return an
		   error. This is done by only letting the strings contain
		   digits (0-9) */
		if (strspn(searchptr, "0123456789") != strlen(searchptr))
			return pop3_error(session, "-ERR wrong parameter\r\n");
		if (strspn(value, "0123456789") != strlen(value))
			return pop3_error(session, "-ERR wrong parameter\r\n");

		top_lines = strtoull(searchptr, NULL, 10);
		top_messageid = strtoull(value, NULL, 10);
		if (top_messageid < 1)
			return pop3_error(session, "-ERR wrong parameter\r\n");

		TRACE(TRACE_DEBUG, "TOP command (partially) retrieving message");

		session->messagelst = g_list_first(session->messagelst);

		/* selecting a message */
		TRACE(TRACE_DEBUG, "TOP command, selecting message");

		while (session->messagelst) {
			msg = (struct message *) session->messagelst->data;
			if (msg->messageid == top_messageid && msg->virtual_messagestatus < MESSAGE_STATUS_DELETE) {	/* message is not deleted */
				char *s = NULL; size_t i;
				if (! (s = db_get_message_lines(msg->realmessageid, top_lines)))
					return -1;
				i = ci_write(ci, "+OK %llu lines of message %llu\r\n%s", top_lines, top_messageid, s);
				ci_write(ci, "\r\n.\r\n");
				g_free(s);
				return i;
			}
			if (! g_list_next(session->messagelst))
				break;
			session->messagelst = g_list_next(session->messagelst);

		}
		return pop3_error(session, "-ERR no such message\r\n");

	case POP3_CAPA:
		ci_write(ci, "+OK Capability list follows\r\nTOP\r\nUSER\r\nUIDL%s\r\n.\r\n", server_conf->ssl?"\r\nSTLS":"");
		return 1;

	default:
		return pop3_error(session, "-ERR command not understood\r\n");
	
	}
	return 1;
}
