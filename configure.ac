m4_define([pkg_cl_name], [m4_esyscmd([head -n 1 ChangeLog | sed -e 's/ .*//' | tr -d '\n'])])
m4_define([pkg_cl_version], [m4_esyscmd([head -n 1 ChangeLog | sed -e 's/.*(//' -e 's/).*//' | tr -d '\n'])])
m4_define([pkg_svn_version], [m4_esyscmd([svnversion 2>/dev/null | sed -e 's/^exported$//' | tr -d '\n'])])

m4_define([opt_with_rev], [m4_esyscmd([echo $WITH_REV | tr -d '\n'])])

m4_define([pkg_version], ifelse(opt_with_rev, [1], m4_format([%s-0r%s], pkg_cl_version, pkg_svn_version), pkg_cl_version))
m4_define([pkg_pretty_version], ifelse(pkg_svn_version, [], pkg_cl_version, m4_format([%s (r%s)], pkg_cl_version, pkg_svn_version)))

AC_PREREQ([2.63])
AC_INIT([pkg_cl_name], [pkg_version])
AM_INIT_AUTOMAKE([no-define foreign])
AC_CONFIG_SRCDIR([src/main.cc])

AC_PROG_AWK
AC_PROG_CXX
AC_PROG_CC
AC_PROG_INSTALL
AC_PROG_RANLIB

AC_C_BIGENDIAN([AC_MSG_ERROR([big endian systems are not supported])])

AC_ARG_ENABLE(
  [tests], 
  [  --enable-tests          Build test programs],
  [case "${enableval}" in
    yes) build_tests=true ;;
    no)  build_tests=false ;;
    *) AC_MSG_ERROR([bad value ${enableval} for --enable-tests]) ;;
   esac], [build_tests=false])

AC_ARG_ENABLE(
  [darwin],
  [  --enable-darwin         Enable support for Darwin (Mac OS X)],
  [case "${enableval}" in
     yes) darwin=true ;;
     no)  darwin=false ;;
     *) AC_MSG_ERROR([bad value ${enableval} for --enable-darwin]) ;;
   esac], [darwin=false])

AC_ARG_ENABLE(
  [code-coverage],
  [  --enable-code-coverage  Enable code coverage output],
  [case "${enableval}" in
     yes) coverage=true ;;
     no)  coverage=false ;;
     *) AC_MSG_ERROR([bad value ${enableval} for --enable-coverage]) ;;
   esac], [coverage=false])

AC_ARG_ENABLE(
  [osx-bundle],
  [  --enable-osx-bundle     Enable OS X app bundle features],
  [case "${enableval}" in
     yes) osx_bundle=true ;;
     no)  osx_bundle=false ;;
     *) AC_MSG_ERROR([bad value ${enableval} for --enable-osx-bundle]) ;;
   esac], [osx_bundle=false])

service_default_aws=true
service_default_gs=true
service_default_fvs=true

AC_ARG_ENABLE(
  [aws],
  [  --enable-aws            Enable Amazon S3 support],
  [case "${enableval}" in
     yes) aws=true ;;
     no)  aws=false ;;
     *) AC_MSG_ERROR([bad value ${enableval} for --enable-aws]) ;;
   esac], [aws=$service_default_aws])

AC_ARG_ENABLE(
  [gs],
  [  --enable-gs             Enable Google Cloud Storage support],
  [case "${enableval}" in
     yes) gs=true ;;
     no)  gs=false ;;
     *) AC_MSG_ERROR([bad value ${enableval} for --enable-gs]) ;;
   esac], [gs=$service_default_gs])

AC_ARG_ENABLE(
  [fvs],
  [  --enable-fvs            Enable FV/S support],
  [case "${enableval}" in
     yes) fvs=true ;;
     no)  fvs=false ;;
     *) AC_MSG_ERROR([bad value ${enableval} for --enable-fvs]) ;;
   esac], [fvs=$service_default_fvs])

AM_CONDITIONAL([BUILD_TESTS], [test x$build_tests = xtrue])
AM_CONDITIONAL([WITH_AWS], [test x$aws = xtrue])
AM_CONDITIONAL([WITH_GS], [test x$gs = xtrue])
AM_CONDITIONAL([WITH_FVS], [test x$fvs = xtrue])

# set LDFLAGS rather than AM_LDFLAGS below because we need these flags
# when using AC_CHECK_LIB
if test "x$darwin" = xtrue; then
  # mavericks seems to link with clang by default, which doesn't like unused library parameters
  AM_CXXFLAGS="$AM_CXXFLAGS -Qunused-arguments"
  AM_LDFLAGS="$AM_LDFLAGS -Qunused-arguments"

  LDFLAGS="$LDFLAGS -L/opt/local/lib"
  LDADD="$LDADD -framework CoreFoundation"
else
  LDFLAGS="$LDFLAGS -Wl,--as-needed"
fi

if test "x$coverage" = xtrue; then
  AM_CXXFLAGS="$AM_CXXFLAGS -ftest-coverage -fprofile-arcs"
fi

if test "x$osx_bundle" = xtrue; then
  AC_DEFINE([OSX_BUNDLE], [])
fi

last_svc_enabled=
svc_count=0

if test "x$aws" = xtrue; then
  AC_DEFINE([WITH_AWS], [])
  last_svc_enabled=aws
  let svc_count++
fi

if test "x$gs" = xtrue; then
  AC_DEFINE([WITH_GS], [])
  last_svc_enabled=gs
  let svc_count++
fi

if test "x$fvs" = xtrue; then
  AC_DEFINE([WITH_FVS], [])
  last_svc_enabled=fvs
  let svc_count++
fi

if test "$svc_count" -eq "1"; then
  AC_DEFINE_UNQUOTED([FIXED_SERVICE], [$last_svc_enabled])
fi

BOOST_REQUIRE([1.41])
BOOST_REGEX()
BOOST_THREADS()

AC_CHECK_LIB([pthread], [pthread_create], [], [AC_MSG_ERROR([cannot find pthreads])])

if test "x$build_tests" = xtrue; then
  AC_CHECK_LIB([gtest_main], [main], [], [AC_MSG_ERROR([cannot find gtest])])
fi

PKG_CHECK_MODULES([DEPS_XML], [libxml-2.0 >= 2.7.6])
PKG_CHECK_MODULES([DEPS_CURL], [libcurl >= 7.0.0])
PKG_CHECK_MODULES([DEPS_FUSE], [fuse >= 2.7.3])

PKG_CHECK_EXISTS([libssl >= 0.9.8], [have_openssl=true], [have_openssl=false])
PKG_CHECK_EXISTS([gnutls >= 2.12.19], [have_gnutls=true], [have_gnutls=false])

PKG_CHECK_MODULES([DEPS_CRYPTO], [libcrypto])

if test "x$have_openssl" = xtrue; then
  PKG_CHECK_MODULES([DEPS_OPENSSL], [libssl >= 0.9.8])
  CPPFLAGS="$CPPFLAGS -DHAVE_OPENSSL"
fi

if test "x$have_gnutls" = xtrue; then
  PKG_CHECK_MODULES([DEPS_GNUTLS], [gnutls >= 2.12.19])
  CPPFLAGS="$CPPFLAGS -DHAVE_GNUTLS"
fi

AC_CHECK_HEADERS([fcntl.h inttypes.h limits.h stddef.h stdint.h stdlib.h string.h strings.h sys/time.h syslog.h termios.h unistd.h])

AC_HEADER_STDBOOL
AC_TYPE_UID_T
AC_C_INLINE
AC_TYPE_MODE_T
AC_TYPE_OFF_T
AC_TYPE_SIZE_T
AC_TYPE_SSIZE_T
AC_CHECK_MEMBERS([struct stat.st_blksize])
AC_STRUCT_ST_BLOCKS
AC_TYPE_UINT32_T
AC_TYPE_UINT64_T
AC_TYPE_UINT8_T

AC_FUNC_CHOWN
AC_FUNC_MALLOC
AC_CHECK_FUNCS([ftruncate gettimeofday memset mkdir strcasecmp strchr strerror strrchr strstr strtol])

AC_CONFIG_FILES([Makefile])
AC_CONFIG_FILES([man/Makefile])
AC_CONFIG_FILES([src/Makefile src/tests/Makefile])
AC_CONFIG_FILES([src/base/Makefile src/base/tests/Makefile])
AC_CONFIG_FILES([src/crypto/Makefile src/crypto/tests/Makefile])
AC_CONFIG_FILES([src/fs/Makefile src/fs/tests/Makefile])
AC_CONFIG_FILES([src/services/Makefile src/services/aws/Makefile src/services/fvs/Makefile src/services/gs/Makefile])
AC_CONFIG_FILES([src/threads/Makefile src/threads/tests/Makefile])

AC_DEFINE(PACKAGE_VERSION_WITH_REV, [m4_format(["%s"], pkg_pretty_version)])

AC_SUBST([AM_CXXFLAGS], ["$AM_CXXFLAGS -g -Wall -Werror"])
AC_SUBST([AM_CPPFLAGS], ["-I \$(top_srcdir)/src -DSYSCONFDIR=\"\\\"\$(sysconfdir)\\\"\" $CPPFLAGS $DEPS_XML_CFLAGS $DEPS_CURL_CFLAGS $DEPS_FUSE_CFLAGS $DEPS_OPENSSL_CFLAGS $DEPS_CRYPTO_CFLAGS $DEPS_GNUTLS_CFLAGS $BOOST_CPPFLAGS"])
AC_SUBST([AM_LDFLAGS], ["$AM_LDFLAGS $BOOST_REGEX_LDFLAGS $BOOST_THREAD_LDFLAGS"])
AC_SUBST([LDADD], ["$LDADD $DEPS_XML_LIBS $DEPS_CURL_LIBS $DEPS_FUSE_LIBS $DEPS_OPENSSL_LIBS $DEPS_CRYPTO_LIBS $DEPS_GNUTLS_LIBS $BOOST_REGEX_LIBS $BOOST_THREAD_LIBS"])

AC_OUTPUT
