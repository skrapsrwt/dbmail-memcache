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
 * \file dbmail-message.h
 * 
 * \brief DbmailMessage class  
 *
 */

#ifndef _DBMAIL_MESSAGE_H
#define _DBMAIL_MESSAGE_H


#include "dbmail.h"

#define MSGBUF_FORCE_UPDATE -1

/* 
 * preliminary goal is to provide an interface between dbmail and gmime.
 *
 * ambition is facilitation of language-bindings.
 *
 * very much unfinished, work in progress, subject to change, etc...
 *
 */


/*
 * initializers
 */

DbmailMessage * dbmail_message_new(void);
DbmailMessage * dbmail_message_init_with_string(DbmailMessage *self, const GString *content);
DbmailMessage * dbmail_message_construct(DbmailMessage *self, 
		const gchar *sender, const gchar *recipient, 
		const gchar *subject, const gchar *body);

/*
 * database facilities
 */

int dbmail_message_store(DbmailMessage *message);
int dbmail_message_cache_headers(const DbmailMessage *message);
gboolean dm_message_store(DbmailMessage *m);

DbmailMessage * dbmail_message_retrieve(DbmailMessage *self, u64_t physid, int filter);

/*
 * attribute accessors
 */
void dbmail_message_set_physid(DbmailMessage *self, u64_t physid);
u64_t dbmail_message_get_physid(const DbmailMessage *self);

void dbmail_message_set_envelope_recipient(DbmailMessage *self, const char *envelope);
gchar * dbmail_message_get_envelope_recipient(const DbmailMessage *self);
	
void dbmail_message_set_internal_date(DbmailMessage *self, char *internal_date);
gchar * dbmail_message_get_internal_date(const DbmailMessage *self, int thisyear);
	
int dbmail_message_set_class(DbmailMessage *self, int klass);
int dbmail_message_get_class(const DbmailMessage *self);

gchar * dbmail_message_to_string(const DbmailMessage *self);
gchar * dbmail_message_hdrs_to_string(const DbmailMessage *self);
gchar * dbmail_message_body_to_string(const DbmailMessage *self);

char * dbmail_message_get_charset(DbmailMessage *self);

size_t dbmail_message_get_size(const DbmailMessage *self, gboolean crlf);

GList * dbmail_message_get_header_addresses(DbmailMessage *message, const char *field);


/*
 * manipulate the actual message content
 */

void dbmail_message_set_header(DbmailMessage *self, const char *header, const char *value);
const gchar * dbmail_message_get_header(const DbmailMessage *self, const char *header);

/* Get all instances of a header. */
GTuples * dbmail_message_get_header_repeated(const DbmailMessage *self, const char *header);

void dbmail_message_cache_referencesfield(const DbmailMessage *self);
void dbmail_message_cache_envelope(const DbmailMessage *self);

/*
 * destructor
 */

void dbmail_message_free(DbmailMessage *self);


/* move these elsewhere: */

unsigned find_end_of_header(const char *h);
char * g_mime_object_get_body(const GMimeObject *object);

// from sort.h
dsn_class_t sort_and_deliver(DbmailMessage *self,
		const char *destination, u64_t useridnr,
		const char *mailbox, mailbox_source_t source);

dsn_class_t sort_deliver_to_mailbox(DbmailMessage *message,
		u64_t useridnr, const char *mailbox, mailbox_source_t source,
		int *msgflags);

// from dm_pipe.h
//
// Either convert the message struct to a
// string, or send the database rows raw.
enum sendwhat {
	SENDMESSAGE     = 0,
	SENDRAW         = 1
};

// Use the system sendmail binary.
#define SENDMAIL        NULL


/**
 * \brief Inserts a message in the database.
 * \return 0
 */
int insert_messages(DbmailMessage *message, GList *dsnusers);
int send_mail(DbmailMessage *message,
		const char *to, const char *from,
		const char *preoutput,
		enum sendwhat sendwhat, char *sendmail_external);

int send_forward_list(DbmailMessage *message, GList *targets, const char *from);


#endif
