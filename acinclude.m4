
AC_DEFUN([DM_MSG_CONFIGURE_START], [dnl
AC_MSG_RESULT([
This is dbmail's GNU configure script.
])
])

AC_DEFUN([DM_MSG_CONFIGURE_RESULTS], [dnl
AC_MSG_RESULT([

 DM_LOGDIR:                 $DM_LOGDIR
 DM_CONFDIR:                $DM_CONFDIR
 DM_STATEDIR:               $DM_STATEDIR
 USE_DM_GETOPT:             $USE_DM_GETOPT
 CFLAGS:                    $CFLAGS
 GLIB:                      $ac_glib_libs
 GMIME:                     $ac_gmime_libs
 SIEVE:                     $SIEVEINC$SIEVELIB
 LDAP:                      $LDAPINC$LDAPLIB
 SHARED:                    $enable_shared
 STATIC:                    $enable_static
 CHECK:                     $CHECK_LIBS
 SOCKETS:                   $SOCKETLIB
 MATH:                      $MATHLIB
 MHASH:                     $MHASHLIB
 LIBEVENT:                  $EVENTLIB
 OPENSSL:                   $SSLLIB
 ZDB:                       $ZDBLIB

])
])


AC_DEFUN([DM_DEFINES],[dnl
AC_ARG_WITH(logdir,
	[  --with-logdir           use logdir for logfiles],
	logdirname="$withval")
if test "x$logdirname" = "x"; then
	DM_LOGDIR="/var/log"
else
	DM_LOGDIR="$logdirname"
fi
if test "x$localstatedir" = 'x${prefix}/var'; then
	DM_STATEDIR='/var/run'
else
	DM_STATEDIR=$localstatedir
fi
if test "x$sysconfdir" = 'x${prefix}/etc'; then
	DM_CONFDIR='/etc'
else
	DM_CONFDIR=$sysconfdir
fi
if test "x$prefix" = "xNONE"; then
	AC_DEFINE_UNQUOTED([PREFIX], "$ac_default_prefix", [Prefix to the installed path])
else
	AC_DEFINE_UNQUOTED([PREFIX], "$prefix", [Prefix to the installed path])
fi
])
	
AC_DEFUN([DM_SET_SHARED_OR_STATIC], [dnl
if test [ "$enable_shared" = "yes" -a "$enable_static" = "yes" ]; then
     AC_MSG_ERROR([
     You cannot enable both shared and static build.
     Please choose only one to enable.
])
fi
if test [ "$enable_shared" = "no" -a "$enable_static" = "no" ]; then
	enable_shared="yes"
fi

])

AC_DEFUN([DM_SIEVE_INC],[
    AC_COMPILE_IFELSE([
    AC_LANG_PROGRAM([[
        #define NULL 0
        #include <sieve2.h>]])],
    [$1],
    [$2])
])
AC_DEFUN([DM_SIEVE_LIB],[
    AC_LINK_IFELSE([
    AC_LANG_PROGRAM([[
        #define NULL 0
        #include <sieve2.h>]])],
    [$1],
    [$2])
])

AC_DEFUN([DM_SIEVE_CONF], [dnl
WARN=0

AC_ARG_WITH(sieve,[  --with-sieve=PATH	  path to libSieve base directory (e.g. /usr/local or /usr)],
	[lookforsieve="$withval"],[lookforsieve="no"])

SORTALIB="modules/.libs/libsort_null.a"
SORTLTLIB="modules/libsort_null.la"

if test [ "x$lookforsieve" != "xno" ]; then
    if test [ "x$lookforsieve" != "xyes" ]; then
        sieveprefixes=$lookforsieve
    fi
    AC_MSG_CHECKING([for libSieve headers])
    STOP_LOOKING_FOR_SIEVE=""
    while test [ -z $STOP_LOOKING_FOR_SIEVE ]; do 

	if test [ "x$lookforsieve" = "xyes" ]; then
            DM_SIEVE_INC([SIEVEINC=""], [SIEVEINC="failed"])
            if test [ "x$SIEVEINC" != "xfailed" ]; then
                break
            fi
        fi
 
        for TEST_PATH in $sieveprefixes; do
	    TEST_PATH="$TEST_PATH/include"
            SAVE_CFLAGS=$CFLAGS
            CFLAGS="$CFLAGS -I$TEST_PATH"
            DM_SIEVE_INC([SIEVEINC="-I$TEST_PATH"], [SIEVEINC="failed"])
            CFLAGS=$SAVE_CFLAGS
            if test [ "x$SIEVEINC" != "xfailed" ]; then
                break 2
            fi
        done

        STOP_LOOKING_FOR_SIEVE="done"
    done

    if test [ "x$SIEVEINC" = "xfailed" ]; then
        AC_MSG_ERROR([Could not find libSieve headers.])
    else
        AC_MSG_RESULT($SIEVEINC)
    fi

    AC_MSG_CHECKING([for libSieve libraries])
    STOP_LOOKING_FOR_SIEVE=""
    while test [ -z $STOP_LOOKING_FOR_SIEVE ]; do 

        if test [ "x$lookforsieve" = "xyes" ]; then
            DM_SIEVE_LIB([SIEVELIB="-lsieve"], [SIEVELIB="failed"])
            if test [ "x$SIEVELIB" != "xfailed" ]; then
                break
            fi
        fi
 
        for TEST_PATH in $sieveprefixes; do
	    TEST_PATH="$TEST_PATH/lib"
            SAVE_CFLAGS=$CFLAGS
            CFLAGS="$CFLAGS -L$TEST_PATH $SIEVEINC"
            DM_SIEVE_LIB([SIEVELIB="-L$TEST_PATH -lsieve"], [SIEVELIB="failed"])
            CFLAGS=$SAVE_CFLAGS
            if test [ "x$SIEVELIB" != "xfailed" ]; then
                break 2
            fi
        done

        STOP_LOOKING_FOR_SIEVE="done"
    done

    if test [ "x$SIEVELIB" = "xfailed" ]; then
        AC_MSG_ERROR([Could not find libSieve library.])
    else
        dnl Found it, set the settings.
        AC_MSG_RESULT($SIEVELIB)
        AC_DEFINE([SIEVE], 1, [Define if Sieve sorting will be used.])
        AC_SUBST(SIEVELIB)
        AC_SUBST(SIEVEINC)
        SORTALIB="modules/.libs/libsort_sieve.a"
        SORTLTLIB="modules/libsort_sieve.la"
    fi
fi
])

AC_DEFUN([DM_LDAP_CONF], [dnl
WARN=0

dnl --with-auth-ldap is deprecated as of DBMail 2.2.2
AC_ARG_WITH(auth-ldap,[  --with-auth-ldap=PATH	  deprecated, use --with-ldap],
	[lookforauthldap="$withval"],[lookforauthldap="no"])

AC_ARG_WITH(ldap,[  --with-ldap=PATH	  path to LDAP base directory (e.g. /usr/local or /usr)],
	[lookforldap="$withval"],[lookforldap="no"])

dnl Set the default auth modules to sql, as
dnl the user may not have asked for LDAP at all.
AUTHALIB="modules/.libs/libauth_sql.a"
AUTHLTLIB="modules/libauth_sql.la"

dnl Go looking for the LDAP headers and libraries.
if ( test [ "x$lookforldap" != "xno" ] || test [ "x$lookforauthldap" != "xno" ] ); then

    dnl Support the deprecated --with-auth-ldap per comment above.
    if ( test [ "x$lookforauthldap" != "xyes" ] && test [ "x$lookforauthldap" != "xno" ] ); then
        lookforldap=$lookforauthldap
    fi

    dnl We were given a specific path. Only look there.
    if ( test [ "x$lookforldap" != "xyes" ] && test [ "x$lookforldap" != "xno" ] ); then
        ldapprefixes=$lookforldap
    fi

    STOP_LOOKING_FOR_LDAP=""
    while test [ -z $STOP_LOOKING_FOR_LDAP ]; do 

        dnl See if we already have the paths we need in the environment.
	dnl ...but only if --with-ldap was given without a specific path.
	if ( test [ "x$lookforldap" = "xyes" ] || test [ "x$lookforauthldap" = "xyes" ] ); then
            AC_CHECK_HEADERS([ldap.h],[LDAPINC=""], [LDAPINC="failed"])
            if test [ "x$LDAPINC" != "xfailed" ]; then
                break
            fi
        fi
 
        dnl Explicitly test paths from --with-ldap or configure.in
        for TEST_PATH in $ldapprefixes; do
	    TEST_PATH="$TEST_PATH/include"
            SAVE_CFLAGS=$CFLAGS
            CFLAGS="$CFLAGS -I$TEST_PATH"
            AC_CHECK_HEADERS([ldap.h],[LDAPINC="-I$TEST_PATH"], [LDAPINC="failed"])
            CFLAGS=$SAVE_CFLAGS
            if test [ "x$LDAPINC" != "xfailed" ]; then
                break 2
            fi
        done

        STOP_LOOKING_FOR_LDAP="done"
    done

    if test [ "x$LDAPINC" = "xfailed" ]; then
        AC_MSG_ERROR([Could not find LDAP headers.])
    fi

    STOP_LOOKING_FOR_LDAP=""
    while test [ -z $STOP_LOOKING_FOR_LDAP ]; do 

        dnl See if we already have the paths we need in the environment.
	dnl ...but only if --with-ldap was given without a specific path.
        if ( test [ "x$lookforldap" = "xyes" ] || test [ "x$lookforauthldap" = "xyes" ] ); then
            AC_CHECK_HEADERS([ldap.h],[LDAPLIB="-lldap"], [LDAPLIB="failed"])
            if test [ "x$LDAPLIB" != "xfailed" ]; then
                break
            fi
        fi
 
        dnl Explicitly test paths from --with-ldap or configure.in
        for TEST_PATH in $ldapprefixes; do
	    TEST_PATH="$TEST_PATH/lib"
            SAVE_CFLAGS=$CFLAGS
	    dnl The headers might be in a funny place, so we need to use -Ipath
            CFLAGS="$CFLAGS -L$TEST_PATH $LDAPINC"
            AC_CHECK_HEADERS([ldap.h],[LDAPLIB="-L$TEST_PATH -lldap"], [LDAPLIB="failed"])
            CFLAGS=$SAVE_CFLAGS
            if test [ "x$LDAPLIB" != "xfailed" ]; then
                break 2
            fi
        done

        STOP_LOOKING_FOR_LDAP="done"
    done

    if test [ "x$LDAPLIB" = "xfailed" ]; then
        AC_MSG_ERROR([Could not find LDAP library.])
    else
        AC_DEFINE([AUTHLDAP], 1, [Define if LDAP will be used.])
        AC_SEARCH_LIBS(ldap_initialize, ldap, AC_DEFINE([HAVE_LDAP_INITIALIZE], 1, [ldap_initialize() can be used instead of ldap_init()]))
        AC_SUBST(LDAPLIB)
        AC_SUBST(LDAPINC)
        AUTHALIB="modules/.libs/libauth_ldap.a"
        AUTHLTLIB="modules/libauth_ldap.la"
    fi
fi
])

AC_DEFUN([DM_CHECK_ZDB], [dnl

	AC_ARG_WITH(zdb,[  --with-zdb=PATH	  path to libzdb base directory (e.g. /usr/local or /usr)],
		[lookforzdb="$withval"],[lookforzdb="no"])

	if test [ "x$lookforzdb" = "xno" ] ; then
		CFLAGS="$CFLAGS -I${prefix}/include/zdb"
	else
		CFLAGS="$CFLAGS -I${lookforzdb}/include/zdb"
	fi

	AC_CHECK_HEADERS([URL.h ResultSet.h PreparedStatement.h Connection.h ConnectionPool.h SQLException.h],
		[ZDBLIB="-lzdb"], 
		[ZDBLIB="failed"],
	[[
#include <URL.h>
#include <ResultSet.h>
#include <PreparedStatement.h>
#include <Connection.h>
#include <ConnectionPool.h>
#include <SQLException.h>	
	]])
	if test [ "x$ZDBLIB" = "xfailed" ]; then
		AC_MSG_ERROR([Could not find ZDB library.])
	else
		LDFLAGS="$LDFLAGS $ZDBLIB"
	fi
])

AC_DEFUN([DM_SET_SQLITECREATE], [dnl
	SQLITECREATE=`sed -e 's/\"/\\\"/g' -e 's/^/\"/' -e 's/$/\\\n\"/' -e '$!s/$/ \\\\/'  sql/sqlite/create_tables.sqlite`
])

AC_DEFUN([DM_CHECK_MATH], [dnl
	AC_CHECK_HEADERS([math.h],[MATHLIB="-lm"], [MATHLIB="failed"])
	if test [ "x$MATHLIB" = "xfailed" ]; then
		AC_MSG_ERROR([Could not find MATH library.])
	else
		LDFLAGS="$LDFLAGS $MATHLIB"
	fi
])

AC_DEFUN([DM_CHECK_MHASH], [dnl
	AC_CHECK_HEADERS([mhash.h],[MHASHLIB="-lmhash"], [MHASHLIB="failed"])
	if test [ "x$MHASHLIB" = "xfailed" ]; then
		AC_MSG_ERROR([Could not find MHASH library.])
	else
		LDFLAGS="$LDFLAGS $MHASHLIB"
	fi
])

AC_DEFUN([DM_CHECK_EVENT], [
	AC_CHECK_HEADERS([event.h], [EVENTLIB="-levent"],[EVENTLIB="failed"], [#include <sys/types.h>])
	if test [ "x$EVENTLIB" = "xfailed" ]; then
		AC_MSG_ERROR([Could not find EVENT library.])
	else
		LDFLAGS="$LDFLAGS $EVENTLIB"
	fi
])

AC_DEFUN([DM_CHECK_SSL], [
	AC_CHECK_HEADERS([openssl/ssl.h], [SSLLIB="-lssl"],[SSLLIB="failed"])
	if test [ "x$SSLLIB" = "xfailed" ]; then
		AC_MSG_ERROR([Could not find OPENSSL library.])
	else
		LDFLAGS="$LDFLAGS $SSLLIB"
	fi
])

AC_DEFUN([AC_COMPILE_WARNINGS],
[AC_MSG_CHECKING(maximum warning verbosity option)
if test -n "$CXX"; then
	if test "$GXX" = "yes"; then
		ac_compile_warnings_opt='-Wall'
	fi
	CXXFLAGS="$CXXFLAGS $ac_compile_warnings_opt"
	ac_compile_warnings_msg="$ac_compile_warnings_opt for C++"
fi
if test -n "$CC"; then
	if test "$GCC" = "yes"; then
		ac_compile_warnings_opt='-W -Wall -Wpointer-arith -Wstrict-prototypes'
	fi
	CFLAGS="$CFLAGS $ac_compile_warnings_opt"
	ac_compile_warnings_msg="$ac_compile_warnings_msg $ac_compile_warnings_opt for C"
fi

AC_MSG_RESULT($ac_compile_warnings_msg)
unset ac_compile_warnings_msg
unset ac_compile_warnings_opt
])

AC_DEFUN([DM_CHECK_GLIB], [dnl
AC_PATH_PROG(glibconfig,pkg-config)
if test [ -z "$glibconfig" ]
then
	AC_MSG_ERROR([pkg-config executable not found. Make sure pkg-config is in your path])
else
	AC_MSG_CHECKING([GLib headers])
	ac_glib_cflags=`${glibconfig} --cflags glib-2.0 --cflags gmodule-2.0`
	if test -z "$ac_glib_cflags"
	then
		AC_MSG_RESULT([no])
		AC_MSG_ERROR([Unable to locate glib development files])
	fi
 
	CFLAGS="$CFLAGS $ac_glib_cflags"
	AC_MSG_RESULT([$ac_glib_cflags])
        AC_MSG_CHECKING([Glib libraries])
	ac_glib_libs=`${glibconfig} --libs glib-2.0 --libs gmodule-2.0`
	if test -z "$ac_glib_libs"
	then
		AC_MSG_RESULT([no])
		AC_MSG_ERROR([Unable to locate glib libaries])
	fi
 	ac_glib_minvers="2.16"
	AC_MSG_CHECKING([GLib version >= $ac_glib_minvers])
	ac_glib_vers=`${glibconfig}  --atleast-version=$ac_glib_minvers glib-2.0 && echo yes`
	if test -z "$ac_glib_vers"
	then
		AC_MSG_ERROR([At least GLib version $ac_glib_minvers is required.])
	else
		AC_MSG_RESULT([$ac_glib_vers])
	fi


	LDFLAGS="$LDFLAGS $ac_glib_libs"
        AC_MSG_RESULT([$ac_glib_libs])
fi
])

AC_DEFUN([DM_CHECK_GMIME], [dnl
AC_PATH_PROG(gmimeconfig,pkg-config)
if test [ -z "$gmimeconfig" ]
then
	AC_MSG_ERROR([pkg-config executable not found. Make sure pkg-config is in your path])
else
	AC_MSG_CHECKING([GMime headers])
	ac_gmime_cflags=`${gmimeconfig} --cflags gmime-2.4`
	if test -z "$ac_gmime_cflags"
	then
		AC_MSG_RESULT([no])
		AC_MSG_ERROR([Unable to locate gmime development files])
	else
		CFLAGS="$CFLAGS $ac_gmime_cflags"
		AC_MSG_RESULT([$ac_gmime_cflags])
	fi
	
        AC_MSG_CHECKING([GMime libraries])
	ac_gmime_libs=`${gmimeconfig} --libs gmime-2.4`
	if test -z "$ac_gmime_libs"
	then
		AC_MSG_RESULT([no])
		AC_MSG_ERROR([Unable to locate gmime libaries])
	else
		LDFLAGS="$LDFLAGS $ac_gmime_libs"
        	AC_MSG_RESULT([$ac_gmime_libs])
	fi
	ac_gmime_minvers="2.4.6"
	AC_MSG_CHECKING([GMime version >= $ac_gmime_minvers])
	ac_gmime_vers=`${gmimeconfig}  --atleast-version=$ac_gmime_minvers gmime-2.4 && echo yes`
	if test -z "$ac_gmime_vers"
	then
		AC_MSG_ERROR([At least GMime version $ac_gmime_minvers is required.])
	else
		AC_MSG_RESULT([$ac_gmime_vers])
	fi
fi
])

AC_DEFUN([DM_PATH_CHECK],
[
  AC_ARG_WITH(check,
  [  --with-check=PATH       prefix where check is installed [default=auto]],
  [test x"$with_check" = xno && with_check="no"],
  [with_check="no"])
  
if test "x$with_check" != xno; then
  min_check_version=ifelse([$1], ,0.8.2,$1)

  AC_MSG_CHECKING(for check - version >= $min_check_version)

  if test x$with_check = xno; then
    AC_MSG_RESULT(disabled)
    ifelse([$3], , AC_MSG_ERROR([disabling check is not supported]), [$3])
  else
    if test "x$with_check" = xyes; then
      CHECK_CFLAGS=""
      CHECK_LIBS="-lcheck"
    else
      CHECK_CFLAGS="-I$with_check/include"
      CHECK_LIBS="-L$with_check/lib -lcheck"
    fi

    ac_save_CFLAGS="$CFLAGS"
    ac_save_LIBS="$LIBS"

    CFLAGS="$CFLAGS $CHECK_CFLAGS"
    LIBS="$CHECK_LIBS $LIBS"

    rm -f conf.check-test
    AC_TRY_RUN([
#include <stdio.h>
#include <stdlib.h>

#include <check.h>

int main ()
{
  int major, minor, micro;
  char *tmp_version;

  system ("touch conf.check-test");

  /* HP/UX 9 (%@#!) writes to sscanf strings */
  tmp_version = strdup("$min_check_version");
  if (sscanf(tmp_version, "%d.%d.%d", &major, &minor, &micro) != 3) {
     printf("%s, bad version string\n", "$min_check_version");
     return 1;
   }
    
  if ((CHECK_MAJOR_VERSION != check_major_version) ||
      (CHECK_MINOR_VERSION != check_minor_version) ||
      (CHECK_MICRO_VERSION != check_micro_version))
    {
      printf("\n*** The check header file (version %d.%d.%d) does not match\n",
	     CHECK_MAJOR_VERSION, CHECK_MINOR_VERSION, CHECK_MICRO_VERSION);
      printf("*** the check library (version %d.%d.%d).\n",
	     check_major_version, check_minor_version, check_micro_version);
      return 1;
    }

  if ((check_major_version > major) ||
      ((check_major_version == major) && (check_minor_version > minor)) ||
      ((check_major_version == major) && (check_minor_version == minor) && (check_micro_version >= micro)))
    {
      return 0;
    }
  else
    {
      printf("\n*** An old version of check (%d.%d.%d) was found.\n",
             check_major_version, check_minor_version, check_micro_version);
      printf("*** You need a version of check being at least %d.%d.%d.\n", major, minor, micro);
      printf("***\n"); 
      printf("*** If you have already installed a sufficiently new version, this error\n");
      printf("*** probably means that the wrong copy of the check library and header\n");
      printf("*** file is being found. Rerun configure with the --with-check=PATH option\n");
      printf("*** to specify the prefix where the correct version was installed.\n");
    }

  return 1;
}
],, no_check=yes, [echo $ac_n "cross compiling; assumed OK... $ac_c"])

    CFLAGS="$ac_save_CFLAGS"
    LIBS="$ac_save_LIBS"

    if test "x$no_check" = x ; then
      AC_MSG_RESULT(yes)
      ifelse([$2], , :, [$2])
    else
      AC_MSG_RESULT(no)
      if test -f conf.check-test ; then
        :
      else
        echo "*** Could not run check test program, checking why..."
        CFLAGS="$CFLAGS $CHECK_CFLAGS"
        LIBS="$CHECK_LIBS $LIBS"
        AC_TRY_LINK([
#include <stdio.h>
#include <stdlib.h>

#include <check.h>
], ,  [ echo "*** The test program compiled, but did not run. This usually means"
        echo "*** that the run-time linker is not finding check. You'll need to set your"
        echo "*** LD_LIBRARY_PATH environment variable, or edit /etc/ld.so.conf to point"
        echo "*** to the installed location  Also, make sure you have run ldconfig if that"
        echo "*** is required on your system"
	echo "***"
        echo "*** If you have an old version installed, it is best to remove it, although"
        echo "*** you may also be able to get things to work by modifying LD_LIBRARY_PATH"],
      [ echo "*** The test program failed to compile or link. See the file config.log for"
        echo "*** the exact error that occured." ])
      
        CFLAGS="$ac_save_CFLAGS"
        LIBS="$ac_save_LIBS"
      fi

      CHECK_CFLAGS=""
      CHECK_LIBS=""

      rm -f conf.check-test
      ifelse([$3], , AC_MSG_ERROR([check not found]), [$3])
    fi
fi

    AC_SUBST(CHECK_CFLAGS)
    AC_SUBST(CHECK_LIBS)

    rm -f conf.check-test

  fi
])

# getopt.m4 serial 12
dnl Copyright (C) 2002, 2003, 2004, 2005, 2006 Free Software Foundation, Inc.
dnl This file is free software; the Free Software Foundation
dnl gives unlimited permission to copy and/or distribute it,
dnl with or without modifications, as long as this notice is preserved.

# The getopt module assume you want GNU getopt, with getopt_long etc,
# rather than vanilla POSIX getopt.  This means your your code should
# always include <getopt.h> for the getopt prototypes.

AC_DEFUN([gl_GETOPT_SUBSTITUTE],
[
  dnl Modified for DBMail, which does not use the Gnulib getopt.
  dnl CFLAGS="$CFLAGS -DUSE_DM_GETOPT"
  AC_DEFINE([USE_DM_GETOPT], 1, [Define if our local getopt will be used.])
  USE_DM_GETOPT=1
])

AC_DEFUN([gl_GETOPT_CHECK_HEADERS],
[
  if test -z "$GETOPT_H"; then
    AC_CHECK_HEADERS([getopt.h], [], [GETOPT_H=getopt.h])
  fi

  if test -z "$GETOPT_H"; then
    AC_CHECK_FUNCS([getopt_long_only], [], [GETOPT_H=getopt.h])
  fi

  dnl BSD getopt_long uses an incompatible method to reset option processing,
  dnl and (as of 2004-10-15) mishandles optional option-arguments.
  if test -z "$GETOPT_H"; then
    AC_CHECK_DECL([optreset], [GETOPT_H=getopt.h], [], [#include <getopt.h>])
  fi

  dnl Solaris 10 getopt doesn't handle `+' as a leading character in an
  dnl option string (as of 2005-05-05).
  if test -z "$GETOPT_H"; then
    AC_CACHE_CHECK([for working GNU getopt function], [gl_cv_func_gnu_getopt],
      [AC_RUN_IFELSE(
	[AC_LANG_PROGRAM([#include <getopt.h>],
	   [[
	     char *myargv[3];
	     myargv[0] = "conftest";
	     myargv[1] = "-+";
	     myargv[2] = 0;
	     return getopt (2, myargv, "+a") != '?';
	   ]])],
	[gl_cv_func_gnu_getopt=yes],
	[gl_cv_func_gnu_getopt=no],
	[dnl cross compiling - pessimistically guess based on decls
	 dnl Solaris 10 getopt doesn't handle `+' as a leading character in an
	 dnl option string (as of 2005-05-05).
	 AC_CHECK_DECL([getopt_clip],
	   [gl_cv_func_gnu_getopt=no], [gl_cv_func_gnu_getopt=yes],
	   [#include <getopt.h>])])])
    if test "$gl_cv_func_gnu_getopt" = "no"; then
      GETOPT_H=getopt.h
    fi
  fi
])

AC_DEFUN([gl_GETOPT_IFELSE],
[
  AC_REQUIRE([gl_GETOPT_CHECK_HEADERS])
  AS_IF([test -n "$GETOPT_H"], [$1], [$2])
])

AC_DEFUN([gl_GETOPT], [gl_GETOPT_IFELSE([gl_GETOPT_SUBSTITUTE])])

# Prerequisites of lib/getopt*.
AC_DEFUN([gl_PREREQ_GETOPT],
[
  AC_CHECK_DECLS_ONCE([getenv])
])

AC_DEFUN([CMU_SOCKETS], [
	save_LIBS="$LIBS"
	SOCKETLIB=""
	AC_CHECK_FUNC(connect, :,
		AC_CHECK_LIB(nsl, gethostbyname,
			     SOCKETLIB="-lnsl $SOCKETLIB")
		AC_CHECK_LIB(socket, connect,
			     SOCKETLIB="-lsocket $SOCKETLIB")
	)
	LIBS="$SOCKETLIB $save_LIBS"
	AC_CHECK_FUNC(res_search, :,
                AC_CHECK_LIB(resolv, res_search,
                              SOCKETLIB="-lresolv $SOCKETLIB") 
        )
	LIBS="$SOCKETLIB $save_LIBS"
	AC_CHECK_FUNCS(dn_expand dns_lookup)
	LIBS="$save_LIBS"
	AC_SUBST(SOCKETLIB)
])

