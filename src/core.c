#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

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

PH_API const char *ph_version(void) { return "1.10.3"; }

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

    ctx->config.gamma = gamma;
    // Precompute LUT for O(1) access during processing
    for (int i = 0; i < 256; i++) {
        double val = i / 255.0;
        // Standard gamma correction: value^(1/gamma)
        double res = pow(val, 1.0 / (double)gamma) * 255.0;
        ctx->config.gamma_lut[i] = (uint8_t)(res > 255.0 ? 255.0 : res);
    }
}

PH_API void ph_context_get_dimensions(ph_context_t *ctx, int *width, int *height, int *channels) {
    if (!ctx)
        return;
    if (width)
        *width = ctx->image.width;
    if (height)
        *height = ctx->image.height;
    if (channels)
        *channels = ctx->image.channels;
}

PH_API int ph_is_loaded(ph_context_t *ctx) { return (ctx && ctx->image.raw_rgb) ? 1 : 0; }

PH_API void ph_context_set_gray_weights(ph_context_t *ctx, int r, int g, int b) {
    if (!ctx)
        return;

    int sum = r + g + b;
    if (sum <= 0) {
        // Fallback to defaults if invalid
        ctx->config.gray_r = PH_GRAY_R;
        ctx->config.gray_g = PH_GRAY_G;
        ctx->config.gray_b = PH_GRAY_B;
        return;
    }

    // Normalize to sum 128 for the >> 7 shift
    ctx->config.gray_r = (r * 128) / sum;
    ctx->config.gray_g = (g * 128) / sum;
    ctx->config.gray_b = 128 - ctx->config.gray_r - ctx->config.gray_g;

    if (ctx->image.gray_cache) {
        free(ctx->image.gray_cache);
        ctx->image.gray_cache = NULL;
    }
}

PH_API void ph_context_set_phash_params(ph_context_t *ctx, int dct_size, int reduction_size) {
    if (!ctx || dct_size <= 0 || reduction_size <= 0 || reduction_size > dct_size)
        return;
    ctx->config.phash_dct_size = dct_size;
    ctx->config.phash_reduction_size = reduction_size;
}

PH_API void ph_context_set_radial_params(ph_context_t *ctx, int projections, int samples) {
    if (!ctx || projections <= 0 || samples <= 0)
        return;
    ctx->config.radial_projections = projections;
    ctx->config.radial_samples = samples;
}

PH_API void ph_context_set_block_params(ph_context_t *ctx, int block_size) {
    if (!ctx || block_size <= 0)
        return;
    ctx->config.block_size = block_size;
}

PH_API void ph_context_set_load_grayscale(ph_context_t *ctx, int enable) {
    if (ctx) {
        ctx->config.load_grayscale = enable;
    }
}

PH_API void ph_context_set_whash_mode(ph_context_t *ctx, ph_whash_mode_t mode) {
    if (ctx) {
        ctx->config.whash_mode = mode;
    }
}

PH_API ph_error_t ph_create(ph_context_t **out_ctx) {
    if (!out_ctx)
        return PH_ERR_INVALID_ARGUMENT;

    ph_context_t *ctx = (ph_context_t *)calloc(1, sizeof(ph_context_t));
    if (!ctx)
        return PH_ERR_ALLOCATION_FAILED;

    ctx->image.raw_rgb = NULL;
    ctx->image.gray_cache = NULL;
    ctx->image.width = 0;
    ctx->image.height = 0;
    ctx->image.channels = 0;
    ctx->image.is_loaded = 0;

    /* Defaults */
    ctx->config.gray_r = PH_GRAY_R;
    ctx->config.gray_g = PH_GRAY_G;
    ctx->config.gray_b = PH_GRAY_B;
    ctx->config.phash_dct_size = PH_DCT_SIZE;
    ctx->config.phash_reduction_size = PH_DCT_REDUCTION_SIZE;
    ctx->config.radial_projections = PH_RADIAL_PROJECTIONS;
    ctx->config.radial_samples = PH_RADIAL_SAMPLES;
    ctx->config.block_size = PH_BLOCK_SIZE;
    ctx->config.whash_mode = PH_WHASH_FAST;

    /* Optimization Defaults: Disabled by default for compatibility */
    ctx->config.load_grayscale = 0;

    ph_context_set_gamma(ctx, PH_DEFAULT_GAMMA);

    *out_ctx = ctx;
    return PH_SUCCESS;
}
PH_API void ph_free(ph_context_t *ctx) {
    if (ctx) {
        if (ctx->image.raw_rgb)
            ph_free_image(ctx->image.raw_rgb);
        if (ctx->image.gray_cache)
            free(ctx->image.gray_cache);
        if (ctx->arena.buffer) {
#if defined(_WIN32)
            _aligned_free(ctx->arena.buffer);
#else
            free(ctx->arena.buffer);
#endif
        }
        free(ctx);
    }
}

uint8_t *ph_get_scratchpad(ph_context_t *ctx, size_t size) {
    if (!ctx || size == 0)
        return NULL;

    /* Auto-trim on top-level calls only to prevent unbounded memory growth */
    if (ctx->arena.offset == 0 && ctx->arena.buffer && ctx->arena.capacity > size * 4) {
#if defined(_WIN32)
        _aligned_free(ctx->arena.buffer);
#else
        free(ctx->arena.buffer);
#endif
        ctx->arena.buffer = NULL;
        ctx->arena.capacity = 0;
    }

    size_t required = ctx->arena.offset + size;

    if (ctx->arena.capacity < required) {
        // Grow by more than required to avoid frequent reallocs
        size_t new_size = required > ctx->arena.capacity * 2 ? required : ctx->arena.capacity * 2;
        if (new_size < 1024)
            new_size = 1024;

        // Ensure new_size is a multiple of 32 for posix_memalign
        new_size = (new_size + 31) & ~(size_t)31;

        uint8_t *new_ptr = NULL;
#if defined(_WIN32)
        new_ptr = (uint8_t *)_aligned_malloc(new_size, 32);
#else
        if (posix_memalign((void **)&new_ptr, 32, new_size) != 0) {
            new_ptr = NULL;
        }
#endif
        if (!new_ptr)
            return NULL;

        if (ctx->arena.buffer) {
            // Realloc alternative for aligned memory
            memcpy(new_ptr, ctx->arena.buffer, ctx->arena.offset);
#if defined(_WIN32)
            _aligned_free(ctx->arena.buffer);
#else
            free(ctx->arena.buffer);
#endif
        }
        ctx->arena.buffer = new_ptr;
        ctx->arena.capacity = new_size;
    }

    uint8_t *ptr = ctx->arena.buffer + ctx->arena.offset;
    ctx->arena.offset += size;

    return ptr;
}

PH_API ph_error_t ph_load_from_file(ph_context_t *ctx, const char *filepath) {
    if (!ctx || !filepath)
        return PH_ERR_INVALID_ARGUMENT;
    if (ctx->image.raw_rgb)
        ph_free_image(ctx->image.raw_rgb);
    if (ctx->image.gray_cache) {
        free(ctx->image.gray_cache);
        ctx->image.gray_cache = NULL;
    }

#if defined(PH_USE_TURBOJPEG) || defined(PH_USE_LIBPNG) || defined(PH_USE_SPNG) ||                 \
    defined(PH_USE_WEBP)
    // mmap the file for zero-copy decoding with bundled static decoders
    int fd = open(filepath, O_RDONLY);
    if (fd >= 0) {
        struct stat st;
        if (fstat(fd, &st) == 0 && st.st_size > 8) {
            void *mapped = mmap(NULL, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
            if (mapped != MAP_FAILED) {
#if defined(__linux__) || defined(__APPLE__) || defined(__FreeBSD__)
                posix_madvise(mapped, st.st_size, POSIX_MADV_SEQUENTIAL);
#endif
                const unsigned char *data = (const unsigned char *)mapped;
                int req_comp_opt = ctx->config.load_grayscale ? 1 : 0;
                int w, h, ch;
                uint8_t *decoded_data =
                    ph_decode_buffer(data, (size_t)st.st_size, &w, &h, &ch, req_comp_opt);

                munmap(mapped, st.st_size);
                close(fd);

                if (decoded_data) {
                    ctx->image.raw_rgb = decoded_data;
                    ctx->image.width = w;
                    ctx->image.height = h;
                    ctx->image.channels = ch;
                    ctx->image.is_loaded = 1;
                    return PH_SUCCESS;
                }
            } else {
                close(fd);
            }
        } else {
            close(fd);
        }
    }
#endif // PH_USE_TURBOJPEG || PH_USE_LIBPNG || PH_USE_SPNG || PH_USE_WEBP

    int req_comp = ctx->config.load_grayscale ? 1 : 0;
    ctx->image.raw_rgb =
        stbi_load(filepath, &ctx->image.width, &ctx->image.height, &ctx->image.channels, req_comp);
    if (!ctx->image.raw_rgb)
        return PH_ERR_DECODE_FAILED;

    // If we requested specific channels, update the struct to reflect that
    if (req_comp != 0) {
        ctx->image.channels = req_comp;
    }

    ctx->image.is_loaded = 1;
    return PH_SUCCESS;
}

PH_API ph_error_t ph_load_from_memory(ph_context_t *ctx, const uint8_t *buffer, size_t length) {
    if (!ctx || !buffer || length == 0)
        return PH_ERR_INVALID_ARGUMENT;
    if (ctx->image.raw_rgb)
        ph_free_image(ctx->image.raw_rgb);
    if (ctx->image.gray_cache) {
        free(ctx->image.gray_cache);
        ctx->image.gray_cache = NULL;
    }

    int req_comp = ctx->config.load_grayscale ? 1 : 0;

    // Try unified decoder first
    int w, h, ch;
    uint8_t *decoded_data = ph_decode_buffer(buffer, length, &w, &h, &ch, req_comp);
    if (decoded_data) {
        ctx->image.raw_rgb = decoded_data;
        ctx->image.width = w;
        ctx->image.height = h;
        ctx->image.channels = ch;
        ctx->image.is_loaded = 1;
        return PH_SUCCESS;
    }

    ctx->image.raw_rgb = stbi_load_from_memory(buffer, (int)length, &ctx->image.width,
                                               &ctx->image.height, &ctx->image.channels, req_comp);
    if (!ctx->image.raw_rgb)
        return PH_ERR_DECODE_FAILED;

    if (req_comp != 0) {
        ctx->image.channels = req_comp;
    }

    ctx->image.is_loaded = 1;
    return PH_SUCCESS;
}
