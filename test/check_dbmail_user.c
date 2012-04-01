/*
 *   Copyright (c) 2005-2011 NFG Net Facilities Group BV support@nfg.nl
 *
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation; either
 *   version 2 of the License, or (at your option) any later
 *   version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 *
 *   
 *
 *
 *  
 *
 *   Basic unit-test framework for dbmail (www.dbmail.org)
 *
 *   See http://check.sf.net for details and docs.
 *
 *
 *   Run 'make check' to see some action.
 *
 */ 

#include <check.h>
#include "check_dbmail.h"

extern char *configFile;
extern int quiet;
extern int reallyquiet;

/*
 *
 * the test fixtures
 *
 */
void setup(void)
{
	reallyquiet = 1;
	configure_debug(255,0);
	config_read(configFile);
	GetDBParams();
	db_connect();
	auth_connect();
	do_add("testfailuser","testpass","md5-hash",0,0,NULL,NULL);
}

void teardown(void)
{
	db_disconnect();
}

//int do_add(const char * const user,
//           const char * const password,
//           const char * const enctype,
//           const u64_t maxmail, const u64_t clientid,
//	   GList * alias_add,
//	   GList * alias_del);

START_TEST(test_do_add)
{
	int result;
	result = do_add("testfailuser","testpass","md5-hash",0,0,NULL,NULL);
	fail_unless(result!=0,"do_add succeeded when it should have failed");
	result = do_add("testadduser","testpass","md5-hash",0,0,NULL,NULL);
	fail_unless(result==0,"do_add failed");
}
END_TEST


//int do_show(const char * const user);
START_TEST(test_do_show)
{
	fail_unless(do_show("testfailuser")==0,"test_do_show failed");
//	fail_unless(do_show("nosuchuser"),"do_show should have failed");
}
END_TEST

//int do_empty(const u64_t useridnr);
START_TEST(test_do_empty)
{
	u64_t user_idnr;
	auth_user_exists("nosuchuser",&user_idnr);
	//fail_unless(do_empty(user_idnr),"do_empty should have failed");
}
END_TEST

//int do_delete(const u64_t useridnr, const char * const user);
START_TEST(test_do_delete)
{
	int result;
	u64_t user_idnr;
	auth_user_exists("testadduser",&user_idnr);
	fail_unless(user_idnr > 0,"abort test_do_delete: can't find user_idnr");
	result = do_delete(user_idnr, "testadduser");
	fail_unless(result==0,"test_do_delete failed");
}
END_TEST

START_TEST(test_dm_match)
{
	int i;
	char *candidate = "MyName is SAMMY @ and I am a SLUG!";
	char *badpatterns[] = {
		"hello", "*hello", "*hello*", "hello*",
		"*hello", "*hello*", "hello*", "?", NULL };
	char *goodpatterns[] = {
		"*", "*and*", "*SLUG!", "My*", "????My*",
		"MyName ?? *", "??Name*", "*SLUG?", NULL };

	for (i = 0; badpatterns[i] != NULL; i++) {
		fail_unless(match_glob(badpatterns[i], candidate)
			== NULL, "test_dm_match failed on a bad pattern:"
			" [%s]", badpatterns[i]);
	}

	for (i = 0; goodpatterns[i] != NULL; i++) {
		fail_unless(match_glob(goodpatterns[i], candidate)
			== candidate, "test_dm_match failed on a good pattern:"
			" [%s]", goodpatterns[i]);
	}

}
END_TEST

START_TEST(test_dm_match_list)
{
}
END_TEST

/* Change operations */
//int do_username(const u64_t useridnr, const char *newuser);
//int do_maxmail(const u64_t useridnr, const u64_t maxmail);
//int do_clientid(const u64_t useridnr, const u64_t clientid);
//int do_password(const u64_t useridnr,
//                const char * const password,
//                const char * const enctype);
//int do_aliases(const u64_t useridnr,
//               GList * alias_add,
//               GList * alias_del);
/* External forwards */
//int do_forwards(const char *alias, const u64_t clientid,
//                GList * fwds_add,
//                GList * fwds_del);

/* Helper functions */
//u64_t strtomaxmail(const char * const str);


Suite *dbmail_common_suite(void)
{
	Suite *s = suite_create("Dbmail User");
	TCase *tc_user = tcase_create("User");
	
	suite_add_tcase(s, tc_user);
	
	tcase_add_checked_fixture(tc_user, setup, teardown);
	tcase_add_test(tc_user, test_do_add);
	tcase_add_test(tc_user, test_do_show);
	tcase_add_test(tc_user, test_do_empty);
	tcase_add_test(tc_user, test_do_delete);
	tcase_add_test(tc_user, test_dm_match);
	tcase_add_test(tc_user, test_dm_match_list);
	
	return s;
}

int main(void)
{
	int nf;
	Suite *s = dbmail_common_suite();
	SRunner *sr = srunner_create(s);
	srunner_run_all(sr, CK_NORMAL);
	nf = srunner_ntests_failed(sr);
	srunner_free(sr);
	return (nf == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
	

