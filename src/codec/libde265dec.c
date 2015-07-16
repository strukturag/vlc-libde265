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

#include <assert.h>

#include <vlc_common.h>
#include <vlc_plugin.h>
#include <vlc_codec.h>

#include <libde265/de265.h>

#include "../../include/libde265_plugin_common.h"

// Default size of length headers for packetized streams.
// Should always come from the "extra" data.
#define DEFAULT_LENGTH_SIZE     4

// Maximum number of threads to use
#define MAX_THREAD_COUNT        32

// Drop all frames if late frames were available for more than 5 seconds
#define LATE_FRAMES_DROP_ALWAYS_AGE 5

// Tell decoder to skip decoding if more than 4 late frames
#define LATE_FRAMES_DROP_DECODER    4

// Don't pass data to decoder if more than 12 late frames
#define LATE_FRAMES_DROP_HARD       12

#define THREADS_TEXT N_("Threads")
#define THREADS_LONGTEXT N_("Number of threads used for decoding, 0 meaning auto")

#define DISABLE_DEBLOCKING_TEXT N_("Disable deblocking?")
#define DISABLE_DEBLOCKING_LONGTEXT N_("Disabling the deblocking filter " \
    "usually has a detrimental effect on quality. However it provides a big " \
    "speedup for high definition streams.")

#define DISABLE_SAO_TEXT N_("Disable SAO filter?")
#define DISABLE_SAO_LONGTEXT N_("Disabling the sample adaptive offset filter " \
    "usually has a detrimental effect on quality. However it provides a big " \
    "speedup for high definition streams.")

#ifndef VLC_CODEC_HEV1
#define VLC_CODEC_HEV1 VLC_FOURCC('h','e','v','1')
#endif

/****************************************************************************
 * Local prototypes
 ****************************************************************************/
static int Open(vlc_object_t *);
static void Close(vlc_object_t *);

#define CODEC_UNKNOWN  VLC_FOURCC(0xff, 0xff, 0xff, 0xff)

#if LIBDE265_NUMERIC_VERSION < 0x01000000
// libde265 < 1.0 only supported 8 bits per pixel
#define de265_get_bits_per_pixel(image, plane) 8
#endif

#ifdef HAVE_VLC_REFCOUNT_PICTURE
#define decoder_DeletePicture(decoder, picture) picture_Release(picture)
#define decoder_LinkPicture(decoder, picture) picture_Hold(picture)
#define decoder_UnlinkPicture(decoder, picture) picture_Release(picture)
#endif

/*****************************************************************************
 * Module descriptor
 *****************************************************************************/

vlc_module_begin ()
    set_shortname(N_("HEVC/H.265"))
    set_description(N_("HEVC/H.265 video decoder using libde265"))
    set_capability("decoder", 200)
    set_callbacks(Open, Close)
    set_category(CAT_INPUT)
    set_subcategory(SUBCAT_INPUT_VCODEC)
    add_shortcut("libde265dec")
    add_integer("libde265-threads", 0, THREADS_TEXT, THREADS_LONGTEXT, true);
    add_bool("libde265-disable-deblocking", false, DISABLE_DEBLOCKING_TEXT, DISABLE_DEBLOCKING_LONGTEXT, false)
    add_bool("libde265-disable-sao", false, DISABLE_SAO_TEXT, DISABLE_SAO_LONGTEXT, false)
vlc_module_end ()

/*****************************************************************************
 * decoder_sys_t: libde265 decoder descriptor
 *****************************************************************************/
struct decoder_sys_t
{
    de265_decoder_context *ctx;

    mtime_t late_frames_start;
    int length_size;
    int late_frames;
    int decode_ratio;
    bool check_extra;
    bool packetized;
    bool disable_deblocking;
    bool disable_sao;
    int direct_rendering_used;
};

/*****************************************************************************
 * picture_ref_t: an reference to a vlc picture stored in a libde265 image
 *****************************************************************************/
struct picture_ref_t
{
    decoder_t *decoder;
    picture_t *picture;
};

static inline enum de265_chroma ImageFormatToChroma(enum de265_image_format format) {
    switch (format) {
    case de265_image_format_mono8:
        return de265_chroma_mono;
    case de265_image_format_YUV420P8:
        return de265_chroma_420;
    case de265_image_format_YUV422P8:
        return de265_chroma_422;
    case de265_image_format_YUV444P8:
        return de265_chroma_444;
    default:
        assert(false);
        return 0;
    }
}

static vlc_fourcc_t GetVlcCodec(decoder_t *dec, enum de265_chroma chroma, int bits_per_pixel) {
    vlc_fourcc_t result = CODEC_UNKNOWN;
    switch (chroma) {
    case de265_chroma_mono:
        result = VLC_CODEC_GREY;
        break;
    case de265_chroma_420:
        switch (bits_per_pixel) {
        case 8:
            result = VLC_CODEC_I420;
            break;
        case 9:
            result = VLC_CODEC_I420_9L;
            break;
        case 10:
            result = VLC_CODEC_I420_10L;
            break;
        default:
            if (bits_per_pixel > 10 && bits_per_pixel <= 16) {
#ifdef VLC_CODEC_I420_16L
                result = VLC_CODEC_I420_16L;
#else
                result = VLC_CODEC_I420_10L;
#endif
            } else {
                msg_Err(dec, "Unsupported output colorspace %d with %d bits per pixel",
                    chroma, bits_per_pixel);
            }
            break;
        }
        break;
    case de265_chroma_422:
        switch (bits_per_pixel) {
        case 8:
            result = VLC_CODEC_I422;
            break;
        case 9:
            result = VLC_CODEC_I422_9L;
            break;
        case 10:
            result = VLC_CODEC_I422_10L;
            break;
        default:
            if (bits_per_pixel > 10 && bits_per_pixel <= 16) {
#ifdef VLC_CODEC_I422_16L
                result = VLC_CODEC_I422_16L;
#else
                result = VLC_CODEC_I422_10L;
#endif
            } else {
                msg_Err(dec, "Unsupported output colorspace %d with %d bits per pixel",
                    chroma, bits_per_pixel);
            }
            break;
        }
        break;
    case de265_chroma_444:
        switch (bits_per_pixel) {
        case 8:
            result = VLC_CODEC_I444;
            break;
        case 9:
            result = VLC_CODEC_I444_9L;
            break;
        case 10:
            result = VLC_CODEC_I444_10L;
            break;
        default:
            if (bits_per_pixel > 10 && bits_per_pixel <= 16) {
#ifdef VLC_CODEC_I444_16L
                result = VLC_CODEC_I444_16L;
#else
                result = VLC_CODEC_I444_10L;
#endif
            } else {
                msg_Err(dec, "Unsupported output colorspace %d with %d bits per pixel",
                    chroma, bits_per_pixel);
            }
            break;
        }
        break;
    default:
        msg_Err(dec, "Unsupported output colorspace %d",
            chroma);
        break;
    }
    return result;
}

/*****************************************************************************
 * SetDecodeRation: tell the decoder to decode only a percentage of the framerate
 *****************************************************************************/
static void SetDecodeRatio(decoder_sys_t *sys, int ratio)
{
    if (ratio != sys->decode_ratio) {
        de265_decoder_context *ctx = sys->ctx;
        sys->decode_ratio = ratio;
        de265_set_framerate_ratio(ctx, ratio);
        if (ratio < 100) {
            de265_set_parameter_bool(sys->ctx, DE265_DECODER_PARAM_DISABLE_DEBLOCKING, true);
            de265_set_parameter_bool(sys->ctx, DE265_DECODER_PARAM_DISABLE_SAO, true);
        } else {
            de265_set_parameter_bool(sys->ctx, DE265_DECODER_PARAM_DISABLE_DEBLOCKING, sys->disable_deblocking);
            de265_set_parameter_bool(sys->ctx, DE265_DECODER_PARAM_DISABLE_SAO, sys->disable_sao);
        }
    }
}

/****************************************************************************
 * Decode: the whole thing
 ****************************************************************************/
static picture_t *Decode(decoder_t *dec, block_t **pp_block)
{
    decoder_sys_t *sys = dec->p_sys;
    de265_decoder_context *ctx = sys->ctx;
    bool drawpicture;
    bool prerolling;
    de265_error err;
    int can_decode_more;
    const struct de265_image *image;

    block_t *block = *pp_block;
    if (!block)
        return NULL;

    if (block->i_flags & (BLOCK_FLAG_DISCONTINUITY|BLOCK_FLAG_CORRUPTED)) {
        SetDecodeRatio(sys, 100);
        sys->late_frames = 0;
        if (block->i_flags & BLOCK_FLAG_DISCONTINUITY) {
            de265_reset(ctx);
        }
        goto error;
    }

    if (sys->check_extra) {
        int extra_length = dec->fmt_in.i_extra;
        sys->check_extra = false;
        if (extra_length > 0) {
            unsigned char *extra = (unsigned char *) dec->fmt_in.p_extra;
            if (extra_length > 3 && extra != NULL && (extra[0] || extra[1] || extra[2] > 1)) {
                // encoded in "hvcC" format (assume version 0)
                sys->packetized = true;
                if (extra_length > 22) {
                    if (extra[0] != 0) {
                        msg_Warn(dec, "Unsupported extra data version %d, decoding may fail", extra[0]);
                    }
                    sys->length_size = (extra[21] & 3) + 1;
                    int num_param_sets = extra[22];
                    int pos = 23;
                    for (int i=0; i<num_param_sets; i++) {
                        if (pos + 3 > extra_length) {
                            msg_Err(dec, "Buffer underrun in extra header (%d >= %d)", pos + 3, extra_length);
                            goto error;
                        }
                        // ignore flags + NAL type (1 byte)
                        int nal_count  = extra[pos+1] << 8 | extra[pos+2];
                        pos += 3;
                        for (int j=0; j<nal_count; j++) {
                            if (pos + 2 > extra_length) {
                                msg_Err(dec, "Buffer underrun in extra nal header (%d >= %d)", pos + 2, extra_length);
                                goto error;
                            }
                            int nal_size = extra[pos] << 8 | extra[pos+1];
                            if (pos + 2 + nal_size > extra_length) {
                                msg_Err(dec, "Buffer underrun in extra nal (%d >= %d)", pos + 2 + nal_size, extra_length);
                                goto error;
                            }
                            err = de265_push_NAL(ctx, extra + pos + 2, nal_size, 0, NULL);
                            if (!de265_isOK(err)) {
                                msg_Err(dec, "Failed to push data: %s (%d)", de265_get_error_text(err), err);
                                goto error;
                            }
                            pos += 2 + nal_size;
                        }
                    }
                }
                msg_Dbg(dec, "Assuming packetized data (%d bytes length)", sys->length_size);
            } else {
                sys->packetized = false;
                msg_Dbg(dec, "Assuming non-packetized data");
                err = de265_push_data(ctx, extra, extra_length, 0, NULL);
                if (!de265_isOK(err)) {
                    msg_Err(dec, "Failed to push extra data: %s (%d)", de265_get_error_text(err), err);
                    goto error;
                }
            }
            de265_push_end_of_NAL(ctx);
            do {
                err = de265_decode(ctx, &can_decode_more);
                switch (err) {
                case DE265_OK:
                    break;

                case DE265_ERROR_IMAGE_BUFFER_FULL:
                case DE265_ERROR_WAITING_FOR_INPUT_DATA:
                    // not really an error
                    can_decode_more = 0;
                    break;

                default:
                    if (!de265_isOK(err)) {
                        msg_Err(dec, "Failed to decode extra data: %s (%d)", de265_get_error_text(err), err);
                        goto error;
                    }
                }
            } while (can_decode_more);
        }
    }

    if ((prerolling = (block->i_flags & BLOCK_FLAG_PREROLL))) {
        SetDecodeRatio(sys, 100);
        sys->late_frames = 0;
        drawpicture = false;
    } else {
        drawpicture = true;
    }

    if (!dec->b_pace_control && (sys->late_frames > 0) &&
        (mdate() - sys->late_frames_start > LATE_FRAMES_DROP_ALWAYS_AGE*CLOCK_FREQ)) {
        sys->late_frames--;
        msg_Err(dec, "more than %d seconds of late video -> "
                "dropping frame (computer too slow ?)", LATE_FRAMES_DROP_ALWAYS_AGE);
        goto error;
    }

    if (!dec->b_pace_control &&
        (sys->late_frames > LATE_FRAMES_DROP_DECODER)) {
        drawpicture = false;
        if (sys->late_frames < LATE_FRAMES_DROP_HARD) {
            // tell the decoder to skip frames
            SetDecodeRatio(sys, 0);
        } else {
            // picture too late, won't decode, but break picture until
            // a new keyframe is available
            sys->late_frames--; /* needed else it will never be decrease */
            msg_Warn(dec, "More than %d late frames, dropping frame", LATE_FRAMES_DROP_DECODER);
            goto error;
        }
    }

    mtime_t pts = block->i_pts;
    bool use_decoder_pts = true;
    if (pts == 0 || pts == VLC_TS_INVALID) {
        pts = block->i_dts;
        use_decoder_pts = false;
    }

    uint8_t *p_buffer = block->p_buffer;
    size_t i_buffer = block->i_buffer;
    if (i_buffer > 0) {
        if (sys->packetized) {
            while (i_buffer >= (size_t) sys->length_size) {
                int i;
                uint32_t length = 0;
                for (i=0; i<sys->length_size; i++) {
                    length = (length << 8) | p_buffer[i];
                }

                p_buffer += sys->length_size;
                i_buffer -= sys->length_size;
                if (length > i_buffer) {
                    msg_Err(dec, "Buffer underrun while pushing data (%d > %ld)", length, i_buffer);
                    goto error;
                }

                err = de265_push_NAL(ctx, p_buffer, length, pts, NULL);
                if (!de265_isOK(err)) {
                    msg_Err(dec, "Failed to push data: %s (%d)", de265_get_error_text(err), err);
                    goto error;
                }

                p_buffer += length;
                i_buffer -= length;
            }
        } else {
            err = de265_push_data(ctx, p_buffer, i_buffer, pts, NULL);
            if (!de265_isOK(err)) {
                msg_Err(dec, "Failed to push data: %s (%d)", de265_get_error_text(err), err);
                goto error;
            }
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

    // decode (and skip) all available images (e.g. when prerolling
    // after a seek)
    do {
        // decode data until we get an image or no more data is
        // available for decoding
        do {
            err = de265_decode(ctx, &can_decode_more);
            switch (err) {
            case DE265_OK:
                break;

            case DE265_ERROR_IMAGE_BUFFER_FULL:
            case DE265_ERROR_WAITING_FOR_INPUT_DATA:
                // not really an error
                can_decode_more = 0;
                break;

            default:
                if (!de265_isOK(err)) {
                    msg_Err(dec, "Failed to decode frame: %s (%d)", de265_get_error_text(err), err);
                    return NULL;
                }
            }

            image = de265_get_next_picture(ctx);
        } while (image == NULL && can_decode_more);

        // log warnings
        for (;;) {
            de265_error warning = de265_get_warning(ctx);
            if (warning == DE265_OK) {
                break;
            }

            msg_Warn(dec, "%s", de265_get_error_text(warning));
        }

        if (!image) {
            return NULL;
        }

        if (use_decoder_pts) {
            pts = de265_get_image_PTS(image);
        }

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
            SetDecodeRatio(sys, 100);
            sys->late_frames = 0;
        }
    } while (!drawpicture);

    int bits_per_pixel = __MAX(__MAX(de265_get_bits_per_pixel(image, 0),
                                     de265_get_bits_per_pixel(image, 1)),
                               de265_get_bits_per_pixel(image, 2));

    vlc_fourcc_t chroma = GetVlcCodec(dec,
        de265_get_chroma_format(image),
        bits_per_pixel);
    if (chroma == CODEC_UNKNOWN) {
        return NULL;
    }

    dec->fmt_out.i_codec = chroma;

    video_format_t *v = &dec->fmt_out.video;
    v->i_chroma = chroma;

    int width = de265_get_image_width(image, 0);
    int height = de265_get_image_height(image, 0);

    if (width != (int) v->i_width || height != (int) v->i_height) {
        v->i_width = width;
        v->i_height = height;
    }
    if (width != (int) v->i_visible_width || height != (int) v->i_visible_height) {
        v->i_visible_width = width;
        v->i_visible_height = height;
    }

    picture_t *pic;
    struct picture_ref_t *ref = (struct picture_ref_t *) de265_get_image_plane_user_data(image, 0);
    if (ref != NULL) {
        // using direct rendering
        pic = ref->picture;
        decoder_LinkPicture(dec, pic);
    } else {
        pic = decoder_NewPicture(dec);
        if (!pic)
            return NULL;

        const vlc_chroma_description_t *vlc_chroma = vlc_fourcc_GetChromaDescription(chroma);
        assert(vlc_chroma != NULL);

        int max_bits_per_pixel = vlc_chroma->pixel_bits;
        for (int plane = 0; plane < pic->i_planes; plane++ ) {
            int src_stride;
            const uint8_t *src = de265_get_image_plane(image, plane, &src_stride);
            int plane_bits_per_pixel = de265_get_bits_per_pixel(image, plane);
            int dst_stride = pic->p[plane].i_pitch;
            uint8_t *dst = pic->p[plane].p_pixels;

            int size = __MIN( src_stride, dst_stride );
            if (plane_bits_per_pixel > max_bits_per_pixel) {
                // More bits per pixel in this plane than supported by the VLC output format
                int shift = (plane_bits_per_pixel - max_bits_per_pixel);
                for( int line = 0; line < pic->p[plane].i_visible_lines; line++ ) {
                    uint16_t *s = (uint16_t *) src;
                    uint16_t *d = (uint16_t *) dst;
                    for (int pos=0; pos<size/2; pos++) {
                        *d = *s >> shift;
                        d++;
                        s++;
                    }
                    src += src_stride;
                    dst += dst_stride;
                }
            } else if (plane_bits_per_pixel < max_bits_per_pixel && plane_bits_per_pixel > 8) {
                // Less bits per pixel in this plane than the rest of the picture
                // but more than 8bpp.
                int shift = (max_bits_per_pixel - plane_bits_per_pixel);
                for( int line = 0; line < pic->p[plane].i_visible_lines; line++ ) {
                    uint16_t *s = (uint16_t *) src;
                    uint16_t *d = (uint16_t *) dst;
                    for (int pos=0; pos<size/2; pos++) {
                        *d = *s << shift;
                        d++;
                        s++;
                    }
                    src += src_stride;
                    dst += dst_stride;
                }
            } else if (plane_bits_per_pixel < max_bits_per_pixel && plane_bits_per_pixel == 8) {
                // 8 bits per pixel in this plane, which is less than the rest of the picture.
                int shift = (max_bits_per_pixel - plane_bits_per_pixel);
                for( int line = 0; line < pic->p[plane].i_visible_lines; line++ ) {
                    uint8_t *s = (uint8_t *) src;
                    uint16_t *d = (uint16_t *) dst;
                    for (int pos=0; pos<size; pos++) {
                        *d = *s << shift;
                        d++;
                        s++;
                    }
                    src += src_stride;
                    dst += dst_stride;
                }
            } else {
                // Bits per pixel of image match output format.
                for( int line = 0; line < pic->p[plane].i_visible_lines; line++ ) {
                    memcpy( dst, src, size );
                    src += src_stride;
                    dst += dst_stride;
                }
            }
        }
    }

    pic->b_progressive = true; /* codec does not support interlacing */
    pic->date = pts;

    return pic;

error:
    block_Release(*pp_block);
    *pp_block = NULL;
    return NULL;
}

/*****************************************************************************
 * ReleasePictureRef: release a reference to a vlc picture
 *****************************************************************************/
static void ReleasePictureRef(struct picture_ref_t *ref)
{
    decoder_UnlinkPicture(ref->decoder, ref->picture);
    free(ref);
}

/*****************************************************************************
 * GetPicture: create a vlc picture that can be used for direct rendering
 *****************************************************************************/
static picture_t *GetPicture(decoder_t *dec, struct de265_image_spec* spec, struct de265_image *image)
{
    decoder_sys_t *sys = dec->p_sys;
    int width = (spec->width + spec->alignment - 1) / spec->alignment * spec->alignment;
    int height = spec->height;

    if (width == 0 || height == 0 || width > 8192 || height > 8192) {
        msg_Err(dec, "Invalid frame size %dx%d.", width, height);
        return NULL;
    }

    enum de265_chroma image_chroma = ImageFormatToChroma(spec->format);
    if (image_chroma != de265_chroma_mono) {
        if (de265_get_bits_per_pixel(image, 0) != de265_get_bits_per_pixel(image, 1) ||
            de265_get_bits_per_pixel(image, 0) != de265_get_bits_per_pixel(image, 2) ||
            de265_get_bits_per_pixel(image, 1) != de265_get_bits_per_pixel(image, 2)) {
            if (sys->direct_rendering_used != 0) {
                msg_Dbg(dec, "input format has multiple bits per pixel (%d/%d/%d)",
                        de265_get_bits_per_pixel(image, 0),
                        de265_get_bits_per_pixel(image, 1),
                        de265_get_bits_per_pixel(image, 2));
            }
            return NULL;
        }
    }

    int bits_per_pixel = de265_get_bits_per_pixel(image, 0);
    vlc_fourcc_t chroma = GetVlcCodec(dec, image_chroma, bits_per_pixel);
    if (chroma == CODEC_UNKNOWN) {
        // Unsupported chroma format.
        return NULL;
    }

    const vlc_chroma_description_t *dsc = vlc_fourcc_GetChromaDescription(chroma);
    assert(dsc != NULL);
    if (dsc->pixel_bits < (unsigned) bits_per_pixel) {
        if (sys->direct_rendering_used != 0) {
            msg_Dbg(dec, "output format doesn't provide enough bits per pixel (%d/%d)",
                    dsc->pixel_bits, bits_per_pixel);
        }
        return NULL;
    }

    for (unsigned int i=0; dsc && i<dsc->plane_count; i++) {
        int plane_width = width * dsc->p[i].w.num / dsc->p[i].w.den;
        int aligned_width = (plane_width + spec->alignment - 1) / spec->alignment * spec->alignment;;
        if (plane_width != aligned_width) {
            if (sys->direct_rendering_used != 0) {
                msg_Dbg(dec, "plane %d: aligned width doesn't match (%d/%d)",
                        i, plane_width, aligned_width);
            }
            return NULL;
        }
    }

    dec->fmt_out.i_codec = chroma;
    dec->fmt_out.video.i_chroma = chroma;
    dec->fmt_out.video.i_width = width;
    dec->fmt_out.video.i_height = height;

    if (width != spec->visible_width || height != spec->visible_height) {
        dec->fmt_out.video.i_x_offset = spec->crop_left;
        dec->fmt_out.video.i_y_offset = spec->crop_top;
        dec->fmt_out.video.i_visible_width = spec->visible_width;
        dec->fmt_out.video.i_visible_height = spec->visible_height;
    } else {
        dec->fmt_out.video.i_x_offset = 0;
        dec->fmt_out.video.i_y_offset = 0;
        dec->fmt_out.video.i_visible_width = width;
        dec->fmt_out.video.i_visible_height = height;
    }

    picture_t *pic = decoder_NewPicture(dec);
    if (pic == NULL) {
        return NULL;
    }

    if (pic->p[0].i_pitch < width * pic->p[0].i_pixel_pitch) {
        if (sys->direct_rendering_used != 0) {
            msg_Dbg(dec, "plane 0: pitch too small (%d/%d*%d)",
                    pic->p[0].i_pitch, width, pic->p[0].i_pixel_pitch);
        }
        goto error;
    }

    if (pic->p[0].i_lines < height) {
        if (sys->direct_rendering_used != 0) {
            msg_Dbg(dec, "plane 0: lines too few (%d/%d)",
                    pic->p[0].i_lines, height);
        }
        goto error;
    }

    for (int i = 0; i < pic->i_planes; i++) {
        if (pic->p[i].i_pitch % spec->alignment) {
            if (sys->direct_rendering_used != 0) {
                msg_Dbg(dec, "plane %d: pitch not aligned (%d%%%d)",
                        i, pic->p[i].i_pitch, spec->alignment);
            }
            goto error;
        }
        if (((uintptr_t)pic->p[i].p_pixels) % spec->alignment) {
            if (sys->direct_rendering_used != 0) {
                msg_Warn(dec, "plane %d not aligned", i);
            }
            goto error;
        }
    }
    return pic;

error:
    decoder_DeletePicture(dec, pic);
    return NULL;
}

/*****************************************************************************
 * GetBuffer: libde265 callback to create images
 *****************************************************************************/
static int GetBuffer(de265_decoder_context* ctx, struct de265_image_spec* spec, struct de265_image* img, void* userdata)
{
    decoder_t *dec = (decoder_t *) userdata;
    decoder_sys_t *sys = dec->p_sys;

    picture_t *pic = GetPicture(dec, spec, img);
    if (pic == NULL) {
        if (sys->direct_rendering_used != 0) {
            msg_Warn(dec, "disabling direct rendering");
            sys->direct_rendering_used = 0;
        }
        return de265_get_default_image_allocation_functions()->get_buffer(ctx, spec, img, userdata);
    }

    if (sys->direct_rendering_used != 1) {
        msg_Dbg(dec, "enabling direct rendering");
        sys->direct_rendering_used = 1;
    }
    for (int i = 0; i < pic->i_planes; i++) {
        struct picture_ref_t *ref = (struct picture_ref_t *) malloc(sizeof (*ref));
        if (ref == NULL) {
            goto error;
        }
        ref->decoder = dec;
        ref->picture = pic;
        decoder_LinkPicture(dec, pic);

        uint8_t *data = pic->p[i].p_pixels;
        int stride = pic->p[i].i_pitch;
        de265_set_image_plane(img, i, data, stride, ref);
    }
    decoder_UnlinkPicture(dec, pic);
    return 1;

error:
    for (int i=0; i<3; i++) {
        struct picture_ref_t *userdata = (struct picture_ref_t *) de265_get_image_plane_user_data(img, i);
        if (userdata) {
            ReleasePictureRef(userdata);
        }
    }
    decoder_DeletePicture(dec, pic);
    return de265_get_default_image_allocation_functions()->get_buffer(ctx, spec, img, userdata);
}

/*****************************************************************************
 * ReleaseBuffer: libde265 callback to release images
 *****************************************************************************/
static void ReleaseBuffer(de265_decoder_context* ctx, struct de265_image* img, void* userdata)
{
    int release_default = 1;
    for (int i=0; i<3; i++) {
        struct picture_ref_t *ref = (struct picture_ref_t *) de265_get_image_plane_user_data(img, i);
        if (ref) {
            ReleasePictureRef(ref);
            release_default = 0;
        }
    }

    if (release_default) {
        // image was created from default allocator
        de265_get_default_image_allocation_functions()->release_buffer(ctx, img, userdata);
    }
}

/*****************************************************************************
 * Open: probe the decoder
 *****************************************************************************/
static int Open(vlc_object_t *p_this)
{
    decoder_t *dec = (decoder_t *)p_this;

    if (dec->fmt_in.i_codec != VLC_CODEC_HEVC && dec->fmt_in.i_codec != VLC_CODEC_HEV1) {
        return VLC_EGENERIC;
    }

    decoder_sys_t *sys = malloc(sizeof(*sys));
    if (!sys)
        return VLC_ENOMEM;
    dec->p_sys = sys;

    msg_Dbg(p_this, "using libde265 version %s", de265_get_version());
    if ((sys->ctx = de265_new_decoder()) == NULL) {
        msg_Err(p_this, "Failed to initialize decoder");
        free(sys);
        return VLC_EGENERIC;
    }

    struct de265_image_allocation allocators;
    allocators.get_buffer = GetBuffer;
    allocators.release_buffer = ReleaseBuffer;
    de265_set_image_allocation_functions(sys->ctx, &allocators, dec);

    int threads = var_InheritInteger(dec, "libde265-threads");
    if (threads <= 0) {
        threads = vlc_GetCPUCount();
        // NOTE: We start more threads than cores for now, as some threads
        // might get blocked while waiting for dependent data. Having more
        // threads increases decoding speed by about 10%.
        threads = threads * 2;
    }
    if (threads > 1) {
        threads = __MIN(threads, MAX_THREAD_COUNT);
        de265_error err = de265_start_worker_threads(sys->ctx, threads);
        if (!de265_isOK(err)) {
            // don't report to caller, decoding will work anyway...
            msg_Err(dec, "Failed to start worker threads: %s (%d)", de265_get_error_text(err), err);
        } else {
            msg_Dbg(p_this, "Started %d worker threads", threads);
        }
    } else {
        msg_Dbg(p_this, "Using single-threaded decoding");
    }

    dec->pf_decode_video = Decode;

    dec->fmt_out.i_cat = VIDEO_ES;
    dec->fmt_out.video.i_chroma = VLC_CODEC_I420;
    dec->fmt_out.video.i_width = dec->fmt_in.video.i_width;
    dec->fmt_out.video.i_height = dec->fmt_in.video.i_height;
    dec->fmt_out.i_codec = VLC_CODEC_I420;
    dec->b_need_packetized = true;

    sys->check_extra = true;
    sys->length_size = DEFAULT_LENGTH_SIZE;
    sys->packetized = dec->fmt_in.b_packetized;
    sys->late_frames = 0;
    sys->decode_ratio = 100;
    sys->direct_rendering_used = -1;
    sys->disable_deblocking = var_InheritBool(dec, "libde265-disable-deblocking");
    sys->disable_sao = var_InheritBool(dec, "libde265-disable-sao");

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
