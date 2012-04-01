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
 * Functions for managing the sievescripts table
 *
 */

#ifndef  _DM_SIEVESCRIPT_H
#define  _DM_SIEVESCRIPT_H

#include "dbmail.h"


/**
 * \brief get a specific sieve script for a user
 * \param user_idnr user id
 * \param scriptname string with name of the script to get
 * \param script pointer to string that will hold the script itself
 * \return
 *        - -2 on failure of allocating memory for string
 *        - -1 on database failure
 *        - 0 on success
 * \attention caller should free the returned script
 */
int dm_sievescript_getbyname(u64_t user_idnr, char *scriptname, char **script);
/**
 * \brief Check if the user has an active sieve script.
 * \param user_idnr user id
 * \return
 *        - -1 on error
 *        - 1 when user has an active script
 *        - 0 when user doesn't have an active script
 */
int dm_sievescript_isactive(u64_t user_idnr);
int dm_sievescript_isactive_byname(u64_t user_idnr, const char *scriptname);
/**
 * \brief get the name of the active sieve script for a user
 * \param user_idnr user id
 * \param scriptname pointer to string that will hold the script name
 * \return
 *        - -3 on failure to find a matching row in database
 *        - -2 on failure of allocating memory for string
 *        - -1 on database failure
 *        - 0 on success
 * \attention caller should free the returned script name
 */
int dm_sievescript_get(u64_t user_idnr, char **scriptname);
/**
 * \brief get a list of sieve scripts for a user
 * \param user_idnr user id
 * \param scriptlist pointer to GList ** that will hold script names
 * \return
 *        - -2 on failure of allocating memory for string
 *        - -1 on database failure
 *        - 0 on success
 */
int dm_sievescript_list(u64_t user_idnr, GList **scriptlist);
/**
 * \brief rename a sieve script for a user
 * \param user_idnr user id
 * \param scriptname is the current name of the script
 * \param newname is the new name the script will be changed to
 * \return
 *        - -3 on non-existent script name
 *        - -2 on NULL scriptname or script
 *        - -1 on database failure
 *        - 0 on success
 */
int dm_sievescript_rename(u64_t user_idnr, char *scriptname, char *newname);
/**
 * \brief add a sieve script for a user
 * \param user_idnr user id
 * \param scriptname is the name of the script to be added
 * \param scriptname is the script itself to be added
 * \return
 *        - -3 on non-existent script name
 *        - -2 on NULL scriptname or script
 *        - -1 on database failure
 *        - 0 on success
 */
int dm_sievescript_add(u64_t user_idnr, char *scriptname, char *script);
/**
 * \brief deactivate a sieve script for a user
 * \param user_idnr user id
 * \param scriptname is the name of the script to be activated
 * \return
 *        - -3 on non-existent script name
 *        - -2 on bad or wrong script name
 *        - -1 on database failure
 *        - 0 on success
 */
int dm_sievescript_deactivate(u64_t user_idnr, char *scriptname);
/**
 * \brief activate a sieve script for a user
 * \param user_idnr user id
 * \param scriptname is the name of the script to be activated
 * \return
 *        - -3 on non-existent script name
 *        - -2 on bad or wrong script name
 *        - -1 on database failure
 *        - 0 on success
 */
int dm_sievescript_activate(u64_t user_idnr, char *scriptname);
/**
 * \brief delete a sieve script for a user
 * \param user_idnr user id
 * \param scriptname is the name of the script to be deleted
 * \return
 *        - -3 on non-existent script name
 *        - -1 on database failure
 *        - 0 on success
 */
int dm_sievescript_delete(u64_t user_idnr, char *scriptname);
/**
 * \brief checks to see if the user has space for a script
 * \param user_idnr user id
 * \param scriptlen is the size of the script we might insert
 * \return
 *        - -3 if there is not enough space
 *        - -1 on database failure
 *        - 0 on success
 */
int dm_sievescript_quota_check(u64_t user_idnr, u64_t scriptlen);
/**
 * \brief sets the sieve script quota for a user
 * \param user_idnr user id
 * \param quotasize is the desired new quota size
 * \return
 *        - -1 on database failure
 *        - 0 on success
 */
int dm_sievescript_quota_set(u64_t user_idnr, u64_t quotasize);
/**
 * \brief gets the current sieve script quota for a user
 * \param user_idnr user id
 * \param quotasize will be filled with the current quota size
 * \return
 *        - -1 on database failure
 *        - 0 on success
 */
int dm_sievescript_quota_get(u64_t user_idnr, u64_t * quotasize);

#endif
