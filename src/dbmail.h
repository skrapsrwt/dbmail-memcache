/*
 Copyright (C) 1999-2004 IC & S, dbmail@ic-s.nl
 Copyright (C) 2001-2007 Aaron Stone, aaron@serendipity.cx
 Copyright (C) 2004-2011 NFG Net Facilities Group BV, support@nfg.nl

 This program is free software; you can redistribute it and/or
 modify it under the terms of the GNU General Public License as
 published by the Free Software Foundation; either version 2 of
 the License, or (at your option) any later version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program; if not, write to the Free Software
 Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/

/**
 * \file dbmail.h
 * header file for a general configuration
 */

#ifndef _DBMAIL_H
#define _DBMAIL_H

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#define __EXTENSIONS__ /* solaris */

#if defined(__FreeBSD__) || defined(__APPLE__) || defined(__SUNPRO_C)

#define _XOPEN_SOURCE

#else

#define _XOPEN_SOURCE	500
#include <features.h>

#endif


#include <assert.h>
#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <fnmatch.h>
#include <gmime/gmime.h>
#include <glib.h>
#include <gmodule.h>
#include <grp.h>
#include <limits.h>
#include <netinet/in.h>
#include <netdb.h>
#include <pwd.h>
#include <signal.h>
#include <syslog.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sysexits.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/utsname.h>
#include <time.h>
#include <termios.h>
#include <unistd.h>
#include <mhash.h>
#include <sys/queue.h>
#include <evhttp.h>
#include <math.h>
#include <openssl/ssl.h>

#ifdef AUTHLDAP
#define LDAP_DEPRECATED 1
#include <ldap.h>
#endif

#include <URL.h>
#include <ResultSet.h>
#include <PreparedStatement.h>
#include <Connection.h>
#include <ConnectionPool.h>
#include <SQLException.h>
#include <libmemcached/memcached.h>
#include "dm_cram.h"
#include "dm_capa.h"
#include "dbmailtypes.h"
#include "dm_config.h"
#include "dm_list.h"
#include "dm_debug.h"
#include "dm_dsn.h"
#include "dm_acl.h"
#include "dm_misc.h"
#include "dm_quota.h"
#include "dm_tls.h"

#include "dbmail-user.h"
#include "dbmail-mailbox.h"
#include "dbmail-message.h"
#include "dbmail-imapsession.h"

#include "imapcommands.h"
#include "server.h"
#include "clientsession.h"
#include "clientbase.h"
#include "lmtp.h"

#include "dm_db.h"
#include "dm_sievescript.h"

#include "auth.h"
#include "authmodule.h"

#include "sort.h"
#include "sortmodule.h"

#include "dm_digest.h"
#include "dm_cidr.h"
#include "dm_iconv.h"
#include "dm_getopt.h"
#include "dm_match.h"
#include "dm_sset.h"
#include "dm_memcache.h"
#ifdef SIEVE
#include <sieve2.h>
#include <sieve2_error.h>
#endif

#ifdef HAVE_CRYPT_H
#include <crypt.h>
#endif

#ifdef HAVE_ENDIAN_H
#include <endian.h>
#endif
#define KEY_SIZE 20
#define GETCONFIGVALUE(key, sect, var) \
	config_get_value(key, sect, var); \
	if (strlen(var) > 0) \
		TRACE(TRACE_DEBUG, "key "#key" section "#sect" var "#var" value [%s]", var)
	/* No final ';' so macro can be called "like a function" */

#define CONFIG_ERROR_LEVEL TRACE_WARNING

#define PRINTF_THIS_IS_DBMAIL printf("This is DBMail version %s\n\n%s\n", VERSION, COPYRIGHT)

#define COPYRIGHT \
"Copyright (C) 1999-2004 IC & S, dbmail@ic-s.nl\n" \
"Copyright (C) 2001-2007 Aaron Stone, aaron@serendipity.cx\n" \
"Copyright (C) 2004-2011 NFG Net Facilities Group BV, support@nfg.nl\n" \
"\n" \
"Please see the AUTHORS and THANKS files for additional contributors.\n" \
"\n" \
"This program is free software; you can redistribute it and/or \n" \
"modify it under the terms of the GNU General Public License as\n" \
"published by the Free Software Foundation; either version 2 of\n" \
"the License, or (at your option) any later version.\n" \
"\n" \
"This program is distributed in the hope that it will be useful,\n" \
"but WITHOUT ANY WARRANTY; without even the implied warranty of\n" \
"MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the\n" \
"GNU General Public License for more details.\n" \
"\n" \
"You should have received a copy of the GNU General Public License\n" \
"along with this program; if not, write to the Free Software\n" \
"Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.\n" \
""

/* include sql/sqlite/create_tables.sqlite for autocreation */
#define DM_SQLITECREATE "-- Copyright (C) 2005 Internet Connection, Inc.\n" \
"-- Copyright (C) 2006-2010 NFG Net Facilities Group BV.\n" \
"--\n" \
"-- This program is free software; you can redistribute it and/or \n" \
"-- modify it under the terms of the GNU General Public License \n" \
"-- as published by the Free Software Foundation; either \n" \
"-- version 2 of the License, or (at your option) any later \n" \
"-- version.\n" \
"--\n" \
"-- This program is distributed in the hope that it will be useful,\n" \
"-- but WITHOUT ANY WARRANTY; without even the implied warranty of\n" \
"-- MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the\n" \
"-- GNU General Public License for more details.\n" \
"--\n" \
"-- You should have received a copy of the GNU General Public License\n" \
"-- along with this program; if not, write to the Free Software\n" \
"-- Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.\n" \
"-- \n" \
"\n" \
"BEGIN TRANSACTION;\n" \
"\n" \
"PRAGMA auto_vacuum = 1;\n" \
"\n" \
"CREATE TABLE dbmail_aliases (\n" \
"   alias_idnr INTEGER PRIMARY KEY,\n" \
"   alias TEXT NOT NULL,\n" \
"   deliver_to TEXT NOT NULL,\n" \
"   client_idnr INTEGER DEFAULT '0' NOT NULL\n" \
");\n" \
"CREATE INDEX dbmail_aliases_index_1 ON dbmail_aliases(alias);\n" \
"CREATE INDEX dbmail_aliases_index_2 ON dbmail_aliases(client_idnr);\n" \
"\n" \
"CREATE TABLE dbmail_authlog (\n" \
"  id INTEGER PRIMARY KEY,\n" \
"  userid TEXT,\n" \
"  service TEXT,\n" \
"  login_time DATETIME,\n" \
"  logout_time DATETIME,\n" \
"  src_ip TEXT,\n" \
"  src_port INTEGER,\n" \
"  dst_ip TEXT,\n" \
"  dst_port INTEGER,\n" \
"  status TEXT DEFAULT 'active',\n" \
"  bytes_rx INTEGER DEFAULT '0' NOT NULL,\n" \
"  bytes_tx INTEGER DEFAULT '0' NOT NULL\n" \
");\n" \
"\n" \
"CREATE TABLE dbmail_users (\n" \
"   user_idnr INTEGER PRIMARY KEY,\n" \
"   userid TEXT NOT NULL,\n" \
"   passwd TEXT NOT NULL,\n" \
"   client_idnr INTEGER DEFAULT '0' NOT NULL,\n" \
"   maxmail_size INTEGER DEFAULT '0' NOT NULL,\n" \
"   curmail_size INTEGER DEFAULT '0' NOT NULL,\n" \
"   encryption_type TEXT DEFAULT '' NOT NULL,\n" \
"   last_login DATETIME DEFAULT '1979-11-03 22:05:58' NOT NULL\n" \
");\n" \
"CREATE UNIQUE INDEX dbmail_users_1 ON dbmail_users(userid);\n" \
"\n" \
"CREATE TABLE dbmail_mailboxes (\n" \
"   mailbox_idnr INTEGER PRIMARY KEY,\n" \
"   owner_idnr INTEGER DEFAULT '0' NOT NULL,\n" \
"   name TEXT BINARY NOT NULL,\n" \
"   seq INTEGER DEFAULT '0' NOT NULL,\n" \
"   seen_flag BOOLEAN default '0' not null,\n" \
"   answered_flag BOOLEAN default '0' not null,\n" \
"   deleted_flag BOOLEAN default '0' not null,\n" \
"   flagged_flag BOOLEAN default '0' not null,\n" \
"   recent_flag BOOLEAN default '0' not null,\n" \
"   draft_flag BOOLEAN default '0' not null,\n" \
"   no_inferiors BOOLEAN default '0' not null,\n" \
"   no_select BOOLEAN default '0' not null,\n" \
"   permission BOOLEAN default '2'\n" \
");\n" \
"CREATE INDEX dbmail_mailboxes_1 ON dbmail_mailboxes(name);\n" \
"CREATE INDEX dbmail_mailboxes_2 ON dbmail_mailboxes(owner_idnr);\n" \
"CREATE UNIQUE INDEX dbmail_mailboxes_3 ON dbmail_mailboxes(owner_idnr,name);\n" \
"CREATE INDEX dbmail_mailbox_4 ON dbmail_mailboxes(seq);\n" \
"\n" \
"CREATE TRIGGER fk_insert_mailboxes_users_idnr\n" \
"	BEFORE INSERT ON dbmail_mailboxes\n" \
"	FOR EACH ROW BEGIN\n" \
"		SELECT CASE \n" \
"			WHEN (new.owner_idnr IS NOT NULL)\n" \
"				AND ((SELECT user_idnr FROM dbmail_users WHERE user_idnr = new.owner_idnr) IS NULL)\n" \
"			THEN RAISE (ABORT, 'insert on table \"dbmail_mailboxes\" violates foreign key constraint \"fk_insert_mailboxes_users_idnr\"')\n" \
"		END;\n" \
"	END;\n" \
"CREATE TRIGGER fk_update1_mailboxes_users_idnr\n" \
"	BEFORE UPDATE ON dbmail_mailboxes\n" \
"	FOR EACH ROW BEGIN\n" \
"		SELECT CASE \n" \
"			WHEN (new.owner_idnr IS NOT NULL)\n" \
"				AND ((SELECT user_idnr FROM dbmail_users WHERE user_idnr = new.owner_idnr) IS NULL)\n" \
"			THEN RAISE (ABORT, 'update on table \"dbmail_mailboxes\" violates foreign key constraint \"fk_update2_mailboxes_users_idnr\"')\n" \
"		END;\n" \
"	END;\n" \
"CREATE TRIGGER fk_update2_mailboxes_users_idnr\n" \
"	AFTER UPDATE ON dbmail_users\n" \
"	FOR EACH ROW BEGIN\n" \
"		UPDATE dbmail_mailboxes SET owner_idnr = new.user_idnr WHERE owner_idnr = OLD.user_idnr;\n" \
"	END;\n" \
"CREATE TRIGGER fk_delete_mailboxes_users_idnr\n" \
"	BEFORE DELETE ON dbmail_users\n" \
"	FOR EACH ROW BEGIN\n" \
"		DELETE FROM dbmail_mailboxes WHERE owner_idnr = OLD.user_idnr;\n" \
"	END;\n" \
"\n" \
"\n" \
"CREATE TABLE dbmail_subscription (\n" \
"	user_id INTEGER NOT NULL,\n" \
"	mailbox_id INTEGER NOT NULL\n" \
");\n" \
"CREATE UNIQUE INDEX dbmail_subscriptioin_1 ON dbmail_subscription(user_id, mailbox_id);\n" \
"\n" \
"CREATE TRIGGER fk_insert_subscription_users_idnr\n" \
"	BEFORE INSERT ON dbmail_subscription\n" \
"	FOR EACH ROW BEGIN\n" \
"		SELECT CASE \n" \
"			WHEN (new.user_id IS NOT NULL)\n" \
"				AND ((SELECT user_idnr FROM dbmail_users WHERE user_idnr = new.user_id) IS NULL)\n" \
"			THEN RAISE (ABORT, 'insert on table \"dbmail_subscription\" violates foreign key constraint \"fk_insert_subscription_users_idnr\"')\n" \
"		END;\n" \
"	END;\n" \
"CREATE TRIGGER fk_update1_subscription_users_idnr\n" \
"	BEFORE UPDATE ON dbmail_subscription\n" \
"	FOR EACH ROW BEGIN\n" \
"		SELECT CASE \n" \
"			WHEN (new.user_id IS NOT NULL)\n" \
"				AND ((SELECT user_idnr FROM dbmail_users WHERE user_idnr = new.user_id) IS NULL)\n" \
"			THEN RAISE (ABORT, 'update on table \"dbmail_subscription\" violates foreign key constraint \"fk_update1_subscription_users_idnr\"')\n" \
"		END;\n" \
"	END;\n" \
"CREATE TRIGGER fk_update2_subscription_users_idnr\n" \
"	AFTER UPDATE ON dbmail_users\n" \
"	FOR EACH ROW BEGIN\n" \
"		UPDATE dbmail_subscription SET user_id = new.user_idnr WHERE user_id = OLD.user_idnr;\n" \
"	END;\n" \
"CREATE TRIGGER fk_delete_subscription_users_idnr\n" \
"	BEFORE DELETE ON dbmail_users\n" \
"	FOR EACH ROW BEGIN\n" \
"		DELETE FROM dbmail_subscription WHERE user_id = OLD.user_idnr;\n" \
"	END;\n" \
"\n" \
"CREATE TRIGGER fk_insert_subscription_mailbox_id\n" \
"	BEFORE INSERT ON dbmail_subscription\n" \
"	FOR EACH ROW BEGIN\n" \
"		SELECT CASE \n" \
"			WHEN (new.mailbox_id IS NOT NULL)\n" \
"				AND ((SELECT mailbox_idnr FROM dbmail_mailboxes WHERE mailbox_idnr = new.mailbox_id) IS NULL)\n" \
"			THEN RAISE (ABORT, 'insert on table \"dbmail_subscription\" violates foreign key constraint \"fk_insert_subscription_mailbox_id\"')\n" \
"		END;\n" \
"	END;\n" \
"CREATE TRIGGER fk_update1_subscription_mailbox_id\n" \
"	BEFORE UPDATE ON dbmail_subscription\n" \
"	FOR EACH ROW BEGIN\n" \
"		SELECT CASE \n" \
"			WHEN (new.mailbox_id IS NOT NULL)\n" \
"				AND ((SELECT mailbox_idnr FROM dbmail_mailboxes WHERE mailbox_idnr = new.mailbox_id) IS NULL)\n" \
"			THEN RAISE (ABORT, 'update on table \"dbmail_subscription\" violates foreign key constraint \"fk_update1_subscription_mailbox_id\"')\n" \
"		END;\n" \
"	END;\n" \
"CREATE TRIGGER fk_update2_subscription_mailbox_id\n" \
"	AFTER UPDATE ON dbmail_mailboxes\n" \
"	FOR EACH ROW BEGIN\n" \
"		UPDATE dbmail_subscription SET mailbox_id = new.mailbox_idnr WHERE mailbox_id = OLD.mailbox_idnr;\n" \
"	END;\n" \
"CREATE TRIGGER fk_delete_subscription_mailbox_id\n" \
"	BEFORE DELETE ON dbmail_mailboxes\n" \
"	FOR EACH ROW BEGIN\n" \
"		DELETE FROM dbmail_subscription WHERE mailbox_id = OLD.mailbox_idnr;\n" \
"	END;\n" \
"\n" \
"\n" \
"CREATE TABLE dbmail_acl (\n" \
"	user_id INTEGER NOT NULL,\n" \
"	mailbox_id INTEGER NOT NULL,\n" \
"	lookup_flag BOOLEAN default '0' not null,\n" \
"	read_flag BOOLEAN default '0' not null,\n" \
"	seen_flag BOOLEAN default '0' not null,\n" \
"	write_flag BOOLEAN default '0' not null,\n" \
"	insert_flag BOOLEAN default '0' not null,	\n" \
"	post_flag BOOLEAN default '0' not null,\n" \
"	create_flag BOOLEAN default '0' not null,	\n" \
"	delete_flag BOOLEAN default '0' not null,	\n" \
"	deleted_flag BOOLEAN default '0' not null,	\n" \
"	expunge_flag BOOLEAN default '0' not null,	\n" \
"	administer_flag BOOLEAN default '0' not null\n" \
");\n" \
"CREATE INDEX dbmail_acl_1 ON dbmail_acl(user_id);\n" \
"CREATE INDEX dbmail_acl_2 ON dbmail_acl(mailbox_id);\n" \
"CREATE UNIQUE INDEX dbmail_acl_3 ON dbmail_acl(user_id, mailbox_id);\n" \
"\n" \
"CREATE TRIGGER fk_insert_acl_user_id\n" \
"	BEFORE INSERT ON dbmail_acl\n" \
"	FOR EACH ROW BEGIN\n" \
"		SELECT CASE \n" \
"			WHEN (new.user_id IS NOT NULL)\n" \
"				AND ((SELECT user_idnr FROM dbmail_users WHERE user_idnr = new.user_id) IS NULL)\n" \
"			THEN RAISE (ABORT, 'insert on table \"dbmail_acl\" violates foreign key constraint \"fk_insert_acl_user_id\"')\n" \
"		END;\n" \
"	END;\n" \
"CREATE TRIGGER fk_update1_acl_user_id\n" \
"	BEFORE UPDATE ON dbmail_acl\n" \
"	FOR EACH ROW BEGIN\n" \
"		SELECT CASE \n" \
"			WHEN (new.user_id IS NOT NULL)\n" \
"				AND ((SELECT user_idnr FROM dbmail_users WHERE user_idnr = new.user_id) IS NULL)\n" \
"			THEN RAISE (ABORT, 'update on table \"dbmail_acl\" violates foreign key constraint \"fk_update1_acl_user_id\"')\n" \
"		END;\n" \
"	END;\n" \
"CREATE TRIGGER fk_update2_acl_user_id\n" \
"	AFTER UPDATE ON dbmail_users\n" \
"	FOR EACH ROW BEGIN\n" \
"		UPDATE dbmail_acl SET user_id = new.user_idnr WHERE user_id = OLD.user_idnr;\n" \
"	END;\n" \
"CREATE TRIGGER fk_delete_acl_user_id\n" \
"	BEFORE DELETE ON dbmail_users\n" \
"	FOR EACH ROW BEGIN\n" \
"		DELETE FROM dbmail_acl WHERE user_id = OLD.user_idnr;\n" \
"	END;\n" \
"\n" \
"CREATE TRIGGER fk_insert_acl_mailbox_id\n" \
"	BEFORE INSERT ON dbmail_acl\n" \
"	FOR EACH ROW BEGIN\n" \
"		SELECT CASE \n" \
"			WHEN (new.mailbox_id IS NOT NULL)\n" \
"				AND ((SELECT mailbox_idnr FROM dbmail_mailboxes WHERE mailbox_idnr = new.mailbox_id) IS NULL)\n" \
"			THEN RAISE (ABORT, 'insert on table \"dbmail_acl\" violates foreign key constraint \"fk_insert_acl_mailbox_id\"')\n" \
"		END;\n" \
"	END;\n" \
"CREATE TRIGGER fk_update1_acl_mailbox_id\n" \
"	BEFORE UPDATE ON dbmail_acl\n" \
"	FOR EACH ROW BEGIN\n" \
"		SELECT CASE \n" \
"			WHEN (new.mailbox_id IS NOT NULL)\n" \
"				AND ((SELECT mailbox_idnr FROM dbmail_mailboxes WHERE mailbox_idnr = new.mailbox_id) IS NULL)\n" \
"			THEN RAISE (ABORT, 'update on table \"dbmail_acl\" violates foreign key constraint \"fk_update1_acl_mailbox_id\"')\n" \
"		END;\n" \
"	END;\n" \
"CREATE TRIGGER fk_update2_acl_mailbox_id\n" \
"	AFTER UPDATE ON dbmail_mailboxes\n" \
"	FOR EACH ROW BEGIN\n" \
"		UPDATE dbmail_acl SET mailbox_id = new.mailbox_idnr WHERE mailbox_id = OLD.mailbox_idnr;\n" \
"	END;\n" \
"CREATE TRIGGER fk_delete_acl_mailbox_id\n" \
"	BEFORE DELETE ON dbmail_mailboxes\n" \
"	FOR EACH ROW BEGIN\n" \
"		DELETE FROM dbmail_acl WHERE mailbox_id = OLD.mailbox_idnr;\n" \
"	END;\n" \
"\n" \
"\n" \
"\n" \
"CREATE TABLE dbmail_physmessage (\n" \
"   id INTEGER PRIMARY KEY,\n" \
"   messagesize INTEGER DEFAULT '0' NOT NULL,\n" \
"   rfcsize INTEGER DEFAULT '0' NOT NULL,\n" \
"   internal_date DATETIME default '0' not null\n" \
");\n" \
" \n" \
"CREATE TABLE dbmail_messages (\n" \
"   message_idnr INTEGER PRIMARY KEY,\n" \
"   mailbox_idnr INTEGER DEFAULT '0' NOT NULL,\n" \
"   physmessage_id INTEGER DEFAULT '0' NOT NULL,\n" \
"   seen_flag BOOLEAN default '0' not null,\n" \
"   answered_flag BOOLEAN default '0' not null,\n" \
"   deleted_flag BOOLEAN default '0' not null,\n" \
"   flagged_flag BOOLEAN default '0' not null,\n" \
"   recent_flag BOOLEAN default '0' not null,\n" \
"   draft_flag BOOLEAN default '0' not null,\n" \
"   unique_id TEXT NOT NULL,\n" \
"   status BOOLEAN unsigned default '0' not null\n" \
");\n" \
"CREATE INDEX dbmail_messages_1 ON dbmail_messages(mailbox_idnr);\n" \
"CREATE INDEX dbmail_messages_2 ON dbmail_messages(physmessage_id);\n" \
"CREATE INDEX dbmail_messages_3 ON dbmail_messages(seen_flag);\n" \
"CREATE INDEX dbmail_messages_4 ON dbmail_messages(unique_id);\n" \
"CREATE INDEX dbmail_messages_5 ON dbmail_messages(status);\n" \
"CREATE INDEX dbmail_messages_6 ON dbmail_messages(mailbox_idnr,status);\n" \
"CREATE INDEX dbmail_messages_7 ON dbmail_messages(mailbox_idnr,status,seen_flag);\n" \
"CREATE INDEX dbmail_messages_8 ON dbmail_messages(mailbox_idnr,status,recent_flag);\n" \
"\n" \
"CREATE TRIGGER fk_insert_messages_physmessage_id\n" \
"	BEFORE INSERT ON dbmail_messages\n" \
"	FOR EACH ROW BEGIN\n" \
"		SELECT CASE \n" \
"			WHEN (new.physmessage_id IS NOT NULL)\n" \
"				AND ((SELECT id FROM dbmail_physmessage WHERE id = new.physmessage_id) IS NULL)\n" \
"			THEN RAISE (ABORT, 'insert on table \"dbmail_messages\" violates foreign key constraint \"fk_insert_messages_physmessage_id\"')\n" \
"		END;\n" \
"	END;\n" \
"CREATE TRIGGER fk_update1_messages_physmessage_id\n" \
"	BEFORE UPDATE ON dbmail_messages\n" \
"	FOR EACH ROW BEGIN\n" \
"		SELECT CASE \n" \
"			WHEN (new.physmessage_id IS NOT NULL)\n" \
"				AND ((SELECT id FROM dbmail_physmessage WHERE id = new.physmessage_id) IS NULL)\n" \
"			THEN RAISE (ABORT, 'update on table \"dbmail_messages\" violates foreign key constraint \"fk_update1_messages_physmessage_id\"')\n" \
"		END;\n" \
"	END;\n" \
"CREATE TRIGGER fk_update2_messages_physmessage_id\n" \
"	AFTER UPDATE ON dbmail_physmessage\n" \
"	FOR EACH ROW BEGIN\n" \
"		UPDATE dbmail_messages SET physmessage_id = new.id WHERE physmessage_id = OLD.id;\n" \
"	END;\n" \
"CREATE TRIGGER fk_delete_message_physmessage_id\n" \
"	BEFORE DELETE ON dbmail_physmessage\n" \
"	FOR EACH ROW BEGIN\n" \
"		DELETE FROM dbmail_messages WHERE physmessage_id = OLD.id;\n" \
"	END;\n" \
"\n" \
"\n" \
"CREATE TRIGGER fk_insert_messages_mailbox_idnr\n" \
"	BEFORE INSERT ON dbmail_messages\n" \
"	FOR EACH ROW BEGIN\n" \
"		SELECT CASE \n" \
"			WHEN (new.mailbox_idnr IS NOT NULL)\n" \
"				AND ((SELECT mailbox_idnr FROM dbmail_mailboxes WHERE mailbox_idnr = new.mailbox_idnr) IS NULL)\n" \
"			THEN RAISE (ABORT, 'insert on table \"dbmail_messages\" violates foreign key constraint \"fk_insert_messages_mailbox_idnr\"')\n" \
"		END;\n" \
"	END;\n" \
"CREATE TRIGGER fk_update1_messages_mailbox_idnr\n" \
"	BEFORE UPDATE ON dbmail_messages\n" \
"	FOR EACH ROW BEGIN\n" \
"		SELECT CASE \n" \
"			WHEN (new.mailbox_idnr IS NOT NULL)\n" \
"				AND ((SELECT mailbox_idnr FROM dbmail_mailboxes WHERE mailbox_idnr = new.mailbox_idnr) IS NULL)\n" \
"			THEN RAISE (ABORT, 'update on table \"dbmail_messages\" violates foreign key constraint \"fk_update1_messages_mailbox_idnr\"')\n" \
"		END;\n" \
"	END;\n" \
"CREATE TRIGGER fk_update2_messages_mailbox_idnr\n" \
"	AFTER UPDATE ON dbmail_mailboxes\n" \
"	FOR EACH ROW BEGIN\n" \
"		UPDATE dbmail_messages SET mailbox_idnr = new.mailbox_idnr WHERE mailbox_idnr = OLD.mailbox_idnr;\n" \
"	END;\n" \
"CREATE TRIGGER fk_delete_messages_mailbox_idnr\n" \
"	BEFORE DELETE ON dbmail_mailboxes\n" \
"	FOR EACH ROW BEGIN\n" \
"		DELETE FROM dbmail_messages WHERE mailbox_idnr = OLD.mailbox_idnr;\n" \
"	END;\n" \
"\n" \
"\n" \
"   \n" \
"CREATE TABLE dbmail_messageblks (\n" \
"   messageblk_idnr INTEGER PRIMARY KEY,\n" \
"   physmessage_id INTEGER DEFAULT '0' NOT NULL,\n" \
"   messageblk TEXT NOT NULL,\n" \
"   blocksize INTEGER DEFAULT '0' NOT NULL,\n" \
"   is_header BOOLEAN DEFAULT '0' NOT NULL\n" \
");\n" \
"CREATE INDEX dbmail_messageblks_1 ON dbmail_messageblks(physmessage_id);\n" \
"CREATE INDEX dbmail_messageblks_2 ON dbmail_messageblks(physmessage_id, is_header);\n" \
"\n" \
"CREATE TRIGGER fk_insert_messageblks_physmessage_id\n" \
"	BEFORE INSERT ON dbmail_messageblks\n" \
"	FOR EACH ROW BEGIN\n" \
"		SELECT CASE \n" \
"			WHEN (new.physmessage_id IS NOT NULL)\n" \
"				AND ((SELECT id FROM dbmail_physmessage WHERE id = new.physmessage_id) IS NULL)\n" \
"			THEN RAISE (ABORT, 'insert on table \"dbmail_messageblks\" violates foreign key constraint \"fk_insert_messageblks_physmessage_id\"')\n" \
"		END;\n" \
"	END;\n" \
"CREATE TRIGGER fk_update1_messageblks_physmessage_id\n" \
"	BEFORE UPDATE ON dbmail_messageblks\n" \
"	FOR EACH ROW BEGIN\n" \
"		SELECT CASE \n" \
"			WHEN (new.physmessage_id IS NOT NULL)\n" \
"				AND ((SELECT id FROM dbmail_physmessage WHERE id = new.physmessage_id) IS NULL)\n" \
"			THEN RAISE (ABORT, 'update on table \"dbmail_messageblks\" violates foreign key constraint \"fk_update1_messageblks_physmessage_id\"')\n" \
"		END;\n" \
"	END;\n" \
"CREATE TRIGGER fk_update2_messageblks_physmessage_id\n" \
"	AFTER UPDATE ON dbmail_physmessage\n" \
"	FOR EACH ROW BEGIN\n" \
"		UPDATE dbmail_messageblks SET physmessage_id = new.id WHERE physmessage_id = OLD.id;\n" \
"	END;\n" \
"CREATE TRIGGER fk_delete_messageblks_physmessage_id\n" \
"	BEFORE DELETE ON dbmail_physmessage\n" \
"	FOR EACH ROW BEGIN\n" \
"		DELETE FROM dbmail_messageblks WHERE physmessage_id = OLD.id;\n" \
"	END;\n" \
"\n" \
" \n" \
"CREATE TABLE dbmail_auto_replies (\n" \
"   user_idnr INTEGER PRIMARY KEY,\n" \
"   reply_body TEXT,  \n" \
"   start_date DATETIME DEFAULT '1980-01-01 22:05:58' NOT NULL,\n" \
"   stop_date DATETIME DEFAULT '1980-01-01 22:05:58' NOT NULL\n" \
");                   \n" \
"CREATE TRIGGER fk_insert_auto_replies_user_idnr\n" \
"        BEFORE INSERT ON dbmail_auto_replies\n" \
"        FOR EACH ROW BEGIN\n" \
"                SELECT CASE \n" \
"                        WHEN (new.user_idnr IS NOT NULL)\n" \
"                                AND ((SELECT user_idnr FROM dbmail_users WHERE user_idnr = new.user_idnr) IS NULL)\n" \
"                        THEN RAISE (ABORT, 'insert on table \"dbmail_auto_replies\" violates foreign key constraint \"fk_insert_auto_replies_user_idnr\"')\n" \
"                END; \n" \
"        END;         \n" \
"CREATE TRIGGER fk_update1_auto_replies_user_idnr\n" \
"        BEFORE UPDATE ON dbmail_auto_replies\n" \
"        FOR EACH ROW BEGIN\n" \
"                SELECT CASE \n" \
"                        WHEN (new.user_idnr IS NOT NULL)\n" \
"                                AND ((SELECT user_idnr FROM dbmail_users WHERE user_idnr = new.user_idnr) IS NULL)\n" \
"                        THEN RAISE (ABORT, 'update on table \"dbmail_auto_replies\" violates foreign key constraint \"fk_update1_auto_replies_user_idnr\"')\n" \
"                END; \n" \
"        END;         \n" \
"CREATE TRIGGER fk_update2_auto_replies_user_idnr\n" \
"        AFTER UPDATE ON dbmail_users\n" \
"        FOR EACH ROW BEGIN\n" \
"                UPDATE dbmail_auto_replies SET user_idnr = new.user_idnr WHERE user_idnr = OLD.user_idnr;\n" \
"        END;         \n" \
"CREATE TRIGGER fk_delete_auto_replies_user_idnr\n" \
"        BEFORE DELETE ON dbmail_users\n" \
"        FOR EACH ROW BEGIN\n" \
"                DELETE FROM dbmail_auto_replies WHERE user_idnr = OLD.user_idnr;\n" \
"        END;         \n" \
"                     \n" \
"CREATE TABLE dbmail_auto_notifications (\n" \
"   user_idnr INTEGER PRIMARY KEY,\n" \
"   notify_address TEXT  \n" \
");                   \n" \
"\n" \
"CREATE TRIGGER fk_insert_auto_notifications_user_idnr\n" \
"        BEFORE INSERT ON dbmail_auto_notifications\n" \
"        FOR EACH ROW BEGIN\n" \
"                SELECT CASE \n" \
"                        WHEN (new.user_idnr IS NOT NULL)\n" \
"                                AND ((SELECT user_idnr FROM dbmail_users WHERE user_idnr = new.user_idnr) IS NULL)\n" \
"                        THEN RAISE (ABORT, 'insert on table \"dbmail_auto_notifications\" violates foreign key constraint \"fk_insert_auto_notifications_user_idnr\"')\n" \
"                END; \n" \
"        END;         \n" \
"CREATE TRIGGER fk_update1_auto_notifications_user_idnr\n" \
"        BEFORE UPDATE ON dbmail_auto_notifications\n" \
"        FOR EACH ROW BEGIN\n" \
"                SELECT CASE \n" \
"                        WHEN (new.user_idnr IS NOT NULL)\n" \
"                                AND ((SELECT user_idnr FROM dbmail_users WHERE user_idnr = new.user_idnr) IS NULL)\n" \
"                        THEN RAISE (ABORT, 'update on table \"dbmail_auto_notifications\" violates foreign key constraint \"fk_update1_auto_notifications_user_idnr\"')\n" \
"                END; \n" \
"        END;         \n" \
"CREATE TRIGGER fk_update2_auto_notifications_user_idnr\n" \
"        AFTER UPDATE ON dbmail_users\n" \
"        FOR EACH ROW BEGIN\n" \
"                UPDATE dbmail_auto_notifications SET user_idnr = new.user_idnr WHERE user_idnr = OLD.user_idnr;\n" \
"        END;         \n" \
"CREATE TRIGGER fk_delete_auto_notifications_user_idnr\n" \
"        BEFORE DELETE ON dbmail_users\n" \
"        FOR EACH ROW BEGIN\n" \
"                DELETE FROM dbmail_auto_notifications WHERE user_idnr = OLD.user_idnr;\n" \
"        END;         \n" \
"                     \n" \
"\n" \
"\n" \
"\n" \
"\n" \
"CREATE TABLE dbmail_pbsp (\n" \
"   idnr INTEGER PRIMARY KEY,\n" \
"   since DATETIME default '0' not null,\n" \
"   ipnumber TEXT NOT NULL\n" \
");\n" \
"CREATE UNIQUE INDEX dbmail_pbsp_1 ON dbmail_pbsp(ipnumber);\n" \
"CREATE INDEX dbmail_pbsp_2 ON dbmail_pbsp(since);\n" \
"\n" \
"CREATE TABLE dbmail_sievescripts (\n" \
"  owner_idnr INTEGER DEFAULT '0' NOT NULL,\n" \
"  name TEXT NOT NULL,\n" \
"  script TEXT,\n" \
"  active BOOLEAN default '0' not null\n" \
");\n" \
"CREATE INDEX dbmail_sievescripts_1 ON dbmail_sievescripts(name);\n" \
"CREATE INDEX dbmail_sievescripts_2 ON dbmail_sievescripts(owner_idnr);\n" \
"CREATE UNIQUE INDEX dbmail_sievescripts_3 ON dbmail_sievescripts(owner_idnr,name);\n" \
"\n" \
"CREATE TRIGGER fk_insert_sievescripts_owner_idnr\n" \
"	BEFORE INSERT ON dbmail_sievescripts\n" \
"	FOR EACH ROW BEGIN\n" \
"		SELECT CASE \n" \
"			WHEN (new.owner_idnr IS NOT NULL)\n" \
"				AND ((SELECT user_idnr FROM dbmail_users WHERE user_idnr = new.owner_idnr) IS NULL)\n" \
"			THEN RAISE (ABORT, 'insert on table \"dbmail_sievescripts\" violates foreign key constraint \"fk_insert_sievescripts_owner_idnr\"')\n" \
"		END;\n" \
"	END;\n" \
"CREATE TRIGGER fk_update1_sievescripts_owner_idnr\n" \
"	BEFORE UPDATE ON dbmail_sievescripts\n" \
"	FOR EACH ROW BEGIN\n" \
"		SELECT CASE \n" \
"			WHEN (new.owner_idnr IS NOT NULL)\n" \
"				AND ((SELECT user_idnr FROM dbmail_users WHERE user_idnr = new.owner_idnr) IS NULL)\n" \
"			THEN RAISE (ABORT, 'update on table \"dbmail_sievescripts\" violates foreign key constraint \"fk_update1_sievescripts_owner_idnr\"')\n" \
"		END;\n" \
"	END;\n" \
"CREATE TRIGGER fk_update2_sievescripts_owner_idnr\n" \
"	AFTER UPDATE ON dbmail_users\n" \
"	FOR EACH ROW BEGIN\n" \
"		UPDATE dbmail_sievescripts SET owner_idnr = new.user_idnr WHERE owner_idnr = OLD.user_idnr;\n" \
"	END;\n" \
"CREATE TRIGGER fk_delete_sievescripts_owner_idnr\n" \
"	BEFORE DELETE ON dbmail_users\n" \
"	FOR EACH ROW BEGIN\n" \
"		DELETE FROM dbmail_sievescripts WHERE owner_idnr = OLD.user_idnr;\n" \
"	END;\n" \
"\n" \
"\n" \
"--\n" \
"-- store all headers by storing all headernames and headervalues in separate\n" \
"-- tables.\n" \
"--\n" \
"\n" \
"CREATE TABLE dbmail_headername (\n" \
"	id		INTEGER PRIMARY KEY,\n" \
"	headername	TEXT NOT NULL DEFAULT ''\n" \
");\n" \
"\n" \
"CREATE UNIQUE INDEX dbmail_headername_1 on dbmail_headername (headername);\n" \
"\n" \
"CREATE TABLE dbmail_headervalue (\n" \
" 	id		INTEGER NOT NULL PRIMARY KEY,\n" \
"	hash 		TEXT NOT NULL,\n" \
"        headervalue   	BLOB NOT NULL,\n" \
"	sortfield	TEXT NOT NULL,\n" \
"	datefield	DATETIME\n" \
");\n" \
"CREATE INDEX dbmail_headervalue_1 ON dbmail_headervalue(hash);\n" \
"CREATE INDEX dbmail_headervalue_2 ON dbmail_headervalue(sortfield);\n" \
"CREATE INDEX dbmail_headervalue_3 ON dbmail_headervalue(datefield);\n" \
"\n" \
"CREATE TABLE dbmail_header (\n" \
"        physmessage_id      INTEGER NOT NULL,\n" \
"	headername_id       INTEGER NOT NULL,\n" \
"        headervalue_id      INTEGER NOT NULL\n" \
");\n" \
"\n" \
"CREATE UNIQUE INDEX dbmail_header_1 ON dbmail_header(physmessage_id,headername_id,headervalue_id);\n" \
"\n" \
"CREATE TRIGGER fk_insert_header_physmessage_id\n" \
"	BEFORE INSERT ON dbmail_header\n" \
"	FOR EACH ROW BEGIN\n" \
"		SELECT CASE \n" \
"			WHEN (new.physmessage_id IS NOT NULL)\n" \
"				AND ((SELECT id FROM dbmail_physmessage WHERE id = new.physmessage_id) IS NULL)\n" \
"			THEN RAISE (ABORT, 'insert on table \"dbmail_header\" violates foreign key constraint \"fk_insert_header_physmessage_id\"')\n" \
"		END;\n" \
"	END;\n" \
"\n" \
"CREATE TRIGGER fk_update_header_physmessage_id\n" \
"	BEFORE UPDATE ON dbmail_header\n" \
"	FOR EACH ROW BEGIN\n" \
"		SELECT CASE \n" \
"			WHEN (new.physmessage_id IS NOT NULL)\n" \
"				AND ((SELECT id FROM dbmail_physmessage WHERE id = new.physmessage_id) IS NULL)\n" \
"			THEN RAISE (ABORT, 'update on table \"dbmail_header\" violates foreign key constraint \"fk_update_header_physmessage_id\"')\n" \
"		END;\n" \
"	END;\n" \
"\n" \
"CREATE TRIGGER fk_update2_header_physmessage_id\n" \
"	AFTER UPDATE ON dbmail_physmessage\n" \
"	FOR EACH ROW BEGIN\n" \
"		UPDATE dbmail_header SET physmessage_id = new.id WHERE physmessage_id = OLD.id;\n" \
"	END;\n" \
"\n" \
"CREATE TRIGGER fk_delete_header_physmessage_id\n" \
"	BEFORE DELETE ON dbmail_physmessage\n" \
"	FOR EACH ROW BEGIN\n" \
"		DELETE FROM dbmail_header WHERE physmessage_id = OLD.id;\n" \
"	END;\n" \
"\n" \
"CREATE TRIGGER fk_insert_header_headername_id\n" \
"	BEFORE INSERT ON dbmail_header\n" \
"	FOR EACH ROW BEGIN\n" \
"		SELECT CASE \n" \
"			WHEN (new.headername_id IS NOT NULL)\n" \
"				AND ((SELECT id FROM dbmail_headername WHERE id = new.headername_id) IS NULL)\n" \
"			THEN RAISE (ABORT, 'insert on table \"dbmail_header\" violates foreign key constraint \"fk_insert_header_headername_id\"')\n" \
"		END;\n" \
"	END;\n" \
"\n" \
"CREATE TRIGGER fk_update_header_headername_id\n" \
"	BEFORE UPDATE ON dbmail_header\n" \
"	FOR EACH ROW BEGIN\n" \
"		SELECT CASE \n" \
"			WHEN (new.headername_id IS NOT NULL)\n" \
"				AND ((SELECT id FROM dbmail_headername WHERE id = new.headername_id) IS NULL)\n" \
"			THEN RAISE (ABORT, 'update on table \"dbmail_header\" violates foreign key constraint \"fk_update_header_headername_id\"')\n" \
"		END;\n" \
"	END;\n" \
"\n" \
"CREATE TRIGGER fk_update2_header_headername_id\n" \
"	AFTER UPDATE ON dbmail_headername\n" \
"	FOR EACH ROW BEGIN\n" \
"		UPDATE dbmail_header SET headername_id = new.id WHERE headername_id = OLD.id;\n" \
"	END;\n" \
"\n" \
"CREATE TRIGGER fk_delete_header_headername_id\n" \
"	BEFORE DELETE ON dbmail_headername\n" \
"	FOR EACH ROW BEGIN\n" \
"		DELETE FROM dbmail_header WHERE headername_id = OLD.id;\n" \
"	END;\n" \
"\n" \
"CREATE TRIGGER fk_insert_header_headervalue_id\n" \
"	BEFORE INSERT ON dbmail_header\n" \
"	FOR EACH ROW BEGIN\n" \
"		SELECT CASE \n" \
"			WHEN (new.headervalue_id IS NOT NULL)\n" \
"				AND ((SELECT id FROM dbmail_headervalue WHERE id = new.headervalue_id) IS NULL)\n" \
"			THEN RAISE (ABORT, 'insert on table \"dbmail_header\" violates foreign key constraint \"fk_insert_header_headervalue_id\"')\n" \
"		END;\n" \
"	END;\n" \
"\n" \
"CREATE TRIGGER fk_update_header_headervalue_id\n" \
"	BEFORE UPDATE ON dbmail_header\n" \
"	FOR EACH ROW BEGIN\n" \
"		SELECT CASE \n" \
"			WHEN (new.headervalue_id IS NOT NULL)\n" \
"				AND ((SELECT id FROM dbmail_headervalue WHERE id = new.headervalue_id) IS NULL)\n" \
"			THEN RAISE (ABORT, 'update on table \"dbmail_header\" violates foreign key constraint \"fk_update_header_headervalue_id\"')\n" \
"		END;\n" \
"	END;\n" \
"\n" \
"CREATE TRIGGER fk_update2_header_headervalue_id\n" \
"	AFTER UPDATE ON dbmail_headervalue\n" \
"	FOR EACH ROW BEGIN\n" \
"		UPDATE dbmail_header SET headervalue_id = new.id WHERE headervalue_id = OLD.id;\n" \
"	END;\n" \
"\n" \
"CREATE TRIGGER fk_delete_header_headervalue_id\n" \
"	BEFORE DELETE ON dbmail_headervalue\n" \
"	FOR EACH ROW BEGIN\n" \
"		DELETE FROM dbmail_header WHERE headervalue_id = OLD.id;\n" \
"	END;\n" \
"\n" \
"\n" \
"\n" \
"-- Threading\n" \
"\n" \
"-- support fast threading by breaking out In-Reply-To/References headers\n" \
"-- these fields contain zero or more Message-Id values that determine the message\n" \
"-- threading\n" \
"\n" \
"CREATE TABLE dbmail_referencesfield (\n" \
"        physmessage_id  INTEGER NOT NULL,\n" \
"	id		INTEGER NOT NULL PRIMARY KEY,\n" \
"	referencesfield	TEXT NOT NULL DEFAULT ''\n" \
");\n" \
"\n" \
"CREATE UNIQUE INDEX dbmail_referencesfield_1 on dbmail_referencesfield (physmessage_id, referencesfield);\n" \
"--	FOREIGN KEY (physmessage_id)\n" \
"--			REFERENCES dbmail_physmessage(id)\n" \
"--			ON UPDATE CASCADE ON DELETE CASCADE\n" \
"\n" \
"CREATE TRIGGER fk_insert_referencesfield_physmessage_id\n" \
"	BEFORE INSERT ON dbmail_referencesfield\n" \
"	FOR EACH ROW BEGIN\n" \
"		SELECT CASE \n" \
"			WHEN (new.physmessage_id IS NOT NULL)\n" \
"				AND ((SELECT id FROM dbmail_physmessage WHERE id = new.physmessage_id) IS NULL)\n" \
"			THEN RAISE (ABORT, 'insert on table \"dbmail_referencesfield\" violates foreign key constraint \"fk_insert_referencesfield_physmessage_id\"')\n" \
"		END;\n" \
"	END;\n" \
"CREATE TRIGGER fk_update1_referencesfield_physmessage_id\n" \
"	BEFORE UPDATE ON dbmail_referencesfield\n" \
"	FOR EACH ROW BEGIN\n" \
"		SELECT CASE \n" \
"			WHEN (new.physmessage_id IS NOT NULL)\n" \
"				AND ((SELECT id FROM dbmail_physmessage WHERE id = new.physmessage_id) IS NULL)\n" \
"			THEN RAISE (ABORT, 'update on table \"dbmail_referencesfield\" violates foreign key constraint \"fk_update1_referencesfield_physmessage_id\"')\n" \
"		END;\n" \
"	END;\n" \
"CREATE TRIGGER fk_update2_referencesfield_physmessage_id\n" \
"	AFTER UPDATE ON dbmail_physmessage\n" \
"	FOR EACH ROW BEGIN\n" \
"		UPDATE dbmail_referencesfield SET physmessage_id = new.id WHERE physmessage_id = OLD.id;\n" \
"	END;\n" \
"CREATE TRIGGER fk_delete_referencesfield_physmessage_id\n" \
"	BEFORE DELETE ON dbmail_physmessage\n" \
"	FOR EACH ROW BEGIN\n" \
"		DELETE FROM dbmail_referencesfield WHERE physmessage_id = OLD.id;\n" \
"	END;\n" \
"\n" \
"-- Table structure for table `dbmail_replycache`\n" \
"\n" \
"CREATE TABLE dbmail_replycache (\n" \
"  to_addr TEXT NOT NULL default '',\n" \
"  from_addr TEXT NOT NULL default '',\n" \
"  handle TEXT NOT NULL default '',\n" \
"  lastseen datetime NOT NULL default '0000-00-00 00:00:00'\n" \
");\n" \
"\n" \
"CREATE UNIQUE INDEX dbmail_replycache_1 on dbmail_replycache (to_addr,from_addr, handle);\n" \
"\n" \
"--\n" \
"-- Add tables and columns to hold Sieve scripts.\n" \
"\n" \
"\n" \
"CREATE TABLE dbmail_usermap (\n" \
"  login TEXT NOT NULL,\n" \
"  sock_allow TEXT NOT NULL,\n" \
"  sock_deny TEXT NOT NULL,\n" \
"  userid TEXT NOT NULL\n" \
");\n" \
"\n" \
"CREATE UNIQUE INDEX usermap_idx_1 ON dbmail_usermap(login, sock_allow, userid);\n" \
"\n" \
"\n" \
"\n" \
"\n" \
"-- Create the user for the delivery chain\n" \
"INSERT INTO dbmail_users (userid, passwd, encryption_type) \n" \
"	VALUES ('__@!internal_delivery_user!@__', '', 'md5');\n" \
"-- Create the 'anyone' user which is used for ACLs.\n" \
"INSERT INTO dbmail_users (userid, passwd, encryption_type) \n" \
"	VALUES ('anyone', '', 'md5');\n" \
"-- Create the user to own #Public mailboxes\n" \
"INSERT INTO dbmail_users (userid, passwd, encryption_type) \n" \
"	VALUES ('__public__', '', 'md5');\n" \
"\n" \
"COMMIT;\n" \
"\n" \
"\n" \
"-- support faster FETCH commands by caching ENVELOPE information\n" \
"\n" \
"CREATE TABLE dbmail_envelope (\n" \
"        physmessage_id  INTEGER NOT NULL,\n" \
"	id		INTEGER NOT NULL PRIMARY KEY,\n" \
"	envelope	TEXT NOT NULL DEFAULT ''\n" \
");\n" \
"\n" \
"CREATE UNIQUE INDEX dbmail_envelope_1 on dbmail_envelope (physmessage_id);\n" \
"CREATE UNIQUE INDEX dbmail_envelope_2 on dbmail_envelope (physmessage_id, id);\n" \
"\n" \
"CREATE TRIGGER fk_insert_envelope_physmessage_id\n" \
"	BEFORE INSERT ON dbmail_envelope\n" \
"	FOR EACH ROW BEGIN\n" \
"		SELECT CASE \n" \
"			WHEN (new.physmessage_id IS NOT NULL)\n" \
"				AND ((SELECT id FROM dbmail_physmessage WHERE id = new.physmessage_id) IS NULL)\n" \
"			THEN RAISE (ABORT, 'insert on table \"dbmail_envelope\" violates foreign key constraint \"fk_insert_envelope_physmessage_id\"')\n" \
"		END;\n" \
"	END;\n" \
"CREATE TRIGGER fk_update1_envelope_physmessage_id\n" \
"	BEFORE UPDATE ON dbmail_envelope\n" \
"	FOR EACH ROW BEGIN\n" \
"		SELECT CASE \n" \
"			WHEN (new.physmessage_id IS NOT NULL)\n" \
"				AND ((SELECT id FROM dbmail_physmessage WHERE id = new.physmessage_id) IS NULL)\n" \
"			THEN RAISE (ABORT, 'update on table \"dbmail_envelope\" violates foreign key constraint \"fk_update1_envelope_physmessage_id\"')\n" \
"		END;\n" \
"	END;\n" \
"CREATE TRIGGER fk_update2_envelope_physmessage_id\n" \
"	AFTER UPDATE ON dbmail_physmessage\n" \
"	FOR EACH ROW BEGIN\n" \
"		UPDATE dbmail_envelope SET physmessage_id = new.id WHERE physmessage_id = OLD.id;\n" \
"	END;\n" \
"CREATE TRIGGER fk_delete_envelope_physmessage_id\n" \
"	BEFORE DELETE ON dbmail_physmessage\n" \
"	FOR EACH ROW BEGIN\n" \
"		DELETE FROM dbmail_envelope WHERE physmessage_id = OLD.id;\n" \
"	END;\n" \
"\n" \
"\n" \
"\n" \
"--\n" \
"-- 2.3.x additions\n" \
"--\n" \
"\n" \
"DROP TABLE IF EXISTS dbmail_mimeparts;\n" \
"CREATE TABLE dbmail_mimeparts (\n" \
"	id	INTEGER NOT NULL PRIMARY KEY,\n" \
"	hash	TEXT NOT NULL,\n" \
"	data	BLOB NOT NULL,\n" \
"	size	INTEGER NOT NULL\n" \
");\n" \
"\n" \
"CREATE INDEX dbmail_mimeparts_1 ON dbmail_mimeparts(hash);\n" \
"\n" \
"DROP TABLE IF EXISTS dbmail_partlists;\n" \
"CREATE TABLE dbmail_partlists (\n" \
"	physmessage_id	INTEGER NOT NULL,\n" \
"   	is_header 	BOOLEAN DEFAULT '0' NOT NULL,\n" \
"	part_key	INTEGER DEFAULT '0' NOT NULL,\n" \
"	part_depth	INTEGER DEFAULT '0' NOT NULL,\n" \
"	part_order	INTEGER DEFAULT '0' NOT NULL,\n" \
"	part_id		INTEGER NOT NULL\n" \
");\n" \
"\n" \
"CREATE INDEX dbmail_partlists_1 ON dbmail_partlists(physmessage_id);\n" \
"CREATE INDEX dbmail_partlists_2 ON dbmail_partlists(part_id);\n" \
"CREATE UNIQUE INDEX message_parts ON dbmail_partlists(physmessage_id, part_key, part_depth, part_order);\n" \
"\n" \
"-- ALTER TABLE ONLY dbmail_partlists\n" \
"--    ADD CONSTRAINT dbmail_partlists_part_id_fkey FOREIGN KEY (part_id) REFERENCES dbmail_mimeparts(id) ON UPDATE CASCADE ON DELETE CASCADE;\n" \
"\n" \
"DROP TRIGGER IF EXISTS fk_insert_partlists_mimeparts_id;\n" \
"CREATE TRIGGER fk_insert_partlists_mimeparts_id\n" \
"	BEFORE INSERT ON dbmail_partlists\n" \
"	FOR EACH ROW BEGIN\n" \
"		SELECT CASE \n" \
"			WHEN (new.part_id IS NOT NULL)\n" \
"				AND ((SELECT id FROM dbmail_mimeparts WHERE id = new.part_id) IS NULL)\n" \
"			THEN RAISE (ABORT, 'insert on table \"dbmail_partlists\" violates foreign key constraint \"fk_insert_partlists_mimeparts_id\"')\n" \
"		END;\n" \
"	END;\n" \
"DROP TRIGGER IF EXISTS fk_update_partlists_mimeparts_id;\n" \
"CREATE TRIGGER fk_update_partlists_mimeparts_id\n" \
"	BEFORE UPDATE ON dbmail_partlists\n" \
"	FOR EACH ROW BEGIN\n" \
"		SELECT CASE \n" \
"			WHEN (new.part_id IS NOT NULL)\n" \
"				AND ((SELECT id FROM dbmail_mimeparts WHERE id = new.part_id) IS NULL)\n" \
"			THEN RAISE (ABORT, 'update on table \"dbmail_partlists\" violates foreign key constraint \"fk_update_partlists_mimeparts_id\"')\n" \
"		END;\n" \
"	END;\n" \
"DROP TRIGGER IF EXISTS fk_update2_partlists_mimeparts_id;\n" \
"CREATE TRIGGER fk_update2_partlists_mimeparts_id\n" \
"	AFTER UPDATE ON dbmail_mimeparts\n" \
"	FOR EACH ROW BEGIN\n" \
"		UPDATE dbmail_partlists SET part_id = new.id WHERE part_id = OLD.id;\n" \
"	END;\n" \
"DROP TRIGGER IF EXISTS fk_delete_partlists_mimeparts_id;\n" \
"CREATE TRIGGER fk_delete_partlists_mimeparts_id\n" \
"	BEFORE DELETE ON dbmail_mimeparts\n" \
"	FOR EACH ROW BEGIN\n" \
"		DELETE FROM dbmail_partlists WHERE part_id = OLD.id;\n" \
"	END;\n" \
"\n" \
"-- ALTER TABLE ONLY dbmail_partlists\n" \
"--    ADD CONSTRAINT dbmail_partlists_physmessage_id_fkey FOREIGN KEY (physmessage_id) REFERENCES dbmail_physmessage(id) ON UPDATE CASCADE ON DELETE CASCADE;\n" \
"\n" \
"DROP TRIGGER IF EXISTS fk_insert_partlists_physmessage_id;\n" \
"CREATE TRIGGER fk_insert_partlists_physmessage_id\n" \
"	BEFORE INSERT ON dbmail_partlists\n" \
"	FOR EACH ROW BEGIN\n" \
"		SELECT CASE \n" \
"			WHEN (new.physmessage_id IS NOT NULL)\n" \
"				AND ((SELECT id FROM dbmail_physmessage WHERE id = new.physmessage_id) IS NULL)\n" \
"			THEN RAISE (ABORT, 'insert on table \"dbmail_partlists\" violates foreign key constraint \"fk_insert_partlists_physmessage_id\"')\n" \
"		END;\n" \
"	END;\n" \
"DROP TRIGGER IF EXISTS fk_update_partlists_physmessage_id;\n" \
"CREATE TRIGGER fk_update_partlists_physmessage_id\n" \
"	BEFORE UPDATE ON dbmail_partlists\n" \
"	FOR EACH ROW BEGIN\n" \
"		SELECT CASE \n" \
"			WHEN (new.physmessage_id IS NOT NULL)\n" \
"				AND ((SELECT id FROM dbmail_physmessage WHERE id = new.physmessage_id) IS NULL)\n" \
"			THEN RAISE (ABORT, 'update on table \"dbmail_partlists\" violates foreign key constraint \"fk_update_partlists_physmessage_id\"')\n" \
"		END;\n" \
"	END;\n" \
"DROP TRIGGER IF EXISTS fk_update2_partlists_physmessage_id;\n" \
"CREATE TRIGGER fk_update2_partlists_physmessage_id\n" \
"	AFTER UPDATE ON dbmail_physmessage\n" \
"	FOR EACH ROW BEGIN\n" \
"		UPDATE dbmail_partlists SET physmessage_id = new.id WHERE physmessage_id = OLD.id;\n" \
"	END;\n" \
"DROP TRIGGER IF EXISTS fk_delete_partlists_physmessage_id;\n" \
"CREATE TRIGGER fk_delete_partlists_physmessage_id\n" \
"	BEFORE DELETE ON dbmail_physmessage\n" \
"	FOR EACH ROW BEGIN\n" \
"		DELETE FROM dbmail_partlists WHERE physmessage_id = OLD.id;\n" \
"	END;\n" \
"\n" \
"CREATE TABLE dbmail_keywords (\n" \
"	keyword		TEXT NOT NULL,\n" \
"	message_idnr	INT NOT NULL\n" \
");\n" \
"CREATE UNIQUE INDEX dbmail_keywords_1 ON dbmail_keywords(keyword,message_idnr);\n" \
"\n" \
"DROP TRIGGER IF EXISTS fk_insert_dbmail_keywords_dbmail_messages_message_idnr;\n" \
"CREATE TRIGGER fk_insert_dbmail_keywords_dbmail_messages_message_idnr\n" \
"	BEFORE INSERT ON dbmail_keywords\n" \
"	FOR EACH ROW BEGIN\n" \
"		SELECT CASE \n" \
"			WHEN (new.message_idnr IS NOT NULL)\n" \
"				AND ((SELECT message_idnr FROM dbmail_messages WHERE message_idnr = new.message_idnr) IS NULL)\n" \
"			THEN RAISE (ABORT, 'insert on table \"dbmail_keywords\" violates foreign key constraint \"fk_insert_dbmail_keywords_dbmail_messages_message_idnr\"')\n" \
"		END;\n" \
"	END;\n" \
"\n" \
"DROP TRIGGER IF EXISTS fk_update_dbmail_keywords_dbmail_messages_message_idnr;\n" \
"CREATE TRIGGER fk_update_dbmail_keywords_dbmail_messages_message_idnr\n" \
"	BEFORE UPDATE ON dbmail_keywords\n" \
"	FOR EACH ROW BEGIN\n" \
"		SELECT CASE \n" \
"			WHEN (new.message_idnr IS NOT NULL)\n" \
"				AND ((SELECT message_idnr FROM dbmail_messages WHERE message_idnr = new.message_idnr) IS NULL)\n" \
"			THEN RAISE (ABORT, 'update on table \"dbmail_keywords\" violates foreign key constraint \"fk_update_dbmail_keywords_dbmail_messages_message_idnr\"')\n" \
"		END;\n" \
"	END;\n" \
"\n" \
"DROP TRIGGER IF EXISTS fk_update2_dbmail_keywords_dbmail_messages_message_idnr;\n" \
"CREATE TRIGGER fk_update2_dbmail_keywords_dbmail_messages_message_idnr\n" \
"	AFTER UPDATE ON dbmail_messages\n" \
"	FOR EACH ROW BEGIN\n" \
"		UPDATE dbmail_keywords SET message_idnr = new.message_idnr WHERE message_idnr = OLD.message_idnr;\n" \
"	END;\n" \
"\n" \
"DROP TRIGGER IF EXISTS fk_delete_dbmail_keywords_dbmail_messages_message_idnr;\n" \
"CREATE TRIGGER fk_delete_dbmail_keywords_dbmail_messages_message_idnr\n" \
"	BEFORE DELETE ON dbmail_messages\n" \
"	FOR EACH ROW BEGIN\n" \
"		DELETE FROM dbmail_keywords WHERE message_idnr = OLD.message_idnr;\n" \
"	END;\n" \
"\n" \
"\n" \
"DROP TABLE IF EXISTS dbmail_filters;\n" \
"CREATE TABLE dbmail_filters (\n" \
"	id           INTEGER PRIMARY KEY,\n" \
"	user_id      INTEGER NOT NULL,\n" \
"	headername   TEXT NOT NULL,\n" \
"	headervalue  TEXT NOT NULL,	\n" \
"	mailbox      TEXT NOT NULL\n" \
");\n" \
"\n" \
"CREATE UNIQUE INDEX dbmail_filters_index_1 ON dbmail_filters(user_id, id);\n" \
"CREATE TRIGGER fk_insert_filters_users_user_idnr\n" \
"	BEFORE INSERT ON dbmail_filters\n" \
"	FOR EACH ROW BEGIN\n" \
"		SELECT CASE \n" \
"			WHEN (new.user_id IS NOT NULL)\n" \
"				AND ((SELECT user_idnr FROM dbmail_users WHERE user_idnr = new.user_id) IS NULL)\n" \
"			THEN RAISE (ABORT, 'insert on table \"dbmail_filters\" violates foreign key constraint \"fk_insert_filters_users_user_idnr\"')\n" \
"		END;\n" \
"	END;\n" \
"\n" \
"CREATE TRIGGER fk_update_filters_users_user_idnr\n" \
"	BEFORE UPDATE ON dbmail_filters\n" \
"	FOR EACH ROW BEGIN\n" \
"		SELECT CASE \n" \
"			WHEN (new.user_id IS NOT NULL)\n" \
"				AND ((SELECT user_idnr FROM dbmail_users WHERE user_idnr = new.user_id) IS NULL)\n" \
"			THEN RAISE (ABORT, 'update on table \"dbmail_filters\" violates foreign key constraint \"fk_update_filters_users_user_idnr\"')\n" \
"		END;\n" \
"	END;\n" \
"\n" \
"CREATE TRIGGER fk_update2_filters_users_user_idnr\n" \
"	AFTER UPDATE ON dbmail_users\n" \
"	FOR EACH ROW BEGIN\n" \
"		UPDATE dbmail_filters SET user_id = new.user_idnr WHERE user_id = OLD.user_idnr;\n" \
"	END;\n" \
"\n" \
"CREATE TRIGGER fk_delete_filters_users_user_idnr\n" \
"	BEFORE DELETE ON dbmail_users\n" \
"	FOR EACH ROW BEGIN\n" \
"		DELETE FROM dbmail_filters WHERE user_id = OLD.user_idnr;\n" \
"	END;\n" \
"\n" \
"CREATE VIEW dbmail_fromfield AS\n" \
"        SELECT physmessage_id,sortfield AS fromfield\n" \
"        FROM dbmail_messages m\n" \
"        JOIN dbmail_header h USING (physmessage_id)\n" \
"        JOIN dbmail_headername n ON h.headername_id = n.id\n" \
"        JOIN dbmail_headervalue v ON h.headervalue_id = v.id\n" \
"WHERE n.headername='from';\n" \
"\n" \
"CREATE VIEW dbmail_ccfield AS\n" \
"        SELECT physmessage_id,sortfield AS ccfield\n" \
"        FROM dbmail_messages m\n" \
"        JOIN dbmail_header h USING (physmessage_id)\n" \
"        JOIN dbmail_headername n ON h.headername_id = n.id\n" \
"        JOIN dbmail_headervalue v ON h.headervalue_id = v.id\n" \
"WHERE n.headername='cc';\n" \
"\n" \
"CREATE VIEW dbmail_tofield AS\n" \
"        SELECT physmessage_id,sortfield AS tofield\n" \
"        FROM dbmail_messages m\n" \
"        JOIN dbmail_header h USING (physmessage_id)\n" \
"        JOIN dbmail_headername n ON h.headername_id = n.id\n" \
"        JOIN dbmail_headervalue v ON h.headervalue_id = v.id\n" \
"WHERE n.headername='to';\n" \
"\n" \
"CREATE VIEW dbmail_subjectfield AS\n" \
"        SELECT physmessage_id,headervalue AS subjectfield\n" \
"        FROM dbmail_messages m\n" \
"        JOIN dbmail_header h USING (physmessage_id)\n" \
"        JOIN dbmail_headername n ON h.headername_id = n.id\n" \
"        JOIN dbmail_headervalue v ON h.headervalue_id = v.id\n" \
"WHERE n.headername='subject';\n" \
"\n" \
"CREATE VIEW dbmail_datefield AS\n" \
"        SELECT physmessage_id,datefield\n" \
"        FROM dbmail_messages m\n" \
"        JOIN dbmail_header h USING (physmessage_id)\n" \
"        JOIN dbmail_headername n ON h.headername_id = n.id\n" \
"        JOIN dbmail_headervalue v ON h.headervalue_id = v.id\n" \
"WHERE n.headername='date';\n" \
"\n" \
"\n" \
"\n"

/** default directory and extension for pidfiles */
#define DEFAULT_PID_DIR "/var/run"
#define DEFAULT_PID_EXT ".pid"
#define DEFAULT_STATE_DIR "/var/run"
#define DEFAULT_STATE_EXT ".state"

/** default location of library files */
#define DEFAULT_LIBRARY_DIR ""

/** default configuration file */
#define DEFAULT_CONFIG_FILE "/etc/dbmail.conf"

/** default log files */
#define DEFAULT_LOG_FILE "/var/log/dbmail.log"
#define DEFAULT_ERROR_LOG "/var/log/dbmail.err"

#define IMAP_CAPABILITY_STRING "IMAP4rev1 AUTH=LOGIN AUTH=CRAM-MD5 ACL RIGHTS=texk NAMESPACE CHILDREN SORT QUOTA THREAD=ORDEREDSUBJECT UNSELECT IDLE STARTTLS ID"
#define IMAP_TIMEOUT_MSG "* BYE dbmail IMAP4 server signing off due to timeout\r\n"
/** prefix for #Users namespace */
#define NAMESPACE_USER "#Users"
/** prefix for public namespace */
#define NAMESPACE_PUBLIC "#Public"
/** seperator for namespaces and mailboxes and submailboxes */
#define MAILBOX_SEPARATOR "/"
/** username for owner of public folders */
// FIXME: Should be #define PUBLIC_FOLDER_USER "__@!internal_public_user!@__"
#define PUBLIC_FOLDER_USER "__public__"
/* name of internal delivery user. */
#define DBMAIL_DELIVERY_USERNAME "__@!internal_delivery_user!@__"
/* standard user for ACL anyone (see RFC 2086 and 4314) */
#define DBMAIL_ACL_ANYONE_USER "anyone"

/* Consumers of this should be using POSTMASTER from dbmail.conf! */
#define DEFAULT_POSTMASTER "DBMAIL-MAILER@dbmail"
#define AUTO_NOTIFY_SENDER "autonotify@dbmail"
#define AUTO_NOTIFY_SUBJECT "NEW MAIL NOTIFICATION"

/* input reading linelimit */
#define MAX_LINESIZE (64*1024)

/* string length for query */
#define DEF_QUERYSIZE 8192
#define DEF_FRAGSIZE 64 

/** default table prefix */
#define DEFAULT_DBPFX "dbmail_"

#define MATCH(x,y) ((x) && (y) && (strcasecmp((x),(y))==0))

#define min(x,y) ((x)<=(y)?(x):(y))
#define max(x,y) ((x)>=(y)?(x):(y))
#define ISCR(a) ((char)(a)=='\r')
#define ISLF(a) ((char)(a)=='\n')
#define ISDOT(a) ((char)(a)=='.')

#define AUTHLOG_ERR "failed"
#define AUTHLOG_ACT "active"
#define AUTHLOG_FIN "closed"

#define LOG_SQLERROR TRACE(TRACE_ERR,"SQLException: %s", Exception_frame.message)
#define DISPATCH(f,a) \
	{ \
		GError *err = NULL; \
		if (! g_thread_create((GThreadFunc)f, (gpointer)a, FALSE, &err) ) \
			TRACE(TRACE_DEBUG,"gthread creation failed [%s]", err->message); \
	}

#define LOCK(a) g_static_rec_mutex_lock(a)
#define UNLOCK(a) g_static_rec_mutex_unlock(a)

#endif
