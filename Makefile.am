ACLOCAL_AMFLAGS = -I m4

AUTOMAKE_OPTIONS = subdir-objects

EXTRA_DIST = \
	autogen.sh \
	m4/empty.dir \
	README.md \
	prepare-package.sh \
	src/demux/COPYING.LIB \
	patches/*

lib_LTLIBRARIES = libde265_plugin.la

libde265_plugin_la_CFLAGS = -Iinclude -DMODULE_STRING=\"de265\" $(libde265_CFLAGS) $(vlc_CFLAGS)
libde265_plugin_la_CPPFLAGS = -Iinclude -DMODULE_STRING=\"de265\" $(libde265_CFLAGS) $(vlc_CFLAGS)
libde265_plugin_la_LIBADD = $(libde265_LIBS) $(vlc_LIBS)
libde265_plugin_la_LDFLAGS = -avoid-version -module -export-symbol-regex ^vlc_entry $(libde265_LDFLAGS) $(vlc_LDFLAGS)
libde265_plugin_la_SOURCES = \
	src/codec/libde265dec.c \
	include/libde265_plugin_common.h

# older versions of vlc don't know about HEVC content in MKV
if !HAVE_VLC_HEVC
  lib_LTLIBRARIES += libde265_mkv_plugin.la

  libde265_mkv_plugin_la_CFLAGS = -DHAVE_ATTRIBUTE_PACKED -DMODULE_STRING=\"mkv\" -Iinclude $(vlc_CFLAGS)
  libde265_mkv_plugin_la_CPPFLAGS = -DHAVE_ATTRIBUTE_PACKED -DMODULE_STRING=\"mkv\" -Iinclude $(vlc_CFLAGS)
  libde265_mkv_plugin_la_LIBADD = $(vlc_LIBS) -lebml -lmatroska
  libde265_mkv_plugin_la_LDFLAGS = -avoid-version -module -export-symbol-regex ^vlc_entry $(vlc_LDFLAGS)
  libde265_mkv_plugin_la_SOURCES = \
		src/demux/vobsub.h \
		src/demux/mp4/libmp4.c \
		src/demux/mp4/libmp4.h \
		src/demux/mkv/chapter_command.cpp \
		src/demux/mkv/chapter_command.hpp \
		src/demux/mkv/chapters.cpp \
		src/demux/mkv/chapters.hpp \
		src/demux/mkv/demux.cpp \
		src/demux/mkv/demux.hpp \
		src/demux/mkv/Ebml_parser.cpp \
		src/demux/mkv/Ebml_parser.hpp \
		src/demux/mkv/matroska_segment.cpp \
		src/demux/mkv/matroska_segment.hpp \
		src/demux/mkv/matroska_segment_parse.cpp \
		src/demux/mkv/mkv.cpp \
		src/demux/mkv/mkv.hpp \
		src/demux/mkv/stream_io_callback.cpp \
		src/demux/mkv/stream_io_callback.hpp \
		src/demux/mkv/util.cpp \
		src/demux/mkv/util.hpp \
		src/demux/mkv/virtual_segment.cpp \
		src/demux/mkv/virtual_segment.hpp \
		include/libde265_plugin_common.h \
		include/vlc_codecs.h
endif

# older versions of vlc don't know about HEVC content in MPEG-TS
if !HAVE_VLC_HEVC_TS
  lib_LTLIBRARIES += libde265_ts_plugin.la

  libde265_ts_plugin_la_CFLAGS = -DHAVE_ATTRIBUTE_PACKED -DMODULE_STRING=\"ts\" -Iinclude $(vlc_CFLAGS)  $(libdvbpsi_CFLAGS) -Wno-deprecated-declarations
  libde265_ts_plugin_la_CPPFLAGS = -DHAVE_ATTRIBUTE_PACKED -DMODULE_STRING=\"ts\" -Iinclude $(vlc_CFLAGS) $(libdvbpsi_CFLAGS)
  libde265_ts_plugin_la_LIBADD = $(vlc_LIBS) $(libdvbpsi_LIBS)
  libde265_ts_plugin_la_LDFLAGS = -avoid-version -module -export-symbol-regex ^vlc_entry $(vlc_LDFLAGS)
  libde265_ts_plugin_la_SOURCES = \
		src/demux/ts.c \
		src/mux/mpeg/csa.c \
		src/mux/mpeg/csa.h \
		src/mux/mpeg/dvbpsi_compat.h \
		include/libde265_plugin_common.h \
		include/vlc_codecs.h
endif
