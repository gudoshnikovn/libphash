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

/* stb_image has no error codes: a failure leaves a bare English sentence in
 * stbi_failure_reason(), so mapping one back to a ph_error_t means comparing against
 * string literals that live inside vendor/stb_image.h. That is a real coupling and it
 * fails silently -- if a vendored update reworded one of these, the comparison would
 * simply stop matching and every unrecognized buffer would be reported as corrupt
 * instead of unsupported, with nothing to notice it.
 *
 * So the literals are collected here rather than spelled out at the comparison, with
 * the vendored version they were read from, and test_stb_failure_classification() in
 * tests/src/test_loader.c pins both halves of the mapping. That test is meant to break
 * on a vendor bump: when it does, re-read the strings below out of the new header.
 *
 * Pinned to vendor/stb_image.h v2.30. These are the reasons that mean "nothing here
 * looked like an image I know"; every other reason means a format was recognized and
 * its bitstream was broken, which is PH_ERR_CORRUPT_DATA. */
static const char *const ph_stb_unsupported_reasons[] = {
    /* stbi__load_main() and stbi__info_main(), after every format test declined. */
    "unknown image type",
};

static int ph_stb_reason_is_unsupported(const char *reason) {
    if (!reason)
        return 0;
    for (size_t i = 0; i < sizeof(ph_stb_unsupported_reasons) / sizeof(*ph_stb_unsupported_reasons);
         i++) {
        if (strcmp(reason, ph_stb_unsupported_reasons[i]) == 0)
            return 1;
    }
    return 0;
}

static uint8_t *ph_decode_stb_mem(const uint8_t *data, size_t len, int *w, int *h, int *ch,
                                  int req_comp, uint64_t max_pixels, ph_error_t *out_err,
                                  char *err_msg, size_t err_msg_cap) {
    /* Run unconditionally, not only when max_pixels is set: the per-dimension cap and
     * the implementation ceiling inside ph_exceeds_pixel_limit() apply even when the
     * caller has disabled their own area limit with max_pixels == 0. Reading the header
     * costs a header parse, which is negligible against the decode that follows. If
     * stbi_info() cannot parse it, there is nothing to judge -- stbi_load() below fails
     * on the same data and reports why. */
    int iw, ih, icomp;
    if (stbi_info_from_memory(data, (int)len, &iw, &ih, &icomp)) {
        uint64_t w64 = ph_abs_dim(iw), h64 = ph_abs_dim(ih);
        if (ph_exceeds_dimension_limit(w64, h64)) {
            if (out_err)
                *out_err = PH_ERR_IMAGE_TOO_LARGE;
            ph_set_err_msg(err_msg, err_msg_cap, "Image dimension exceeds the supported maximum");
            return NULL;
        }
        if (ph_exceeds_pixel_limit(w64, h64, max_pixels)) {
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
            *out_err = ph_stb_reason_is_unsupported(reason) ? PH_ERR_UNSUPPORTED_FORMAT
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

    /* PNG is judged here rather than in a backend, so that the per-dimension cap holds
     * in a stb_image-only build too and every configuration answers the same input with
     * the same code. Every other format reaches the cap through its backend, which gets
     * the dimensions from its own header parse. */
    if (ph_magic_is_png(buffer, length) && !ph_png_dimensions_within_limit(buffer, length)) {
        if (out_err)
            *out_err = PH_ERR_IMAGE_TOO_LARGE;
        ph_set_err_msg(err_msg, err_msg_cap, "PNG dimension exceeds the supported maximum");
        return NULL;
    }

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
