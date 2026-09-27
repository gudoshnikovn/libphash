#include "loader.h"
#include "../vendor/stb_image.h"
#include "loaders/internal.h"
#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

/* Last-resort fallback backend: whatever no native decoder recognized, hand to
 * stb_image. This is what actually gives us BMP/GIF/TGA/PSD/HDR/PIC/PNM support
 * (stb_image is always linked in, via STB_IMAGE_IMPLEMENTATION in
 * src/loaders/stb_image_impl.c) --
 * and in a build with a native decoder missing for JPEG/PNG, it covers those
 * too, since stb_image decodes both natively. Two formats it does NOT cover:
 * WebP (excluded below on purpose -- see comment on ph_can_read_stb) and TIFF
 * (stb_image has no TIFF support at all; would need a dedicated PHASH_USE_TIFF
 * backend on libtiff if that's ever needed). Animated GIF: only the first frame is
 * decoded/hashed, via this fallback. Animated WebP does NOT get the same treatment
 * from the native WebP backend (src/loaders/webp.c) -- it fails outright rather than
 * decoding a frame; see the @note on ph_load_from_memory() in include/libphash.h.
 *
 * Every format named above (including TIFF's absence and GIF's first-frame-only
 * behavior) has a hand-built fixture and a passing assertion in
 * test_stb_extended_fallback_formats(), test_animated_gif_first_frame_only() and
 * test_tiff_unsupported() in tests/src/test_loader.c -- update that coverage
 * alongside this comment if the claim here ever changes.
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

/* Same coupling as ph_stb_unsupported_reasons above, for the other reason worth telling
 * apart: stb_image's own malloc()/realloc() calls failing mid-decode. Left unrecognized,
 * this used to fall into PH_ERR_CORRUPT_DATA -- telling the caller the file is bad when
 * the truth is the process ran out of memory, while the native decoder backends (jpeg.c,
 * png.c, webp.c) already report their own malloc failures as PH_ERR_ALLOCATION_FAILED.
 * Recognizing it here closes that gap between the stb-only build and native-decoder
 * builds. test_stb_oom_reason_pinned() in tests/src/test_alloc_failure.c pins this
 * literal against a real forced allocation failure (not just a mocked reason string);
 * if a vendor bump reworks the wording, that test breaks and this array is where to fix
 * it -- do not relax the assertion instead.
 *
 * Reading this reason at all only works because vendor/stb_image.h carries a local patch
 * (marker: "libphash local patch") making stb set it in two paths where upstream loses
 * it: its zlib entry points return NULL without setting any reason, and its format
 * dispatch overwrites an out-of-memory reason from an allocating probe with its own
 * verdict. Without that patch some allocation failures arrive here as "no SOI" or
 * "unknown image type" and are classified as corrupt/unsupported. A vendor bump that
 * drops the patch shows up as failures in test_alloc_failure.c, not here; see
 * docs/development.md. */
static const char *const ph_stb_oom_reasons[] = {
    /* stbi__malloc()/stbi__malloc_mad*() etc., wherever stb_image's internal allocator
     * returns NULL. */
    "outofmem",
};

static int ph_stb_reason_is_oom(const char *reason) {
    if (!reason)
        return 0;
    for (size_t i = 0; i < sizeof(ph_stb_oom_reasons) / sizeof(*ph_stb_oom_reasons); i++) {
        if (strcmp(reason, ph_stb_oom_reasons[i]) == 0)
            return 1;
    }
    return 0;
}

/* Third pinned mapping: "too large" is stb_image's answer to its own size and overflow
 * checks -- STBI_MAX_DIMENSIONS, stbi__mad2sizes_valid()/stbi__mad3sizes_valid(), and the
 * two per-row buffer size checks in the PNG decoder that say "Corrupt PNG" in their long
 * form but test an overflowing computed size, not the bitstream. None of its eleven
 * sites is about damaged data, so it maps to PH_ERR_IMAGE_TOO_LARGE, which is what the
 * native decoders answer for the same headers. It matters because stb checks these
 * inside stbi_info() too: a header it refuses there never reaches the pixel-limit check
 * in ph_decode_stb_mem(), and the decode that follows fails with this reason.
 *
 * Pinned to vendor/stb_image.h v2.30, like the two arrays above;
 * test_stb_too_large_is_image_too_large() in tests/src/test_loader.c pins it. */
static const char *const ph_stb_too_large_reasons[] = {
    "too large",
};

static int ph_stb_reason_is_too_large(const char *reason) {
    if (!reason)
        return 0;
    for (size_t i = 0; i < sizeof(ph_stb_too_large_reasons) / sizeof(*ph_stb_too_large_reasons);
         i++) {
        if (strcmp(reason, ph_stb_too_large_reasons[i]) == 0)
            return 1;
    }
    return 0;
}

static uint8_t *ph_decode_stb_mem(const uint8_t *data, size_t len, int *w, int *h, int *ch,
                                  int req_comp, uint64_t max_pixels, ph_decode_scale_t decode_scale,
                                  ph_error_t *out_err, char *err_msg, size_t err_msg_cap) {
    /* stb_image has no scaled-decode path; decode_scale is a JPEG-only optimization
     * (see ph_context_set_decode_scale()), silently ignored here as documented. */
    (void)decode_scale;
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
        if (out_err) {
            if (ph_stb_reason_is_unsupported(reason))
                *out_err = PH_ERR_UNSUPPORTED_FORMAT;
            else if (ph_stb_reason_is_oom(reason))
                *out_err = PH_ERR_ALLOCATION_FAILED;
            else if (ph_stb_reason_is_too_large(reason))
                *out_err = PH_ERR_IMAGE_TOO_LARGE;
            else
                *out_err = PH_ERR_CORRUPT_DATA;
        }
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
                               uint64_t max_pixels, ph_decode_scale_t decode_scale,
                               ph_error_t *out_err, char *err_msg, size_t err_msg_cap) {
    (void)data;
    (void)len;
    (void)req;
    (void)max_pixels;
    (void)decode_scale;
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

#if defined(PH_USE_LIBPNG) || defined(PH_USE_SPNG)
/* zlib-ng -- the inflate under both native PNG backends -- picks its CPU-specific
 * routines on first use by writing a global function table that other threads then read
 * without synchronisation. All writers store the same pointers, so nothing observable goes
 * wrong, but it is a data race under the C memory model, and ThreadSanitizer reports it
 * with a stack through ph_hash_files() for anyone who runs their application under it.
 * So the first decode of the process happens here, once, under a lock, before any
 * backend is dispatched: decoding one pixel of PNG runs zlib-ng's initialisation to
 * completion, and every later call only reads the table. Harmless with a zlib that has
 * no such table. Do not "simplify" this away without a TSan run of
 * tests/src/test_cold_parallel_decode.c on a libpng + zlib-ng build. */
static const uint8_t ph_warmup_png[] = {
    0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, 0x00, 0x00, 0x00, 0x0D, 0x49, 0x48,
    0x44, 0x52, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x08, 0x00, 0x00, 0x00,
    0x00, 0x3A, 0x7E, 0x9B, 0x55, 0x00, 0x00, 0x00, 0x0A, 0x49, 0x44, 0x41, 0x54, 0x78,
    0x9C, 0x63, 0x68, 0x00, 0x00, 0x00, 0x82, 0x00, 0x81, 0x77, 0xCD, 0x72, 0xB6, 0x00,
    0x00, 0x00, 0x00, 0x49, 0x45, 0x4E, 0x44, 0xAE, 0x42, 0x60, 0x82};

static atomic_flag s_warmup_lock = ATOMIC_FLAG_INIT;
static atomic_bool s_warmup_done = false;

static void ph_warm_decoder_dispatch(void) {
    if (atomic_load(&s_warmup_done))
        return;
    while (atomic_flag_test_and_set(&s_warmup_lock)) {
    }
    if (!atomic_load(&s_warmup_done)) {
        int w, h, ch;
        ph_error_t err = PH_SUCCESS;
        uint8_t *px = ph_decode_png_mem(ph_warmup_png, sizeof(ph_warmup_png), &w, &h, &ch, 0, 0,
                                        PH_DECODE_SCALE_FULL, &err, NULL, 0);
        /* A failure here (out of memory) leaves the dispatch cold; the real decode that
         * follows reports its own error, and the next call tries again. */
        if (px) {
            ph_free_image(px);
            atomic_store(&s_warmup_done, true);
        }
    }
    atomic_flag_clear(&s_warmup_lock);
}
#endif

/* --- Container completeness -------------------------------------------------
 *
 * stb_image decodes whatever part of a truncated JPEG or PNG it could read, fills the
 * rest, and reports success; the native decoders reject the same bytes as corrupt. So a
 * half-downloaded file got an error in one build and a plausible hash of half a picture
 * in another. These checks give every build the same answer: after a successful decode,
 * the container must reach its own end -- EOI for JPEG, IEND for PNG, the RIFF size for
 * WebP. Bytes after that end are allowed (camera trailers, appended data); a missing end
 * is PH_ERR_CORRUPT_DATA. They run after the decoder, not before, so a decoder's own and
 * more specific verdict (too large, out of memory, its own corruption message) wins. */

/* Walks the JPEG marker structure: segments are skipped by their length (so an EOI inside
 * an embedded EXIF thumbnail does not count), entropy-coded data after SOS is scanned for
 * the next real marker (FF followed by anything but a stuffed 00 or a restart marker). */
static int ph_jpeg_reaches_eoi(const uint8_t *p, size_t n) {
    if (n < 2 || p[0] != 0xFF || p[1] != 0xD8)
        return 0;
    size_t pos = 2;
    for (;;) {
        while (pos < n && p[pos] != 0xFF) /* tolerate stray bytes between segments */
            pos++;
        while (pos < n && p[pos] == 0xFF) /* fill bytes */
            pos++;
        if (pos >= n)
            return 0;
        uint8_t m = p[pos++];
        if (m == 0xD9)
            return 1;
        if (m == 0x01 || (m >= 0xD0 && m <= 0xD7) || m == 0x00)
            continue; /* standalone markers carry no length */
        if (n - pos < 2)
            return 0;
        size_t seglen = ((size_t)p[pos] << 8) | p[pos + 1];
        if (seglen < 2 || seglen > n - pos)
            return 0;
        pos += seglen;
        if (m != 0xDA)
            continue;
        /* Entropy-coded data up to the next marker. */
        for (;;) {
            if (pos >= n)
                return 0;
            if (p[pos] != 0xFF) {
                pos++;
                continue;
            }
            if (pos + 1 >= n)
                return 0;
            uint8_t next = p[pos + 1];
            if (next == 0x00 || (next >= 0xD0 && next <= 0xD7)) {
                pos += 2;
                continue;
            }
            break; /* a marker: back to the segment loop */
        }
    }
}

/* Walks the PNG chunks from the signature: every chunk must fit, and IEND must come. */
static int ph_png_reaches_iend(const uint8_t *p, size_t n) {
    size_t pos = 8;
    while (n - pos >= 12) {
        size_t len = ((size_t)p[pos] << 24) | ((size_t)p[pos + 1] << 16) |
                     ((size_t)p[pos + 2] << 8) | p[pos + 3];
        if (len > 0x7FFFFFFFu || len > n - pos - 12)
            return 0;
        if (memcmp(p + pos + 4, "IEND", 4) == 0)
            return 1;
        pos += 12 + len;
    }
    return 0;
}

/* The RIFF header states the file size; a truncated file is shorter than it says. */
static int ph_webp_riff_complete(const uint8_t *p, size_t n) {
    if (n < 12)
        return 0;
    uint32_t riff =
        (uint32_t)p[4] | ((uint32_t)p[5] << 8) | ((uint32_t)p[6] << 16) | ((uint32_t)p[7] << 24);
    return (uint64_t)riff + 8 <= (uint64_t)n;
}

/* NULL when the container is complete (or not one of the three formats), otherwise the
 * diagnostic for a truncated one. */
static const char *ph_container_truncation(const uint8_t *p, size_t n) {
    if (n >= 2 && p[0] == 0xFF && p[1] == 0xD8)
        return ph_jpeg_reaches_eoi(p, n) ? NULL : "JPEG is truncated: no end-of-image marker";
    if (ph_magic_is_png(p, n))
        return ph_png_reaches_iend(p, n) ? NULL : "PNG is truncated: no IEND chunk";
    if (ph_magic_is_webp(p, n))
        return ph_webp_riff_complete(p, n) ? NULL : "WebP is truncated: shorter than its RIFF size";
    return NULL;
}

uint8_t *ph_decode_buffer(const uint8_t *buffer, size_t length, int *width, int *height,
                          int *channels, int req_comp, uint64_t max_pixels,
                          ph_decode_scale_t decode_scale, ph_error_t *out_err, char *err_msg,
                          size_t err_msg_cap) {
    if (out_err)
        *out_err = PH_SUCCESS;
    if (!buffer || length == 0)
        return NULL;

    if (length > PH_MAX_ENCODED_SIZE) {
        if (out_err)
            *out_err = PH_ERR_IMAGE_TOO_LARGE;
        ph_set_err_msg(err_msg, err_msg_cap, "Encoded image is larger than 2 GiB - 1 byte");
        return NULL;
    }

#if defined(PH_USE_LIBPNG) || defined(PH_USE_SPNG)
    ph_warm_decoder_dispatch();
#endif

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
            uint8_t *data =
                backends[i].decode(buffer, length, width, height, channels, req_comp, max_pixels,
                                   decode_scale, &err, err_msg, err_msg_cap);
            if (data) {
                const char *truncated = ph_container_truncation(buffer, length);
                if (!truncated)
                    return data;
                ph_free_image(data);
                if (out_err)
                    *out_err = PH_ERR_CORRUPT_DATA;
                ph_set_err_msg(err_msg, err_msg_cap, truncated);
                return NULL;
            }
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
