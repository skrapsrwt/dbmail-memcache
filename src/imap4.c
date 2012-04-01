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
 * imap4.c
 *
 * implements an IMAP 4 rev 1 server.
 */

#include "dbmail.h"

#define THIS_MODULE "imap"

/* max number of BAD/NO responses */
#define MAX_FAULTY_RESPONSES 5

const char *IMAP_COMMANDS[] = {
	"", "capability", "noop", "logout",
	"authenticate", "login",
	"select", "examine", "create", "delete", "rename", "subscribe",
	"unsubscribe",
	"list", "lsub", "status", "append",
	"check", "close", "expunge", "search", "fetch", "store", "copy",
	"uid", "sort", "getquotaroot", "getquota",
	"setacl", "deleteacl", "getacl", "listrights", "myrights",
	"namespace","thread","unselect","idle","starttls", "id",
	"***NOMORE***"
};

extern int selfpipe[2];
extern serverConfig_t *server_conf;
extern GAsyncQueue *queue;

const char AcceptedTagChars[] =
    "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789"
    "!@#$%^&-=_`~\\|'\" ;:,.<>/? ";


const IMAP_COMMAND_HANDLER imap_handler_functions[] = {
	NULL,
	_ic_capability, _ic_noop, _ic_logout,
	_ic_authenticate, _ic_login,
	_ic_select, _ic_examine, _ic_create, _ic_delete, _ic_rename,
	_ic_subscribe, _ic_unsubscribe, _ic_list, _ic_lsub, _ic_status,
	_ic_append,
	_ic_check, _ic_close, _ic_expunge, _ic_search, _ic_fetch,
	_ic_store, _ic_copy, _ic_uid, _ic_sort,
	_ic_getquotaroot, _ic_getquota,
	_ic_setacl, _ic_deleteacl, _ic_getacl, _ic_listrights,
	_ic_myrights,
	_ic_namespace, _ic_thread, _ic_unselect, _ic_idle, _ic_starttls,
	_ic_id,
	NULL
};


/*
 */

static int imap4_tokenizer(ImapSession *, char *);
static int imap4(ImapSession *);
static void imap_handle_input(ImapSession *);

static void imap_session_cleanup_enter(dm_thread_data *D)
{
	ImapSession *session = D->session;

	if ((! session->state) || session->state == CLIENTSTATE_QUIT_QUEUED)
		return;

	dbmail_imap_session_set_state(session, CLIENTSTATE_QUIT_QUEUED);
	D->session->command_state = TRUE;
	g_async_queue_push(queue, (gpointer)D);
	if (selfpipe[1] > -1) {
		if (write(selfpipe[1], "Q", 1) != 1) { /* ignore */; }
	}
	return;
}

static void imap_session_cleanup_leave(dm_thread_data *D)
{
	ImapSession *session = D->session;
	TRACE(TRACE_DEBUG,"[%p] ci [%p] state [%d]", session, session->ci, session->state);

	if (session->state != CLIENTSTATE_QUIT_QUEUED)
		return;

	ci_close(session->ci);
	session->ci = NULL;
	dbmail_imap_session_delete(&session);

	// brute force:
	if (server_conf->no_daemonize == 1) _exit(0);

}

static void imap_session_bailout(ImapSession *session)
{
	TRACE(TRACE_DEBUG,"[%p] state [%d] ci[%p]", session, session->state, session->ci);

	assert(session && session->ci);

	ci_cork(session->ci);

	if (session->state != CLIENTSTATE_QUIT_QUEUED) {
		dm_thread_data_push((gpointer)session, imap_session_cleanup_enter, imap_session_cleanup_leave, NULL);
	}
}

void socket_write_cb(int fd UNUSED, short what UNUSED, void *arg)
{
	ImapSession *session = (ImapSession *)arg;
	switch(session->state) {
		case CLIENTSTATE_LOGOUT:
		case CLIENTSTATE_ERROR:
			imap_session_bailout(session);
			break;
		default:
			ci_write_cb(session->ci);
			break;
	}
}


void imap_cb_read(void *arg)
{
	ImapSession *session = (ImapSession *) arg;

	ci_read_cb(session->ci);

	size_t have = session->ci->read_buffer->len;
	size_t need = session->ci->rbuff_size;

	int enough = (need>0?(have == 0):(have > 0));

	int state = session->ci->client_state;

	TRACE(TRACE_DEBUG,"reading %d: %ld/%ld", enough, have, need);
	if (state & CLIENT_ERR) {
		ci_cork(session->ci);
		dbmail_imap_session_set_state(session,CLIENTSTATE_ERROR);
		return;
	} 
	if (state & CLIENT_EOF) {
		if (enough)
			imap_handle_input(session);
		else
			imap_session_bailout(session);
		return;
	} 
	if (have > 0)
		imap_handle_input(session);
}


void socket_read_cb(int fd UNUSED, short what, void *arg)
{
	ImapSession *session = (ImapSession *)arg;
	TRACE(TRACE_DEBUG,"[%p]", session);
	if (what == EV_READ)
		imap_cb_read(session);
	else if (what == EV_TIMEOUT && session->ci->cb_time)
		session->ci->cb_time(session);
	
}

/* 
 * only the main thread may write to the network event
 * worker threads must use an async queue
 */
static int imap_session_printf(ImapSession * self, char * message, ...)
{
        va_list ap, cp;
        size_t l;
	int e = 0;

        assert(message);
        va_start(ap, message);
	va_copy(cp, ap);
        g_string_vprintf(self->buff, message, cp);
        va_end(cp);

	if ((e = ci_write(self->ci, self->buff->str)) < 0) {
		TRACE(TRACE_DEBUG, "ci_write failed [%s]", strerror(e));
		dbmail_imap_session_set_state(self,CLIENTSTATE_ERROR);
		return e;
	}

        l = self->buff->len;
	self->buff = g_string_truncate(self->buff, 0);
	g_string_maybe_shrink(self->buff);

        return (int)l;
}

static void send_greeting(ImapSession *session)
{
	/* greet user */
	field_t banner;
	GETCONFIGVALUE("banner", "IMAP", banner);
	if (strlen(banner) > 0)
		imap_session_printf(session, "* OK [CAPABILITY %s] %s\r\n", Capa_as_string(session->preauth_capa), banner);
	else
		imap_session_printf(session, "* OK [CAPABILITY %s] dbmail %s ready.\r\n", Capa_as_string(session->preauth_capa), VERSION);
	dbmail_imap_session_set_state(session,CLIENTSTATE_NON_AUTHENTICATED);
}

/*
 * the default timeout callback */

void imap_cb_time(void *arg)
{
	ImapSession *session = (ImapSession *) arg;
	TRACE(TRACE_DEBUG,"[%p]", session);

	if ( session->command_type == IMAP_COMM_IDLE  && session->command_state == IDLE ) { // session is in a IDLE loop
		ci_cork(session->ci);
		if (! (session->loop++ % 10)) {
			imap_session_printf(session, "* OK\r\n");
		}
		dbmail_imap_session_mailbox_status(session,TRUE);
		dbmail_imap_session_buff_flush(session);
		ci_uncork(session->ci);
	} else {
		dbmail_imap_session_set_state(session,CLIENTSTATE_ERROR);
		imap_session_printf(session, "%s", IMAP_TIMEOUT_MSG);
		imap_session_bailout(session);
	}
}

static int checktag(const char *s)
{
	int i;
	for (i = 0; s[i]; i++) {
		if (!strchr(AcceptedTagChars, s[i])) return 0;
	}
	return 1;
}

static void imap_handle_exit(ImapSession *session, int status)
{
	if (! session) return;

	TRACE(TRACE_DEBUG, "[%p] state [%d] command_status [%d] [%s] returned with status [%d]", 
		session, session->state, session->command_state, session->command, status);

	switch(status) {
		case -1:
			dbmail_imap_session_set_state(session,CLIENTSTATE_ERROR);	/* fatal error occurred, kick this user */
			dbmail_imap_session_reset(session);
			imap_session_bailout(session);
			break;

		case 0:
			/* only do this in the main thread */
			if (session->state < CLIENTSTATE_LOGOUT) {
				if (session->buff) {
					int e = 0;
					if ((e = ci_write(session->ci, session->buff->str)) < 0) {
						TRACE(TRACE_DEBUG,"ci_write returned error [%s]", strerror(e));
						dbmail_imap_session_set_state(session,CLIENTSTATE_ERROR);
						return;
					}
					dbmail_imap_session_buff_clear(session);
				}
				if ((session->ci->write_buffer->len - session->ci->write_buffer_offset) > 0) {
					ci_write(session->ci, NULL);
				} else if (session->command_state == TRUE) {
					dbmail_imap_session_reset(session);
				}
			} else {
				dbmail_imap_session_buff_clear(session);
			}				

			// handle buffered pending input
			if (session->ci->read_buffer->len > 0)
				imap_handle_input(session);
		
			break;
		case 1:
			session->command_state = TRUE;
			dbmail_imap_session_buff_flush(session);
			session->error_count++;	/* server returned BAD or NO response */
			dbmail_imap_session_reset(session);
			break;

		case 2:
			/* only do this in the main thread */
			imap_session_printf(session, "* BYE\r\n");
			imap_session_printf(session, "%s OK LOGOUT completed\r\n", session->tag);
			break;

		case 3:
			/* returning from starttls */
			dbmail_imap_session_reset(session);
			break;

	}
}

void imap_handle_input(ImapSession *session)
{
	char buffer[MAX_LINESIZE];
	int l, result;

	assert(session);
	assert(session->ci);
	assert(session->ci->write_buffer);

	// first flush the output buffer
	if (session->ci->write_buffer->len) {
		TRACE(TRACE_DEBUG,"[%p] write buffer not empty", session);
		ci_write(session->ci, NULL);
		return;
	}

	// nothing left to handle
	if (session->ci->read_buffer->len == 0) {
		TRACE(TRACE_DEBUG,"[%p] read buffer empty", session);
		return;
	}

	// command in progress
	if (session->command_state == FALSE && session->parser_state == TRUE) {
		TRACE(TRACE_DEBUG,"[%p] command in-progress", session);
		return;
	}

	// reset if we're done with the previous command
	if (session->command_state == TRUE)
		dbmail_imap_session_reset(session);


	// Read in a line at a time if we don't have a string literal size defined
	// Otherwise read in rbuff_size amount of data
	while (TRUE) {

		memset(buffer, 0, sizeof(buffer));

		if (session->ci->rbuff_size <= 0) {
			l = ci_readln(session->ci, buffer);
		} else {
			int needed = MIN(session->ci->rbuff_size, (int)sizeof(buffer)-1);
			l = ci_read(session->ci, buffer, needed);
		}

		if (l == 0) break; // done

		if (session->error_count >= MAX_FAULTY_RESPONSES) {
			dbmail_imap_session_set_state(session,CLIENTSTATE_ERROR);
			imap_session_printf(session, "* BYE [TRY RFC]\r\n");
			break;
		}

		// session is in a IDLE loop
		if (session->command_type == IMAP_COMM_IDLE  && session->command_state == IDLE) { 
			if (strlen(buffer) > 4 && strncasecmp(buffer,"DONE",4)==0)
				imap_session_printf(session, "%s OK IDLE terminated\r\n", session->tag);
			else
				imap_session_printf(session,"%s BAD Expecting DONE\r\n", session->tag);

			session->command_state = TRUE; // done
			dbmail_imap_session_reset(session);

			continue;
		}

		if (! imap4_tokenizer(session, buffer))
			continue;

		if ( session->parser_state < 0 ) {
			imap_session_printf(session, "%s BAD parse error\r\n", session->tag);
			imap_handle_exit(session, 1);
			break;
		}

		if ( session->parser_state ) {
			result = imap4(session);
			TRACE(TRACE_DEBUG,"imap4 returned [%d]", result);

			if (result || (session->command_type == IMAP_COMM_IDLE  && session->command_state == IDLE)) { 
				imap_handle_exit(session, result);
			}
			break;
		}

		if (session->state == CLIENTSTATE_ERROR) {
			TRACE(TRACE_NOTICE, "session->state: ERROR. abort");
			break;
		}
	}

	return;
}
static void reset_callbacks(ImapSession *session)
{
	session->ci->cb_time = imap_cb_time;
	session->ci->timeout->tv_sec = server_conf->login_timeout;

	UNBLOCK(session->ci->rx);
	UNBLOCK(session->ci->tx);

	ci_uncork(session->ci);
}

int imap_handle_connection(client_sock *c)
{
	ImapSession *session;
	clientbase_t *ci;

	if (c)
		ci = client_init(c);
	else
		ci = client_init(NULL);

	session = dbmail_imap_session_new();

	dbmail_imap_session_set_state(session, CLIENTSTATE_NON_AUTHENTICATED);

	event_set(ci->rev, ci->rx, EV_READ|EV_PERSIST, socket_read_cb, (void *)session);
	event_set(ci->wev, ci->tx, EV_WRITE, socket_write_cb, (void *)session);

	session->ci = ci;

	if ((! server_conf->ssl) || (ci->ssl_state == TRUE)) 
		Capa_remove(session->capa, "STARTTLS");

	reset_callbacks(session);
	
	send_greeting(session);
	
	return EOF;
}

void dbmail_imap_session_reset(ImapSession *session)
{
	TRACE(TRACE_DEBUG,"[%p]", session);
	if (session->tag) {
		g_free(session->tag);
		session->tag = NULL;
	}

	if (session->command) {
		g_free(session->command);
		session->command = NULL;
	}

	session->use_uid = 0;
	session->command_type = 0;
	session->command_state = FALSE;
	session->parser_state = FALSE;
	dbmail_imap_session_args_free(session, FALSE);

	session->ci->timeout->tv_sec = server_conf->timeout; 
	ci_uncork(session->ci);
	
	return;
}

int imap4_tokenizer (ImapSession *session, char *buffer)
{
	char *tag = NULL, *cpy, *command;
	size_t i = 0;
		
	if (!(*buffer))
		return 0;

	/* read tag & command */
	cpy = buffer;

	/* fetch the tag and command */
	if (! session->tag) {

		if (strcmp(buffer,"\n")==0 || strcmp(buffer,"\r\n")==0)
			return 0;

		session->parser_state = 0;
		TRACE(TRACE_INFO, "[%p] COMMAND: [%s]", session, buffer);

		// tag
		i = stridx(cpy, ' ');	/* find next space */
		if (i == strlen(cpy)) {
			if (checktag(cpy))
				imap_session_printf(session, "%s BAD No command specified\r\n", cpy);
			else
				imap_session_printf(session, "* BAD Invalid tag specified\r\n");
			session->error_count++;
			session->command_state=TRUE;
			return 0;
		}

		tag = g_strndup(cpy,i);	/* set tag */
		dbmail_imap_session_set_tag(session,tag);
		g_free(tag);

		cpy[i] = '\0';
		cpy = cpy + i + 1;	/* cpy points to command now */

		if (!checktag(session->tag)) {
			imap_session_printf(session, "* BAD Invalid tag specified\r\n");
			session->error_count++;
			session->command_state=TRUE;
			return 0;
		}

		// command
		i = stridx(cpy, ' ');	/* find next space */

		command = g_strndup(cpy,i);	/* set command */
		strip_crlf(command);
		dbmail_imap_session_set_command(session,command);
		g_free(command);

		cpy = cpy + i;	/* cpy points to args now */

	}

	session->parser_state = imap4_tokenizer_main(session, cpy);	/* build argument array */

	if (session->parser_state)
		TRACE(TRACE_DEBUG,"parser_state: [%d]", session->parser_state);

	return session->parser_state;
}
	
void _ic_cb_leave(gpointer data)
{
	dm_thread_data *D = (dm_thread_data *)data;
	ImapSession *session = D->session;
	TRACE(TRACE_DEBUG,"handling imap session [%p]",session);

	ci_uncork(session->ci);
	imap_handle_exit(session, D->status);
}

static void imap_unescape_args(ImapSession *session)
{
	u64_t i = 0;
	assert(session->command_type);
	switch (session->command_type) {
		case IMAP_COMM_EXAMINE:
		case IMAP_COMM_SELECT:
		case IMAP_COMM_SEARCH:
		case IMAP_COMM_CREATE:
		case IMAP_COMM_DELETE:
		case IMAP_COMM_RENAME:
		case IMAP_COMM_SUBSCRIBE:
		case IMAP_COMM_UNSUBSCRIBE:
		case IMAP_COMM_STATUS:
		case IMAP_COMM_COPY:
		case IMAP_COMM_LOGIN:

		for (i = 0; session->args[i]; i++) { 
			imap_unescape(session->args[i]);
		}
		break;
		default:
		break;
	}
#if 1
	for (i = 0; session->args[i]; i++) { 
		TRACE(TRACE_DEBUG, "[%p] arg[%llu]: '%s'\n", session, i, session->args[i]); 
	}
#endif

}


int imap4(ImapSession *session)
{
	// 
	// the parser/tokenizer is satisfied we're ready reading from the client
	// so now it's time to act upon the read input
	//
	int j = 0;
	
	if (! dm_db_ping()) {
		dbmail_imap_session_set_state(session,CLIENTSTATE_ERROR);
		return DM_EQUERY;
	}

	if (session->command_state==TRUE) // done already
		return 0;

	session->command_state=TRUE; // set command-is-done-state while doing some checks
	if (! (session->tag && session->command)) {
		TRACE(TRACE_ERR,"no tag or command");
		return 1;
	}
	if (! session->args) {
		imap_session_printf(session, "%s BAD invalid argument specified\r\n",session->tag);
		session->error_count++;
		return 1;
	}

	/* lookup the command */
	for (j = IMAP_COMM_NONE; j < IMAP_COMM_LAST && strcasecmp(session->command, IMAP_COMMANDS[j]); j++);
	if (j <= IMAP_COMM_NONE || j >= IMAP_COMM_LAST) { /* unknown command */
		imap_session_printf(session, "%s BAD no valid command\r\n", session->tag);
		return 1;
	}

	session->error_count = 0;
	session->command_type = j;
	session->command_state=FALSE; // unset command-is-done-state while command in progress

	imap_unescape_args(session);

	TRACE(TRACE_INFO, "dispatch [%s]...\n", IMAP_COMMANDS[session->command_type]);
	return (*imap_handler_functions[session->command_type]) (session);
}
