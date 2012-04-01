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
* httpd.c
*
* main prg for http daemon
*/

#include "dbmail.h"
#include "dm_request.h"

#define THIS_MODULE "httpd"
#define PNAME "dbmail/httpd"

int main(int argc, char *argv[])
{
	serverConfig_t config;
	int result;

	g_mime_init(0);
	g_mime_parser_get_type();
	g_mime_stream_get_type();
	g_mime_stream_mem_get_type();
	g_mime_stream_file_get_type();
	g_mime_stream_buffer_get_type();
	g_mime_stream_filter_get_type();
	g_mime_filter_crlf_get_type();
	
	openlog(PNAME, LOG_PID, LOG_MAIL);

        memset(&config, 0, sizeof(serverConfig_t));
	result = server_getopt(&config, "HTTP", argc, argv);
	if (result == -1)
		goto shutdown;

	if (result == 1) {
		server_showhelp("dbmail-httpd",
			"This daemon provides HTTP services.");
		goto shutdown;
	}

	config.cb = Request_cb;
	result = server_mainloop(&config, "HTTP", "dbmail-httpd");
	
shutdown:
	g_mime_shutdown();
	config_free();

	TRACE(TRACE_INFO, "exit");
	return result;
}

