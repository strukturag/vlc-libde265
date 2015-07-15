# Process this file with autoconf to produce a configure script.
AC_PREREQ([2.68])
AC_INIT([vlc-libde265], [0.1.7], [bauch@struktur.de])
AC_CONFIG_SRCDIR([src/codec/libde265dec.c])
AC_CONFIG_HEADERS([config.h])
AC_CONFIG_MACRO_DIR([m4])
AC_CANONICAL_SYSTEM
AC_USE_SYSTEM_EXTENSIONS
LT_INIT

# Checks for programs.
AC_PROG_CXX
AC_PROG_CC
AC_PROG_CC_C99
AC_PROG_INSTALL
AC_PROG_LN_S
PKG_PROG_PKG_CONFIG

# Initialize automake stuff
AM_INIT_AUTOMAKE([foreign])

AC_HEADER_STDBOOL
AC_TYPE_INT8_T
AC_TYPE_INT16_T
AC_TYPE_INT32_T
AC_TYPE_INT64_T
AC_TYPE_UINT8_T
AC_TYPE_UINT16_T
AC_TYPE_UINT32_T
AC_TYPE_UINT64_T
AC_TYPE_OFF_T
AC_TYPE_SIZE_T

AC_C_INLINE
AC_FUNC_MALLOC
AC_FUNC_REALLOC
AC_FUNC_STRNLEN
AC_SEARCH_LIBS([sqrt], [m])
AC_CHECK_FUNCS([memset sqrt strcasecmp strcspn strdup strndup strstr strtol])

PKG_CHECK_MODULES([libde265], [libde265 >= 0.7])
PKG_CHECK_MODULES([vlc], [vlc-plugin >= 2.0])
PKG_CHECK_MODULES([vlc212], [vlc-plugin >= 2.1],
    [AC_DEFINE([HAVE_VLC_HEVC], [1], [Whether VLC with HEVC support was found.])
     have_vlc_hevc="yes"],
    [have_vlc_hevc="no"])
PKG_CHECK_MODULES([vlc220], [vlc-plugin >= 2.2],
    [AC_DEFINE([HAVE_VLC_HEVC_TS], [1], [Whether VLC with HEVC in MPEG-TS support was found.])
     have_vlc_hevc_ts="yes" have_vlc_hevc_mp4="yes"],
    [have_vlc_hevc_ts="no" have_vlc_hevc_mp4="no"])
PKG_CHECK_MODULES([vlc300], [vlc-plugin >= 3.0],
    [AC_DEFINE([HAVE_VLC_REFCOUNT_PICTURE], [1], [Whether VLC refcounts pictures directly.])
     have_vlc_refcount_picture="yes"],
    [have_vlc_refcount_picture="no"])

AM_CONDITIONAL([HAVE_VLC_HEVC], [test "x$have_vlc_hevc" != "xno"])
AM_CONDITIONAL([HAVE_VLC_HEVC_TS], [test "x$have_vlc_hevc_ts" != "xno"])
AM_CONDITIONAL([HAVE_VLC_HEVC_MP4], [test "x$have_vlc_hevc_mp4" != "xno"])

if eval "test $have_vlc_hevc != yes" ; then
    AC_LANG_PUSH([C++])
    AC_CHECK_HEADERS([ebml/EbmlVersion.h])
    AC_CHECK_HEADERS([matroska/KaxTag.h])
    AC_LANG_POP([C++])
fi

if eval "test $have_vlc_hevc_ts != yes" ; then
PKG_CHECK_MODULES([libdvbpsi], [libdvbpsi])
fi

AC_CONFIG_FILES([Makefile])
AC_OUTPUT
