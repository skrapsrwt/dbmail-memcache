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
#ifndef _DM_CIDR_H
#define _DM_CIDR_H
/*
 * provide some CIDR utilities.
 */
#define T Cidr_T

typedef struct T *T;

extern T      cidr_new(const char *str);
extern char * cidr_repr(T);
extern int    cidr_match(T, T);
extern void   cidr_free(T *);

#undef T

#endif
