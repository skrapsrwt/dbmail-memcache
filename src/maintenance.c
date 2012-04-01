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
 * This is the dbmail housekeeping program. 
 *	It checks the integrity of the database and does a cleanup of all
 *	deleted messages. 
 */

#include "dbmail.h"
#define THIS_MODULE "maintenance"
#define PNAME "dbmail/maintenance"

extern db_param_t _db_params;
#define DBPFX _db_params.pfx

/* Loudness and assumptions. */
int yes_to_all = 0;
int no_to_all = 0;
int verbose = 0;
/* Don't be helpful. */
int quiet = 0;
/* Don't print errors. */
int reallyquiet = 0;

char *configFile = DEFAULT_CONFIG_FILE;

int has_errors = 0;
int serious_errors = 0;

static int find_time(const char *timespec, timestring_t *timestring);
static int do_check_integrity(void);
static int do_purge_deleted(void);
static int do_set_deleted(void);
static int do_dangling_aliases(void);
static int do_header_cache(void);
static int do_check_iplog(const char *timespec);
static int do_check_replycache(const char *timespec);
static int do_vacuum_db(void);
static int do_rehash(void);
static int do_migrate(int migrate_limit);

int do_showhelp(void) {
	printf("*** dbmail-util ***\n");

	printf(
//	Try to stay under the standard 80 column width
//	0........10........20........30........40........50........60........70........80
	"Use this program to maintain your DBMail database.\n"
	"See the man page for more info. Summary:\n\n"
	"     -a        perform all checks (in this release: -ctubpds)\n"
	"     -c        clean up database (optimize/vacuum)\n"
	"     -t        test for message integrity\n"
	"     -b        body/header/envelope cache check\n"
	"     -p        purge messages have the DELETE status set\n"
	"     -d        set DELETE status for deleted messages\n"
	"     -s        remove dangling/invalid aliases and forwards\n"
	"     -r time   clear the replycache used for autoreply/vacation\n"
	"     -l time   clear the IP log used for IMAP/POP-before-SMTP\n"
	"               the time syntax is [<hours>h][<minutes>m]\n"
	"               valid examples: 72h, 4h5m, 10m\n"
	"     -M        migrate legacy 2.2.x messageblks to mimeparts table\n"
	"     -m limit  limit migration to [limit] number of physmessages. Default 10000 per run\n"
	"\nCommon options for all DBMail utilities:\n"
	"     -f file   specify an alternative config file\n"
	"     -q        quietly skip interactive prompts\n"
	"               use twice to suppress error messages\n"
	"     -n        show the intended action but do not perform it, no to all\n"
	"     -y        perform all proposed actions, as though yes to all\n"
	"     -v        verbose details\n"
	"     -V        show the version\n"
	"     -h        show this help message\n"
	);

	return 0;
}

int main(int argc, char *argv[])
{
	int check_integrity = 0;
	int check_iplog = 0, check_replycache = 0;
	char *timespec_iplog = NULL, *timespec_replycache = NULL;
	int vacuum_db = 0, purge_deleted = 0, set_deleted = 0, dangling_aliases = 0, rehash = 0;
	int show_help = 0;
	int do_nothing = 1;
	int is_header = 0;
	int migrate = 0, migrate_limit = 10000;
	static struct option long_options[] = {
		{ "rehash", 0, 0, 0 },
		{ 0, 0, 0, 0 }
	};
	int opt_index = 0;
	int opt;

	g_mime_init(0);
	openlog(PNAME, LOG_PID, LOG_MAIL);
	setvbuf(stdout, 0, _IONBF, 0);

	/* get options */
	opterr = 0;		/* suppress error message from getopt() */
	while ((opt = getopt_long(argc, argv, "-acbtl:r:pudsMm:" "i" "f:qnyvVh", long_options, &opt_index)) != -1) {
		/* The initial "-" of optstring allows unaccompanied
		 * options and reports them as the optarg to opt 1 (not '1') */
		switch (opt) {
		case 0:
			do_nothing = 0;
			if (strcmp(long_options[opt_index].name,"rehash")==0)
				rehash = 1;
			break;
		case 'a':
			/* This list should be kept up to date. */
			vacuum_db = 1;
			purge_deleted = 1;
			set_deleted = 1;
			dangling_aliases = 1;
			check_integrity = 1;
			is_header = 1;
			do_nothing = 0;
			break;

		case 'c':
			vacuum_db = 1;
			do_nothing = 0;
			break;

		case 'b':
			is_header = 1;
			do_nothing = 0;
			break;

		case 'p':
			purge_deleted = 1;
			do_nothing = 0;
			break;

		case 'd':
			set_deleted = 1;
			do_nothing = 0;
			break;

		case 's':
			dangling_aliases = 1;
			do_nothing = 0;
			break;

		case 't':
			check_integrity = 1;
			do_nothing = 0;
			break;

		case 'u':
			/* deprecated */
			break;

		case 'l':
			check_iplog = 1;
			do_nothing = 0;
			if (optarg)
				timespec_iplog = g_strdup(optarg);
			break;

		case 'r':
			check_replycache = 1;
			do_nothing = 0;
			if (optarg)
				timespec_replycache = g_strdup(optarg);
			break;

		case 'M':
			migrate = 1;
			do_nothing = 0;
			break;
		case 'm':
			if (optarg)
				migrate_limit = atoi(optarg);
			break;

		case 'i':
			qerrorf("Interactive console is not supported in this release.\n");
			return 1;

		/* Common options */
		case 'h':
			show_help = 1;
			do_nothing = 0;
			break;

		case 'n':
			no_to_all = 1;
			break;

		case 'y':
			yes_to_all = 1;
			break;

		case 'q':
                        /* If we get q twice, be really quiet! */
                        if (quiet)
	                                reallyquiet = 1;
                        if (!verbose)
	                                quiet = 1;
			break;

		case 'f':
			if (optarg && strlen(optarg) > 0)
				configFile = optarg;
			else {
				qerrorf("dbmail-util: -f requires a filename\n\n" );
				return 1;
			}
			break;

		case 'v':
			verbose = 1;
			break;

		case 'V':
			PRINTF_THIS_IS_DBMAIL;
			return 1;

		default:
			printf("unrecognized option [%c]\n", optopt); 
			show_help = 1;
			break;
		}
	}

	if (do_nothing || show_help || (no_to_all && yes_to_all)) {
		do_showhelp();
		return 1;
	}

 	/* Don't make any changes unless specifically authorized. */
 	if (!yes_to_all) {
		qprintf("Choosing dry-run mode. No changes will be made at this time.\n");
		no_to_all = 1;
 	}

	config_read(configFile);
	SetTraceLevel("DBMAIL");
	GetDBParams();

	qverbosef("Opening connection to database... \n");
	if (db_connect() != 0) {
		qerrorf("Failed. An error occured. Please check log.\n");
		return -1;
	}

	qverbosef("Opening connection to authentication... \n");
	if (auth_connect() != 0) {
		qerrorf("Failed. An error occured. Please check log.\n");
		return -1;
	}

	qverbosef("Ok. Connected.\n");

	if (check_integrity) do_check_integrity();
	if (purge_deleted) do_purge_deleted();
	if (is_header) do_header_cache();
	if (set_deleted) do_set_deleted();
	if (dangling_aliases) do_dangling_aliases();
	if (check_iplog) do_check_iplog(timespec_iplog);
	if (check_replycache) do_check_replycache(timespec_replycache);
	if (vacuum_db) do_vacuum_db();
	if (rehash) do_rehash();
	if (migrate) do_migrate(migrate_limit);

	if (!has_errors && !serious_errors) {
		qprintf("\nMaintenance done. No errors found.\n");
	} else {
		qerrorf("\nMaintenance done. Errors were found");
		if (serious_errors) {
			qerrorf(" but not fixed due to failures.\n");
			qerrorf("Please check the logs for further details, "
				"turning up the trace level as needed.\n");
			// Indicate that something went really wrong
			has_errors = 3;
		} else if (no_to_all) {
			qerrorf(" but not fixed.\n");
			qerrorf("Run again with the '-y' option to "
				"repair the errors.\n");
			// Indicate that the program should be run with -y
			has_errors = 2;
		} else if (yes_to_all) {
			qerrorf(" and fixed.\n");
			qerrorf("We suggest running dbmail-util again to "
				"confirm that all errors were repaired.\n");
			// Indicate that the program should be run again
			has_errors = 1;
		}
	}

	auth_disconnect();
	db_disconnect();
	config_free();
	g_mime_shutdown();
	
	return has_errors;
}

static int db_count_iplog(timestring_t lasttokeep, u64_t *rows)
{
	C c; R r; volatile int t = DM_SUCCESS;
	field_t to_date_str;
	assert(rows != NULL);
	*rows = 0;

	char2date_str(lasttokeep, &to_date_str);

	c = db_con_get();
	TRY
		r = db_query(c, "SELECT COUNT(*) FROM %spbsp WHERE since < %s", DBPFX, to_date_str);
		if (db_result_next(r))
			*rows = db_result_get_u64(r,0);
	CATCH(SQLException)
		LOG_SQLERROR;
		t = DM_EQUERY;
	FINALLY
		db_con_close(c);
	END_TRY;

	return t;
}

static int db_cleanup_iplog(timestring_t lasttokeep)
{
	field_t to_date_str;
	char2date_str(lasttokeep, &to_date_str);
	return db_update("DELETE FROM %spbsp WHERE since < %s", DBPFX, to_date_str);
}

static int db_count_replycache(timestring_t lasttokeep, u64_t *rows)
{
	C c; R r; volatile int t = FALSE;
	field_t to_date_str;
	assert(rows != NULL);
	*rows = 0;

	char2date_str(lasttokeep, &to_date_str);

	c = db_con_get();
	TRY
		r = db_query(c, "SELECT COUNT(*) FROM %sreplycache WHERE lastseen < %s", DBPFX, to_date_str);
		if (db_result_next(r))
			*rows = db_result_get_u64(r,0);
	CATCH(SQLException)
		LOG_SQLERROR;
		t = DM_EQUERY;
	FINALLY
		db_con_close(c);
	END_TRY;

	return t;
}

static int db_cleanup_replycache(timestring_t lasttokeep)
{
	field_t to_date_str;
	char2date_str(lasttokeep, &to_date_str);
	return db_update("DELETE FROM %sreplycache WHERE lastseen < %s", DBPFX, to_date_str);
}

static int db_count_deleted(u64_t * rows)
{
	C c; R r; volatile int t = TRUE;
	assert(rows != NULL); *rows = 0;

	c = db_con_get();
	TRY
		r = db_query(c, "SELECT COUNT(*) FROM %smessages WHERE status = %d", DBPFX, MESSAGE_STATUS_DELETE);
		if (db_result_next(r))
			*rows = db_result_get_int(r,0);
	CATCH(SQLException)
		LOG_SQLERROR;
		t = DM_EQUERY;
	FINALLY
		db_con_close(c);
	END_TRY;

	return t;
}

static int db_set_deleted(void)
{
	return db_update("UPDATE %smessages SET status = %d WHERE status = %d", DBPFX, MESSAGE_STATUS_PURGE, MESSAGE_STATUS_DELETE);
}

static int db_deleted_purge(void)
{
	return db_update("DELETE FROM %smessages WHERE status=%d", DBPFX, MESSAGE_STATUS_PURGE);
}

static int db_deleted_count(u64_t * rows)
{
	C c; R r; volatile int t = FALSE;
	assert(rows); *rows = 0;

	c = db_con_get();
	r = db_query(c, "SELECT COUNT(*) FROM %smessages WHERE status=%d", DBPFX, MESSAGE_STATUS_PURGE);
	TRY
		if (db_result_next(r)) {
			*rows = db_result_get_int(r,0);
			t = TRUE;
		}
	CATCH(SQLException)
		LOG_SQLERROR;
	FINALLY
		db_con_close(c);
	END_TRY;

	return t;
}


int do_purge_deleted(void)
{
	u64_t deleted_messages;

	if (no_to_all) {
		qprintf("\nCounting messages with DELETE status...\n");
		if (! db_deleted_count(&deleted_messages)) {
			qerrorf ("Failed. An error occured. Please check log.\n");
			serious_errors = 1;
			return -1;
		}
		qprintf("Ok. [%llu] messages have DELETE status.\n", deleted_messages);
	}
	if (yes_to_all) {
		qprintf("\nDeleting messages with DELETE status...\n");
		if (! db_deleted_purge()) {
			qerrorf ("Failed. An error occured. Please check log.\n");
			serious_errors = 1;
			return -1;
		}
		qprintf("Ok. Messages deleted.\n");
	}
	return 0;
}

int do_set_deleted(void)
{
	u64_t messages_set_to_delete;

	if (no_to_all) {
		// TODO: Count messages to delete.
		qprintf("\nCounting deleted messages that need the DELETE status set...\n");
		if (! db_count_deleted(&messages_set_to_delete)) {
			qerrorf ("Failed. An error occured. Please check log.\n");
			serious_errors = 1;
			return -1;
		}
		qprintf("Ok. [%llu] messages need to be set for deletion.\n", messages_set_to_delete);
	}
	if (yes_to_all) {
		qprintf("\nSetting DELETE status for deleted messages...\n");
		if (! db_set_deleted()) {
			qerrorf ("Failed. An error occured. Please check log.\n");
			serious_errors = 1;
			return -1;
		}
		qprintf("Ok. Messages set for deletion.\n");
		qprintf("Re-calculating used quota for all users...\n");
		if (dm_quota_rebuild() < 0) {
			qerrorf ("Failed. An error occured. Please check log.\n");
			serious_errors = 1;
			return -1;
		}
		qprintf("Ok. Used quota updated for all users.\n");
	}
	return 0;
}

GList *find_dangling_aliases(const char * const name)
{
	int result;
	char *username;
	GList *uids = NULL;
	GList *fwds = NULL;
	GList *dangling = NULL;

	/* For each alias, figure out if it resolves to a valid user
	 * or some forwarding address. If neither, remove it. */

	result = auth_check_user_ext(name,&uids,&fwds,0);
	
	if (!result) {
		qerrorf("Nothing found searching for [%s].\n", name);
		serious_errors = 1;
		return dangling;
	}

	uids = g_list_first(uids);
	while (uids) {
		username = auth_get_userid(*(u64_t *)uids->data);
		if (!username)
			dangling = g_list_prepend(dangling, uids->data);

		g_free(username);
		if (! g_list_next(uids))
			break;
		uids = g_list_next(uids);
	}

	return dangling;
}

int do_dangling_aliases(void)
{
	int count = 0;
	int result = 0;
	GList *aliases = NULL;

	if (no_to_all)
		qprintf("\nCounting aliases with nonexistent delivery userid's...\n");
	if (yes_to_all)
		qprintf("\nRemoving aliases with nonexistent delivery userid's...\n");

	aliases = auth_get_known_aliases();
	aliases = g_list_dedup(aliases, (GCompareFunc)strcmp, TRUE);
	aliases = g_list_first(aliases);
	while (aliases) {
		char deliver_to[21];
		GList *dangling = find_dangling_aliases(aliases->data);

		dangling = g_list_first(dangling);
		while (dangling) {
			count++;
			g_snprintf(deliver_to, 21, "%llu", *(u64_t *)dangling->data);
			qverbosef("Dangling alias [%s] delivers to nonexistent user [%s]\n",
				(char *)aliases->data, deliver_to);
			if (yes_to_all) {
				if (auth_removealias_ext(aliases->data, deliver_to) < 0) {
					qerrorf("Error: could not remove alias [%s] deliver to [%s] \n",
						(char *)aliases->data, deliver_to);
					serious_errors = 1;
					result = -1;
				}
			}
			if (!g_list_next(dangling))
				break;
			dangling = g_list_next(dangling);
		}
		g_list_destroy(g_list_first(dangling));

		if (! g_list_next(aliases))
			break;
		aliases = g_list_next(aliases);
	}
	g_list_destroy(g_list_first(aliases));

	if (count > 0) {
		qerrorf("Ok. Found [%d] dangling aliases.\n", count);
		has_errors = 1;
	} else {
		qprintf("Ok. Found [%d] dangling aliases.\n", count);
	}

	return result;
}

int do_check_integrity(void)
{
	time_t start, stop;
	GList *lost = NULL;
	const char *action;
	gboolean cleanup;
	long count = 0;

	if (yes_to_all) {
		action = "Repairing";
		cleanup = TRUE;
	} else {
		action = "Checking";
		cleanup = FALSE;
	}

	qprintf("\n%s DBMAIL message integrity...\n", action);

	/* This is what we do:
	 3. Check for loose physmessages
	 4. Check for loose partlists
	 5. Check for loose mimeparts
	 */

	/* part 3 */
	start = stop;
	qprintf("\n%s DBMAIL physmessage integrity...\n", action);
	if ((count = db_icheck_physmessages(cleanup)) < 0) {
		qerrorf("Failed. An error occurred. Please check log.\n");
		serious_errors = 1;
		return -1;
	}
	if (count > 0) {
		qerrorf("Ok. Found [%ld] unconnected physmessages.\n", count);
		if (cleanup) {
			qerrorf("Ok. Orphaned physmessages deleted.\n");
		}
	} else {
		qprintf("Ok. Found [%ld] unconnected physmessages.\n", count);
	}

	time(&stop);
	qverbosef("--- %s unconnected physmessages took %g seconds\n",
		action, difftime(stop, start));
	/* end part 3 */

	/* part 4 */
	start = stop;
	qprintf("\n%s DBMAIL partlists integrity...\n", action);
	if ((count = db_icheck_partlists(cleanup)) < 0) {
		qerrorf("Failed. An error occurred. Please check log.\n");
		serious_errors = 1;
		return -1;
	}
	if (count > 0) {
		qerrorf("Ok. Found [%ld] unconnected partlists.\n", count);
		if (cleanup) {
			qerrorf("Ok. Orphaned partlists deleted.\n");
		}
	} else {
		qprintf("Ok. Found [%ld] unconnected partlists.\n", count);
	}

	time(&stop);
	qverbosef("--- %s unconnected partlists took %g seconds\n",
		action, difftime(stop, start));
	/* end part 4 */

	/*  part 5 */
	start = stop;
	qprintf("\n%s DBMAIL mimeparts integrity...\n", action);
	if ((count = db_icheck_mimeparts(cleanup)) < 0) {
		qerrorf("Failed. An error occurred. Please check log.\n");
		serious_errors = 1;
		return -1;
	}
	if (count > 0) {
		qerrorf("Ok. Found [%ld] unconnected mimeparts.\n", count);
		if (cleanup) {
			qerrorf("Ok. Orphaned mimeparts deleted.\n");
		}
	} else {
		qprintf("Ok. Found [%ld] unconnected mimeparts.\n", count);
	}

	time(&stop);
	qverbosef("--- %s unconnected mimeparts took %g seconds\n",
		action, difftime(stop, start));
	/* end part 5 */

	g_list_destroy(lost);
	lost = NULL;

	time(&stop);
	qverbosef("--- %s block integrity took %g seconds\n", action, difftime(stop, start));
	/* end part 6 */

	return 0;
}

static int do_rfc_size(void)
{
	time_t start, stop;
	GList *lost = NULL;

	if (no_to_all) {
		qprintf("\nChecking DBMAIL for rfcsize field...\n");
	}
	if (yes_to_all) {
		qprintf("\nRepairing DBMAIL for rfcsize field...\n");
	}
	time(&start);

	if (db_icheck_rfcsize(&lost) < 0) {
		qerrorf("Failed. An error occured. Please check log.\n");
		serious_errors = 1;
		return -1;
	}

	if (g_list_length(lost) > 0) {
		qerrorf("Ok. Found [%d] missing rfcsize values.\n", g_list_length(lost));
		has_errors = 1;
	} else {
		qprintf("Ok. Found [%d] missing rfcsize values.\n", g_list_length(lost));
	}

	if (yes_to_all) {
		if (db_update_rfcsize(lost) < 0) {
			qerrorf("Error setting the rfcsize values");
			has_errors = 1;
		}
	}

	g_list_destroy(lost);

	time(&stop);
	qverbosef("--- checking rfcsize field took %g seconds\n",
	       difftime(stop, start));
	
	return 0;

}

static int do_envelope(void)
{
	time_t start, stop;
	GList *lost = NULL;

	if (no_to_all) {
		qprintf("\nChecking DBMAIL for cached envelopes...\n");
	}
	if (yes_to_all) {
		qprintf("\nRepairing DBMAIL for cached envelopes...\n");
	}
	time(&start);

	if (db_icheck_envelope(&lost) < 0) {
		qerrorf("Failed. An error occured. Please check log.\n");
		serious_errors = 1;
		return -1;
	}

	if (g_list_length(lost) > 0) {
		qerrorf("Ok. Found [%d] missing envelope values.\n", g_list_length(lost));
		has_errors = 1;
	} else {
		qprintf("Ok. Found [%d] missing envelope values.\n", g_list_length(lost));
	}

	if (yes_to_all) {
		if (db_set_envelope(lost) < 0) {
			qerrorf("Error setting the envelope cache");
			has_errors = 1;
		}
	}

	g_list_free(g_list_first(lost));

	time(&stop);
	qverbosef("--- checking envelope cache took %g seconds\n",
	       difftime(stop, start));
	
	return 0;

}


int do_header_cache(void)
{
	time_t start, stop;
	GList *lost = NULL;
	
	if (do_rfc_size()) {
		serious_errors = 1;
		return -1;
	}
	if (do_envelope()) {
		serious_errors = 1;
		return -1;
	}
	
	if (no_to_all) 
		qprintf("\nChecking DBMAIL for cached header values...\n");
	if (yes_to_all) 
		qprintf("\nRepairing DBMAIL for cached header values...\n");
	
	time(&start);

	if (db_icheck_headercache(&lost) < 0) {
		qerrorf("Failed. An error occured. Please check log.\n");
		serious_errors = 1;
		return -1;
	}

	if (g_list_length(lost) > 0) {
		qerrorf("Ok. Found [%d] un-cached physmessages.\n", g_list_length(lost));
		has_errors = 1;
	} else {
		qprintf("Ok. Found [%d] un-cached physmessages.\n", g_list_length(lost));
	}

	if (yes_to_all) {
		if (db_set_headercache(lost) < 0) {
			qerrorf("Error caching the header values ");
			serious_errors = 1;
		}
	}

	g_list_free(lost);

	time(&stop);
	qverbosef("--- checking cached headervalues took %g seconds\n",
	       difftime(stop, start));

	return 0;
}


int do_check_iplog(const char *timespec)
{
	u64_t log_count;
	timestring_t timestring;

	if (find_time(timespec, &timestring) != 0) {
		qerrorf("\nFailed to find a timestring: [%s] is not <hours>h<minutes>m.\n",
		       timespec);
		serious_errors = 1;
		return -1;
	}

	if (no_to_all) {
		qprintf("\nCounting IP entries older than [%s]...\n", timestring);
		if (db_count_iplog(timestring, &log_count) < 0) {
			qerrorf("Failed. An error occured. Check the log.\n");
			serious_errors = 1;
			return -1;
		}
		qprintf("Ok. [%llu] IP entries are older than [%s].\n",
		    log_count, timestring);
	}
	if (yes_to_all) {
		qprintf("\nRemoving IP entries older than [%s]...\n", timestring);
		if (! db_cleanup_iplog(timestring)) {
			qerrorf("Failed. Please check the log.\n");
			serious_errors = 1;
			return -1;
		}

		qprintf("Ok. IP entries older than [%s] removed.\n",
		       timestring);
	}
	return 0;
}

int do_check_replycache(const char *timespec)
{
	u64_t log_count;
	timestring_t timestring;

	if (find_time(timespec, &timestring) != 0) {
		qerrorf("\nFailed to find a timestring: [%s] is not <hours>h<minutes>m.\n",
		       timespec);
		serious_errors = 1;
		return -1;
	}

	if (no_to_all) {
		qprintf("\nCounting RC entries older than [%s]...\n", timestring);
		if (db_count_replycache(timestring, &log_count) < 0) {
			qerrorf("Failed. An error occured. Check the log.\n");
			serious_errors = 1;
			return -1;
		}
		qprintf("Ok. [%llu] RC entries are older than [%s].\n",
		    log_count, timestring);
	}
	if (yes_to_all) {
		qprintf("\nRemoving RC entries older than [%s]...\n", timestring);
		if (! db_cleanup_replycache(timestring)) {
			qerrorf("Failed. Please check the log.\n");
			serious_errors = 1;
			return -1;
		}

		qprintf("Ok. RC entries were older than [%s] cleaned.\n", timestring);
	}
	return 0;
}

int do_vacuum_db(void)
{
	if (no_to_all) {
		qprintf("\nVacuum and optimize not performed.\n");
	}
	if (yes_to_all) {
		qprintf("\nVacuuming and optimizing database...\n");
		fflush(stdout);
		if (db_cleanup() < 0) {
			qerrorf("Failed. Please check the log.\n");
			serious_errors = 1;
			return -1;
		}

		qprintf("Ok. Database cleaned up.\n");
	}
	return 0;
}

int do_rehash(void)
{
	if (yes_to_all) {
		qprintf ("Rebuild hash keys for stored message chunks...\n");
		if (db_rehash_store() == DM_EQUERY) {
			qerrorf("Failed. Please check the log.\n");
			serious_errors = 1;
			return -1;
		}
		qprintf ("Ok. Hash keys rebuild successfully.\n");
	}

	return 0;

}

int do_migrate(int migrate_limit)
{
	C c; R r;
	int id = 0;
	int count = 0;
	DbmailMessage *m;
	
	qprintf ("Mirgrate legacy 2.2.x messageblks to mimeparts...\n");
	if (!yes_to_all) {
		qprintf ("\tmigration skipped. Use -y option to perform mirgration.\n");
		return 0;
	}
	qprintf ("Preparing to migrate %d physmessages.\n", migrate_limit);

	c = db_con_get();
	TRY
		r = db_query(c, "SELECT DISTINCT(physmessage_id) FROM %smessageblks LIMIT %d", DBPFX, migrate_limit);
		qprintf ("Migrating %d physmessages...\n", migrate_limit);
		while (db_result_next(r))
		{
			count++;
			id = db_result_get_u64(r,0);
			m = dbmail_message_new();
			m = dbmail_message_retrieve(m, id, DBMAIL_MESSAGE_FILTER_FULL);
			if(!dm_message_store(m))
			{
				if(verbose) qprintf ("%d ",id);
				db_update("DELETE FROM %smessageblks WHERE physmessage_id = %d", DBPFX, id);
			}
			else
			{
				if(!verbose) qprintf ("migrating physmessage_id: %d\n",id);
				qprintf ("failed\n");
				return -1;
			}
			dbmail_message_free(m);
		}
	CATCH(SQLException)
		LOG_SQLERROR;
		return -1;
	FINALLY
		db_con_close(c);
	END_TRY;
	
	qprintf ("Migration complete. Migrated %d physmessages.\n", count);
	return 0;
}

/* Makes a date/time string: YYYY-MM-DD HH:mm:ss
 * based on current time minus timespec
 * timespec contains: <n>h<m>m for a timespan of n hours, m minutes
 * hours or minutes may be absent, not both
 *
 * Returns NULL on error.
 */
int find_time(const char *timespec, timestring_t *timestring)
{
	time_t td;
	struct tm tm;
	int min = -1, hour = -1;
	long tmp;
	char *end = NULL;

	time(&td);		/* get time */

	if (!timespec) {
		serious_errors = 1;
		return -1;
	}

	/* find first num */
	tmp = strtol(timespec, &end, 10);
	if (!end) {
		serious_errors = 1;
		return -1;
	}

	if (tmp < 0) {
		serious_errors = 1;
		return -1;
	}

	switch (*end) {
	case 'h':
	case 'H':
		hour = tmp;
		break;

	case 'm':
	case 'M':
		hour = 0;
		min = tmp;
		if (end[1]) {	/* should end here */
			serious_errors = 1;
			return -1;
		}

		break;

	default:
		serious_errors = 1;
		return -1;
	}


	/* find second num */
	if (timespec[end - timespec + 1]) {
		tmp = strtol(&timespec[end - timespec + 1], &end, 10);
		if (end) {
			if ((*end != 'm' && *end != 'M') || end[1]) {
				serious_errors = 1;
				return -1;
			}

			if (tmp < 0) {
				serious_errors = 1;
				return -1;
			}

			if (min >= 0) {	/* already specified minutes */
				serious_errors = 1;
				return -1;
			}

			min = tmp;
		}
	}

	if (min < 0)
		min = 0;

	/* adjust time */
	td -= (hour * 3600L + min * 60L);

	tm = *localtime(&td);	/* get components */
	strftime((char *) timestring, sizeof(timestring_t),
		 "%Y-%m-%d %H:%M:%S", &tm);

	return 0;
}

