/*****************************************************************************
 * libde265dec.c: libde265 decoder (HEVC/H.265) module
 *****************************************************************************
 * Copyright (C) 2014 struktur AG
 *
 * Authors: Joachim Bauch <bauch@struktur.de>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; either version 2.1 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston MA 02110-1301, USA.
 *****************************************************************************/

/*****************************************************************************
 * Preamble
 *****************************************************************************/

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <vlc_common.h>
#include <vlc_plugin.h>
#include <vlc_demux.h>
#include <assert.h>

#include "../../include/libde265_plugin_common.h"

/*****************************************************************************
 * Module descriptor
 *****************************************************************************/
static int  Open(vlc_object_t *);
static void Close(vlc_object_t *);

#define DETECT_BUFFER_SIZE      1024

#define DEMUX_FRAME_SIZE        4096

#define NAL_UNIT_BLA_W_LP       16      // BLA = broken link access
#define NAL_UNIT_BLA_W_RADL     17
#define NAL_UNIT_BLA_N_LP       18
#define NAL_UNIT_IDR_W_RADL     19
#define NAL_UNIT_IDR_N_LP       20
#define NAL_UNIT_CRA_NUT        21      // CRA = clean random access
#define NAL_UNIT_VPS_NUT        32
#define NAL_UNIT_SPS_NUT        33
#define NAL_UNIT_PPS_NUT        34

#define FPS_TEXT N_("Frames per Second")
#define FPS_LONGTEXT N_("This is the desired frame rate when " \
    "playing raw bitstreams. In the form 30000/1001 or 29.97")

vlc_module_begin()
    set_shortname(N_("libde265demux"))
    set_description(N_("HEVC/H.265 raw bitstream demuxer"))
    set_capability("demux", 200)
    set_category(CAT_INPUT)
    set_subcategory(SUBCAT_INPUT_DEMUX)
    set_callbacks(Open, Close)
    add_shortcut("libde265demux")
    add_string("libde265demux-fps", NULL, FPS_TEXT, FPS_LONGTEXT, false)
vlc_module_end()

/*****************************************************************************
 * Definitions of structures used by this plugin
 *****************************************************************************/
struct demux_sys_t
{
    es_out_id_t *es_video;
    es_format_t fmt_video;
    date_t pcr;
};

/*****************************************************************************
 * Local prototypes
 *****************************************************************************/
static int Demux(demux_t *);
static int Control(demux_t *, int i_query, va_list args);

// supported file extensions
static const char *extensions[] =
{
    "hevc",
    "h265",
    "265",
    "bin",
    NULL
};

/*****************************************************************************
 * Open: initializes raw DV demux structures
 *****************************************************************************/
static int Open(vlc_object_t * p_this)
{
    demux_t *demux = (demux_t *) p_this;
    const uint8_t *buffer;
    int buffer_size;
    uint32_t code=-1;
    int vps = 0, sps=0, pps=0, irap=0;
    unsigned fps_num=25, fps_den=1;  // assume 25/1 fps by default
    int i;
    char *psz_tmp;

    if (!demux->b_force) {
        /* guess preset based on file extension */
        if (!demux->psz_file) {
            return VLC_EGENERIC;
        }

        const char *extension = strrchr(demux->psz_file, '.');
        if (!extension) {
            return VLC_EGENERIC;
        }
        extension++;

        for (unsigned i=0; extensions[i]; i++) {
            if (!strcasecmp(extension, extensions[i])) {
                goto supported_extension;
            }
        }
        return VLC_EGENERIC;
    }

supported_extension:
    buffer_size = stream_Peek(demux->s, &buffer, DETECT_BUFFER_SIZE);
    for(i=0; i<buffer_size-1; i++){
        code = (code << 8) + buffer[i];
        if ((code & 0xffffff00) == 0x100) {
            uint8_t nal2 = buffer[i + 1];
            int type = (code & 0x7E) >> 1;

            if (code & 0x81) { // forbidden and reserved zero bits
                return VLC_EGENERIC;
            }

            if (nal2 & 0xf8) { // reserved zero
                return VLC_EGENERIC;
            }

            switch (type) {
            case NAL_UNIT_VPS_NUT: vps++; break;
            case NAL_UNIT_SPS_NUT: sps++; break;
            case NAL_UNIT_PPS_NUT: pps++; break;
            case NAL_UNIT_BLA_W_LP:
            case NAL_UNIT_BLA_W_RADL:
            case NAL_UNIT_BLA_N_LP:
            case NAL_UNIT_IDR_W_RADL:
            case NAL_UNIT_IDR_N_LP:
            case NAL_UNIT_CRA_NUT: irap++; break;
            default: break;
            }
        }
    }

    // check if it looks like a valid bitstream
    if (!vps || !sps || !pps || !irap) {
        return VLC_EGENERIC;
    }

    psz_tmp = var_CreateGetNonEmptyString(demux, "libde265demux-fps");
    if (psz_tmp) {
        char *p_ptr;
        /* fps can either be n/d or q.f
         * for accuracy, avoid representation in float */
        fps_num = strtol(psz_tmp, &p_ptr, 10);
        if (*p_ptr == '/') {
            p_ptr++;
            fps_den = strtol(p_ptr, NULL, 10);
        } else if(*p_ptr == '.') {
            char *p_end;
            p_ptr++;
            int i_frac = strtol(p_ptr, &p_end, 10);
            fps_den = (p_end - p_ptr) * 10;
            if (!fps_den) {
                fps_den = 1;
            }
            fps_num = fps_num * fps_den + i_frac;
        }
        else if (*p_ptr == '\0') {
            fps_den = 1;
        }
        free(psz_tmp);
    }

    if (!fps_num || !fps_den) {
        msg_Err(demux, "invalid or no framerate specified.");
        return VLC_EGENERIC;
    }

    demux_sys_t *sys = malloc(sizeof(*sys));
    if (!sys) {
        return VLC_ENOMEM;
    }
    demux->p_sys = sys;

    es_format_Init(&sys->fmt_video, VIDEO_ES, VLC_CODEC_HEVC);
    sys->fmt_video.b_packetized = 0;

    vlc_ureduce(&sys->fmt_video.video.i_frame_rate,
                &sys->fmt_video.video.i_frame_rate_base,
                fps_num, fps_den, 0);
    date_Init(&sys->pcr, sys->fmt_video.video.i_frame_rate,
               sys->fmt_video.video.i_frame_rate_base);
    date_Set(&sys->pcr, 0);

    demux->pf_demux = Demux;
    demux->pf_control = Control;

    sys->es_video = es_out_Add(demux->out, &sys->fmt_video);
    return VLC_SUCCESS;
}

/*****************************************************************************
 * Close: frees unused data
 *****************************************************************************/
static void Close(vlc_object_t *p_this)
{
    demux_t *demux = (demux_t *) p_this;
    demux_sys_t *sys = demux->p_sys;

    free(sys);
}

/*****************************************************************************
 * Demux: reads and demuxes data packets
 *****************************************************************************
 * Returns -1 in case of error, 0 in case of EOF, 1 otherwise
 *****************************************************************************/
static int Demux(demux_t *p_demux)
{
    demux_sys_t *sys = p_demux->p_sys;
    mtime_t pcr = date_Get(&sys->pcr);
    block_t *p_block;

    /* Call the pace control */
    es_out_Control(p_demux->out, ES_OUT_SET_PCR, VLC_TS_0 + pcr);

    // TODO(fancycode): make sure to read a complete frame
    if ((p_block = stream_Block(p_demux->s, DEMUX_FRAME_SIZE)) == NULL) {
        /* EOF */
        return 0;
    }

    p_block->i_dts = p_block->i_pts = VLC_TS_0 + pcr;
    es_out_Send(p_demux->out, sys->es_video, p_block);

    date_Increment(&sys->pcr, 1);
    return 1;
}

/*****************************************************************************
 * Control:
 *****************************************************************************/
static int Control(demux_t *p_demux, int i_query, va_list args)
{
    // TODO(fancycode): which queries should we handle directly?
    return demux_vaControlHelper(p_demux->s, 0, -1, -1, -1, i_query, args);
}
