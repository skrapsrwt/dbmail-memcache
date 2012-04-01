/*
  
 Copyright (c) 2008-2011 NFG Net Facilities Group BV support@nfg.nl

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

#include "dbmail.h"
#include "dm_cram.h"

#define THIS_MODULE "CRAM"

/*
 */

#define T Cram_T

struct T {
	char *challenge;
	char *username;
	char *response;
};

T Cram_new(void)
{
	T C;
	C = g_malloc0(sizeof(*C));
	return C;
}

static gchar * init_challenge(void)
{
	char name[255];
	memset(name,0, sizeof(name));
	if (getdomainname(name,sizeof(name))) {
		if (gethostname(name, sizeof(name)))
			strcpy(name,"(none)");
	}
	return g_mime_utils_generate_message_id(name);
}

void Cram_setChallenge(T C, const char *challenge)
{
	if (! challenge)
		C->challenge = init_challenge();
	else
		C->challenge = g_strdup(challenge);
}

const gchar * Cram_getChallenge(T C)
{
	if (! C->challenge)
		Cram_setChallenge(C, NULL);
	return (const gchar *)C->challenge;
}

const gchar * Cram_getUsername(T C)
{
	assert(C->username);
	return C->username;
}

gboolean Cram_decode(T C, const char * response)
{
	gsize len = 0, space = 0;
	gchar *s = dm_base64_decode(response, &len);
	space = stridx((const char *)s,' ');
	if (space == len)
		return FALSE;
	C->username = g_strndup(s, space);
	space++;
	C->response = g_strndup(s + space, len - space);
	g_free(s);
	return TRUE;
}

gboolean Cram_verify(T C, const char *credentials)
{
	gboolean r = FALSE;
	unsigned char d[1024];
	memset(d,0,sizeof(d));

	MHASH ctx = mhash_hmac_init(MHASH_MD5, (void *)credentials, strlen(credentials), mhash_get_hash_pblock(MHASH_MD5));
	mhash(ctx, C->challenge, strlen(C->challenge));
	mhash_hmac_deinit(ctx, (gpointer)d);
	char *result = dm_digest(d, MHASH_MD5);

	if (strncmp(C->response, result, strlen(C->response))==0)
		r = TRUE;

	g_free(result);
	return r;
}

void Cram_free(T *C)
{
	T c = *C;
	if (c->challenge) g_free(c->challenge);
	if (c->username) g_free(c->username);
	if (c->response) g_free(c->response);
	if (c) g_free(c);
}



