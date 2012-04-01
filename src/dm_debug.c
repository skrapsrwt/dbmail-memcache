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
 *
 * Debugging and memory checking functions */

#include "dbmail.h"
#if defined(__FreeBSD__) || defined(__NetBSD__) || defined(BSD4_4)
extern char     *__progname;            /* Program name, from crt0. */
#else
char            *__progname = NULL;
#endif

char		hostname[16];
 
/* the debug variables */
static trace_t TRACE_SYSLOG = TRACE_EMERG | TRACE_ALERT | TRACE_CRIT | TRACE_ERR | TRACE_WARNING;  /* default: emerg, alert, crit, err, warning */
static trace_t TRACE_STDERR = TRACE_EMERG | TRACE_ALERT | TRACE_CRIT | TRACE_ERR | TRACE_WARNING;  /* default: emerg, alert, crit, err, warning */

/*
 * configure the debug settings
 */
void configure_debug(trace_t trace_syslog, trace_t trace_stderr)
{
	TRACE_SYSLOG = trace_syslog;
	TRACE_STDERR = trace_stderr;
}

/* Make sure that these match trace_t. */
void null_logger(const char UNUSED *log_domain, GLogLevelFlags UNUSED log_level, const char UNUSED *message, gpointer UNUSED data)
{
	// ignore glib messages	
	return;
}

static const char * trace_to_text(trace_t level)
{
	const char * const trace_text[] = {
		"EMERGENCY",
		"Alert",
		"Critical",
		"Error",
		"Warning",
		"Notice",
		"Info",
		"Debug",
		"Database"
	};
	return trace_text[ilogb((double) level)];
}

/* Call me like this:
 *
 * TRACE(TRACE_ERR, "Something happened with error code [%d]", resultvar);
 *
 * Please #define THIS_MODULE "mymodule" at the top of each file.
 * Arguments for __FILE__ and __func__ are added by the TRACE macro.
 *
 * trace() and TRACE() are macros in debug.h
 *
 */

#define SYSLOGFORMAT "[%p] %s:[%s] %s(+%d): %s"
#define STDERRFORMAT "%s %s %s[%d]: [%p] %s:[%s] %s(+%d): %s"
void trace(trace_t level, const char * module, const char * function, int line, const char *formatstring, ...)
{
	trace_t syslog_level;
	va_list ap, cp;

	gchar *message = NULL;
	static int configured=0;
	size_t l, maxlen=120;

	/* Return now if we're not logging anything. */
	if ( !(level & TRACE_STDERR) && !(level & TRACE_SYSLOG))
		return;

	va_start(ap, formatstring);
	va_copy(cp, ap);
	message = g_strdup_vprintf(formatstring, cp);
	va_end(cp);

	l = strlen(message);
	
	if (message[l] == '\n')
		message[l] = '\0';

	if (level & TRACE_STDERR) {
		time_t now = time(NULL);
		struct tm tmp;
		char date[32];

 		if (! configured) {
 			memset(hostname,'\0',sizeof(hostname));
 			gethostname(hostname,15);
 			configured=1;
 		}
 
		memset(date,0,sizeof(date));
		localtime_r(&now, &tmp);
		strftime(date,32,"%b %d %H:%M:%S", &tmp);

 		fprintf(stderr, STDERRFORMAT, date, hostname, __progname?__progname:"", getpid(), 
			g_thread_self(), trace_to_text(level), module, function, line, message);
 
		if (message[l] != '\n')
			fprintf(stderr, "\n");
		fflush(stderr);
	}

	if (level & TRACE_SYSLOG) {
		/* Convert our extended log levels (>128) to syslog levels */
		switch((int)ilogb((double) level))
		{
			case 0:
				syslog_level = LOG_EMERG;
				break;
			case 1:
				syslog_level = LOG_ALERT;
				break;
			case 2:
				syslog_level = LOG_CRIT;
				break;
			case 3:
				syslog_level = LOG_ERR;
				break;
			case 4:
				syslog_level = LOG_WARNING;
				break;
			case 5:
				syslog_level = LOG_NOTICE;
				break;
			case 6:
				syslog_level = LOG_INFO;
				break;
			case 7:
				syslog_level = LOG_DEBUG;
				break;
			case 8:
				syslog_level = LOG_DEBUG;
				break;
			default:
				syslog_level = LOG_DEBUG;
				break;
		}
		size_t w = min(l,maxlen);
		message[w] = '\0';
		syslog(syslog_level, SYSLOGFORMAT, g_thread_self(), trace_to_text(level), module, function, line, message);
	}
	g_free(message);

	/* Bail out on fatal errors. */
	if (level == TRACE_EMERG)
		exit(EX_TEMPFAIL);
}


