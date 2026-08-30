#include "internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

unsigned char *ph_decode_png_mem(const unsigned char *buffer, unsigned long size, int *width,
                                 int *height, int *channels, int req_comp, uint64_t max_pixels,
                                 ph_error_t *out_err) {
    if (!buffer || size < 8)
        return NULL;

    if (png_sig_cmp(buffer, 0, 8) != 0)
        return NULL;

    png_structp png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (!png_ptr)
        return NULL;

    png_infop info_ptr = png_create_info_struct(png_ptr);
    if (!info_ptr) {
        png_destroy_read_struct(&png_ptr, NULL, NULL);
        return NULL;
    }

    if (setjmp(png_jmpbuf(png_ptr))) {
        png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
        return NULL;
    }

    // Zero-copy memory reading from mmap'd buffer
    PngMemReader reader = {.data = buffer, .size = size, .offset = 0};
    png_set_read_fn(png_ptr, &reader, png_mem_read_fn);

    if (max_pixels != 0) {
        // Defense in depth: cap each dimension individually (in addition to the
        // width*height check below) and cap ancillary-chunk allocations, so a
        // malicious header can't force a huge allocation before we even see w/h.
        png_uint_32 dim_limit =
            (max_pixels > 0xFFFFFFFFULL) ? 0xFFFFFFFFu : (png_uint_32)max_pixels;
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
        png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
        return NULL;
    }

    png_bytep *row_ptrs = (png_bytep *)malloc(sizeof(png_bytep) * h);
    if (!row_ptrs) {
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
                                 ph_error_t *out_err) {
    if (!buffer || size < 8)
        return NULL;

    spng_ctx *ctx = spng_ctx_new(0);
    if (!ctx)
        return NULL;

    if (max_pixels != 0) {
        // Defense in depth against huge ancillary-chunk allocations.
        spng_set_chunk_limits(ctx, 128 * 1024 * 1024, 128 * 1024 * 1024);
    }

    if (spng_set_png_buffer(ctx, buffer, size) != 0) {
        spng_ctx_free(ctx);
        return NULL;
    }

    struct spng_ihdr ihdr;
    if (spng_get_ihdr(ctx, &ihdr) != 0) {
        spng_ctx_free(ctx);
        return NULL;
    }

    if (ph_exceeds_pixel_limit(ihdr.width, ihdr.height, max_pixels)) {
        if (out_err)
            *out_err = PH_ERR_IMAGE_TOO_LARGE;
        spng_ctx_free(ctx);
        return NULL;
    }

    // Select format based on req_comp
    int fmt = (req_comp == 1) ? SPNG_FMT_G8 : SPNG_FMT_RGB8;
    size_t out_size;
    if (spng_decoded_image_size(ctx, fmt, &out_size) != 0) {
        spng_ctx_free(ctx);
        return NULL;
    }

    unsigned char *data = (unsigned char *)malloc(out_size);
    if (!data) {
        spng_ctx_free(ctx);
        return NULL;
    }

    if (spng_decode_image(ctx, data, out_size, fmt, 0) != 0) {
        free(data);
        spng_ctx_free(ctx);
        return NULL;
    }

    spng_ctx_free(ctx);

    *width = (int)ihdr.width;
    *height = (int)ihdr.height;
    *channels = (req_comp == 1) ? 1 : 3;
    return data;
}

#else
// No PNG decoder — stb_image will handle PNG
PH_API int ph_can_use_libpng(void) { return 0; }
#endif // PH_USE_LIBPNG / PH_USE_SPNG
