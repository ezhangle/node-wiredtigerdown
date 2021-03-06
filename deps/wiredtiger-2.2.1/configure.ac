# DO NOT EDIT
# This file is built automatically from build_posix/configure.ac.in.
PACKAGE=wiredtiger
AC_PREREQ(2.63)
AC_INIT(WiredTiger, m4_normalize(m4_include([build_posix/aclocal/version.m4])),
	[support@wiredtiger.com])

m4_include([build_posix/aclocal/version-set.m4])

AC_CONFIG_AUX_DIR([build_posix/gnu-support])
AC_CONFIG_MACRO_DIR([build_posix/aclocal])
AC_CONFIG_SRCDIR([RELEASE])

AM_INIT_AUTOMAKE([1.11 foreign parallel-tests subdir-objects])
m4_ifdef([AM_SILENT_RULES], [AM_SILENT_RULES([no])])

# Configure options.  The AM_OPTIONS and the libtool configuration
# need to stay here.  Moving them below the compiler and other
# configurations causes -Wcast_align warnings and other warnings
# on MacOS.
AM_OPTIONS

define([AC_LIBTOOL_LANG_CXX_CONFIG], [:])dnl
define([AC_LIBTOOL_LANG_F77_CONFIG], [:])dnl
LT_PREREQ(2.2.6)
LT_INIT([pic-only])
AC_SUBST([LIBTOOL_DEPS])

# If CFLAGS was not set on entry, default to "-O3 -g"
: ${CFLAGS="-O3 -g"}

AC_PROG_CC(cc gcc)
# AC_PROG_CXX(c++ g++)

if test "$GCC" = "yes"; then
	# The Solaris gcc compiler gets the additional -pthreads flag.
	if test "`uname -s`" = "SunOS"; then
		AM_CFLAGS="$AM_CFLAGS -pthreads"
	fi
else
	# The Solaris native compiler gets the additional -mt flag.
	if test "`uname -s`" = "SunOS"; then
		AM_CFLAGS="$AM_CFLAGS -mt"
	fi
fi

# Java and Python APIs
if test "$wt_cv_enable_java" = "yes" -o "$wt_cv_enable_python" = "yes"; then
	AX_PKG_SWIG(2.0.4, [],
	    [AC_MSG_WARN([SWIG is required to rebuild Java or Python APIs.])])
fi

if test "$wt_cv_enable_java" = "yes"; then
	JAVAC=${JAVAC-javac}
	AX_PROG_JAVAC
	AX_PROG_JAR
	AX_JNI_INCLUDE_DIR
	AX_CHECK_JUNIT
	for JNI_INCLUDE_DIR in $JNI_INCLUDE_DIRS ; do
		JNI_CPPFLAGS="$JNI_CPPFLAGS -I$JNI_INCLUDE_DIR"
	done
	AC_SUBST(JNI_CPPFLAGS)
fi

if test "$wt_cv_enable_python" = "yes"; then
	AM_PATH_PYTHON([2.6])
	if test -n "$with_python_prefix" ; then
		PYTHON_INSTALL_ARG="-d $with_python_prefix"
	fi
	AC_SUBST(PYTHON_INSTALL_ARG)
fi

AM_TYPES

AC_PROG_INSTALL

AC_CHECK_HEADERS([pthread_np.h])
AC_CHECK_LIB(pthread, pthread_create)
AC_CHECK_LIB(dl, dlopen)
AC_CHECK_LIB(rt, sched_yield)

AC_CHECK_FUNCS([\
	clock_gettime fallocate fcntl ftruncate gettimeofday posix_fadvise\
	posix_fallocate posix_madvise posix_memalign strtouq sync_file_range])

# OS X wrongly reports that it has fdatasync
AS_CASE([$host_os], [darwin*], [], [AC_CHECK_FUNCS([fdatasync])])

AC_SYS_LARGEFILE

AC_MSG_CHECKING([if the adaptive mutex type is available])
AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[#include <pthread.h>]],[[
	pthread_mutexattr_t attr;
	pthread_mutexattr_init(&attr);
	pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_ADAPTIVE_NP);]])],
    [AC_MSG_RESULT([yes])
     AC_DEFINE([HAVE_MUTEX_ADAPTIVE], [1],
        [Define if adaptive mutexes are supported.])],
    [AC_MSG_RESULT([no])])

AC_C_BIGENDIAN

# Linux requires _GNU_SOURCE to be defined
case "$host_os" in
linux*)	AM_CFLAGS="$AM_CFLAGS -D_GNU_SOURCE" ;;
esac

# Linux requires buffers aligned to 4KB boundaries for O_DIRECT to work.
BUFFER_ALIGNMENT=0
if test "$ac_cv_func_posix_memalign" = "yes" ; then
	case "$host_os" in
	linux*)	BUFFER_ALIGNMENT=4096 ;;
	esac
fi
AC_DEFINE_UNQUOTED(WT_BUFFER_ALIGNMENT_DEFAULT, $BUFFER_ALIGNMENT,
	[Default alignment of buffers used for I/O])

AC_SUBST(AM_CFLAGS)

# test/format requires an installed Oracle Berkeley DB release tree.
AC_MSG_CHECKING([if --with-berkeleydb=DIR option specified])
AC_ARG_WITH(berkeleydb,
    [AS_HELP_STRING([--with-berkeleydb=DIR],
	[Specify installed library directory of Berkeley DB])],
    [with_berkeleydb="$withval"], [with_berkeleydb="NO_BERKELEY_DB_LIBRARY"])
AC_MSG_RESULT($with_berkeleydb)
AM_CONDITIONAL([HAVE_BERKELEY_DB], [test -d $with_berkeleydb])
AC_SUBST(BERKELEY_DB_PATH, [$with_berkeleydb])

# test/format optionally supports the Levyx/Helium key/value store.
AC_MSG_CHECKING([if --with-helium=DIR option specified])
AC_ARG_WITH(helium,
    [AS_HELP_STRING([--with-helium=DIR],
	[Specify installed library directory of Helium])],
    [with_helium="$withval"], [with_helium="NO_HELIUM_LIBRARY"])
AC_MSG_RESULT($with_helium)
AM_CONDITIONAL([HAVE_HELIUM], [test -d $with_helium])
AC_SUBST(HELIUM_PATH, [$with_helium])

# Warn that diagnostic builds should not be used in production
if test "$wt_cv_enable_diagnostic" = "yes"; then
	AC_MSG_WARN(
	    [DIAGNOSTIC BUILDS ARE NOT RECOMMENDED FOR PRODUCTION DEPLOYMENT.])
fi

# If we are building in a tree without documentation, check if doxygen is
# available.
if test -f "$srcdir/docs/index.html" ; then
	wt_cv_docs_exist=yes
else
	wt_cv_docs_exist=no
fi

if test "$wt_cv_docs_exist" = "no"; then
	AC_CHECK_PROG([DOXYGEN], [doxygen], [doxygen], [false])
fi

if test "$wt_cv_docs_exist" = "yes" -o "$DOXYGEN" = "doxygen" ; then
	MAN1_PAGES="$srcdir/docs/man/man1/wt.1"
	AC_SUBST(MAN1_PAGES)
	MAN3_PAGES="$srcdir/docs/man/man3/wiredtiger.3"
	AC_SUBST(MAN3_PAGES)
fi

# Output files
AC_CONFIG_HEADERS([wiredtiger_config.h:build_posix/config.hin])

# BEGIN check existence -- maintained by reconf and Make.subdirs
AC_CONFIG_FILES([api/leveldb/Makefile])
AC_CONFIG_FILES([bench/tcbench/Makefile])
AC_CONFIG_FILES([bench/wtperf/Makefile])
AC_CONFIG_FILES([examples/c/Makefile])
AC_CONFIG_FILES([ext/collators/reverse/Makefile])
AC_CONFIG_FILES([ext/compressors/bzip2/Makefile])
AC_CONFIG_FILES([ext/compressors/nop/Makefile])
AC_CONFIG_FILES([ext/compressors/snappy/Makefile])
AC_CONFIG_FILES([ext/compressors/zlib/Makefile])
AC_CONFIG_FILES([ext/datasources/helium/Makefile])
AC_CONFIG_FILES([ext/test/kvs_bdb/Makefile])
AC_CONFIG_FILES([lang/java/Makefile])
AC_CONFIG_FILES([lang/python/Makefile])
AC_CONFIG_FILES([test/bloom/Makefile])
AC_CONFIG_FILES([test/checkpoint/Makefile])
AC_CONFIG_FILES([test/fops/Makefile])
AC_CONFIG_FILES([test/huge/Makefile])
# END check existence

AC_CONFIG_FILES([
	Makefile
	wiredtiger.h:src/include/wiredtiger.in
	wiredtiger.pc:build_posix/wiredtiger.pc.in
])
AC_OUTPUT
