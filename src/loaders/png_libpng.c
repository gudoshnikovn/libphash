/* The PNG backend on libpng (PHASH_USE_LIBPNG). The build compiles this file or
 * png_spng.c, never both: they are two implementations of the same backend and both
 * define ph_decode_png_mem(). */
#include "image/image.h"
#include "loader.h"
#include "loaders/backends.h"
#include "safety.h"

#include <png.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Custom memory read callback for png_set_read_fn
typedef struct {
    const unsigned char *data;
    size_t size;
    size_t offset;
} PngMemReader;

static void png_mem_read_fn(png_structp png_ptr, png_bytep out, png_size_t count) {
    PngMemReader *reader = (PngMemReader *)png_get_io_ptr(png_ptr);
    if (reader->offset + count > reader->size) {
        png_error(png_ptr, "Read past end of buffer");
        return;
    }
    memcpy(out, reader->data + reader->offset, count);
    reader->offset += count;
}

// Captures libpng's error text (normally lost to the longjmp) into our fixed-size
// diagnostic buffer instead of libpng's default behavior of printing to stderr.
typedef struct {
    char *err_msg;
    size_t err_msg_cap;
    ph_error_t *out_err;
} PngErrorCtx;

/* Three distinct, independently-worded OOM messages can reach png_error_fn():
 *   - libpng's own memory manager (pngmem.c) reports a failed internal malloc via
 *     png_error(png_ptr, "Out of memory") / png_error(png_ptr, "Out of Memory") --
 *     two different casings depending on which of its two allocation paths
 *     failed.
 *   - zlib running out of memory while inflating IDAT data (pixel decompression,
 *     not header parsing) surfaces as Z_MEM_ERROR, which png_zstream_error()
 *     (png.c) translates to the zstream message "insufficient memory" -- a third,
 *     unrelated wording, since it never goes through libpng's own allocator at
 *     all.
 * All three are otherwise ordinary fatal errors indistinguishable from a
 * malformed bitstream unless checked for here. (The JPEG backend needs no such
 * table: libjpeg reports every failed allocation as JERR_OUT_OF_MEMORY.) */
static int ph_png_message_is_oom(const char *msg) {
    /* Substring, not exact match: a chunk-level failure (e.g. reading IDAT itself
     * running out of memory) reaches here via png_chunk_error()/png_chunk_benign_error(),
     * which prepend the chunk name -- "IDAT: out of memory", "IDAT: insufficient
     * memory" -- to the same wording a bare (non-chunk) png_error() uses on its own. */
    return msg &&
           (strstr(msg, "Out of memory") != NULL || strstr(msg, "Out of Memory") != NULL ||
            strstr(msg, "out of memory") != NULL || strstr(msg, "insufficient memory") != NULL);
}

static void png_error_fn(png_structp png_ptr, png_const_charp msg) {
    PngErrorCtx *ectx = (PngErrorCtx *)png_get_error_ptr(png_ptr);
    /* If png_warning_fn below already classified this failure (currently: the
     * per-dimension user-limit check), keep its code and message -- the fatal error
     * that follows a user-limit warning is libpng's generic "Invalid IHDR data",
     * which would overwrite a specific answer with a useless one. */
    if (ectx && (!ectx->out_err || *ectx->out_err == PH_SUCCESS)) {
        ph_set_err_msg(ectx->err_msg, ectx->err_msg_cap, msg);
        if (ectx->out_err && ph_png_message_is_oom(msg)) {
            *ectx->out_err = PH_ERR_ALLOCATION_FAILED;
        }
    }
    longjmp(png_jmpbuf(png_ptr), 1);
}

static void png_warning_fn(png_structp png_ptr, png_const_charp msg) {
    (void)png_ptr;
    /* png_check_IHDR() reports png_set_user_limits() rejections as a *warning*
     * ("Image width/height exceeds user limit in IHDR"), not the png_error() that
     * follows moments later -- so without this, the fatal error's own generic message
     * ("Invalid IHDR data") is all that reaches the caller, and the code defaults to
     * PH_ERR_CORRUPT_DATA in the setjmp catch below. That mislabels a configured-limit
     * rejection as a broken file. Reclassify here, while the specific reason is still
     * available. */
    PngErrorCtx *ectx = (PngErrorCtx *)png_get_error_ptr(png_ptr);
    if (ectx && ectx->out_err && strstr(msg, "exceeds user limit") != NULL) {
        *ectx->out_err = PH_ERR_IMAGE_TOO_LARGE;
        ph_set_err_msg(ectx->err_msg, ectx->err_msg_cap, msg);
    }
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

    if (png_sig_cmp(buffer, 0, 8) != 0) {
        return NULL;
    }

    /* From the setjmp() below on, the diagnostic buffer is reached through ectx rather
     * than through the err_msg parameter: ectx lives in memory (libpng holds its
     * address), so longjmp() cannot leave it stale, where a parameter kept in a register
     * across setjmp() could be. */
    PngErrorCtx ectx = {.err_msg = err_msg, .err_msg_cap = err_msg_cap, .out_err = out_err};
    png_structp png_ptr =
        png_create_read_struct(PNG_LIBPNG_VER_STRING, &ectx, png_error_fn, png_warning_fn);
    if (!png_ptr) {
        /* png_create_read_struct()'s only failure mode is its own allocation failing,
         * and it fails by returning NULL directly rather than through png_error_fn --
         * the error callbacks aren't registered on png_ptr yet at this point, since
         * png_ptr doesn't exist. Leaving *out_err untouched here would fall through
         * to ph_decode_buffer()'s PH_SUCCESS-turned-PH_ERR_CORRUPT_DATA fallback
         * (src/loader.c) and misreport an OOM as corrupt image data. */
        if (out_err) {
            *out_err = PH_ERR_ALLOCATION_FAILED;
        }
        ph_set_err_msg(err_msg, err_msg_cap, "Memory allocation failed");
        return NULL;
    }

    /* setjmp() must be armed before ANY other libpng call on png_ptr that CAN
     * longjmp() through png_error_fn. png_create_info_struct() cannot (it
     * deliberately allocates through libpng's non-erroring png_malloc_base(), see
     * below), but every other libpng call below this point can.
     *
     * info_for_cleanup is volatile because it is assigned after setjmp() and read in
     * the longjmp branch: a non-volatile local modified between setjmp and longjmp has
     * an indeterminate value there (C11 7.13.2.1p3). It exists only so the jump branch
     * can free an info struct that may or may not have been created yet; the rest of
     * the function keeps using the plain info_ptr below.
     *
     * data_for_cleanup/row_ptrs_for_cleanup exist for the same reason, covering
     * png_read_image() below: it can still longjmp here (e.g. Z_MEM_ERROR from zlib
     * running out of memory mid-IDAT, translated to png_error() by
     * png_zstream_error()) after this function's own `data`/`row_ptrs` buffers are
     * already allocated -- without these, that path would leak both, since the plain
     * `data`/`row_ptrs` locals below aren't in scope up here and can't be read from
     * the jump branch regardless (same indeterminate-value rule as info_for_cleanup).
     *
     * Note the placement of the `volatile` on the two below: `unsigned char *
     * volatile` (volatile pointer) is what's needed, not `volatile unsigned char *`
     * (pointer to volatile data) -- the latter leaves the pointer *variable* itself
     * unprotected across the longjmp, which is exactly the object C11 7.13.2.1p3
     * calls out as having an indeterminate value. png_infop above sidesteps this
     * only because the typedef itself already names a pointer type, so `volatile
     * png_infop` lands the qualifier on the pointer as intended. */
    volatile png_infop info_for_cleanup = NULL;
    unsigned char *volatile data_for_cleanup = NULL;
    png_bytep *volatile row_ptrs_for_cleanup = NULL;

    if (setjmp(png_jmpbuf(png_ptr))) {
        // png_error_fn already captured the message and/or code (PH_ERR_IMAGE_TOO_LARGE
        // sites below set *out_err before their own longjmp-free early returns; this
        // path is libpng's own fatal errors, which are always a malformed bitstream).
        png_infop jumped_info = info_for_cleanup;
        free(row_ptrs_for_cleanup);
        free(data_for_cleanup);
        if (out_err && *out_err == PH_SUCCESS) {
            *out_err = PH_ERR_CORRUPT_DATA;
        }
        png_destroy_read_struct(&png_ptr, jumped_info ? &jumped_info : NULL, NULL);
        return NULL;
    }

    png_infop info_ptr = png_create_info_struct(png_ptr);
    if (!info_ptr) {
        /* png_create_info_struct() allocates via png_malloc_base() (vendor/libpng/png.c), libpng's
         * deliberately non-erroring allocator variant, specifically so this call
         * "always returns ok" instead of going through png_error()/longjmp() --
         * an OOM here returns NULL directly, bypassing png_error_fn entirely. */
        if (out_err) {
            *out_err = PH_ERR_ALLOCATION_FAILED;
        }
        ph_set_err_msg(ectx.err_msg, ectx.err_msg_cap, "Memory allocation failed");
        png_destroy_read_struct(&png_ptr, NULL, NULL);
        return NULL;
    }
    info_for_cleanup = info_ptr;

    // Zero-copy memory reading from mmap'd buffer
    PngMemReader reader = {.data = buffer, .size = size, .offset = 0};
    png_set_read_fn(png_ptr, &reader, png_mem_read_fn);

    if (max_pixels != 0) {
        // Defense in depth: cap each dimension individually (in addition to the
        // width*height check below) and cap ancillary-chunk allocations, so a
        // malicious header can't force a huge allocation before we even see w/h.
        /* Only ever LOWER libpng's own per-dimension default (1000000): passing
         * max_pixels (256 Mi by default) through would raise it, telling libpng a
         * 268435456-pixel-wide image is acceptable. */
        png_uint_32 dim_limit = (max_pixels > PH_MAX_IMAGE_DIMENSION) ? PH_MAX_IMAGE_DIMENSION
                                                                      : (png_uint_32)max_pixels;
        png_set_user_limits(png_ptr, dim_limit, dim_limit);
        png_set_chunk_malloc_max(png_ptr, 128 * 1024 * 1024);
    }

    png_read_info(png_ptr, info_ptr);

    png_uint_32 w, h;
    int bit_depth, color_type;
    png_get_IHDR(png_ptr, info_ptr, &w, &h, &bit_depth, &color_type, NULL, NULL, NULL);

    if (ph_exceeds_pixel_limit(w, h, max_pixels)) {
        if (out_err) {
            *out_err = PH_ERR_IMAGE_TOO_LARGE;
        }
        ph_set_err_msg(ectx.err_msg, ectx.err_msg_cap,
                       "Image exceeds the configured maximum pixel count");
        png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
        return NULL;
    }

    // Transform to 8-bit
    if (color_type == PNG_COLOR_TYPE_PALETTE) {
        png_set_palette_to_rgb(png_ptr);
    }
    if (color_type == PNG_COLOR_TYPE_GRAY && bit_depth < 8) {
        png_set_expand_gray_1_2_4_to_8(png_ptr);
    }
    if (bit_depth == 16) {
        png_set_strip_16(png_ptr);
    }
    if (png_get_valid(png_ptr, info_ptr, PNG_INFO_tRNS)) {
        png_set_tRNS_to_alpha(png_ptr);
    }

    if (req_comp == 1) {
        // Force grayscale using same weights as ph_to_grayscale (Rec. 601)
        if (color_type & PNG_COLOR_MASK_COLOR) {
            // Weights are scaled by 100,000 for libpng
            // R: 38/128 = 0.296875 -> 29688
            // G: 75/128 = 0.5859375 -> 58594
            png_set_rgb_to_gray_fixed(png_ptr, 1, 29688, 58594);
        }
        if (color_type & PNG_COLOR_MASK_ALPHA) {
            png_set_strip_alpha(png_ptr);
        }
    } else {
        // Force RGB (strip alpha)
        if (color_type == PNG_COLOR_TYPE_GRAY || color_type == PNG_COLOR_TYPE_GRAY_ALPHA) {
            png_set_gray_to_rgb(png_ptr);
        }
        if (color_type & PNG_COLOR_MASK_ALPHA) {
            png_set_strip_alpha(png_ptr);
        }
    }

    png_read_update_info(png_ptr, info_ptr);

    size_t rowbytes = png_get_rowbytes(png_ptr, info_ptr);
    int out_channels = (int)(rowbytes / w);

    size_t alloc_size;
    if (!ph_safe_image_alloc_size(rowbytes, h, 1, &alloc_size)) {
        if (out_err) {
            *out_err = PH_ERR_IMAGE_TOO_LARGE;
        }
        ph_set_err_msg(ectx.err_msg, ectx.err_msg_cap,
                       "Image exceeds the configured maximum pixel count");
        png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
        return NULL;
    }

    unsigned char *data = (unsigned char *)malloc(alloc_size);
    if (!data) {
        if (out_err) {
            *out_err = PH_ERR_ALLOCATION_FAILED;
        }
        ph_set_err_msg(ectx.err_msg, ectx.err_msg_cap, "Memory allocation failed");
        png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
        return NULL;
    }
    data_for_cleanup = data;

    /* Overflow-checked like its neighbour above. `h` comes straight from the PNG header as a
     * png_uint_32, so on a 32-bit target sizeof(png_bytep) * h wraps and produces a too-small array
     * that png_read_image() then writes past. Refuse instead. */
    size_t row_ptrs_size;
    if (!ph_safe_image_alloc_size(sizeof(png_bytep), h, 1, &row_ptrs_size)) {
        if (out_err) {
            *out_err = PH_ERR_IMAGE_TOO_LARGE;
        }
        ph_set_err_msg(ectx.err_msg, ectx.err_msg_cap,
                       "Image exceeds the configured maximum pixel count");
        free(data);
        png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
        return NULL;
    }

    png_bytep *row_ptrs = (png_bytep *)malloc(row_ptrs_size);
    if (!row_ptrs) {
        if (out_err) {
            *out_err = PH_ERR_ALLOCATION_FAILED;
        }
        ph_set_err_msg(ectx.err_msg, ectx.err_msg_cap, "Memory allocation failed");
        free(data);
        png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
        return NULL;
    }
    row_ptrs_for_cleanup = row_ptrs;

    for (png_uint_32 i = 0; i < h; i++) {
        row_ptrs[i] = data + i * rowbytes;
    }

    png_read_image(png_ptr, row_ptrs);
    free(row_ptrs);
    png_destroy_read_struct(&png_ptr, &info_ptr, NULL);

    *width = (int)w;
    *height = (int)h;
    *channels = out_channels;
    return data;
}
