#include "internal.h"
#include "loader.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#ifndef _WIN32
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#define STB_IMAGE_IMPLEMENTATION
#include "../vendor/stb_image.h"

PH_API const char *ph_version(void) { return "1.9.0"; }

PH_API const char *ph_get_error_string(ph_error_t err) {
    switch (err) {
    case PH_SUCCESS:
        return "Success";
    case PH_ERR_ALLOCATION_FAILED:
        return "Memory allocation failed";
    case PH_ERR_DECODE_FAILED:
        return "Image decoding failed";
    case PH_ERR_INVALID_ARGUMENT:
        return "Invalid argument";
    case PH_ERR_NOT_IMPLEMENTED:
        return "Not implemented";
    case PH_ERR_EMPTY_IMAGE:
        return "Empty image (no image loaded)";
    default:
        return "Unknown error";
    }
}

PH_API void ph_context_set_gamma(ph_context_t *ctx, float gamma) {
    if (!ctx || gamma <= PH_GAMMA_EPSILON)
        return;

    ctx->gamma = gamma;
    // Precompute LUT for O(1) access during processing
    for (int i = 0; i < 256; i++) {
        double val = i / 255.0;
        // Standard gamma correction: value^(1/gamma)
        double res = pow(val, 1.0 / (double)gamma) * 255.0;
        ctx->gamma_lut[i] = (uint8_t)(res > 255.0 ? 255.0 : res);
    }
}

PH_API void ph_context_get_dimensions(ph_context_t *ctx, int *width, int *height, int *channels) {
    if (!ctx)
        return;
    if (width)
        *width = ctx->width;
    if (height)
        *height = ctx->height;
    if (channels)
        *channels = ctx->channels;
}

PH_API int ph_is_loaded(ph_context_t *ctx) { return (ctx && ctx->data) ? 1 : 0; }

PH_API void ph_context_set_gray_weights(ph_context_t *ctx, int r, int g, int b) {
    if (!ctx)
        return;

    int sum = r + g + b;
    if (sum <= 0) {
        // Fallback to defaults if invalid
        ctx->gray_r = PH_GRAY_R;
        ctx->gray_g = PH_GRAY_G;
        ctx->gray_b = PH_GRAY_B;
        return;
    }

    // Normalize to sum 128 for the >> 7 shift
    ctx->gray_r = (r * 128) / sum;
    ctx->gray_g = (g * 128) / sum;
    ctx->gray_b = 128 - ctx->gray_r - ctx->gray_g;
    
    if (ctx->gray_data) {
        free(ctx->gray_data);
        ctx->gray_data = NULL;
    }
}

PH_API void ph_context_set_phash_params(ph_context_t *ctx, int dct_size, int reduction_size) {
    if (!ctx || dct_size <= 0 || reduction_size <= 0 || reduction_size > dct_size)
        return;
    ctx->phash_dct_size = dct_size;
    ctx->phash_reduction_size = reduction_size;
}

PH_API void ph_context_set_radial_params(ph_context_t *ctx, int projections, int samples) {
    if (!ctx || projections <= 0 || samples <= 0)
        return;
    ctx->radial_projections = projections;
    ctx->radial_samples = samples;
}

PH_API void ph_context_set_block_params(ph_context_t *ctx, int block_size) {
    if (!ctx || block_size <= 0)
        return;
    ctx->block_size = block_size;
}

PH_API void ph_context_set_load_grayscale(ph_context_t *ctx, int enable) {
    if (ctx) {
        ctx->load_grayscale = enable;
    }
}

PH_API void ph_context_set_whash_mode(ph_context_t *ctx, ph_whash_mode_t mode) {
    if (ctx) {
        ctx->whash_mode = mode;
    }
}

PH_API ph_error_t ph_create(ph_context_t **out_ctx) {
    if (!out_ctx)
        return PH_ERR_INVALID_ARGUMENT;

    ph_context_t *ctx = (ph_context_t *)calloc(1, sizeof(ph_context_t));
    if (!ctx)
        return PH_ERR_ALLOCATION_FAILED;

    ctx->data = NULL;
    ctx->gray_data = NULL;
    ctx->width = 0;
    ctx->height = 0;
    ctx->channels = 0;
    ctx->is_loaded = 0;

    /* Defaults */
    ctx->gray_r = PH_GRAY_R;
    ctx->gray_g = PH_GRAY_G;
    ctx->gray_b = PH_GRAY_B;
    ctx->phash_dct_size = PH_DCT_SIZE;
    ctx->phash_reduction_size = PH_DCT_REDUCTION_SIZE;
    ctx->radial_projections = PH_RADIAL_PROJECTIONS;
    ctx->radial_samples = PH_RADIAL_SAMPLES;
    ctx->block_size = PH_BLOCK_SIZE;
    ctx->whash_mode = PH_WHASH_FAST;

    /* Optimization Defaults: Disabled by default for compatibility */
    ctx->load_grayscale = 0;

    ph_context_set_gamma(ctx, PH_DEFAULT_GAMMA);

    *out_ctx = ctx;
    return PH_SUCCESS;
}
PH_API void ph_free(ph_context_t *ctx) {
    if (ctx) {
        if (ctx->data)
            stbi_image_free(ctx->data);
        if (ctx->gray_data)
            free(ctx->gray_data);
        if (ctx->scratchpad)
            free(ctx->scratchpad);
        free(ctx);
    }
}

uint8_t *ph_get_scratchpad(ph_context_t *ctx, size_t size) {
    if (!ctx || size == 0)
        return NULL;

    /* Auto-trim: if the buffer is 4x+ larger than needed, free it to prevent
     * unbounded memory growth after processing a single large image. */
    if (ctx->scratchpad && ctx->scratchpad_size > size * 4) {
        free(ctx->scratchpad);
        ctx->scratchpad = NULL;
        ctx->scratchpad_size = 0;
    }

    if (ctx->scratchpad_size < size) {
        uint8_t *new_ptr = (uint8_t *)realloc(ctx->scratchpad, size);
        if (!new_ptr)
            return NULL;
        ctx->scratchpad = new_ptr;
        ctx->scratchpad_size = size;
    }
    return ctx->scratchpad;
}

PH_API ph_error_t ph_load_from_file(ph_context_t *ctx, const char *filepath) {
    if (!ctx || !filepath)
        return PH_ERR_INVALID_ARGUMENT;
    if (ctx->data)
        stbi_image_free(ctx->data);
    if (ctx->gray_data) {
        free(ctx->gray_data);
        ctx->gray_data = NULL;
    }

#if defined(PH_USE_TURBOJPEG) || defined(PH_USE_LIBPNG) || defined(PH_USE_SPNG) || defined(PH_USE_WEBP)
    // mmap the file for zero-copy decoding with bundled static decoders
    int fd = open(filepath, O_RDONLY);
    if (fd >= 0) {
        struct stat st;
        if (fstat(fd, &st) == 0 && st.st_size > 8) {
            void *mapped = mmap(NULL, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
            if (mapped != MAP_FAILED) {
                const unsigned char *data = (const unsigned char *)mapped;
                int decoded = 0;

#ifdef PH_USE_TURBOJPEG
                // JPEG Check (FF D8)
                if (data[0] == 0xFF && data[1] == 0xD8) {
                    int w, h, ch;
                    int req_comp_turbo = ctx->load_grayscale ? 1 : 0;
                    unsigned char *turbo_data = ph_decode_jpeg_tj(data, (unsigned long)st.st_size,
                                                                  &w, &h, &ch, req_comp_turbo);
                    if (turbo_data) {
                        ctx->data = turbo_data;
                        ctx->width = w;
                        ctx->height = h;
                        ctx->channels = ch;
                        ctx->is_loaded = 1;
                        decoded = 1;
                    }
                }
#endif

#if defined(PH_USE_LIBPNG) || defined(PH_USE_SPNG)
                // PNG Check (89 50 4E 47 0D 0A 1A 0A)
                if (!decoded && st.st_size >= 8 && data[0] == 0x89 && data[1] == 0x50 &&
                    data[2] == 0x4E && data[3] == 0x47 && data[4] == 0x0D && data[5] == 0x0A &&
                    data[6] == 0x1A && data[7] == 0x0A) {
                    int w, h, ch;
                    int req_comp_png = ctx->load_grayscale ? 1 : 0;
                    unsigned char *png_data = ph_decode_png_mem(data, (unsigned long)st.st_size, &w,
                                                                &h, &ch, req_comp_png);
                    if (png_data) {
                        ctx->data = png_data;
                        ctx->width = w;
                        ctx->height = h;
                        ctx->channels = ch;
                        ctx->is_loaded = 1;
                        decoded = 1;
                    }
                }
#endif

#ifdef PH_USE_WEBP
                /* WebP Check: RIFF....WEBP */
                if (!decoded && st.st_size >= 12 && data[0] == 'R' && data[1] == 'I' &&
                    data[2] == 'F' && data[3] == 'F' && data[8] == 'W' && data[9] == 'E' &&
                    data[10] == 'B' && data[11] == 'P') {
                    int w, h, ch;
                    int req_comp_webp = ctx->load_grayscale ? 1 : 0;
                    unsigned char *webp_data = ph_decode_webp_mem(data, (unsigned long)st.st_size,
                                                                  &w, &h, &ch, req_comp_webp);
                    if (webp_data) {
                        ctx->data = webp_data;
                        ctx->width = w;
                        ctx->height = h;
                        ctx->channels = ch;
                        ctx->is_loaded = 1;
                        decoded = 1;
                    }
                }
#endif

                munmap(mapped, st.st_size);
                close(fd);
                if (decoded)
                    return PH_SUCCESS;
            } else {
                close(fd);
            }
        } else {
            close(fd);
        }
    }
#endif // PH_USE_TURBOJPEG || PH_USE_LIBPNG || PH_USE_SPNG || PH_USE_WEBP

    int req_comp = ctx->load_grayscale ? 1 : 0;
    ctx->data = stbi_load(filepath, &ctx->width, &ctx->height, &ctx->channels, req_comp);
    if (!ctx->data)
        return PH_ERR_DECODE_FAILED;

    // If we requested specific channels, update the struct to reflect that
    if (req_comp != 0) {
        ctx->channels = req_comp;
    }

    ctx->is_loaded = 1;
    return PH_SUCCESS;
}

PH_API ph_error_t ph_load_from_memory(ph_context_t *ctx, const uint8_t *buffer, size_t length) {
    if (!ctx || !buffer || length == 0)
        return PH_ERR_INVALID_ARGUMENT;
    if (ctx->data)
        stbi_image_free(ctx->data);
    if (ctx->gray_data) {
        free(ctx->gray_data);
        ctx->gray_data = NULL;
    }

#ifdef PH_USE_TURBOJPEG
    if (length >= 2 && buffer[0] == 0xFF && buffer[1] == 0xD8) {
        int w, h, ch;
        int req_comp_turbo = ctx->load_grayscale ? 1 : 0;
        unsigned char *turbo_data =
            ph_decode_jpeg_tj(buffer, (unsigned long)length, &w, &h, &ch, req_comp_turbo);
        if (turbo_data) {
            ctx->data = turbo_data;
            ctx->width = w;
            ctx->height = h;
            ctx->channels = ch;
            ctx->is_loaded = 1;
            return PH_SUCCESS;
        }
    }
#endif

#if defined(PH_USE_LIBPNG) || defined(PH_USE_SPNG)
    if (length >= 8 && buffer[0] == 0x89 && buffer[1] == 0x50) {
        int w, h, ch;
        int req_comp_png = ctx->load_grayscale ? 1 : 0;
        unsigned char *png_data =
            ph_decode_png_mem(buffer, (unsigned long)length, &w, &h, &ch, req_comp_png);
        if (png_data) {
            ctx->data = png_data;
            ctx->width = w;
            ctx->height = h;
            ctx->channels = ch;
            ctx->is_loaded = 1;
            return PH_SUCCESS;
        }
    }
#endif

#ifdef PH_USE_WEBP
    /* WebP Check: RIFF....WEBP */
    if (length >= 12 && buffer[0] == 'R' && buffer[1] == 'I' && buffer[2] == 'F' &&
        buffer[3] == 'F' && buffer[8] == 'W' && buffer[9] == 'E' && buffer[10] == 'B' &&
        buffer[11] == 'P') {
        int w, h, ch;
        int req_comp_webp = ctx->load_grayscale ? 1 : 0;
        unsigned char *webp_data =
            ph_decode_webp_mem(buffer, (unsigned long)length, &w, &h, &ch, req_comp_webp);
        if (webp_data) {
            ctx->data = webp_data;
            ctx->width = w;
            ctx->height = h;
            ctx->channels = ch;
            ctx->is_loaded = 1;
            return PH_SUCCESS;
        }
    }
#endif

    int req_comp = ctx->load_grayscale ? 1 : 0;
    ctx->data = stbi_load_from_memory(buffer, (int)length, &ctx->width, &ctx->height,
                                      &ctx->channels, req_comp);
    if (!ctx->data)
        return PH_ERR_DECODE_FAILED;

    if (req_comp != 0) {
        ctx->channels = req_comp;
    }

    ctx->is_loaded = 1;
    return PH_SUCCESS;
}
