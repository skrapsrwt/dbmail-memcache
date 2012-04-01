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

#ifndef _DM_ICONV_H
#define _DM_ICONV_H

#include "dbmail.h"

struct DbmailIconv {
	field_t db_charset;
	field_t msg_charset;

	iconv_t to_db;
	iconv_t from_db;
	iconv_t from_msg;
};


void dbmail_iconv_init(void);
char * dbmail_iconv_str_to_db(const char* str_in, const char *charset);
char * dbmail_iconv_str_to_utf8(const char* str_in, const char *charset);
char * dbmail_iconv_db_to_utf7(const char* str_in);
char * dbmail_iconv_decode_text(const char *in);
char * dbmail_iconv_decode_address(char *address);
char * dbmail_iconv_decode_field(const char *in, const char *charset, gboolean isaddr);


#endif
