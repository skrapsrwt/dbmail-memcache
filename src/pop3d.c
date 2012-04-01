/*
 Copyright (C) 1999-2004 IC & S  dbmail@ic-s.nl
 Copyright (c) 2004-2011 NFG Net Facilities Group BV support@nfg.nl
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
* pop3d.c
*
* main prg for pop3 daemon
*/

#include "dbmail.h"
#define THIS_MODULE "pop3d"
#define PNAME "dbmail/pop3d"

/* also used in pop3.c */
int pop_before_smtp = 0;

int main(int argc, char *argv[])
{
	serverConfig_t config;
	int result;
		
	openlog(PNAME, LOG_PID, LOG_MAIL);
        memset(&config, 0, sizeof(serverConfig_t));
	result = server_getopt(&config, "POP", argc, argv);
	if (result == -1) goto shutdown;
	if (result == 1) {
		server_showhelp("dbmail-pop3d",
			"This daemon provides Post Office Protocol v3 services.\n");
		goto shutdown;
	}
	config.ClientHandler = pop3_handle_connection;
	pop_before_smtp = config.service_before_smtp;
	result = server_mainloop(&config, "POP", "dbmail-pop3d");
shutdown:
	TRACE(TRACE_INFO, "exit");
	return result;
}

