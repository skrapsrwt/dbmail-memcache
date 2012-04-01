/*
 Copyright (C) 1999-2004 IC & S  dbmail@ic-s.nl
 Copyright (C) 2006 Aaron Stone aaron@serendipity.cx

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
* lmtpd.c
*
* main prg for lmtp daemon
*/

#include "dbmail.h"
#define THIS_MODULE "lmtpd"
#define PNAME "dbmail/lmtpd"

int main(int argc, char *argv[])
{
	serverConfig_t config;
	int result;
		
	openlog(PNAME, LOG_PID, LOG_MAIL);
        memset(&config, 0, sizeof(serverConfig_t));
	result = server_getopt(&config, "LMTP", argc, argv);
	if (result == -1) goto shutdown;
	if (result == 1) {
		server_showhelp("dbmail-lmtpd",
			"This daemon provides Local Mail Transport Protocol services.");
		goto shutdown;
	}

	config.ClientHandler = lmtp_handle_connection;
	result = server_mainloop(&config, "LMTP", "dbmail-lmtpd");
shutdown:
	TRACE(TRACE_INFO, "exit");
	return result;
}

