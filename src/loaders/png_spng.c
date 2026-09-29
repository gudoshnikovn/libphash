/* The PNG backend on spng (PHASH_USE_SPNG). The build compiles this file or
 * png_libpng.c, never both: they are two implementations of the same backend and both
 * define ph_decode_png_mem(). */
#include "image/image.h"
#include "loader.h"
#include "loaders/backends.h"
#include "safety.h"

#include "spng.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Unlike libpng's setjmp/longjmp model, every spng call returns its own status
 * code directly -- SPNG_EMEM is spng's own distinct "an internal allocation
 * failed" value (vendor/spng/spng/spng.h), so this is a precise, non-string-guess
 * classification, the same idea as VP8_STATUS_OUT_OF_MEMORY for the WebP backend
 * (src/loaders/webp.c). Without this, every spng failure -- OOM or genuinely
 * corrupt data -- would collapse to PH_ERR_CORRUPT_DATA below. */
static ph_error_t ph_spng_err(int ret) {
    return (ret == SPNG_EMEM) ? PH_ERR_ALLOCATION_FAILED : PH_ERR_CORRUPT_DATA;
}

unsigned char *ph_decode_png_mem(const unsigned char *buffer, size_t size, int *width, int *height,
                                 int *channels, int req_comp, uint64_t max_pixels,
                                 ph_decode_scale_t decode_scale, ph_error_t *out_err, char *err_msg,
                                 size_t err_msg_cap) {
    /* PNG has no format-level scaled decode; decode_scale is a JPEG-only optimization
     * (see ph_context_set_decode_scale()), silently ignored here as documented. */
    (void)decode_scale;
    if (!buffer || size < 8) {
        return NULL;
    }

    /* Checked before the buffer reaches libpng/spng so both backends agree on the
     * verdict and the error code, and so an absurd dimension is refused before any
     * row buffer is sized. */
    if (!ph_png_dimensions_within_limit(buffer, size)) {
        if (out_err) {
            *out_err = PH_ERR_IMAGE_TOO_LARGE;
        }
        ph_set_err_msg(err_msg, err_msg_cap, "PNG dimension exceeds the supported maximum");
        return NULL;
    }

    spng_ctx *ctx = spng_ctx_new(0);
    if (!ctx) {
        if (out_err) {
            *out_err = PH_ERR_ALLOCATION_FAILED;
        }
        ph_set_err_msg(err_msg, err_msg_cap, "Memory allocation failed");
        return NULL;
    }

    if (max_pixels != 0) {
        // Defense in depth against huge ancillary-chunk allocations.
        spng_set_chunk_limits(ctx, 128 * 1024 * 1024, 128 * 1024 * 1024);
    }

    int ret = spng_set_png_buffer(ctx, buffer, size);
    if (ret != 0) {
        if (out_err) {
            *out_err = ph_spng_err(ret);
        }
        ph_set_err_msg(err_msg, err_msg_cap, spng_strerror(ret));
        spng_ctx_free(ctx);
        return NULL;
    }

    struct spng_ihdr ihdr;
    ret = spng_get_ihdr(ctx, &ihdr);
    if (ret != 0) {
        if (out_err) {
            *out_err = ph_spng_err(ret);
        }
        ph_set_err_msg(err_msg, err_msg_cap, spng_strerror(ret));
        spng_ctx_free(ctx);
        return NULL;
    }

    if (ph_exceeds_pixel_limit(ihdr.width, ihdr.height, max_pixels)) {
        if (out_err) {
            *out_err = PH_ERR_IMAGE_TOO_LARGE;
        }
        ph_set_err_msg(err_msg, err_msg_cap, "Image exceeds the configured maximum pixel count");
        spng_ctx_free(ctx);
        return NULL;
    }

    /* spng accepts SPNG_FMT_G8 only for a genuinely grayscale PNG -- color type 0
     * with a bit depth of 8 or less (check_decode_fmt() in spng.c). For anything
     * else -- truecolor, palette, gray+alpha, 16-bit -- it rejects the request with
     * SPNG_EFMT ("invalid format") and the whole decode fails, so G8 cannot be
     * requested unconditionally.
     *
     * Take the same route libpng does: let spng deliver RGB8 (RGBA8 when the image has
     * alpha) whenever G8 is not applicable, then fold the pixels down here with the exact
     * weights the libpng path hands to png_set_rgb_to_gray_fixed() -- PH_GRAY_R/G/B over
     * 128, i.e. the Rec.601 weights of ph_to_grayscale() -- so both backends produce the
     * same bytes for the same input. Alpha comes back as the last channel, as from every
     * backend, for the caller to resolve (ph_resolve_alpha()); a tRNS chunk counts as
     * alpha, as libpng's png_set_tRNS_to_alpha() makes it. */
    struct spng_trns trns;
    const int has_trns = spng_get_trns(ctx, &trns) == 0;
    const int has_alpha = has_trns || ihdr.color_type == SPNG_COLOR_TYPE_GRAYSCALE_ALPHA ||
                          ihdr.color_type == SPNG_COLOR_TYPE_TRUECOLOR_ALPHA;
    const int gray_native =
        (ihdr.color_type == SPNG_COLOR_TYPE_GRAYSCALE && ihdr.bit_depth <= 8 && !has_alpha);
    int fmt;
    if (has_alpha) {
        fmt = SPNG_FMT_RGBA8;
    } else if (req_comp == 1 && gray_native) {
        fmt = SPNG_FMT_G8;
    } else {
        fmt = SPNG_FMT_RGB8;
    }
    const int decoded_channels = (fmt == SPNG_FMT_RGBA8) ? 4 : (fmt == SPNG_FMT_G8) ? 1 : 3;
    const int out_channels = (req_comp == 1) ? (has_alpha ? 2 : 1) : decoded_channels;
    size_t out_size;
    ret = spng_decoded_image_size(ctx, fmt, &out_size);
    if (ret != 0) {
        if (out_err) {
            *out_err = ph_spng_err(ret);
        }
        ph_set_err_msg(err_msg, err_msg_cap, spng_strerror(ret));
        spng_ctx_free(ctx);
        return NULL;
    }

    unsigned char *data = (unsigned char *)malloc(out_size);
    if (!data) {
        if (out_err) {
            *out_err = PH_ERR_ALLOCATION_FAILED;
        }
        ph_set_err_msg(err_msg, err_msg_cap, "Memory allocation failed");
        spng_ctx_free(ctx);
        return NULL;
    }

    ret = spng_decode_image(ctx, data, out_size, fmt, has_trns ? SPNG_DECODE_TRNS : 0);
    if (ret != 0) {
        if (out_err) {
            *out_err = ph_spng_err(ret);
        }
        ph_set_err_msg(err_msg, err_msg_cap, spng_strerror(ret));
        free(data);
        spng_ctx_free(ctx);
        return NULL;
    }

    spng_ctx_free(ctx);

    if (decoded_channels != out_channels) {
        /* In-place colour -> gray, keeping alpha: the destination index never runs ahead
         * of the source index, so a forward pass is safe. */
        size_t num_pixels = out_size / (size_t)decoded_channels;
        for (size_t i = 0; i < num_pixels; i++) {
            const unsigned char *src = data + i * (size_t)decoded_channels;
            unsigned int r = src[0];
            unsigned int g = src[1];
            unsigned int b = src[2];
            unsigned char alpha = has_alpha ? src[3] : 0;
            unsigned char *dst = data + i * (size_t)out_channels;
            dst[0] = (unsigned char)((PH_GRAY_R * r + PH_GRAY_G * g + PH_GRAY_B * b) >> 7);
            if (has_alpha) {
                dst[1] = alpha;
            }
        }
        /* Hand back a buffer of the size the caller believes it got. A failed shrink
         * is harmless -- the original block stays valid and merely oversized. */
        size_t out_bytes = num_pixels * (size_t)out_channels;
        unsigned char *shrunk = (unsigned char *)realloc(data, out_bytes ? out_bytes : 1);
        if (shrunk) {
            data = shrunk;
        }
    }

    *width = (int)ihdr.width;
    *height = (int)ihdr.height;
    *channels = out_channels;
    return data;
}
