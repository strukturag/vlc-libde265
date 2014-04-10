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
#include <vlc_codec.h>

#include <libde265_plugin_common.h>

#include <libde265/de265.h>

/****************************************************************************
 * Local prototypes
 ****************************************************************************/
static int Open(vlc_object_t *);
static void Close(vlc_object_t *);

/*****************************************************************************
 * Module descriptor
 *****************************************************************************/

vlc_module_begin ()
    set_shortname("libde265dec")
    set_description(N_("HEVC/H.265 video decoder"))
    set_capability("decoder", 200)
    set_callbacks(Open, Close)
    set_category(CAT_INPUT)
    set_subcategory(SUBCAT_INPUT_VCODEC)
vlc_module_end ()

/*****************************************************************************
 * decoder_sys_t: libde265 decoder descriptor
 *****************************************************************************/
struct decoder_sys_t
{
    de265_decoder_context *ctx;

    int late_frames;
    mtime_t late_frames_start;
};

/****************************************************************************
 * Decode: the whole thing
 ****************************************************************************/
static picture_t *Decode(decoder_t *dec, block_t **pp_block)
{
    decoder_sys_t *sys = dec->p_sys;
    de265_decoder_context *ctx = sys->ctx;
    int drawpicture;
    int prerolling;

    block_t *block = *pp_block;
    if (!block)
        return NULL;

    if (block->i_flags & (BLOCK_FLAG_DISCONTINUITY|BLOCK_FLAG_CORRUPTED)) {
        sys->late_frames = 0;
        if (block->i_flags & BLOCK_FLAG_DISCONTINUITY) {
            de265_reset(ctx);
        }
        goto error;
    }

    if ((prerolling = (block->i_flags & BLOCK_FLAG_PREROLL))) {
        sys->late_frames = 0;
        drawpicture = 0;
    } else {
        drawpicture = 1;
    }

    if (!dec->b_pace_control && (sys->late_frames > 0) &&
        (mdate() - sys->late_frames_start > INT64_C(5000000))) {
        sys->late_frames--;
        msg_Err(dec, "more than 5 seconds of late video -> "
                "dropping frame (computer too slow ?)");
        goto error;
    }

    if (!dec->b_pace_control &&
        (sys->late_frames > 4)) {
        drawpicture = 0;
        if (sys->late_frames < 12) {
            // TODO(fancycode): tell decoder to skip frame
        } else {
            /* picture too late, won't decode
             * but break picture until a new I, and for mpeg4 ...*/
            sys->late_frames--; /* needed else it will never be decrease */
            msg_Warn(dec, "More than 4 late frames, dropping frame");
            goto error;
        }
    }

    de265_error err;
    uint8_t *p_buffer = block->p_buffer;
    size_t i_buffer = block->i_buffer;
    if (i_buffer > 0) {
        while (i_buffer >= 4) {
            uint32_t length = (p_buffer[0]<<24) + (p_buffer[1]<<16) + (p_buffer[2]<<8) + p_buffer[3];
            p_buffer += 4;
            i_buffer -= 4;
            if (length > i_buffer) {
                msg_Err(dec, "Buffer underrun while pushing data (%d > %ld)", length, i_buffer);
                goto error;
            }

            err = de265_push_NAL(ctx, p_buffer, length, block->i_pts, NULL);
            if (!de265_isOK(err)) {
                msg_Err(dec, "Failed to push data: %s (%d)", de265_get_error_text(err), err);
                goto error;
            }
            
            p_buffer += length;
            i_buffer -= length;
        }
    } else {
        err = de265_flush_data(ctx);
        if (!de265_isOK(err)) {
            msg_Err(dec, "Failed to flush data: %s (%d)", de265_get_error_text(err), err);
            goto error;
        }
    }
    block_Release(block);
    *pp_block = NULL;

    int more;
    const struct de265_image *image;
    mtime_t pts;
    // decode (and skip) all available images (e.g. when prerolling
    // after a seek)
    do {
        // decode data until we get an image or no more data is
        // available for decoding
        do {
            err = de265_decode(ctx, &more);
            switch (err) {
            case DE265_OK:
                break;

            case DE265_ERROR_IMAGE_BUFFER_FULL:
            case DE265_ERROR_WAITING_FOR_INPUT_DATA:
                // not really an error
                more = 0;
                break;

            default:
                if (!de265_isOK(err)) {
                    msg_Err(dec, "Failed to decode frame: %s (%d)", de265_get_error_text(err), err);
                    return NULL;
                }
            }

            image = de265_get_next_picture(ctx);
        } while (image == NULL && more);
        if (!image) {
            return NULL;
        }

        if (de265_get_chroma_format(image) != de265_chroma_420) {
            msg_Err(dec, "Unsupported output colorspace %d", de265_get_chroma_format(image));
            return NULL;
        }

        pts = de265_get_image_PTS(image);

        mtime_t display_date = 0;
        if (!prerolling) {
            display_date = decoder_GetDisplayDate(dec, pts);
        }

        if (display_date > 0 && display_date <= mdate()) {
            sys->late_frames++;
            if (sys->late_frames == 1) {
                sys->late_frames_start = mdate();
            }
        } else {
            sys->late_frames = 0;
        }
    } while (!drawpicture);

    video_format_t *v = &dec->fmt_out.video;
    int width = de265_get_image_width(image, 0);
    int height = de265_get_image_height(image, 0);

    if (width != (int) v->i_visible_width || height != (int) v->i_visible_height) {
        v->i_visible_width = width;
        v->i_visible_height = height;
    }

    picture_t *pic = decoder_NewPicture(dec);
    if (!pic)
        return NULL;

    for (int plane = 0; plane < pic->i_planes; plane++ ) {
        int src_stride;
        const uint8_t *src = de265_get_image_plane(image, plane, &src_stride);
        int dst_stride = pic->p[plane].i_pitch;
        uint8_t *dst = pic->p[plane].p_pixels;

        int size = __MIN( src_stride, dst_stride );
        for( int line = 0; line < pic->p[plane].i_visible_lines; line++ ) {
            memcpy( dst, src, size );
            src += src_stride;
            dst += dst_stride;
        }
    }

    pic->b_progressive = true; /* codec does not support interlacing */
    pic->date = pts;

    return pic;

error:
    if (*pp_block != NULL) {
        block_Release(*pp_block);
    }
    *pp_block = NULL;
    return NULL;
}

/*****************************************************************************
 * Open: probe the decoder
 *****************************************************************************/
static int Open(vlc_object_t *p_this)
{
    decoder_t *dec = (decoder_t *)p_this;

    if (dec->fmt_in.i_codec != VLC_CODEC_HEVC) {
        return VLC_EGENERIC;
    }

    decoder_sys_t *sys = malloc(sizeof(*sys));
    if (!sys)
        return VLC_ENOMEM;
    dec->p_sys = sys;

    msg_Dbg(p_this, "using libde265 version %s", de265_get_version());
    if ((sys->ctx = de265_new_decoder()) == NULL) {
        msg_Err(p_this, "Failed to initialize decoder\n");
        free(sys);
        return VLC_EGENERIC;
    }

    int threads = __MIN(vlc_GetCPUCount() * 2, 32);
    if (threads > 1) {
        de265_error err;
        err = de265_start_worker_threads(sys->ctx, threads);
        if (!de265_isOK(err)) {
            // don't report to caller, decoding will work anyway...
            msg_Err(dec, "Failed to start worker threads: %s (%d)", de265_get_error_text(err), err);
        } else {
            msg_Dbg(p_this, "started %d worker threads", threads);
        }
    }

    dec->pf_decode_video = Decode;

    dec->fmt_out.i_cat = VIDEO_ES;
    dec->fmt_out.video.i_width = dec->fmt_in.video.i_width;
    dec->fmt_out.video.i_height = dec->fmt_in.video.i_height;
    dec->fmt_out.i_codec = VLC_CODEC_I420;
    dec->b_need_packetized = true;

    sys->late_frames = 0;

    return VLC_SUCCESS;
}

/*****************************************************************************
 * Close: decoder destruction
 *****************************************************************************/
static void Close(vlc_object_t *p_this)
{
    decoder_t *dec = (decoder_t *)p_this;
    decoder_sys_t *sys = dec->p_sys;

    de265_free_decoder(sys->ctx);

    free(sys);
}
