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

 
 headers for debug.c 
 
*/

#ifndef  _DEBUG_H
#define  _DEBUG_H

#include "dbmail.h"

typedef enum {
	TRACE_EMERG = 1,
	TRACE_ALERT = 2,
	TRACE_CRIT = 4,
	TRACE_ERR = 8,
	TRACE_WARNING = 16,
	TRACE_NOTICE = 32,
	TRACE_INFO = 64,
	TRACE_DEBUG = 128,
	TRACE_DATABASE = 256 // Logs at Debug Level
} trace_t;


/* Define several macros for GCC specific attributes.
 * Although the __attribute__ macro can be easily defined
 * to nothing, these macros make them a little prettier.
 * */
#ifdef __GNUC__
#define UNUSED __attribute__((__unused__))
#define PRINTF_ARGS(X, Y) __attribute__((format(printf, X, Y)))
#else
#define UNUSED
#define PRINTF_ARGS(X, Y)
#endif


#define TRACE(level, fmt...) trace(level, THIS_MODULE, __func__, __LINE__, fmt)
void trace(trace_t level, const char * module, const char * function, int line, const char *formatstring, ...) PRINTF_ARGS(5, 6);

void configure_debug(trace_t trace_syslog, trace_t trace_stderr);

void null_logger(const char UNUSED *log_domain, GLogLevelFlags UNUSED log_level, const char UNUSED *message, gpointer UNUSED data);
#endif
