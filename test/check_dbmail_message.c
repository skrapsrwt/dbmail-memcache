/*
 *   Copyright (c) 2004-2011 NFG Net Facilities Group BV support@nfg.nl
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
extern char *multipart_message;
extern char *multipart_message_part;
extern char *raw_lmtp_data;


/*
 *
 * the test fixtures
 *
 */

	
void setup(void)
{
	configure_debug(511,0);
	config_read(configFile);
	GetDBParams();
	db_connect();
	auth_connect();
}

void teardown(void)
{
	db_disconnect();
	auth_disconnect();
	config_free();
}

//DbmailMessage * dbmail_message_new(void);
START_TEST(test_dbmail_message_new)
{
	DbmailMessage *m = dbmail_message_new();
	fail_unless(m->id==0,"dbmail_message_new failed");
	dbmail_message_free(m);
}
END_TEST
//void dbmail_message_set_class(DbmailMessage *self, int klass);
START_TEST(test_dbmail_message_set_class)
{
	int result;
	DbmailMessage *m = dbmail_message_new();
	result = dbmail_message_set_class(m,DBMAIL_MESSAGE);
	fail_unless(result==0,"dbmail_message_set_class failed");
	result = dbmail_message_set_class(m,DBMAIL_MESSAGE_PART);
	fail_unless(result==0,"dbmail_message_set_class failed");
	result = dbmail_message_set_class(m,10);
	fail_unless(result!=0,"dbmail_message_set_class failed");
	
	dbmail_message_free(m);
}
END_TEST
//int dbmail_message_get_class(DbmailMessage *self);
START_TEST(test_dbmail_message_get_class)
{
	int klass;
	DbmailMessage *m = dbmail_message_new();
	
	/* default */
	klass = dbmail_message_get_class(m);
	fail_unless(klass==DBMAIL_MESSAGE,"dbmail_message_get_class failed");
	
	/* set explicitely */
	dbmail_message_set_class(m,DBMAIL_MESSAGE_PART);
	klass = dbmail_message_get_class(m);
	fail_unless(klass==DBMAIL_MESSAGE_PART,"dbmail_message_get_class failed");
	
	dbmail_message_set_class(m,DBMAIL_MESSAGE);
	klass = dbmail_message_get_class(m);
	fail_unless(klass==DBMAIL_MESSAGE,"dbmail_message_get_class failed");

	dbmail_message_free(m);
}
END_TEST
static char * showdiff(const char *a, const char *b)
{
	assert(a && b);
	while (*a++ == *b++)
		;
	if (--a && --b)
		return g_strdup_printf("[%s]\n[%s]\n", a, b);
	return NULL;
}

FILE *i;
#define COMPARE(a,b) \
	{ \
	int d; size_t l; char *s;\
	l = strlen(a); \
	d = memcmp((a),(b),l); \
	s = showdiff(a,b); \
	fail_unless(d == 0, "store store/retrieve failed\n%s\n\n", s); \
	g_free(s); \
	}

static DbmailMessage  * message_init(const char *message)
{
	GString *s;
	DbmailMessage *m;

	s = g_string_new(message);
	m = dbmail_message_new();
	m = dbmail_message_init_with_string(m, s);
	g_string_free(s,TRUE);

	fail_unless(m != NULL, "dbmail_message_init_with_string failed");

	return m;

}
static char *store_and_retrieve(DbmailMessage *m)
{
	u64_t physid;
	DbmailMessage *n;
	char *t;

	dbmail_message_store(m);
	physid = dbmail_message_get_physid(m);
	fail_unless(physid != 0,"dbmail_message_store failed. physid [%llu]", physid);
	dbmail_message_free(m);

	n = dbmail_message_new();
	dbmail_message_set_physid(n, physid);
	n = dbmail_message_retrieve(n,physid,DBMAIL_MESSAGE_FILTER_FULL);
	fail_unless(n != NULL, "_mime_retrieve failed");
	
	t = dbmail_message_to_string(n);
	dbmail_message_free(n);
	return t;
}

START_TEST(test_g_mime_object_get_body)
{
	char * result;
	DbmailMessage *m;

	m = message_init(multipart_message);
	
	result = g_mime_object_get_body(GMIME_OBJECT(m->content));
	fail_unless(strlen(result)==1057,"g_mime_object_get_body failed [%d:%s]\n", strlen(result), result);
	g_free(result);
	dbmail_message_free(m);
	
	m = message_init(rfc822);
	result = g_mime_object_get_body(GMIME_OBJECT(m->content));
	COMPARE("\n    this is a test message\n\n", result);
	g_free(result);
	dbmail_message_free(m);
}
END_TEST


START_TEST(test_dbmail_message_store)
{
	DbmailMessage *m;
	char *t, *e;

	//-----------------------------------------
	m = message_init("From: paul\n");
	e = dbmail_message_to_string(m);
	t = store_and_retrieve(m);
	COMPARE(e,t);
	g_free(e);
	g_free(t);
	//-----------------------------------------
	m = message_init(simple);
	e = dbmail_message_to_string(m);
	t = store_and_retrieve(m);
	COMPARE(e,t);
	g_free(e);
	g_free(t);
	//-----------------------------------------
	m = message_init(rfc822);
	e = dbmail_message_to_string(m);
	t = store_and_retrieve(m);
	COMPARE(e,t);
	g_free(e);
	g_free(t);
	//-----------------------------------------
	m = message_init(multipart_message);
	e = dbmail_message_to_string(m);
	t = store_and_retrieve(m);
	COMPARE(e,t);
	g_free(e);
	g_free(t);
	//-----------------------------------------
	m = message_init(multipart_message2);
	e = dbmail_message_to_string(m);
	t = store_and_retrieve(m);
	COMPARE(e,t);
	g_free(e);
	g_free(t);
	//-----------------------------------------
	m = message_init(message_rfc822);
	e = dbmail_message_to_string(m);
	t = store_and_retrieve(m);
	COMPARE(e,t);
	g_free(e);
	g_free(t);
	//-----------------------------------------
	m = message_init(multipart_message3);
	e = dbmail_message_to_string(m);
	t = store_and_retrieve(m);
	COMPARE(e,t);
	g_free(e);
	g_free(t);
	//-----------------------------------------
	m = message_init(multipart_message4);
	e = dbmail_message_to_string(m);
	t = store_and_retrieve(m);
	COMPARE(e,t);
	g_free(e);
	g_free(t);
	//-----------------------------------------
	m = message_init(multipart_message5);
	e = dbmail_message_to_string(m);
	t = store_and_retrieve(m);
	COMPARE(e,t);
	g_free(e);
	g_free(t);
	//-----------------------------------------
	m = message_init(multipart_message6);
	e = dbmail_message_to_string(m);
	t = store_and_retrieve(m);
	COMPARE(e,t);
	g_free(e);
	g_free(t);
	//-----------------------------------------
	m = message_init(multipart_mixed);
	e = dbmail_message_to_string(m);
	t = store_and_retrieve(m);
	COMPARE(e,t);
	g_free(e);
	g_free(t);
	//-----------------------------------------
	m = message_init(broken_message);
	e = dbmail_message_to_string(m);
	t = store_and_retrieve(m);
	//COMPARE(expect,t);
	g_free(e);
	g_free(t);
	//-----------------------------------------
	m = message_init(encoded_message_latin_1);
	e = dbmail_message_to_string(m);
	t = store_and_retrieve(m);
	COMPARE(e,t);
	g_free(e);
	g_free(t);
	//-----------------------------------------
	m = message_init(encoded_message_latin_2);
	e = dbmail_message_to_string(m);
	t = store_and_retrieve(m);
	COMPARE(e,t);
	g_free(e);
	g_free(t);
	//-----------------------------------------
	m = message_init(encoded_message_utf8);
	e = dbmail_message_to_string(m);
	t = store_and_retrieve(m);
	COMPARE(e,t);
	g_free(e);
	g_free(t);
	//-----------------------------------------
	m = message_init(encoded_message_utf8_1);
	e = dbmail_message_to_string(m);
	t = store_and_retrieve(m);
	COMPARE(e,t);
	g_free(e);
	g_free(t);
	//-----------------------------------------
	m = message_init(encoded_message_utf8_2);
	e = dbmail_message_to_string(m);
	t = store_and_retrieve(m);
	COMPARE(e,t);
	g_free(e);
	g_free(t);
	//-----------------------------------------
	m = message_init(encoded_message_koi);
	e = dbmail_message_to_string(m);
	t = store_and_retrieve(m);
	COMPARE(e,t);
	g_free(e);
	g_free(t);
	//-----------------------------------------
	m = message_init(raw_message_koi);
	e = dbmail_message_to_string(m);
	t = store_and_retrieve(m);
	COMPARE(e,t);
	g_free(e);
	g_free(t);
	//-----------------------------------------
	m = message_init(multipart_alternative);
	e = dbmail_message_to_string(m);
	t = store_and_retrieve(m);
	COMPARE(e,t);
	g_free(e);
	g_free(t);
	//-----------------------------------------
	m = message_init(outlook_multipart);
	e = dbmail_message_to_string(m);
	t = store_and_retrieve(m);
	COMPARE(e,t);
	g_free(e);
	g_free(t);
	//-----------------------------------------
	m = message_init(multipart_alternative2);
	e = dbmail_message_to_string(m);
	t = store_and_retrieve(m);
	// FIXME COMPARE(e,t);
	g_free(e);
	g_free(t);
}
END_TEST

START_TEST(test_dbmail_message_store2)
{
	DbmailMessage *m, *n;
	u64_t physid;
	char *t;
	char *expect;

	m = message_init(broken_message2);
	
	dbmail_message_set_header(m, "Return-Path", dbmail_message_get_header(m, "From"));
	
	expect = dbmail_message_to_string(m);
	fail_unless(m != NULL, "dbmail_message_store2 failed");

	dbmail_message_store(m);
	physid = dbmail_message_get_physid(m);
	dbmail_message_free(m);

	n = dbmail_message_new();
	dbmail_message_set_physid(n, physid);
	n = dbmail_message_retrieve(n,physid,DBMAIL_MESSAGE_FILTER_FULL);
	fail_unless(n != NULL, "_mime_retrieve failed");
	
	t = dbmail_message_to_string(n);
	
	COMPARE(expect,t);
	
	dbmail_message_free(n);
	g_free(expect);
	g_free(t);
	
}
END_TEST


//DbmailMessage * dbmail_message_retrieve(DbmailMessage *self, u64_t physid, int filter);
START_TEST(test_dbmail_message_retrieve)
{
	DbmailMessage *m, *n;
	GString *s;
	u64_t physid;

	s = g_string_new(multipart_message);
	m = dbmail_message_new();
	m = dbmail_message_init_with_string(m, s);
	fail_unless(m != NULL, "dbmail_message_init_with_string failed");

	dbmail_message_set_header(m, 
			"References", 
			"<20050326155326.1afb0377@ibook.linuks.mine.nu> <20050326181954.GB17389@khazad-dum.debian.net> <20050326193756.77747928@ibook.linuks.mine.nu> ");
	dbmail_message_store(m);

	physid = dbmail_message_get_physid(m);
	fail_unless(physid > 0, "dbmail_message_get_physid failed");
	
	n = dbmail_message_new();
	n = dbmail_message_retrieve(n,physid,DBMAIL_MESSAGE_FILTER_HEAD);	
	fail_unless(n != NULL, "dbmail_message_retrieve failed");
	fail_unless(n->content != NULL, "dbmail_message_retrieve failed");

	dbmail_message_free(m);
	dbmail_message_free(n);
	g_string_free(s,TRUE);

}
END_TEST
//DbmailMessage * dbmail_message_init_with_string(DbmailMessage *self, const GString *content);
START_TEST(test_dbmail_message_init_with_string)
{
	DbmailMessage *m;
	GTuples *t;
	
	m = message_init(multipart_message);
	
	t = g_relation_select(m->headers, "Received", 0);
	fail_unless(t->len==2,"Too few or too many headers in tuple [%d]\n", t->len);
	g_tuples_destroy(t);
	dbmail_message_free(m);
	
//	m = message_init(simple_message_part);
//	fail_unless(dbmail_message_get_class(m)==DBMAIL_MESSAGE_PART, "init_with string failed");
//	dbmail_message_free(m);
}
END_TEST

START_TEST(test_dbmail_message_get_internal_date)
{
	DbmailMessage *m;
	GString *s;

	s = g_string_new(rfc822);

	m = dbmail_message_new();
	m = dbmail_message_init_with_string(m, s);
	// From_ contains: Wed Sep 14 16:47:48 2005
	const char *expect = "2005-09-14 16:47:48";
	const char *expect03 = "2003-09-14 16:47:48";
	const char *expect75 = "1975-09-14 16:47:48";
	char *result;

	/* baseline */
	result = dbmail_message_get_internal_date(m, 0);
	fail_unless(MATCH(expect,result),"dbmail_message_get_internal_date failed exp [%s] got [%s]", expect, result);
	g_free(result);

	/* should be the same */
	result = dbmail_message_get_internal_date(m, 2007);
	fail_unless(MATCH(expect,result),"dbmail_message_get_internal_date failed exp [%s] got [%s]", expect, result);
	g_free(result);

	/* capped to 2004, which should also be the same  */
	result = dbmail_message_get_internal_date(m, 2004);
	fail_unless(MATCH(expect,result),"dbmail_message_get_internal_date failed exp [%s] got [%s]", expect, result);
	g_free(result);

	/* capped to 2003, should be different */
	result = dbmail_message_get_internal_date(m, 2003);
	fail_unless(MATCH(expect03,result),"dbmail_message_get_internal_date failed exp [%s] got [%s]", expect03, result);
	g_free(result);

	/* capped to 1975, should be way different */
	result = dbmail_message_get_internal_date(m, 1975);
	fail_unless(MATCH(expect75,result),"dbmail_message_get_internal_date failed exp [%s] got [%s]", expect75, result);
	g_free(result);

	g_string_free(s,TRUE);
	dbmail_message_free(m);
	
}
END_TEST

START_TEST(test_dbmail_message_to_string)
{
        char *result;
	DbmailMessage *m;
        
	m = message_init(multipart_message);
        result = dbmail_message_to_string(m);
	COMPARE(multipart_message, result);
	g_free(result);
	dbmail_message_free(m);

	//
	m = message_init(simple_with_from);
	result = dbmail_message_to_string(m);
	COMPARE(simple_with_from, result);
	g_free(result);
	dbmail_message_free(m);

}
END_TEST
    
//DbmailMessage * dbmail_message_init_with_stream(DbmailMessage *self, GMimeStream *stream, int type);
/*
START_TEST(test_dbmail_message_init_with_stream)
{
}
END_TEST
*/
//gchar * dbmail_message_hdrs_to_string(DbmailMessage *self);

START_TEST(test_dbmail_message_hdrs_to_string)
{
	char *result;
	GString *s;
	DbmailMessage *m;
	
	s = g_string_new(multipart_message);
	m = dbmail_message_new();
        m = dbmail_message_init_with_string(m, s);

	result = dbmail_message_hdrs_to_string(m);
	fail_unless(strlen(result)==676, "dbmail_message_hdrs_to_string failed [%d] != [634]\n[%s]\n", strlen(result), result);
	
	g_string_free(s,TRUE);
        dbmail_message_free(m);
	g_free(result);
}
END_TEST

//gchar * dbmail_message_body_to_string(DbmailMessage *self);

START_TEST(test_dbmail_message_body_to_string)
{
	char *result;
	GString *s;
	DbmailMessage *m;
	
	s = g_string_new(multipart_message);
	m = dbmail_message_new();
        m = dbmail_message_init_with_string(m,s);
	result = dbmail_message_body_to_string(m);
	fail_unless(strlen(result)==1057, "dbmail_message_body_to_string failed [%d] != [1057]\n[%s]\n", strlen(result),result);
	
        dbmail_message_free(m);
	g_string_free(s,TRUE);
	g_free(result);

	s = g_string_new(outlook_multipart);
	m = dbmail_message_new();
        m = dbmail_message_init_with_string(m,s);
	result = dbmail_message_body_to_string(m);
	fail_unless(strlen(result)==330, "dbmail_message_body_to_string failed [330 != %d:%s]", strlen(result), result);
	
        dbmail_message_free(m);
	g_string_free(s,TRUE);
	g_free(result);

	
}
END_TEST

//void dbmail_message_free(DbmailMessage *self);
START_TEST(test_dbmail_message_free)
{
	DbmailMessage *m = dbmail_message_new();
	dbmail_message_free(m);
}
END_TEST

START_TEST(test_dbmail_message_set_header)
{
	DbmailMessage *m;
	GString *s;
	s =  g_string_new(multipart_message);
	m = dbmail_message_new();
	m = dbmail_message_init_with_string(m,s);
	dbmail_message_set_header(m, "X-Foobar","Foo Bar");
	fail_unless(dbmail_message_get_header(m, "X-Foobar")!=NULL, "set_header failed");
	dbmail_message_free(m);
	g_string_free(s,TRUE);
}
END_TEST

START_TEST(test_dbmail_message_get_header)
{
	char *t;
	DbmailMessage *h = dbmail_message_new();
	DbmailMessage *m = dbmail_message_new();
	GString *s, *j;
	
	s = g_string_new(multipart_message);
	m = dbmail_message_init_with_string(m, s);
	t = dbmail_message_hdrs_to_string(m);
	j = g_string_new(t);
	h = dbmail_message_init_with_string(h, j);
	g_free(t);
	
	fail_unless(dbmail_message_get_header(m, "X-Foobar")==NULL, "get_header failed on full message");
	fail_unless(strcmp(dbmail_message_get_header(m,"Subject"),"multipart/mixed")==0,"get_header failed on full message");

	fail_unless(dbmail_message_get_header(h, "X-Foobar")==NULL, "get_header failed on headers-only message");
	fail_unless(strcmp(dbmail_message_get_header(h,"Subject"),"multipart/mixed")==0,"get_header failed on headers-only message");
	
	dbmail_message_free(m);
	dbmail_message_free(h);
	g_string_free(s,TRUE);
	g_string_free(j,TRUE);

}
END_TEST

START_TEST(test_dbmail_message_encoded)
{
	DbmailMessage *m = dbmail_message_new();
	GString *s = g_string_new(encoded_message_koi);
	//const char *exp = ":: [ Arrty ] :: [ Roy (L) St�phanie ]  <over.there@hotmail.com>";
	u64_t id = 0;

	m = dbmail_message_init_with_string(m, s);

	fail_unless(strcmp(dbmail_message_get_header(m,"From"),"=?koi8-r?Q?=E1=CE=D4=CF=CE=20=EE=C5=C8=CF=D2=CF=DB=C9=C8=20?=<bad@foo.ru>")==0, 
			"dbmail_message_get_header failed for koi-8 encoded header");
	dbmail_message_free(m);


	m = dbmail_message_new();
	g_string_printf(s, "%s", utf7_header);
	m = dbmail_message_init_with_string(m, s);
	g_string_free(s, TRUE);

	dbmail_message_store(m);
	id = dbmail_message_get_physid(m);
	dbmail_message_free(m);

	m = dbmail_message_new();
	m = dbmail_message_retrieve(m, id, DBMAIL_MESSAGE_FILTER_FULL);
	dbmail_message_free(m);


}
END_TEST
START_TEST(test_dbmail_message_8bit)
{
	DbmailMessage *m = dbmail_message_new();
	GString *s = g_string_new(raw_message_koi);

	m = dbmail_message_init_with_string(m, s);
	g_string_free(s,TRUE);

	dbmail_message_store(m);
	dbmail_message_free(m);

	/* */
	m = dbmail_message_new();
	s = g_string_new(encoded_message_utf8);
	m = dbmail_message_init_with_string(m, s);
	g_string_free(s, TRUE);

	dbmail_message_store(m);
	dbmail_message_free(m);
}
END_TEST

START_TEST(test_dbmail_message_cache_headers)
{
	DbmailMessage *m = dbmail_message_new();
	char *s = g_new0(char,20);
	GString *j =  g_string_new(multipart_message);
	m = dbmail_message_init_with_string(m,j);
	dbmail_message_set_header(m, 
			"References", 
			"<20050326155326.1afb0377@ibook.linuks.mine.nu> <20050326181954.GB17389@khazad-dum.debian.net> <20050326193756.77747928@ibook.linuks.mine.nu> ");
	dbmail_message_store(m);
	dbmail_message_free(m);
	g_string_free(j,TRUE);

	sprintf(s,"%.*s",10,"abcdefghijklmnopqrstuvwxyz");
	fail_unless(MATCH(s,"abcdefghij"),"string truncate failed");
	g_free(s);

	m = dbmail_message_new();
	j = g_string_new(multipart_message);
	m = dbmail_message_init_with_string(m,j);
	dbmail_message_set_header(m,
			"Subject",
			"=?utf-8?Q?[xxxxxxxxxxxxxxxxxx.xx_0000747]:_=C3=84nderungen_an_der_Artikel?= =?utf-8?Q?-Detailseite?="
			);
	dbmail_message_store(m);
	dbmail_message_free(m);
	g_string_free(j,TRUE);
}
END_TEST

START_TEST(test_dbmail_message_get_header_addresses)
{
	GList * result;
	GString *s;
	DbmailMessage *m;

	s = g_string_new(multipart_message);
	m = dbmail_message_new();
	m = dbmail_message_init_with_string(m,s);
	
	result = dbmail_message_get_header_addresses(m, "Cc");
	result = g_list_first(result);

	fail_unless(result != NULL, "dbmail_message_get_header_addresses failed");
	fail_unless(g_list_length(result)==2,"dbmail_message_get_header_addresses failed");
	fail_unless(strcmp((char *)result->data,"vol@inter7.com")==0, "dbmail_message_get_header_addresses failed");

	g_list_destroy(result);
	dbmail_message_free(m);
	g_string_free(s,TRUE);
}
END_TEST

START_TEST(test_dbmail_message_get_header_repeated)
{
	GTuples *headers;
	GString *s;
	DbmailMessage *m;

	s = g_string_new(multipart_message);
	m = dbmail_message_new();
	m = dbmail_message_init_with_string(m,s);
	
	headers = dbmail_message_get_header_repeated(m, "Received");

	fail_unless(headers != NULL, "dbmail_message_get_header_repeated failed");
	fail_unless(headers->len==2, "dbmail_message_get_header_repeated failed");
	
	g_tuples_destroy(headers);

	headers = dbmail_message_get_header_repeated(m, "received");

	fail_unless(headers != NULL, "dbmail_message_get_header_repeated failed");
	fail_unless(headers->len==2, "dbmail_message_get_header_repeated failed");
	
	g_tuples_destroy(headers);

	dbmail_message_free(m);
	g_string_free(s,TRUE);
}
END_TEST


START_TEST(test_dbmail_message_construct)
{
	const gchar *sender = "foo@bar.org";
	const gchar *subject = "Some test";
	const gchar *recipient = "<bar@foo.org> Bar";
	gchar *body = g_strdup("\ntesting\n\n�����\n\n");
	gchar *expect = g_strdup("From: foo@bar.org\n"
	"Subject: Some test\n"
	"To: bar@foo.org\n"
	"MIME-Version: 1.0\n"
	"Content-Type: text/plain; charset=utf-8\n"
	"Content-Transfer-Encoding: base64\n"
	"\n"
	"CnRlc3RpbmcKCuHh4eHk");
	gchar *result;

	DbmailMessage *message = dbmail_message_new();
	message = dbmail_message_construct(message,recipient,sender,subject,body);
	result = dbmail_message_to_string(message);
	fail_unless(MATCH(expect,result),"dbmail_message_construct failed\n%s\n%s", expect, result);
	dbmail_message_free(message);
	g_free(body);
	g_free(result);
	g_free(expect);

	body = g_strdup("Mit freundlichen Gr=C3=BC=C3=9Fen");
	message = dbmail_message_new();
	message = dbmail_message_construct(message,recipient,sender,subject,body);
	//printf ("%s\n", dbmail_message_to_string(message));
	dbmail_message_free(message);
	g_free(body);
}
END_TEST

START_TEST(test_encoding)
{
	char *raw, *enc, *dec;

	raw = g_strdup( "Kristoffer Br�nemyr");
	enc = g_mime_utils_header_encode_phrase((char *)raw);
	dec = g_mime_utils_header_decode_phrase((char *)enc);
	fail_unless(MATCH(raw,dec),"decode/encode failed");
	g_free(raw);
	g_free(dec);
	g_free(enc);
}
END_TEST

START_TEST(test_dbmail_message_get_size)
{
	DbmailMessage *m;
	size_t i, j;

	/* */
	m = dbmail_message_new();
	m = dbmail_message_init_with_string(m, g_string_new(rfc822));

	i = dbmail_message_get_size(m, FALSE);
	fail_unless(i==277, "dbmail_message_get_size failed");
	j = dbmail_message_get_size(m, TRUE);
	fail_unless(j==289, "dbmail_message_get_size failed");

	dbmail_message_free(m);

	/* */
	m = dbmail_message_new();
	m = dbmail_message_init_with_string(m, g_string_new("From: paul\n\n"));

	i = dbmail_message_get_size(m, FALSE);
	fail_unless(i==12, "dbmail_message_get_size failed [%d]", i);
	j = dbmail_message_get_size(m, TRUE);
	fail_unless(j==14, "dbmail_message_get_size failed [%d]", j);

	dbmail_message_free(m);

}
END_TEST

Suite *dbmail_message_suite(void)
{
	Suite *s = suite_create("Dbmail Message");
	TCase *tc_message = tcase_create("Message");
	
	suite_add_tcase(s, tc_message);
	tcase_add_checked_fixture(tc_message, setup, teardown);

	tcase_add_test(tc_message, test_dbmail_message_new);
	tcase_add_test(tc_message, test_dbmail_message_set_class);
	tcase_add_test(tc_message, test_dbmail_message_get_class);
	tcase_add_test(tc_message, test_dbmail_message_get_internal_date);
	tcase_add_test(tc_message, test_g_mime_object_get_body);
	tcase_add_test(tc_message, test_dbmail_message_store);
	tcase_add_test(tc_message, test_dbmail_message_store2);
	tcase_add_test(tc_message, test_dbmail_message_retrieve);
	tcase_add_test(tc_message, test_dbmail_message_init_with_string);
	tcase_add_test(tc_message, test_dbmail_message_to_string);
//	tcase_add_test(tc_message, test_dbmail_message_init_with_stream);
	tcase_add_test(tc_message, test_dbmail_message_hdrs_to_string);
	tcase_add_test(tc_message, test_dbmail_message_body_to_string);
	tcase_add_test(tc_message, test_dbmail_message_set_header);
	tcase_add_test(tc_message, test_dbmail_message_set_header);
	tcase_add_test(tc_message, test_dbmail_message_get_header);
	tcase_add_test(tc_message, test_dbmail_message_cache_headers);
	tcase_add_test(tc_message, test_dbmail_message_free);
	tcase_add_test(tc_message, test_dbmail_message_encoded);
	tcase_add_test(tc_message, test_dbmail_message_8bit);
	tcase_add_test(tc_message, test_dbmail_message_get_header_addresses);
	tcase_add_test(tc_message, test_dbmail_message_get_header_repeated);
	tcase_add_test(tc_message, test_dbmail_message_construct);
	tcase_add_test(tc_message, test_dbmail_message_get_size);
	tcase_add_test(tc_message, test_encoding);
	return s;
}

int main(void)
{
	int nf;
	g_mime_init(0);
	Suite *s = dbmail_message_suite();
	SRunner *sr = srunner_create(s);
	srunner_run_all(sr, CK_NORMAL);
	nf = srunner_ntests_failed(sr);
	srunner_free(sr);
	g_mime_shutdown();
	return (nf == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
	

