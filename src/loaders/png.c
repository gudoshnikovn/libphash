#include "internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Two alternative implementations of one backend: the decoder below is selected
// with #ifdef/#elif, so defining both would silently drop spng and still link it.
// The CMake build refuses the combination up front; this catches the other build
// paths (Makefile, python-libphash's _build.py, hand-rolled CFLAGS).
#if defined(PH_USE_LIBPNG) && defined(PH_USE_SPNG)
#error "PH_USE_LIBPNG and PH_USE_SPNG are mutually exclusive: define exactly one PNG backend."
#endif

#if defined(PH_USE_LIBPNG) || defined(PH_USE_SPNG)
int ph_can_read_png(const uint8_t *magic, size_t len) {
    return (len >= 8 && magic[0] == 0x89 && magic[1] == 0x50 && magic[2] == 0x4E &&
            magic[3] == 0x47 && magic[4] == 0x0D && magic[5] == 0x0A && magic[6] == 0x1A &&
            magic[7] == 0x0A);
}
#endif

/* --- PNG Decoder (libpng) --- */
#ifdef PH_USE_LIBPNG

#include <png.h>

PH_API int ph_can_use_libpng(void) { return 1; }

// Custom memory read callback for png_set_read_fn
typedef struct {
    const unsigned char *data;
    unsigned long size;
    unsigned long offset;
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
} PngErrorCtx;

static void png_error_fn(png_structp png_ptr, png_const_charp msg) {
    PngErrorCtx *ectx = (PngErrorCtx *)png_get_error_ptr(png_ptr);
    if (ectx)
        ph_set_err_msg(ectx->err_msg, ectx->err_msg_cap, msg);
    longjmp(png_jmpbuf(png_ptr), 1);
}

static void png_warning_fn(png_structp png_ptr, png_const_charp msg) {
    (void)png_ptr;
    (void)msg; // Warnings aren't fatal; nothing to surface for now.
}

unsigned char *ph_decode_png_mem(const unsigned char *buffer, unsigned long size, int *width,
                                 int *height, int *channels, int req_comp, uint64_t max_pixels,
                                 ph_error_t *out_err, char *err_msg, size_t err_msg_cap) {
    if (!buffer || size < 8)
        return NULL;

    /* Checked before the buffer reaches libpng/spng so both backends agree on the
     * verdict and the error code, and so an absurd dimension is refused before any
     * row buffer is sized (R16/M1). */
    if (!ph_png_dimensions_within_limit(buffer, size)) {
        if (out_err)
            *out_err = PH_ERR_IMAGE_TOO_LARGE;
        ph_set_err_msg(err_msg, err_msg_cap, "PNG dimension exceeds the supported maximum");
        return NULL;
    }

    if (png_sig_cmp(buffer, 0, 8) != 0)
        return NULL;

    PngErrorCtx ectx = {.err_msg = err_msg, .err_msg_cap = err_msg_cap};
    png_structp png_ptr =
        png_create_read_struct(PNG_LIBPNG_VER_STRING, &ectx, png_error_fn, png_warning_fn);
    if (!png_ptr)
        return NULL;

    /* setjmp() must be armed before ANY other libpng call on png_ptr: png_error_fn
     * unconditionally longjmp()s to png_jmpbuf(png_ptr), and libpng can raise a fatal
     * error from inside png_create_info_struct() itself (allocation failure). With the
     * setjmp() placed after that call, such a failure jumped through an uninitialized
     * jmp_buf -- undefined behaviour (R18/M3).
     *
     * info_for_cleanup is volatile because it is assigned after setjmp() and read in
     * the longjmp branch: a non-volatile local modified between setjmp and longjmp has
     * an indeterminate value there (C11 7.13.2.1p3). It exists only so the jump branch
     * can free an info struct that may or may not have been created yet; the rest of
     * the function keeps using the plain info_ptr below. */
    volatile png_infop info_for_cleanup = NULL;

    if (setjmp(png_jmpbuf(png_ptr))) {
        // png_error_fn already captured the message and/or code (PH_ERR_IMAGE_TOO_LARGE
        // sites below set *out_err before their own longjmp-free early returns; this
        // path is libpng's own fatal errors, which are always a malformed bitstream).
        png_infop jumped_info = info_for_cleanup;
        if (out_err && *out_err == PH_SUCCESS)
            *out_err = PH_ERR_CORRUPT_DATA;
        png_destroy_read_struct(&png_ptr, jumped_info ? &jumped_info : NULL, NULL);
        return NULL;
    }

    png_infop info_ptr = png_create_info_struct(png_ptr);
    if (!info_ptr) {
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
        /* Only ever LOWER libpng's own per-dimension default (1000000). Passing
         * max_pixels straight through raised it -- with the default 256 MP that meant
         * telling libpng a 268435456-pixel-wide image is acceptable (R16/M1). */
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
        if (out_err)
            *out_err = PH_ERR_IMAGE_TOO_LARGE;
        png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
        return NULL;
    }

    // Transform to 8-bit
    if (color_type == PNG_COLOR_TYPE_PALETTE)
        png_set_palette_to_rgb(png_ptr);
    if (color_type == PNG_COLOR_TYPE_GRAY && bit_depth < 8)
        png_set_expand_gray_1_2_4_to_8(png_ptr);
    if (bit_depth == 16)
        png_set_strip_16(png_ptr);
    if (png_get_valid(png_ptr, info_ptr, PNG_INFO_tRNS))
        png_set_tRNS_to_alpha(png_ptr);

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
        if (color_type == PNG_COLOR_TYPE_GRAY || color_type == PNG_COLOR_TYPE_GRAY_ALPHA)
            png_set_gray_to_rgb(png_ptr);
        if (color_type & PNG_COLOR_MASK_ALPHA)
            png_set_strip_alpha(png_ptr);
    }

    png_read_update_info(png_ptr, info_ptr);

    size_t rowbytes = png_get_rowbytes(png_ptr, info_ptr);
    int out_channels = (int)(rowbytes / w);

    size_t alloc_size;
    if (!ph_safe_image_alloc_size(rowbytes, h, 1, &alloc_size)) {
        if (out_err)
            *out_err = PH_ERR_IMAGE_TOO_LARGE;
        png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
        return NULL;
    }

    unsigned char *data = (unsigned char *)malloc(alloc_size);
    if (!data) {
        if (out_err)
            *out_err = PH_ERR_ALLOCATION_FAILED;
        png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
        return NULL;
    }

    /* The only allocation in this decoder that used to skip the overflow check while
     * its neighbour above went through ph_safe_image_alloc_size() (R17/M2). `h` comes
     * straight from the PNG header as a png_uint_32, so on a 32-bit target
     * sizeof(png_bytep) * h wraps and produces a too-small array that png_read_image()
     * then writes past. Refuse instead. */
    size_t row_ptrs_size;
    if (!ph_safe_image_alloc_size(sizeof(png_bytep), h, 1, &row_ptrs_size)) {
        if (out_err)
            *out_err = PH_ERR_IMAGE_TOO_LARGE;
        free(data);
        png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
        return NULL;
    }

    png_bytep *row_ptrs = (png_bytep *)malloc(row_ptrs_size);
    if (!row_ptrs) {
        if (out_err)
            *out_err = PH_ERR_ALLOCATION_FAILED;
        free(data);
        png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
        return NULL;
    }

    for (png_uint_32 i = 0; i < h; i++)
        row_ptrs[i] = data + i * rowbytes;

    png_read_image(png_ptr, row_ptrs);
    free(row_ptrs);
    png_destroy_read_struct(&png_ptr, &info_ptr, NULL);

    *width = (int)w;
    *height = (int)h;
    *channels = out_channels;
    return data;
}

#elif defined(PH_USE_SPNG)
/* --- PNG Decoder (spng) --- */
#include "spng.h"

PH_API int ph_can_use_libpng(void) { return 1; }

unsigned char *ph_decode_png_mem(const unsigned char *buffer, unsigned long size, int *width,
                                 int *height, int *channels, int req_comp, uint64_t max_pixels,
                                 ph_error_t *out_err, char *err_msg, size_t err_msg_cap) {
    if (!buffer || size < 8)
        return NULL;

    /* Checked before the buffer reaches libpng/spng so both backends agree on the
     * verdict and the error code, and so an absurd dimension is refused before any
     * row buffer is sized (R16/M1). */
    if (!ph_png_dimensions_within_limit(buffer, size)) {
        if (out_err)
            *out_err = PH_ERR_IMAGE_TOO_LARGE;
        ph_set_err_msg(err_msg, err_msg_cap, "PNG dimension exceeds the supported maximum");
        return NULL;
    }

    spng_ctx *ctx = spng_ctx_new(0);
    if (!ctx) {
        if (out_err)
            *out_err = PH_ERR_ALLOCATION_FAILED;
        return NULL;
    }

    if (max_pixels != 0) {
        // Defense in depth against huge ancillary-chunk allocations.
        spng_set_chunk_limits(ctx, 128 * 1024 * 1024, 128 * 1024 * 1024);
    }

    int ret = spng_set_png_buffer(ctx, buffer, size);
    if (ret != 0) {
        if (out_err)
            *out_err = PH_ERR_CORRUPT_DATA;
        ph_set_err_msg(err_msg, err_msg_cap, spng_strerror(ret));
        spng_ctx_free(ctx);
        return NULL;
    }

    struct spng_ihdr ihdr;
    ret = spng_get_ihdr(ctx, &ihdr);
    if (ret != 0) {
        if (out_err)
            *out_err = PH_ERR_CORRUPT_DATA;
        ph_set_err_msg(err_msg, err_msg_cap, spng_strerror(ret));
        spng_ctx_free(ctx);
        return NULL;
    }

    if (ph_exceeds_pixel_limit(ihdr.width, ihdr.height, max_pixels)) {
        if (out_err)
            *out_err = PH_ERR_IMAGE_TOO_LARGE;
        spng_ctx_free(ctx);
        return NULL;
    }

    /* spng accepts SPNG_FMT_G8 only for a genuinely grayscale PNG -- color type 0
     * with a bit depth of 8 or less (check_decode_fmt() in spng.c). For anything
     * else -- truecolor, palette, gray+alpha, 16-bit -- it rejects the request with
     * SPNG_EFMT ("invalid format") and the whole decode fails. Asking for G8
     * unconditionally therefore broke grayscale loading of every ordinary PNG in
     * this backend: ph_context_set_load_grayscale(ctx, 1) reported
     * PH_ERR_CORRUPT_DATA on a perfectly valid file, while the libpng backend
     * converted it happily.
     *
     * Take the same route libpng does: let spng deliver RGB8 whenever G8 is not
     * applicable, then fold the pixels down here with the exact weights the libpng
     * path hands to png_set_rgb_to_gray_fixed() -- PH_GRAY_R/G/B over 128, i.e. the
     * Rec.601 weights of ph_to_grayscale() -- so both backends produce the same
     * bytes for the same input. */
    const int gray_native = (ihdr.color_type == SPNG_COLOR_TYPE_GRAYSCALE && ihdr.bit_depth <= 8);
    int fmt;
    if (req_comp == 1)
        fmt = gray_native ? SPNG_FMT_G8 : SPNG_FMT_RGB8;
    else
        fmt = SPNG_FMT_RGB8;
    size_t out_size;
    ret = spng_decoded_image_size(ctx, fmt, &out_size);
    if (ret != 0) {
        if (out_err)
            *out_err = PH_ERR_CORRUPT_DATA;
        ph_set_err_msg(err_msg, err_msg_cap, spng_strerror(ret));
        spng_ctx_free(ctx);
        return NULL;
    }

    unsigned char *data = (unsigned char *)malloc(out_size);
    if (!data) {
        if (out_err)
            *out_err = PH_ERR_ALLOCATION_FAILED;
        spng_ctx_free(ctx);
        return NULL;
    }

    ret = spng_decode_image(ctx, data, out_size, fmt, 0);
    if (ret != 0) {
        if (out_err)
            *out_err = PH_ERR_CORRUPT_DATA;
        ph_set_err_msg(err_msg, err_msg_cap, spng_strerror(ret));
        free(data);
        spng_ctx_free(ctx);
        return NULL;
    }

    spng_ctx_free(ctx);

    if (req_comp == 1 && fmt == SPNG_FMT_RGB8) {
        /* In-place RGB -> gray: the destination index i never runs ahead of the
         * source index 3*i, so a forward pass is safe. */
        size_t num_pixels = out_size / 3;
        for (size_t i = 0; i < num_pixels; i++) {
            unsigned int r = data[i * 3];
            unsigned int g = data[i * 3 + 1];
            unsigned int b = data[i * 3 + 2];
            data[i] = (unsigned char)((PH_GRAY_R * r + PH_GRAY_G * g + PH_GRAY_B * b) >> 7);
        }
        /* Hand back a buffer of the size the caller believes it got. A failed shrink
         * is harmless -- the original block stays valid and merely oversized. */
        unsigned char *shrunk = (unsigned char *)realloc(data, num_pixels ? num_pixels : 1);
        if (shrunk)
            data = shrunk;
    }

    *width = (int)ihdr.width;
    *height = (int)ihdr.height;
    *channels = (req_comp == 1) ? 1 : 3;
    return data;
}

#else
// No PNG decoder — stb_image will handle PNG
PH_API int ph_can_use_libpng(void) { return 0; }
#endif // PH_USE_LIBPNG / PH_USE_SPNG
