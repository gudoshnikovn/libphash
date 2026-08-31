#include "loader.h"
#include "../vendor/stb_image.h"
#include "loaders/internal.h"
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

/* Last-resort fallback backend: whatever no native decoder recognized, hand to
 * stb_image. This is what actually gives us BMP/GIF/TGA/PSD/HDR/PIC/PNM support
 * (stb_image is always linked in, via STB_IMAGE_IMPLEMENTATION in core.c) --
 * and in a build with a native decoder missing for JPEG/PNG, it covers those
 * too, since stb_image decodes both natively. Two formats it does NOT cover:
 * WebP (excluded below on purpose -- see comment on ph_can_read_stb) and TIFF
 * (stb_image has no TIFF support at all; would need a dedicated PHASH_USE_TIFF
 * backend on libtiff if that's ever needed). Animated GIF/WebP: only the first
 * frame is decoded/hashed, same as everything else in this library.
 */
static int ph_can_read_stb(const uint8_t *magic, size_t len) {
    // Let the PH_USE_WEBP-less path below report PH_ERR_DECODER_UNAVAILABLE
    // precisely instead of stb_image failing generically with "unknown image
    // type" -- stb_image has no WebP decoder at all, so it would never have
    // succeeded here anyway.
    if (ph_magic_is_webp(magic, len))
        return 0;
    return 1;
}

/* stbi_info(_from_memory) reports height as a plain (signed) int, and a
 * top-down BMP legitimately has a negative height in its header -- casting
 * that straight to uint64_t for the pixel-limit check would wrap to a huge
 * value and reject a perfectly small image. Take the magnitude first. */
static uint64_t ph_abs_dim(int v) { return (v < 0) ? (uint64_t)(-(int64_t)v) : (uint64_t)v; }

static uint8_t *ph_decode_stb_mem(const uint8_t *data, size_t len, int *w, int *h, int *ch,
                                  int req_comp, uint64_t max_pixels, ph_error_t *out_err,
                                  char *err_msg, size_t err_msg_cap) {
    if (max_pixels != 0) {
        int iw, ih, icomp;
        if (stbi_info_from_memory(data, (int)len, &iw, &ih, &icomp) &&
            ph_exceeds_pixel_limit(ph_abs_dim(iw), ph_abs_dim(ih), max_pixels)) {
            if (out_err)
                *out_err = PH_ERR_IMAGE_TOO_LARGE;
            ph_set_err_msg(err_msg, err_msg_cap,
                           "Image exceeds the configured maximum pixel count");
            return NULL;
        }
    }

    uint8_t *decoded = stbi_load_from_memory(data, (int)len, w, h, ch, req_comp);
    if (!decoded) {
        const char *reason = stbi_failure_reason();
        if (reason)
            ph_set_err_msg(err_msg, err_msg_cap, reason);
        if (out_err)
            *out_err = (reason && strcmp(reason, "unknown image type") == 0)
                           ? PH_ERR_UNSUPPORTED_FORMAT
                           : PH_ERR_CORRUPT_DATA;
        return NULL;
    }
    if (req_comp != 0)
        *ch = req_comp;
    return decoded;
}

#ifdef PH_ENABLE_MOCK_BACKEND
/* Mock backend for exercising the dispatcher loop without real decoders.
 *
 * Guarded by its own opt-in flag (CMake: PHASH_ENABLE_MOCK_BACKEND, Makefile:
 * PHASH_ENABLE_MOCK_BACKEND=1), deliberately NOT by PH_TESTING/PHASH_BUILD_TESTS:
 * those are ON in the recommended Release build, which used to ship a library
 * that "decodes" any buffer starting with DE AD into a 1x1 image. This backend
 * is registered ahead of the stb catch-all, so it really does intercept input --
 * it must never end up in a shipped artifact. */
static int ph_mock_can_read(const uint8_t *magic, size_t len) {
    if (len >= 4 && magic[0] == 0xDE && magic[1] == 0xAD)
        return 1;
    return 0;
}
static uint8_t *ph_mock_decode(const uint8_t *data, size_t len, int *w, int *h, int *ch, int req,
                               uint64_t max_pixels, ph_error_t *out_err, char *err_msg,
                               size_t err_msg_cap) {
    (void)data;
    (void)len;
    (void)req;
    (void)max_pixels;
    (void)out_err;
    (void)err_msg;
    (void)err_msg_cap;
    *w = 1;
    *h = 1;
    *ch = 3;
    return malloc(3);
}
#endif

static const ph_image_backend_t backends[] = {
#ifdef PH_USE_TURBOJPEG
    {ph_can_read_jpeg, ph_decode_jpeg_tj},
#endif
#if defined(PH_USE_LIBPNG) || defined(PH_USE_SPNG)
    {ph_can_read_png, ph_decode_png_mem},
#endif
#ifdef PH_USE_WEBP
    {ph_can_read_webp, ph_decode_webp_mem},
#endif
#ifdef PH_ENABLE_MOCK_BACKEND
    {ph_mock_can_read, ph_mock_decode},
#endif
    {ph_can_read_stb, ph_decode_stb_mem},   {NULL, NULL}};

uint8_t *ph_decode_buffer(const uint8_t *buffer, size_t length, int *width, int *height,
                          int *channels, int req_comp, uint64_t max_pixels, ph_error_t *out_err,
                          char *err_msg, size_t err_msg_cap) {
    if (out_err)
        *out_err = PH_SUCCESS;
    if (!buffer || length == 0)
        return NULL;
    for (int i = 0; backends[i].can_read != NULL; i++) {
        if (backends[i].can_read(buffer, length)) {
            ph_error_t err = PH_SUCCESS;
            uint8_t *data = backends[i].decode(buffer, length, width, height, channels, req_comp,
                                               max_pixels, &err, err_msg, err_msg_cap);
            if (data)
                return data;
            // The magic bytes matched this backend, so a decode failure here is a
            // definitive answer (too large / corrupt): don't let a later backend or
            // the stb_image fallback re-attempt the same data.
            if (out_err)
                *out_err = (err != PH_SUCCESS) ? err : PH_ERR_CORRUPT_DATA;
            return NULL;
        }
    }
#ifndef PH_USE_WEBP
    if (ph_magic_is_webp(buffer, length)) {
        if (out_err)
            *out_err = PH_ERR_DECODER_UNAVAILABLE;
        ph_set_err_msg(err_msg, err_msg_cap,
                       "WebP support was not compiled into this build (PH_USE_WEBP)");
        return NULL;
    }
#endif
    return NULL;
}

/* Every decode path -- native backends (plain malloc in jpeg.c/png.c/webp.c)
 * and stb_image (STBI_MALLOC/STBI_FREE default to malloc/free, unoverridden
 * in this project) -- hands out a plain malloc()'d buffer, so this is just
 * free(). Spelled out directly rather than via stbi_image_free() so freeing
 * a native buffer doesn't depend on stb_image's allocator macros still being
 * the libc default if that ever changes. */
void ph_free_image(uint8_t *data) { free(data); }
