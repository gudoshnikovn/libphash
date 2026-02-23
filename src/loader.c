#include "loader.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// =====================================================================
// JPEG Decoder
// =====================================================================
#ifdef PH_USE_TURBOJPEG

#include "turbojpeg.h"

PH_API int ph_can_use_libjpeg(void) { return 1; }

unsigned char *ph_decode_jpeg_tj(const unsigned char *buffer, unsigned long size, int *width,
                                 int *height, int *channels, int req_comp) {
    if (!buffer || size == 0)
        return NULL;

    tjhandle handle = tjInitDecompress();
    if (!handle)
        return NULL;

    int w, h, subsamp, colorspace;
    if (tjDecompressHeader3(handle, buffer, size, &w, &h, &subsamp, &colorspace) < 0) {
        tjDestroy(handle);
        return NULL;
    }

    int pixelFormat = (req_comp == 1) ? TJPF_GRAY : TJPF_RGB;
    int out_channels = (req_comp == 1) ? 1 : 3;
    int pitch = w * out_channels;

    unsigned char *output = (unsigned char *)malloc(pitch * h);
    if (!output) {
        tjDestroy(handle);
        return NULL;
    }

    int flags = TJFLAG_FASTDCT | TJFLAG_NOREALLOC;
    if (tjDecompress2(handle, buffer, size, output, w, pitch, h, pixelFormat, flags) < 0) {
        free(output);
        tjDestroy(handle);
        return NULL;
    }

    *width = w;
    *height = h;
    *channels = out_channels;
    tjDestroy(handle);
    return output;
}

#else
// No TurboJPEG — stb_image will handle JPEG
PH_API int ph_can_use_libjpeg(void) { return 0; }
#endif // PH_USE_TURBOJPEG

// =====================================================================
// PNG Decoder
// =====================================================================
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
                                 int *height, int *channels, int req_comp) {
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

    png_read_info(png_ptr, info_ptr);

    png_uint_32 w, h;
    int bit_depth, color_type;
    png_get_IHDR(png_ptr, info_ptr, &w, &h, &bit_depth, &color_type, NULL, NULL, NULL);

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

    unsigned char *data = (unsigned char *)malloc(rowbytes * h);
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
// =====================================================================
// PNG Decoder (spng — fast on x86, single-call API)
// =====================================================================
#include "spng.h"

PH_API int ph_can_use_libpng(void) { return 1; }

unsigned char *ph_decode_png_mem(const unsigned char *buffer, unsigned long size, int *width,
                                 int *height, int *channels, int req_comp) {
    if (!buffer || size < 8)
        return NULL;

    spng_ctx *ctx = spng_ctx_new(0);
    if (!ctx)
        return NULL;

    if (spng_set_png_buffer(ctx, buffer, size) != 0) {
        spng_ctx_free(ctx);
        return NULL;
    }

    struct spng_ihdr ihdr;
    if (spng_get_ihdr(ctx, &ihdr) != 0) {
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
