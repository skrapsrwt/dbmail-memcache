/*
 *
 Copyright (c) 2004-2011 NFG Net Facilities Group BV support@nfg.nl
 *
 */
	
#ifndef _DBMAIL_COMMANDCHANNEL_H
#define _DBMAIL_COMMANDCHANNEL_H

#include "dbmail.h"
#include "dm_cache.h"

// command state during idle command
#define IDLE -1 

typedef struct cmd_t *cmd_t;

/* ImapSession definition */
typedef struct {
	clientbase_t *ci;
	Capa_T preauth_capa;   // CAPABILITY
	Capa_T capa;   // CAPABILITY
	char *tag;
	char *command;
	int command_type;
	int command_state;

	gboolean use_uid;
	u64_t msg_idnr;  // replace this with a GList

	GString *buff; // output buffer

	int parser_state;
	char **args;
	u64_t args_idx;

	int loop; // idle loop counter
	fetch_items_t *fi;

	DbmailMailbox *mailbox;	/* currently selected mailbox */
	u64_t lo; // lower boundary for message ids
	u64_t hi; // upper boundary for message ids
	u64_t ceiling; // upper boundary during prefetching

	DbmailMessage *message;
	Cache_T cache;  

	u64_t userid;		/* userID of client in dbase */

	GTree *ids;
	GTree *physids;		// cache physmessage_ids for uids 
	GTree *envelopes;
	GTree *mbxinfo; 	// cache MailboxState_T 
	GList *ids_list;

	cmd_t cmd; // command structure (wip)
	gboolean error; // command result
	clientstate_t state; // session status 
	int error_count;
	Connection_T c; // database-connection;
} ImapSession;


typedef int (*IMAP_COMMAND_HANDLER) (ImapSession *);

/* thread data */
typedef struct {
	void (* cb_enter)(gpointer);		/* callback on thread entry		*/
	void (* cb_leave)(gpointer);		/* callback on thread exit		*/
	ImapSession *session;
	clientbase_t ci;
	gpointer data;				/* payload				*/
	int status;				/* command result 			*/
} dm_thread_data;

/* public methods */

ImapSession * dbmail_imap_session_new(void);
ImapSession * dbmail_imap_session_set_tag(ImapSession * self, char * tag);
ImapSession * dbmail_imap_session_set_command(ImapSession * self, char * command);

void dbmail_imap_session_reset(ImapSession *session);

void dbmail_imap_session_args_free(ImapSession *self, gboolean all);
void dbmail_imap_session_fetch_free(ImapSession *self);
void dbmail_imap_session_delete(ImapSession ** self);

void dbmail_imap_session_buff_clear(ImapSession *self);
void dbmail_imap_session_buff_flush(ImapSession *self);
int dbmail_imap_session_buff_printf(ImapSession * self, char * message, ...);

int dbmail_imap_session_set_state(ImapSession *self, clientstate_t state);
int client_is_authenticated(ImapSession * self);
int check_state_and_args(ImapSession * self, int minargs, int maxargs, clientstate_t state);
int dbmail_imap_session_handle_auth(ImapSession * self, const char * username, const char * password);
int dbmail_imap_session_prompt(ImapSession * self, char * prompt);

MailboxState_T dbmail_imap_session_mbxinfo_lookup(ImapSession *self, u64_t mailbox_idnr);

int dbmail_imap_session_mailbox_get_selectable(ImapSession * self, u64_t idnr);

int dbmail_imap_session_mailbox_status(ImapSession * self, gboolean update);
int dbmail_imap_session_mailbox_expunge(ImapSession *self);
int dbmail_imap_session_update_recent(ImapSession *self);

int dbmail_imap_session_fetch_get_items(ImapSession *self);
int dbmail_imap_session_fetch_parse_args(ImapSession * self);

void dbmail_imap_session_bodyfetch_new(ImapSession *self);
void dbmail_imap_session_bodyfetch_free(ImapSession *self);
body_fetch_t * dbmail_imap_session_bodyfetch_get_last(ImapSession *self);
void dbmail_imap_session_bodyfetch_rewind(ImapSession *self);

int dbmail_imap_session_bodyfetch_set_partspec(ImapSession *self, char *partspec, int length);
char *dbmail_imap_session_bodyfetch_get_last_partspec(ImapSession *self);

int dbmail_imap_session_bodyfetch_set_itemtype(ImapSession *self, int itemtype);
int dbmail_imap_session_bodyfetch_get_last_itemtype(ImapSession *self);

int dbmail_imap_session_bodyfetch_set_argstart(ImapSession *self);
int dbmail_imap_session_bodyfetch_get_last_argstart(ImapSession *self);

int dbmail_imap_session_bodyfetch_set_argcnt(ImapSession *self);
int dbmail_imap_session_bodyfetch_get_last_argcnt(ImapSession *self);

int dbmail_imap_session_bodyfetch_set_octetstart(ImapSession *self, guint64 octet);
guint64 dbmail_imap_session_bodyfetch_get_last_octetstart(ImapSession *self);

int dbmail_imap_session_bodyfetch_set_octetcnt(ImapSession *self, guint64 octet);
guint64 dbmail_imap_session_bodyfetch_get_last_octetcnt(ImapSession *self);

int imap4_tokenizer_main(ImapSession *self, const char *buffer);


/* threaded work queue */
	
/*
 * thread launcher
 *
 */
void dm_thread_data_push(gpointer session, gpointer cb_enter, gpointer cb_leave, gpointer data);

void dm_thread_data_sendmessage(gpointer data);
void _ic_cb_leave(gpointer data);

#endif

