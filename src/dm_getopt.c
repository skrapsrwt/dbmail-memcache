/*
 *  Renamed to dm_getopt because MySQL keeps putting things in my_ space.
 *
 *  dm_getopt.c - my re-implementation of getopt.
 *  Copyright 1997, 2000, 2001, 2002, Benjamin Sittler
 *
 *  Permission is hereby granted, free of charge, to any person
 *  obtaining a copy of this software and associated documentation
 *  files (the "Software"), to deal in the Software without
 *  restriction, including without limitation the rights to use, copy,
 *  modify, merge, publish, distribute, sublicense, and/or sell copies
 *  of the Software, and to permit persons to whom the Software is
 *  furnished to do so, subject to the following conditions:
 *  
 *  The above copyright notice and this permission notice shall be
 *  included in all copies or substantial portions of the Software.
 *  
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 *  EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 *  MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 *  NONINFRINGEMENT.  IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 *  HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 *  WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 *  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 *  DEALINGS IN THE SOFTWARE.
 */

#include "dbmail.h"

int dm_optind=1, dm_opterr=1, dm_optopt=0;
char *dm_optarg=0;

/* this is the plain old UNIX getopt, with GNU-style extensions. */
/* if you're porting some piece of UNIX software, this is all you need. */
/* this supports GNU-style permution and optional arguments */

int dm_getopt(int argc, char * argv[], const char *opts)
{
  static int charind=0;
  const char *s;
  char mode, colon_mode;
  int off = 0, opt = -1;

  if(getenv("POSIXLY_CORRECT")) colon_mode = mode = '+';
  else {
    if((colon_mode = *opts) == ':') off ++;
    if(((mode = opts[off]) == '+') || (mode == '-')) {
      off++;
      if((colon_mode != ':') && ((colon_mode = opts[off]) == ':'))
        off ++;
    }
  }
  dm_optarg = 0;
  if(charind) {
    dm_optopt = argv[dm_optind][charind];
    for(s=opts+off; *s; s++) if(dm_optopt == *s) {
      charind++;
      if((*(++s) == ':') || ((dm_optopt == 'W') && (*s == ';'))) {
        if(argv[dm_optind][charind]) {
          dm_optarg = &(argv[dm_optind++][charind]);
          charind = 0;
        } else if(*(++s) != ':') {
          charind = 0;
          if(++dm_optind >= argc) {
            if(dm_opterr) fprintf(stderr,
                                "%s: option requires an argument -- %c\n",
                                argv[0], dm_optopt);
            opt = (colon_mode == ':') ? ':' : '?';
            goto dm_getopt_ok;
          }
          dm_optarg = argv[dm_optind++];
        }
      }
      opt = dm_optopt;
      goto dm_getopt_ok;
    }
    if(dm_opterr) fprintf(stderr,
                        "%s: illegal option -- %c\n",
                        argv[0], dm_optopt);
    opt = '?';
    if(argv[dm_optind][++charind] == '\0') {
      dm_optind++;
      charind = 0;
    }
  dm_getopt_ok:
    if(charind && ! argv[dm_optind][charind]) {
      dm_optind++;
      charind = 0;
    }
  } else if((dm_optind >= argc) ||
             ((argv[dm_optind][0] == '-') &&
              (argv[dm_optind][1] == '-') &&
              (argv[dm_optind][2] == '\0'))) {
    dm_optind++;
    opt = -1;
  } else if((argv[dm_optind][0] != '-') ||
             (argv[dm_optind][1] == '\0')) {
    char *tmp;
    int i, j, k;

    if(mode == '+') opt = -1;
    else if(mode == '-') {
      dm_optarg = argv[dm_optind++];
      charind = 0;
      opt = 1;
    } else {
      for(i=j=dm_optind; i<argc; i++) if((argv[i][0] == '-') &&
                                        (argv[i][1] != '\0')) {
        dm_optind=i;
        opt=dm_getopt(argc, argv, opts);
        while(i > j) {
          tmp=argv[--i];
          for(k=i; k+1<dm_optind; k++) argv[k]=argv[k+1];
          argv[--dm_optind]=tmp;
        }
        break;
      }
      if(i == argc) opt = -1;
    }
  } else {
    charind++;
    opt = dm_getopt(argc, argv, opts);
  }
  if (dm_optind > argc) dm_optind = argc;
  return opt;
}

/* this is the extended getopt_long{,_only}, with some GNU-like
 * extensions. Implements _getopt_internal in case any programs
 * expecting GNU libc getopt call it.
 */

int _dm_getopt_internal(int argc, char * argv[], const char *shortopts,
                     const struct option *longopts, int *longind,
                     int long_only)
{
  char mode, colon_mode = *shortopts;
  int shortoff = 0, opt = -1;

  if(getenv("POSIXLY_CORRECT")) colon_mode = mode = '+';
  else {
    if((colon_mode = *shortopts) == ':') shortoff ++;
    if(((mode = shortopts[shortoff]) == '+') || (mode == '-')) {
      shortoff++;
      if((colon_mode != ':') && ((colon_mode = shortopts[shortoff]) == ':'))
        shortoff ++;
    }
  }
  dm_optarg = 0;
  if((dm_optind >= argc) ||
      ((argv[dm_optind][0] == '-') &&
       (argv[dm_optind][1] == '-') &&
       (argv[dm_optind][2] == '\0'))) {
    dm_optind++;
    opt = -1;
  } else if((argv[dm_optind][0] != '-') ||
            (argv[dm_optind][1] == '\0')) {
    char *tmp;
    int i, j, k;

    opt = -1;
    if(mode == '+') return -1;
    else if(mode == '-') {
      dm_optarg = argv[dm_optind++];
      return 1;
    }
    for(i=j=dm_optind; i<argc; i++) if((argv[i][0] == '-') &&
                                    (argv[i][1] != '\0')) {
      dm_optind=i;
      opt=_dm_getopt_internal(argc, argv, shortopts,
                              longopts, longind,
                              long_only);
      while(i > j) {
        tmp=argv[--i];
        for(k=i; k+1<dm_optind; k++)
          argv[k]=argv[k+1];
        argv[--dm_optind]=tmp;
      }
      break;
    }
  } else if((!long_only) && (argv[dm_optind][1] != '-'))
    opt = dm_getopt(argc, argv, shortopts);
  else {
    int charind, offset;
    int found = 0, ind, hits = 0;

    if(((dm_optopt = argv[dm_optind][1]) != '-') && ! argv[dm_optind][2]) {
      int c;
      
      ind = shortoff;
      while((c = shortopts[ind++])) {
        if(((shortopts[ind] == ':') ||
            ((c == 'W') && (shortopts[ind] == ';'))) &&
           (shortopts[++ind] == ':'))
          ind ++;
        if(dm_optopt == c) return dm_getopt(argc, argv, shortopts);
      }
    }
    offset = 2 - (argv[dm_optind][1] != '-');
    for(charind = offset;
        (argv[dm_optind][charind] != '\0') &&
          (argv[dm_optind][charind] != '=');
        charind++);
    for(ind = 0; longopts[ind].name && !hits; ind++)
      if((strlen(longopts[ind].name) == (size_t) (charind - offset)) &&
         (strncmp(longopts[ind].name,
                  argv[dm_optind] + offset, charind - offset) == 0))
        found = ind, hits++;
    if(!hits) for(ind = 0; longopts[ind].name; ind++)
      if(strncmp(longopts[ind].name,
                 argv[dm_optind] + offset, charind - offset) == 0)
        found = ind, hits++;
    if(hits == 1) {
      opt = 0;

      if(argv[dm_optind][charind] == '=') {
        if(longopts[found].has_arg == 0) {
          opt = '?';
          if(dm_opterr) fprintf(stderr,
                             "%s: option `--%s' doesn't allow an argument\n",
                             argv[0], longopts[found].name);
        } else {
          dm_optarg = argv[dm_optind] + ++charind;
          charind = 0;
        }
      } else if(longopts[found].has_arg == 1) {
        if(++dm_optind >= argc) {
          opt = (colon_mode == ':') ? ':' : '?';
          if(dm_opterr) fprintf(stderr,
                             "%s: option `--%s' requires an argument\n",
                             argv[0], longopts[found].name);
        } else dm_optarg = argv[dm_optind];
      }
      if(!opt) {
        if (longind) *longind = found;
        if(!longopts[found].flag) opt = longopts[found].val;
        else *(longopts[found].flag) = longopts[found].val;
      }
      dm_optind++;
    } else if(!hits) {
      if(offset == 1) opt = dm_getopt(argc, argv, shortopts);
      else {
        opt = '?';
        if(dm_opterr) fprintf(stderr,
                           "%s: unrecognized option `%s'\n",
                           argv[0], argv[dm_optind++]);
      }
    } else {
      opt = '?';
      if(dm_opterr) fprintf(stderr,
                         "%s: option `%s' is ambiguous\n",
                         argv[0], argv[dm_optind++]);
    }
  }
  if (dm_optind > argc) dm_optind = argc;
  return opt;
}

int dm_getopt_long(int argc, char * argv[], const char *shortopts,
                const struct option *longopts, int *longind)
{
  return _dm_getopt_internal(argc, argv, shortopts, longopts, longind, 0);
}

int dm_getopt_long_only(int argc, char * argv[], const char *shortopts,
                const struct option *longopts, int *longind)
{
  return _dm_getopt_internal(argc, argv, shortopts, longopts, longind, 1);
}
