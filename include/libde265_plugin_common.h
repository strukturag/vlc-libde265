#ifndef _LIBDE265_PLUGIN_COMMON_H_
#define _LIBDE265_PLUGIN_COMMON_H_

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <vlc_common.h>
#include <vlc_plugin.h>
#include <vlc_codec.h>

#ifndef N_
#define N_(x) (x)
#endif

#ifndef _
#define _(x) (x)
#endif

#ifndef VLC_CODEC_HEVC
#ifdef HAVE_VLC_HEVC
#error "should have VLC_CODEC_HEVC with this version of VLC"
#endif
#define VLC_CODEC_HEVC VLC_FOURCC('h','e','v','c')
#endif

#ifndef VLC_CODEC_SCTE_27
#ifdef HAVE_VLC_HEVC_TS
#error "should have VLC_CODEC_SCTE_27 with this version of VLC"
#endif
#define VLC_CODEC_SCTE_27 VLC_FOURCC('S','C','2','7')
#endif

#endif  // _LIBDE265_PLUGIN_COMMON_H_
