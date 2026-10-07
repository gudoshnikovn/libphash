#include "arena.h"
#include "context.h"
#include "fileio.h"
#include "image/image.h"
#include "loader.h"
#include "safety.h"

#include <stdlib.h>
#include <string.h>

PH_API const char *ph_get_error_string(ph_error_t err) {
    switch (err) {
        case PH_SUCCESS:
            return "Success";
        case PH_ERR_ALLOCATION_FAILED:
            return "Memory allocation failed";
        case PH_ERR_INVALID_ARGUMENT:
            return "Invalid argument";
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
        case PH_ERR_REQUIRES_COLOR:
            return "Algorithm requires a color image, but the loaded image is grayscale";
        case PH_ERR_CANCELLED:
            return "Batch cancelled before this item was started";
        case PH_ERR_NO_STRUCTURE:
            return "Radial digest carries no structure to compare";
        case PH_ERR_FORCE_INT32_: /* width spacer, not an error code */
        default:
            return "Unknown error";
    }
}

PH_API const char *ph_get_last_error_message(const ph_context_t *ctx) {
    if (!ctx) {
        return "";
    }
    return ctx->last_error;
}

PH_API ph_error_t ph_create(ph_context_t **out_ctx) {
    if (!out_ctx) {
        return PH_ERR_INVALID_ARGUMENT;
    }

    /* calloc: no image, an empty arena and an empty diagnostic message are all zero. */
    ph_context_t *ctx = calloc(1, sizeof(ph_context_t));
    if (!ctx) {
        return PH_ERR_ALLOCATION_FAILED;
    }

    ph_config_init_defaults(&ctx->config);

    /* PH_DEFAULT_GAMMA is in range by construction, so this cannot fail; checked anyway
     * so a future change to either constant that breaks that invariant fails loudly
     * here instead of shipping a context with an unset gamma. */
    if (ph_context_set_gamma(ctx, PH_DEFAULT_GAMMA) != PH_SUCCESS) {
        free(ctx);
        return PH_ERR_INVALID_ARGUMENT;
    }

    *out_ctx = ctx;
    return PH_SUCCESS;
}

/* Picks the right EXIF-orientation scanner for the encoded (still-compressed)
 * bytes based on magic, or reports "no transform needed" (1) for anything else. */
static int ph_scan_orientation(const uint8_t *data, size_t len) {
    static const uint8_t png_sig[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    if (len >= 2 && data[0] == 0xFF && data[1] == 0xD8) {
        return ph_exif_orientation_from_jpeg(data, len);
    }
    if (ph_magic_is_webp(data, len)) {
        return ph_exif_orientation_from_webp(data, len);
    }
    if (len >= 8 && memcmp(data, png_sig, 8) == 0) {
        return ph_exif_orientation_from_png(data, len);
    }
    return 1;
}

/* The diagnostic message is documented as describing "the most recent failure on this
 * context". That obliges every failable entry point to clear it on the way in, including
 * the ones that fail before they get as far as touching the image -- otherwise a rejected
 * argument leaves the previous call's text standing and the caller reads a message about
 * something else entirely. Kept separate from ph_reset_loaded_image() because clearing the
 * message must NOT imply discarding the loaded image: ph_load_from_pixels() deliberately
 * keeps the previous image when it fails. */
static void ph_clear_last_error(ph_context_t *ctx) { ctx->last_error[0] = '\0'; }

/* Drops whatever image the context is holding. Both load entry points call this
 * first, so a failed load never leaves the previously loaded image visible. */
static void ph_reset_loaded_image(ph_context_t *ctx) {
    ph_clear_last_error(ctx);
    if (ctx->image.raw_rgb) {
        ph_free_image(ctx->image.raw_rgb);
    }
    ctx->image.raw_rgb = NULL;
    ctx->image.is_loaded = 0;
    /* The dimensions go with the pixels: ph_context_get_dimensions() on an empty
     * context reports 0/0/0, not the size of an image that is no longer there. */
    ctx->image.width = 0;
    ctx->image.height = 0;
    ctx->image.channels = 0;
    ph_drop_gray_cache(ctx);
}

PH_API void ph_free(ph_context_t *ctx) {
    if (!ctx) {
        return;
    }
    ph_reset_loaded_image(ctx);
    ph_arena_free(&ctx->arena);
    free(ctx);
}

/* The one decode path. Both ph_load_from_file() and ph_load_from_memory() reach
 * the decoders through this, which is what makes backend dispatch, the
 * pixel-count limit, error classification and EXIF auto-orientation identical for
 * a file and for a buffer. */
static ph_error_t ph_load_encoded_bytes(ph_context_t *ctx, const uint8_t *data, size_t length) {
    int req_comp = ctx->config.load_grayscale ? 1 : 0;
    int w = 0, h = 0, ch = 0;
    ph_error_t decode_err = PH_SUCCESS;

    uint8_t *decoded = ph_decode_buffer(data, length, &w, &h, &ch, req_comp, ctx->config.max_pixels,
                                        ctx->config.decode_scale, &decode_err, ctx->last_error,
                                        sizeof(ctx->last_error));
    if (!decoded) {
        /* ph_decode_buffer() resolves a non-empty buffer to either decoded data
         * or a specific error (its last-resort stb_image backend claims anything
         * not already claimed, except WebP without PH_USE_WEBP, which it reports
         * itself), so the fallback below is belt-and-braces: never report success
         * without an image. */
        return (decode_err != PH_SUCCESS) ? decode_err : PH_ERR_CORRUPT_DATA;
    }

    /* A decoder hands alpha back as a fourth (or, gray, second) channel; nothing past this
     * point sees it. */
    ph_resolve_alpha(&decoded, ph_size(w) * ph_size(h), &ch, ctx->config.alpha_mode);

    ctx->image.raw_rgb = decoded;
    ctx->image.width = w;
    ctx->image.height = h;
    ctx->image.channels = ch;
    ctx->image.is_loaded = 1;

    if (ctx->config.auto_orient) {
        int orientation = ph_scan_orientation(data, length);
        ph_error_t orient_err =
            ph_apply_exif_orientation(&ctx->image.raw_rgb, &ctx->image.width, &ctx->image.height,
                                      ctx->image.channels, orientation);
        if (orient_err != PH_SUCCESS) {
            /* The image is decoded but still in its stored orientation. Keeping it would
             * hash what the caller asked not to hash, so the load fails as a whole. */
            ph_reset_loaded_image(ctx);
            ph_set_err_msg(ctx->last_error, sizeof(ctx->last_error),
                           "could not apply the EXIF orientation to the decoded image");
            return orient_err;
        }
    }
    return PH_SUCCESS;
}

PH_API ph_error_t ph_load_from_file(ph_context_t *ctx, const char *filepath) {
    if (!ctx) {
        return PH_ERR_INVALID_ARGUMENT;
    }
    ph_reset_loaded_image(ctx);
    if (!filepath) {
        return PH_ERR_INVALID_ARGUMENT;
    }

    ph_file_bytes_t bytes;
    ph_error_t err = ph_open_file_bytes(filepath, &bytes, ctx->last_error, sizeof(ctx->last_error));
    if (err != PH_SUCCESS) {
        return err;
    }

    err = ph_load_encoded_bytes(ctx, bytes.data, bytes.length);
    ph_release_file_bytes(&bytes);
    return err;
}

PH_API ph_error_t ph_load_from_memory(ph_context_t *ctx, const uint8_t *buffer, size_t length) {
    if (!ctx) {
        return PH_ERR_INVALID_ARGUMENT;
    }
    ph_reset_loaded_image(ctx);
    if (!buffer || length == 0) {
        return PH_ERR_INVALID_ARGUMENT;
    }
    return ph_load_encoded_bytes(ctx, buffer, length);
}

PH_API ph_error_t ph_load_from_pixels(ph_context_t *ctx, const uint8_t *pixels, int width,
                                      int height, int channels, int stride) {
    if (!ctx) {
        return PH_ERR_INVALID_ARGUMENT;
    }
    /* Only the message: unlike the file and buffer paths, this one keeps the previously
     * loaded image when it fails, and the image is not discarded until the new buffer has
     * actually been allocated and filled below. */
    ph_clear_last_error(ctx);
    if (!pixels) {
        return PH_ERR_INVALID_ARGUMENT;
    }
    if (width <= 0 || height <= 0) {
        return PH_ERR_INVALID_ARGUMENT;
    }
    if (channels != 1 && channels != 3 && channels != 4) {
        return PH_ERR_INVALID_ARGUMENT;
    }

    /* Decompression-bomb protection applies here too, with the same check and the same
     * error code as the file and buffer paths; max_pixels == 0 means "no caller limit". */
    if (ph_exceeds_pixel_limit(ph_size(width), ph_size(height), ctx->config.max_pixels)) {
        return PH_ERR_IMAGE_TOO_LARGE;
    }

    /* width, height and channels are positive from here on. A product that does not fit
     * size_t (a 32-bit build) is refused rather than wrapped. */
    size_t row_bytes, total_bytes;
    if (!ph_safe_image_alloc_size(ph_size(width), 1, ph_size(channels), &row_bytes) ||
        !ph_safe_image_alloc_size(ph_size(width), ph_size(height), ph_size(channels),
                                  &total_bytes)) {
        return PH_ERR_INVALID_ARGUMENT;
    }
    if (stride < 0 || (stride != 0 && ph_size(stride) < row_bytes)) {
        return PH_ERR_INVALID_ARGUMENT;
    }
    size_t src_stride = (stride == 0) ? row_bytes : ph_size(stride);

    uint8_t *dst = malloc(total_bytes);
    if (!dst) {
        return PH_ERR_ALLOCATION_FAILED;
    }

    for (size_t y = 0, rows = ph_size(height); y < rows; y++) {
        memcpy(dst + y * row_bytes, pixels + y * src_stride, row_bytes);
    }
    ph_resolve_alpha(&dst, ph_size(width) * ph_size(height), &channels, ctx->config.alpha_mode);

    if (ctx->image.raw_rgb) {
        ph_free_image(ctx->image.raw_rgb);
    }
    ctx->image.raw_rgb = NULL;
    ctx->image.is_loaded = 0;
    ph_drop_gray_cache(ctx);

    ctx->image.raw_rgb = dst;
    ctx->image.width = width;
    ctx->image.height = height;
    ctx->image.channels = channels;
    ctx->image.is_loaded = 1;
    return PH_SUCCESS;
}
