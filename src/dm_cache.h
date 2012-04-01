/*
  
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

#ifndef DM_CACHE_H
#define DM_CACHE_H

#include "dm_memblock.h"

#define IMAP_CACHE_MEMDUMP 1
#define IMAP_CACHE_TMPDUMP 2

#define T Cache_T

typedef struct T *T;

extern T     Cache_new(void);
extern u64_t Cache_set_dump(T C, char *buf, int dumptype);
extern void  Cache_clear(T C);
extern u64_t Cache_update(T C, DbmailMessage *message, int filter);
extern u64_t Cache_get_size(T C);
extern void  Cache_set_memdump(T C, Mem_T M);
extern Mem_T Cache_get_memdump(T C);
extern void  Cache_set_tmpdump(T C, Mem_T M);
extern Mem_T Cache_get_tmpdump(T C);
extern void  Cache_free(T *C);

#undef T
#endif
