#ifndef _DBMAIL_ACL_H
#define _DBMAIL_ACL_H
/*
 Copyright (C) 2004 IC & S  dbmail@ic-s.nl

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
 * \file acl.h
 *
 * \brief header file for ACL (access control list) functions of DBMail.
 *        see RFC 2086 for details on IMAP ACL
 *
 * \author (c) 2004 IC&S
 */

/** 
 * different rights a user can have on a mailbox 
 */

#include "dbmailtypes.h"
#include "dm_mailboxstate.h"

/**
 * \brief sets new rights to a mailbox for a user.
 * \param userid id of user
 * \param mboxid id of mailbox
 * \param rightsstring string of righs
 * \return 
 *     - -1 on error
 *     -  1 on success
 */
int acl_set_rights(u64_t userid, u64_t mboxid, const char *rightsstring);

/**
 * \brief delete identifier, rights pair for selected user for mailbox
 * \param userid id of user
 * \param mboxid id of mailbox
 * \return 
 *      - -1 on error
 *      -  0 if nothing removed (i.e. no acl was found)
 *      -  1 if acl removed
 */
//int acl_delete_acl(u64_t userid, u64_t mboxid);
#define acl_delete_acl(a,b) db_acl_delete_acl((a),(b))

/**
 * \brief checks if a user has a certain right to a mailbox 
 * \param userid id of user
 * \param mboxid id of mailbox
 * \param right the right to check for
 * \return 
 *     - -1 on db error
 *     -  0 if no right
 *     -  1 if user has this right
 */
int acl_has_right(MailboxState_T S, u64_t userid, ACLRight_t right);

/**
 * \brief get complete acl for a mailbox
 * \param mboxid id of mailbox
 * \return
 *     - NULL on error
 *     - acl string (list of identifier-rights pairs, might by empty)
 * \note string should be freed by caller
 */
/*@null@*/ char *acl_get_acl(u64_t mboxid);

/**
 * \brief list rights that may be granted to a user on a mailbox
 * \param userid id of user
 * \param mboxid id of mailbox
 * \return
 *     - NULL on error
 *     - string of rights otherwise (SEE RFC for details)
 * \note string should be freed by caller
 */
/*@null@*/ /*@only@*/ char *acl_listrights(u64_t userid, u64_t mboxid);

/**
 * \brief list rights that a user has on a mailbox
 * \param userid id of user
 * \param mboxid id of mailbox
 * \return
 *     - NULL on error
 *     - string of rights otherwise (SEE RFC)
 * \note string should be freed by caller
 */
/*@null@*/ char *acl_myrights(u64_t userid, u64_t mboxid);

#endif
