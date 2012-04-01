/* config.h.  Generated from config.in by configure.  */
/* config.in.  Generated from configure.in by autoheader.  */

/* Define if building universal (internal helper macro) */
/* #undef AC_APPLE_UNIVERSAL_BUILD */

/* Define if LDAP will be used. */
/* #undef AUTHLDAP */

/* Define to 1 if you have the <ConnectionPool.h> header file. */
#define HAVE_CONNECTIONPOOL_H 1

/* Define to 1 if you have the <Connection.h> header file. */
#define HAVE_CONNECTION_H 1

/* Define to 1 if you have the <crypt.h> header file. */
#define HAVE_CRYPT_H 1

/* Define to 1 if you have the <dlfcn.h> header file. */
#define HAVE_DLFCN_H 1

/* Define to 1 if you have the `dns_lookup' function. */
/* #undef HAVE_DNS_LOOKUP */

/* Define to 1 if you have the `dn_expand' function. */
#define HAVE_DN_EXPAND 1

/* Define to 1 if you have the <endian.h> header file. */
#define HAVE_ENDIAN_H 1

/* Define to 1 if you have the <event.h> header file. */
#define HAVE_EVENT_H 1

/* Define to 1 if you have the <getopt.h> header file. */
#define HAVE_GETOPT_H 1

/* Define to 1 if you have the `getopt_long_only' function. */
#define HAVE_GETOPT_LONG_ONLY 1

/* Define to 1 if you have the <inttypes.h> header file. */
#define HAVE_INTTYPES_H 1

/* Define to 1 if you have the <ldap.h> header file. */
/* #undef HAVE_LDAP_H */

/* ldap_initialize() can be used instead of ldap_init() */
/* #undef HAVE_LDAP_INITIALIZE */

/* Define to 1 if you have the <math.h> header file. */
#define HAVE_MATH_H 1

/* Define to 1 if you have the <memory.h> header file. */
#define HAVE_MEMORY_H 1

/* Define to 1 if you have the <mhash.h> header file. */
#define HAVE_MHASH_H 1

/* Define to 1 if you have the <openssl/ssl.h> header file. */
#define HAVE_OPENSSL_SSL_H 1

/* Define to 1 if you have the <PreparedStatement.h> header file. */
#define HAVE_PREPAREDSTATEMENT_H 1

/* Define to 1 if you have the <ResultSet.h> header file. */
#define HAVE_RESULTSET_H 1

/* Define to 1 if you have the <SQLException.h> header file. */
#define HAVE_SQLEXCEPTION_H 1

/* Define to 1 if you have the <stdint.h> header file. */
#define HAVE_STDINT_H 1

/* Define to 1 if you have the <stdlib.h> header file. */
#define HAVE_STDLIB_H 1

/* Define to 1 if you have the <strings.h> header file. */
#define HAVE_STRINGS_H 1

/* Define to 1 if you have the <string.h> header file. */
#define HAVE_STRING_H 1

/* Define to 1 if you have the <sys/stat.h> header file. */
#define HAVE_SYS_STAT_H 1

/* Define to 1 if you have the <sys/types.h> header file. */
#define HAVE_SYS_TYPES_H 1

/* Define to 1 if you have the <unistd.h> header file. */
#define HAVE_UNISTD_H 1

/* Define to 1 if you have the <URL.h> header file. */
#define HAVE_URL_H 1

/* Define to the sub-directory in which libtool stores uninstalled libraries.
   */
#define LT_OBJDIR ".libs/"

/* Name of package */
#define PACKAGE "dbmail"

/* Define to the address where bug reports for this package should be sent. */
#define PACKAGE_BUGREPORT "dbmail@dbmail.org"

/* Define to the full name of this package. */
#define PACKAGE_NAME "dbmail"

/* Define to the full name and version of this package. */
#define PACKAGE_STRING "dbmail 3.0.2"

/* Define to the one symbol short name of this package. */
#define PACKAGE_TARNAME "dbmail"

/* Define to the home page for this package. */
#define PACKAGE_URL ""

/* Define to the version of this package. */
#define PACKAGE_VERSION "3.0.2"

/* Prefix to the installed path */
#define PREFIX "/usr/local"

/* Define if Sieve sorting will be used. */
/* #undef SIEVE */

/* Define to 1 if you have the ANSI C header files. */
#define STDC_HEADERS 1

/* Define if our local getopt will be used. */
/* #undef USE_DM_GETOPT */

/* Version number of package */
#define VERSION "3.0.2"

/* Define WORDS_BIGENDIAN to 1 if your processor stores words with the most
   significant byte first (like Motorola and SPARC, unlike Intel). */
#if defined AC_APPLE_UNIVERSAL_BUILD
# if defined __BIG_ENDIAN__
#  define WORDS_BIGENDIAN 1
# endif
#else
# ifndef WORDS_BIGENDIAN
/* #  undef WORDS_BIGENDIAN */
# endif
#endif

/* 'Defined GNU SOURCE' */
#define _GNU_SOURCE 'TRUE'

/* Define to empty if `const' does not conform to ANSI C. */
/* #undef const */

/* Define to `__inline__' or `__inline' if that's what the C compiler
   calls it, or to nothing if 'inline' is not supported under any name.  */
#ifndef __cplusplus
/* #undef inline */
#endif
