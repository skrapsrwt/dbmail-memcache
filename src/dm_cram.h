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

#ifndef CRAM_H
#define CRAM_H

#include <glib.h>

#define T Cram_T

typedef struct T *T;

extern T               Cram_new(void);
extern void            Cram_setChallenge(T, const char *);
extern const gchar *   Cram_getChallenge(T);
extern const gchar *   Cram_getUsername(T);
extern gboolean        Cram_decode(T, const char *); 
extern gboolean        Cram_verify(T, const char *);
extern void            Cram_free(T *);

#undef T

#endif
