#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include "internal.h"
#include "loader.h"
#include "phash_version.h"
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef _WIN32
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#define STB_IMAGE_IMPLEMENTATION
#include "../vendor/stb_image.h"

PH_API const char *ph_version(void) { return PH_VERSION_STRING; }

PH_API int ph_version_number(void) { return PH_VERSION_NUMBER; }

PH_API const char *ph_get_error_string(ph_error_t err) {
    switch (err) {
        case PH_SUCCESS:
            return "Success";
        case PH_ERR_ALLOCATION_FAILED:
            return "Memory allocation failed";
        case PH_ERR_INVALID_ARGUMENT:
            return "Invalid argument";
        case PH_ERR_NOT_IMPLEMENTED:
            return "Not implemented";
        case PH_ERR_EMPTY_IMAGE:
            return "Empty image (no image loaded)";
        case PH_ERR_IMAGE_TOO_LARGE:
            return "Image exceeds the configured maximum pixel count";
        case PH_ERR_UNSUPPORTED_FORMAT:
            return "Data is not a recognized image format";
        case PH_ERR_CORRUPT_DATA:
            return "Recognized image format, but the data is corrupt or truncated";
        case PH_ERR_DECODER_UNAVAILABLE:
            return "Recognized image format, but no decoder for it was compiled into this build";
        case PH_ERR_IO:
            return "File could not be opened or read";
        default:
            return "Unknown error";
    }
}

PH_API const char *ph_get_last_error_message(const ph_context_t *ctx) {
    if (!ctx)
        return "";
    return ctx->last_error;
}

/* R04: every ph_context_set_* below returns ph_error_t. Contract, uniform across all
 * nine of them: valid input -> PH_SUCCESS; invalid input -> PH_ERR_INVALID_ARGUMENT with
 * the configuration left exactly as it was. No partial application, no clamping and no
 * silent fallback to defaults -- a caller that cannot see its argument was refused ends
 * up hashing with a configuration it did not ask for, which is the root cause M12
 * describes behind H5 and H6.
 *
 * Deliberately NOT marked PH_NODISCARD, unlike the ph_compute_ and ph_load_ family. These
 * setters are routinely called for their effect in sequences where the arguments are
 * compile-time constants known to be valid (see ph_create() below, the benchmark harness
 * and most tests); requiring every such call site to consume the result would produce a
 * large number of warnings that carry no information. Callers passing runtime values are
 * expected to check the return; callers passing literals are not forced to. */
PH_API ph_error_t ph_context_set_gamma(ph_context_t *ctx, float gamma) {
    if (!ctx)
        return PH_ERR_INVALID_ARGUMENT;

    /* isfinite() has to come first. The previous guard was `gamma <= PH_GAMMA_EPSILON`,
     * and every comparison against NaN is false, so NAN (and INFINITY, which is also
     * greater than the epsilon) passed validation. pow() then filled all 256 LUT entries
     * with NaN, the (uint8_t) conversion of a NaN is undefined, and every subsequent hash
     * became garbage while the call still reported success -- e.g. gamma = NAN yielded
     * aHash = 00000000ffffffff with PH_SUCCESS. The upper bound keeps 1.0/gamma a
     * meaningful exponent; see PH_GAMMA_MAX. */
    if (!isfinite((double)gamma) || gamma <= PH_GAMMA_EPSILON || gamma > PH_GAMMA_MAX)
        return PH_ERR_INVALID_ARGUMENT;

    ctx->config.gamma = gamma;
    // Precompute LUT for O(1) access during processing
    for (int i = 0; i < 256; i++) {
        double val = i / 255.0;
        // Standard gamma correction: value^(1/gamma)
        double res = pow(val, 1.0 / (double)gamma) * 255.0;
        ctx->config.gamma_lut[i] = (uint8_t)(res > 255.0 ? 255.0 : res);
    }
    return PH_SUCCESS;
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

PH_API ph_error_t ph_context_set_gray_weights(ph_context_t *ctx, int r, int g, int b) {
    if (!ctx)
        return PH_ERR_INVALID_ARGUMENT;

    /* A negative weight is not a "dark" channel, it is a channel that subtracts
     * luminance -- the >> 7 grayscale path assumes non-negative weights summing to 128
     * and would produce out-of-range intermediate values. Rejected rather than
     * interpreted. The sum is accumulated in long long because three int weights can
     * overflow int even when each of them is individually valid. */
    if (r < 0 || g < 0 || b < 0)
        return PH_ERR_INVALID_ARGUMENT;

    long long sum = (long long)r + (long long)g + (long long)b;
    /* sum == 0 used to reset the weights to the ITU-R BT.601 defaults and report nothing,
     * so "0, 0, 0" silently changed the configuration to something the caller never
     * asked for. It is an error now (R04). */
    if (sum <= 0 || sum > PH_GRAY_WEIGHT_MAX_SUM)
        return PH_ERR_INVALID_ARGUMENT;

    // Normalize to sum 128 for the >> 7 shift
    ctx->config.gray_r = (int)(((long long)r * 128) / sum);
    ctx->config.gray_g = (int)(((long long)g * 128) / sum);
    ctx->config.gray_b = 128 - ctx->config.gray_r - ctx->config.gray_g;

    if (ctx->image.gray_cache) {
        free(ctx->image.gray_cache);
        ctx->image.gray_cache = NULL;
    }
    return PH_SUCCESS;
}

PH_API ph_error_t ph_context_set_phash_params(ph_context_t *ctx, int dct_size, int reduction_size) {
    /* Upper bounds are hard limits of the pHash implementation:
     * dct_size <= PH_DCT_MAX_SIZE and reduction_size <= PH_DCT_MAX_REDUCTION_SIZE
     * (the hash must fit into 64 bits). Out-of-range input is rejected without
     * touching the configuration; it is never clamped. (R02, kept as-is by R04.) */
    if (!ctx || dct_size <= 0 || dct_size > PH_DCT_MAX_SIZE || reduction_size <= 0 ||
        reduction_size > PH_DCT_MAX_REDUCTION_SIZE || reduction_size > dct_size)
        return PH_ERR_INVALID_ARGUMENT;
    ctx->config.phash_dct_size = dct_size;
    ctx->config.phash_reduction_size = reduction_size;
    return PH_SUCCESS;
}

PH_API ph_error_t ph_context_set_radial_params(ph_context_t *ctx, int projections, int samples) {
    /* projections: one digest byte each, so PH_DIGEST_MAX_BYTES is the real capacity.
     * samples: bounded by the diagonal of the largest image the library will process.
     * Derivations are next to PH_RADIAL_MAX_PROJECTIONS / PH_RADIAL_MAX_SAMPLES. */
    if (!ctx || projections <= 0 || projections > PH_RADIAL_MAX_PROJECTIONS || samples <= 0 ||
        samples > PH_RADIAL_MAX_SAMPLES)
        return PH_ERR_INVALID_ARGUMENT;
    ctx->config.radial_projections = projections;
    ctx->config.radial_samples = samples;
    return PH_SUCCESS;
}

PH_API ph_error_t ph_context_set_block_params(ph_context_t *ctx, int block_size) {
    /* block_size^2 bits have to fit into a ph_digest_t; see PH_BLOCK_MAX_SIZE. */
    if (!ctx || block_size <= 0 || block_size > PH_BLOCK_MAX_SIZE)
        return PH_ERR_INVALID_ARGUMENT;
    ctx->config.block_size = block_size;
    return PH_SUCCESS;
}

PH_API ph_error_t ph_context_set_load_grayscale(ph_context_t *ctx, int enable) {
    if (!ctx)
        return PH_ERR_INVALID_ARGUMENT;
    /* A boolean flag: any int is a valid argument, normalized to 0/1 (it used to be
     * stored verbatim). There is nothing to reject, so this never fails for a non-NULL
     * context. */
    ctx->config.load_grayscale = enable ? 1 : 0;
    return PH_SUCCESS;
}

PH_API ph_error_t ph_context_set_auto_orient(ph_context_t *ctx, int enable) {
    if (!ctx)
        return PH_ERR_INVALID_ARGUMENT;
    ctx->config.auto_orient = enable ? 1 : 0;
    return PH_SUCCESS;
}

PH_API ph_error_t ph_context_set_whash_mode(ph_context_t *ctx, ph_whash_mode_t mode) {
    if (!ctx)
        return PH_ERR_INVALID_ARGUMENT;
    /* An enum argument is not a guarantee: in C any int value can be passed through an
     * enum parameter, and FFI callers routinely do. Only the declared enumerators are
     * accepted -- ph_compute_whash() dispatches on this field, so an unknown value would
     * silently pick whichever branch the comparison happened to take. */
    if (mode != PH_WHASH_FAST && mode != PH_WHASH_FULL)
        return PH_ERR_INVALID_ARGUMENT;
    ctx->config.whash_mode = mode;
    return PH_SUCCESS;
}

PH_API ph_error_t ph_context_set_max_pixels(ph_context_t *ctx, uint64_t max_pixels) {
    if (!ctx)
        return PH_ERR_INVALID_ARGUMENT;
    /* Every uint64_t is a valid request, 0 included ("no limit of my own"). No upper
     * bound is enforced here on purpose: the implementation ceiling
     * PH_MAX_SUPPORTED_PIXELS is applied where the limit is used, by
     * ph_exceeds_pixel_limit() (R48), so a caller can ask for more than the library
     * supports and simply gets PH_ERR_IMAGE_TOO_LARGE at load time. Rejecting it here
     * would duplicate that policy in two places. */
    ctx->config.max_pixels = max_pixels;
    return PH_SUCCESS;
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
    ctx->config.max_pixels = PH_DEFAULT_MAX_PIXELS;

    /* Optimization Default: disabled by default for compatibility with
     * ColorHash and custom weights. */
    ctx->config.load_grayscale = 0;
    /* Applying EXIF/WebP-metadata orientation defaults to on: an image hashed
     * "as the sensor stored it" instead of "as it displays" is a correctness
     * bug, not a neutral choice. See ph_context_set_auto_orient(). */
    ctx->config.auto_orient = 1;

    /* PH_DEFAULT_GAMMA is in range by construction, so this cannot fail; checked anyway
     * because a context whose gamma LUT was never filled would hash every image through
     * an all-zero table, and calloc() makes that failure look like a valid context. */
    if (ph_context_set_gamma(ctx, PH_DEFAULT_GAMMA) != PH_SUCCESS) {
        free(ctx);
        return PH_ERR_INVALID_ARGUMENT;
    }

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

/* stb_image is the last-resort fallback for anything the native backends didn't
 * recognize/handle; classify its terse failure reason into one of our specific
 * error codes instead of one generic code for every cause (the old catch-all,
 * PH_ERR_DECODE_FAILED, was removed in 2.0.0 -- see include/libphash.h). */
static ph_error_t ph_classify_stb_failure(ph_context_t *ctx) {
    const char *reason = stbi_failure_reason();
    if (reason) {
        ph_set_err_msg(ctx->last_error, sizeof(ctx->last_error), reason);
        if (strcmp(reason, "unknown image type") == 0)
            return PH_ERR_UNSUPPORTED_FORMAT;
    }
    return PH_ERR_CORRUPT_DATA;
}

/* Picks the right EXIF-orientation scanner for the encoded (still-compressed)
 * bytes based on magic, or reports "no transform needed" (1) for anything else. */
static int ph_scan_orientation(const uint8_t *data, size_t len) {
    static const uint8_t png_sig[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    if (len >= 2 && data[0] == 0xFF && data[1] == 0xD8)
        return ph_exif_orientation_from_jpeg(data, len);
    if (ph_magic_is_webp(data, len))
        return ph_exif_orientation_from_webp(data, len);
    if (len >= 8 && memcmp(data, png_sig, 8) == 0)
        return ph_exif_orientation_from_png(data, len);
    return 1;
}

PH_API ph_error_t ph_load_from_file(ph_context_t *ctx, const char *filepath) {
    if (!ctx || !filepath)
        return PH_ERR_INVALID_ARGUMENT;
    ctx->last_error[0] = '\0';
    if (ctx->image.raw_rgb)
        ph_free_image(ctx->image.raw_rgb);
    ctx->image.raw_rgb = NULL;
    ctx->image.is_loaded = 0;
    if (ctx->image.gray_cache) {
        free(ctx->image.gray_cache);
        ctx->image.gray_cache = NULL;
    }

#ifndef _WIN32
    // Distinguish "can't even open this path" from a decode failure up front, for
    // both the native-decoder and stb_image-only builds below.
    {
        int probe_fd = open(filepath, O_RDONLY);
        if (probe_fd < 0) {
            char msg[PH_LAST_ERROR_MAX];
            snprintf(msg, sizeof(msg), "Cannot open '%s': %s", filepath, strerror(errno));
            ph_set_err_msg(ctx->last_error, sizeof(ctx->last_error), msg);
            return PH_ERR_IO;
        }
        close(probe_fd);
    }
#endif

#ifndef PH_USE_WEBP
    // Report "recognized but unavailable" precisely even in a build with no native
    // decoders at all, where the mmap+ph_decode_buffer path below is compiled out
    // entirely and would otherwise silently fall through to stb_image (which has
    // no WebP support either) and report a misleading PH_ERR_UNSUPPORTED_FORMAT.
    {
        FILE *probe = fopen(filepath, "rb");
        if (probe) {
            uint8_t magic[12];
            size_t n = fread(magic, 1, sizeof(magic), probe);
            fclose(probe);
            if (ph_magic_is_webp(magic, n)) {
                ph_set_err_msg(ctx->last_error, sizeof(ctx->last_error),
                               "WebP support was not compiled into this build (PH_USE_WEBP)");
                return PH_ERR_DECODER_UNAVAILABLE;
            }
        }
    }
#endif

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
                ph_error_t decode_err = PH_SUCCESS;
                uint8_t *decoded_data = ph_decode_buffer(
                    data, (size_t)st.st_size, &w, &h, &ch, req_comp_opt, ctx->config.max_pixels,
                    &decode_err, ctx->last_error, sizeof(ctx->last_error));

                // Scan the still-mmap'd compressed bytes for orientation metadata
                // before unmapping them below.
                int orientation = 1;
                if (decoded_data && ctx->config.auto_orient)
                    orientation = ph_scan_orientation(data, (size_t)st.st_size);

                munmap(mapped, st.st_size);
                close(fd);

                if (decoded_data) {
                    ctx->image.raw_rgb = decoded_data;
                    ctx->image.width = w;
                    ctx->image.height = h;
                    ctx->image.channels = ch;
                    ctx->image.is_loaded = 1;
                    if (orientation != 1)
                        ph_apply_exif_orientation(&ctx->image.raw_rgb, &ctx->image.width,
                                                  &ctx->image.height, ctx->image.channels,
                                                  orientation);
                    return PH_SUCCESS;
                }
                // A native backend recognized the format and gave a definitive answer
                // (too large / corrupt / unavailable) — trust it over trying stb_image.
                if (decode_err != PH_SUCCESS)
                    return decode_err;
            } else {
                close(fd);
            }
        } else {
            close(fd);
        }
    }
#endif // PH_USE_TURBOJPEG || PH_USE_LIBPNG || PH_USE_SPNG || PH_USE_WEBP

    if (ctx->config.max_pixels != 0) {
        int iw, ih, icomp;
        // stbi_info reports height as a signed int, and a top-down BMP
        // legitimately has a negative height in its header -- casting that
        // straight to uint64_t would wrap to a huge value and reject a
        // perfectly small image (see the equivalent ph_abs_dim() in loader.c).
        if (stbi_info(filepath, &iw, &ih, &icomp) &&
            ph_exceeds_pixel_limit((uint64_t)(iw < 0 ? -(int64_t)iw : iw),
                                   (uint64_t)(ih < 0 ? -(int64_t)ih : ih),
                                   ctx->config.max_pixels)) {
            return PH_ERR_IMAGE_TOO_LARGE;
        }
    }

    int req_comp = ctx->config.load_grayscale ? 1 : 0;
    ctx->image.raw_rgb =
        stbi_load(filepath, &ctx->image.width, &ctx->image.height, &ctx->image.channels, req_comp);
    if (!ctx->image.raw_rgb)
        return ph_classify_stb_failure(ctx);

    // If we requested specific channels, update the struct to reflect that
    if (req_comp != 0) {
        ctx->image.channels = req_comp;
    }

    if (ctx->config.auto_orient) {
        // No mmap'd buffer available on this path (stb_image reads by path
        // itself), so re-read the raw bytes just to scan for the tag.
        FILE *f = fopen(filepath, "rb");
        if (f) {
            fseek(f, 0, SEEK_END);
            long sz = ftell(f);
            fseek(f, 0, SEEK_SET);
            if (sz > 0) {
                uint8_t *raw = (uint8_t *)malloc((size_t)sz);
                if (raw && fread(raw, 1, (size_t)sz, f) == (size_t)sz) {
                    int orientation = ph_scan_orientation(raw, (size_t)sz);
                    if (orientation != 1)
                        ph_apply_exif_orientation(&ctx->image.raw_rgb, &ctx->image.width,
                                                  &ctx->image.height, ctx->image.channels,
                                                  orientation);
                }
                free(raw);
            }
            fclose(f);
        }
    }

    ctx->image.is_loaded = 1;
    return PH_SUCCESS;
}

PH_API ph_error_t ph_load_from_memory(ph_context_t *ctx, const uint8_t *buffer, size_t length) {
    if (!ctx || !buffer || length == 0)
        return PH_ERR_INVALID_ARGUMENT;
    ctx->last_error[0] = '\0';
    if (ctx->image.raw_rgb)
        ph_free_image(ctx->image.raw_rgb);
    ctx->image.raw_rgb = NULL;
    ctx->image.is_loaded = 0;
    if (ctx->image.gray_cache) {
        free(ctx->image.gray_cache);
        ctx->image.gray_cache = NULL;
    }

    int req_comp = ctx->config.load_grayscale ? 1 : 0;

    // Try unified decoder first
    int w, h, ch;
    ph_error_t decode_err = PH_SUCCESS;
    uint8_t *decoded_data =
        ph_decode_buffer(buffer, length, &w, &h, &ch, req_comp, ctx->config.max_pixels, &decode_err,
                         ctx->last_error, sizeof(ctx->last_error));
    if (decoded_data) {
        ctx->image.raw_rgb = decoded_data;
        ctx->image.width = w;
        ctx->image.height = h;
        ctx->image.channels = ch;
        ctx->image.is_loaded = 1;
        if (ctx->config.auto_orient) {
            int orientation = ph_scan_orientation(buffer, length);
            if (orientation != 1)
                ph_apply_exif_orientation(&ctx->image.raw_rgb, &ctx->image.width,
                                          &ctx->image.height, ctx->image.channels, orientation);
        }
        return PH_SUCCESS;
    }
    // ph_decode_buffer() always resolves a non-empty buffer to either decoded
    // data or a specific error now (its last-resort stb_image backend claims
    // anything not already claimed, except WebP without PH_USE_WEBP, which the
    // WebP-unavailable check inside ph_decode_buffer itself covers) -- so
    // decode_err is never PH_SUCCESS here.
    return decode_err;
}

PH_API ph_error_t ph_load_from_pixels(ph_context_t *ctx, const uint8_t *pixels, int width,
                                      int height, int channels, int stride) {
    if (!ctx || !pixels)
        return PH_ERR_INVALID_ARGUMENT;
    if (width <= 0 || height <= 0)
        return PH_ERR_INVALID_ARGUMENT;
    if (channels != 1 && channels != 3 && channels != 4)
        return PH_ERR_INVALID_ARGUMENT;

    /* L6: this used to be the one load path with no decompression-bomb protection,
     * which is what made the int-overflow in the pixel-count arithmetic (H6) reachable
     * with the default configuration. Same check and same error code as the file and
     * buffer paths; max_pixels == 0 still means "unlimited". */
    if (ph_exceeds_pixel_limit((uint64_t)width, (uint64_t)height, ctx->config.max_pixels))
        return PH_ERR_IMAGE_TOO_LARGE;

    unsigned long long row_bytes = (unsigned long long)width * (unsigned long long)channels;
    if (stride < 0 || (stride != 0 && (unsigned long long)stride < row_bytes))
        return PH_ERR_INVALID_ARGUMENT;
    unsigned long long src_stride = (stride == 0) ? row_bytes : (unsigned long long)stride;

    /* Cannot wrap: width and height are each <= INT_MAX and channels <= 4, so the
     * product is at most 4 * (2^31 - 1)^2, which stays below ULLONG_MAX. */
    unsigned long long total_bytes = row_bytes * (unsigned long long)height;
    if (total_bytes == 0 || total_bytes > SIZE_MAX)
        return PH_ERR_INVALID_ARGUMENT;

    uint8_t *dst = malloc((size_t)total_bytes);
    if (!dst)
        return PH_ERR_ALLOCATION_FAILED;

    for (int y = 0; y < height; y++) {
        memcpy(dst + (size_t)y * row_bytes, pixels + (size_t)y * src_stride, (size_t)row_bytes);
    }

    if (ctx->image.raw_rgb)
        ph_free_image(ctx->image.raw_rgb);
    ctx->image.raw_rgb = NULL;
    ctx->image.is_loaded = 0;
    if (ctx->image.gray_cache) {
        free(ctx->image.gray_cache);
        ctx->image.gray_cache = NULL;
    }

    ctx->image.raw_rgb = dst;
    ctx->image.width = width;
    ctx->image.height = height;
    ctx->image.channels = channels;
    ctx->image.is_loaded = 1;
    return PH_SUCCESS;
}
