#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ(2.61)
AC_INIT(spatialite-tools, 5.0.1, a.furieri@lqt.it)
AC_LANG(C)
AC_CONFIG_MACRO_DIR([m4])

AM_INIT_AUTOMAKE
AM_MAINTAINER_MODE
AM_CONFIG_HEADER(config.h)

# enabling huge-file support (64 bit file pointers)
AH_TEMPLATE([_LARGE_FILE],
            [Must be defined in order to enable huge-file support.])
AC_DEFINE(_LARGE_FILE)
AH_TEMPLATE([_FILE_OFFSET_BITS],
            [Must be =64 in order to enable huge-file support.])
AC_DEFINE(_FILE_OFFSET_BITS, 64)
AH_TEMPLATE([_LARGEFILE_SOURCE],
            [Must be defined in order to enable huge-file support.])
AC_DEFINE(_LARGEFILE_SOURCE)

# supporting SPATIALITE_AMALGAMATION
AH_TEMPLATE([SPATIALITE_AMALGAMATION],
	    [must be defined when using libspatialite-amalgamation])
AH_TEMPLATE([OMIT_READOSM],
            [Should be defined in order to disable ReadOSM support.])
AH_TEMPLATE([HAVE_DECL_SQLITE_CONFIG_URI],
            [depending on SQLite library version.])
AH_TEMPLATE([HAVE_DECL_SQLITE_DBSTATUS_LOOKASIDE_USED],
            [depending on SQLite library version.])
AH_TEMPLATE([HAVE_DECL_SQLITE_DBSTATUS_LOOKASIDE_HIT],
            [depending on SQLite library version.])
AH_TEMPLATE([HAVE_DECL_SQLITE_DBSTATUS_LOOKASIDE_MISS_SIZE],
            [depending on SQLite library version.])
AH_TEMPLATE([HAVE_DECL_SQLITE_DBSTATUS_LOOKASIDE_MISS_FULL],
            [depending on SQLite library version.])
AH_TEMPLATE([HAVE_DECL_SQLITE_DBSTATUS_CACHE_USED],
            [depending on SQLite library version.])
AH_TEMPLATE([HAVE_DECL_SQLITE_DBSTATUS_CACHE_HIT],
            [depending on SQLite library version.])
AH_TEMPLATE([HAVE_DECL_SQLITE_DBSTATUS_CACHE_MISS],
            [depending on SQLite library version.])
AH_TEMPLATE([HAVE_DECL_SQLITE_DBSTATUS_CACHE_WRITE],
            [depending on SQLite library version.])
AH_TEMPLATE([HAVE_DECL_SQLITE_DBSTATUS_SCHEMA_USED],
            [depending on SQLite library version.])
AH_TEMPLATE([HAVE_DECL_SQLITE_DBSTATUS_STMT_USED],
            [depending on SQLite library version.])
AH_TEMPLATE([HAVE_DECL_SQLITE_STMTSTATUS_FULLSCAN_STEP],
            [depending on SQLite library version.])
AH_TEMPLATE([HAVE_DECL_SQLITE_STMTSTATUS_SORT],
            [depending on SQLite library version.])
AH_TEMPLATE([HAVE_DECL_SQLITE_STMTSTATUS_AUTOINDEX],
            [depending on SQLite library version.])
AH_TEMPLATE([HAVE_DECL_SQLITE_DBSTATUS_STMT_USED],
            [depending on SQLite library version.])
AH_TEMPLATE([HAVE_DECL_SQLITE_TESTCTRL_EXPLAIN_STMT],
            [depending on SQLite library version.])
AH_TEMPLATE([HAVE_DECL_SQLITE_FCNTL_VFSNAME],
            [depending on SQLite library version.])

# Checks for header files.
AC_CHECK_HEADERS(stdlib.h,, [AC_MSG_ERROR([cannot find stdlib.h, bailing out])])
AC_CHECK_HEADERS(stdio.h,, [AC_MSG_ERROR([cannot find stdio.h, bailing out])])
AC_CHECK_HEADERS(string.h,, [AC_MSG_ERROR([cannot find string.h, bailing out])])
AC_CHECK_HEADERS(memory.h,, [AC_MSG_ERROR([cannot find memory.h, bailing out])])
AC_CHECK_HEADERS(math.h,, [AC_MSG_ERROR([cannot find math.h, bailing out])])
AC_CHECK_HEADERS(float.h,, [AC_MSG_ERROR([cannot find float.h, bailing out])])
AC_CHECK_HEADERS(fcntl.h,, [AC_MSG_ERROR([cannot find fcntl.h, bailing out])])
AC_CHECK_HEADERS(inttypes.h,, [AC_MSG_ERROR([cannot find inttypes.h, bailing out])])
AC_CHECK_HEADERS(stddef.h,, [AC_MSG_ERROR([cannot find stddef.h, bailing out])])
AC_CHECK_HEADERS(stdint.h,, [AC_MSG_ERROR([cannot find stdint.h, bailing out])])
AC_CHECK_HEADERS(sys/time.h,, [AC_MSG_ERROR([cannot find sys/time.h, bailing out])])
AC_CHECK_HEADERS(unistd.h,, [AC_MSG_ERROR([cannot find unistd.h, bailing out])])
AC_CHECK_HEADERS(sqlite3.h,, [AC_MSG_ERROR([cannot find sqlite3.h, bailing out])])
AC_CHECK_HEADERS(sqlite3ext.h,, [AC_MSG_ERROR([cannot find sqlite3ext.h, bailing out])])

# Checks for programs.
AC_PROG_CXX
AC_PROG_CC
AC_PROG_CPP
AC_PROG_INSTALL
AC_PROG_LN_S
AC_PROG_MAKE_SET
AC_LIBTOOL_WIN32_DLL
AC_PROG_LIBTOOL

# Checks for typedefs, structures, and compiler characteristics.
AC_C_CONST
AC_TYPE_OFF_T
AC_TYPE_SIZE_T
AC_HEADER_TIME
AC_STRUCT_TM
AC_C_VOLATILE

# Checks for library functions.
AC_FUNC_LSTAT
AC_FUNC_LSTAT_FOLLOWS_SLASHED_SYMLINK
AC_FUNC_MEMCMP
AC_FUNC_STAT
AC_FUNC_STRFTIME
AC_CHECK_FUNCS([memset sqrt strcasecmp strerror strncasecmp strstr fdatasync ftruncate getcwd gettimeofday localtime_r memmove strerror])
 
# Checks for installed libraries
AC_CHECK_LIB(sqlite3,sqlite3_prepare_v2,,AC_MSG_ERROR(['libsqlite3' is required but it doesn't seem to be installed on this system.]),-lm)

AC_CONFIG_FILES([Makefile])

#-----------------------------------------------------------------------
#   --enable-readline
#
AC_ARG_ENABLE(readline, [AS_HELP_STRING(
  [--enable-readline],
  [use readline in shell tool (yes, no) [default=yes]])],
  [], [enable_readline=yes])
if test x"$enable_readline" != xno ; then
  sLIBS=$LIBS
  LIBS=""
  AC_SEARCH_LIBS(tgetent, curses, [], [])
  AC_SEARCH_LIBS(readline, readline, [], [enable_readline=no])
  AC_CHECK_FUNCS(readline, [], [])
  READLINE_LIBS=$LIBS
  LIBS=$sLIBS
fi
AC_SUBST(READLINE_LIBS)

AC_CHECK_HEADERS(expat.h,, [AC_MSG_ERROR([cannot find expat.h, bailing out])])
AC_CHECK_LIB(expat,XML_ParserCreate,,AC_MSG_ERROR(['expat' is required but it doesn't seem to be installed on this system.]))

PKG_CHECK_MODULES([LIBSPATIALITE], [spatialite], , AC_MSG_ERROR(['libspatialite' is required but it doesn't seem to be installed on this system.]))
AC_SUBST(LIBSPATIALITE_CFLAGS)
# testing for libspatialite-amalgamation
if test "x$(pkg-config --cflags spatialite|grep "DSPATIALITE_AMALGAMATION=1")" != "x"; then
  AC_DEFINE(SPATIALITE_AMALGAMATION, 1)
fi
AC_SUBST(LIBSPATIALITE_LIBS)

#-----------------------------------------------------------------------
#   --enable-readosm
#
AC_ARG_ENABLE(readosm, [AS_HELP_STRING(
  [--enable-readosm], [enables ReadOSM inclusion [default=yes]])],
  [], [enable_readosm=yes])
if test x"$enable_readosm" != "xno"; then
  AC_SUBST(LIBREADOSM_LIBS)PKG_CHECK_MODULES([LIBREADOSM], [readosm], , AC_MSG_ERROR(['libreadosm' is required but it doesn't seem to be installed on this system.]))
  AC_SEARCH_LIBS(readosm_version,readosm,,AC_MSG_ERROR(['libreadosm' (>= v.1.1.0) is required but an older version was found.]))
  AC_SUBST(LIBREADOSM_CFLAGS)
  AC_SUBST(LIBREADOSM_LIBS)
else
  AC_DEFINE(OMIT_READOSM)
fi
AM_CONDITIONAL([READOSM], [test x$enable_readosm != xno])

PKG_CHECK_MODULES([LIBXML2], [libxml-2.0], , AC_MSG_ERROR(['libxml2' is required but it doesn't seem to be installed on this system.]))
AC_SUBST(LIBXML2_CFLAGS)
AC_SUBST(LIBXML2_LIBS)

# checks for SQLite version-depending constants
AC_CHECK_DECL([SQLITE_CONFIG_URI],
  [AC_DEFINE(HAVE_DECL_SQLITE_CONFIG_URI)],[],[[#include <sqlite3.h>]])
AC_CHECK_DECL([SQLITE_DBSTATUS_LOOKASIDE_USED],
  [AC_DEFINE(HAVE_DECL_SQLITE_DBSTATUS_LOOKASIDE_USED)],[],[[#include <sqlite3.h>]])
AC_CHECK_DECL([SQLITE_DBSTATUS_LOOKASIDE_HIT],
  [AC_DEFINE(HAVE_DECL_SQLITE_DBSTATUS_LOOKASIDE_HIT)],[],[[#include <sqlite3.h>]])
AC_CHECK_DECL([SQLITE_DBSTATUS_LOOKASIDE_MISS_SIZE],
  [AC_DEFINE(HAVE_DECL_SQLITE_DBSTATUS_LOOKASIDE_MISS_SIZE)],[],[[#include <sqlite3.h>]])
AC_CHECK_DECL([SQLITE_DBSTATUS_LOOKASIDE_MISS_FULL],
  [AC_DEFINE(HAVE_DECL_SQLITE_DBSTATUS_LOOKASIDE_MISS_FULL)],[],[[#include <sqlite3.h>]])
AC_CHECK_DECL([SQLITE_DBSTATUS_CACHE_USED],
  [AC_DEFINE(HAVE_DECL_SQLITE_DBSTATUS_CACHE_USED)],[],[[#include <sqlite3.h>]])
AC_CHECK_DECL([SQLITE_DBSTATUS_CACHE_HIT],
  [AC_DEFINE(HAVE_DECL_SQLITE_DBSTATUS_CACHE_HIT)],[],[[#include <sqlite3.h>]])
AC_CHECK_DECL([SQLITE_DBSTATUS_CACHE_MISS],
  [AC_DEFINE(HAVE_DECL_SQLITE_DBSTATUS_CACHE_MISS)],[],[[#include <sqlite3.h>]])
AC_CHECK_DECL([SQLITE_DBSTATUS_CACHE_WRITE],
  [AC_DEFINE(HAVE_DECL_SQLITE_DBSTATUS_CACHE_WRITE)],[],[[#include <sqlite3.h>]])
AC_CHECK_DECL([SQLITE_DBSTATUS_SCHEMA_USED],
  [AC_DEFINE(HAVE_DECL_SQLITE_DBSTATUS_SCHEMA_USED)],[],[[#include <sqlite3.h>]])
AC_CHECK_DECL([SQLITE_DBSTATUS_STMT_USED],
  [AC_DEFINE(HAVE_DECL_SQLITE_DBSTATUS_STMT_USED)],[],[[#include <sqlite3.h>]])
AC_CHECK_DECL([SQLITE_STMTSTATUS_FULLSCAN_STEP],
  [AC_DEFINE(HAVE_DECL_SQLITE_STMTSTATUS_FULLSCAN_STEP)],[],[[#include <sqlite3.h>]])
AC_CHECK_DECL([SQLITE_STMTSTATUS_SORT],
  [AC_DEFINE(HAVE_DECL_SQLITE_STMTSTATUS_SORT)],[],[[#include <sqlite3.h>]])
AC_CHECK_DECL([SQLITE_STMTSTATUS_AUTOINDEX],
  [AC_DEFINE(HAVE_DECL_SQLITE_STMTSTATUS_AUTOINDEX)],[],[[#include <sqlite3.h>]])
AC_CHECK_DECL([SQLITE_TESTCTRL_EXPLAIN_STMT],
  [AC_DEFINE(HAVE_DECL_SQLITE_TESTCTRL_EXPLAIN_STMT)],[],[[#include <sqlite3.h>]])
AC_CHECK_DECL([SQLITE_FCNTL_VFSNAME],
  [AC_DEFINE(HAVE_DECL_SQLITE_FCNTL_VFSNAME)],[],[[#include <sqlite3.h>]])

AC_OUTPUT
