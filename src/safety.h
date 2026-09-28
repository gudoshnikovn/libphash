#ifndef PH_SAFETY_H
#define PH_SAFETY_H

/* Limits every decode path enforces, overflow-checked allocation sizes, and the
 * fixed-size diagnostic buffer helpers. Nothing here allocates. */

#include <limits.h>
#include <stddef.h>
#include <stdint.h>

/* Default cap on width*height before decoding a pixel buffer (decompression-bomb
 * protection). Overridable via ph_context_set_max_pixels(); 0 disables it. */
#define PH_DEFAULT_MAX_PIXELS ((uint64_t)256 * 1024 * 1024)

/* Computes w * h * channels for an allocation size, refusing to silently wrap.
 * Returns 0 (and leaves *out untouched) if the product would overflow size_t;
 * returns 1 and sets *out to the byte count otherwise. */
static inline int ph_safe_image_alloc_size(uint64_t w, uint64_t h, uint64_t channels, size_t *out) {
    if (w == 0 || h == 0 || channels == 0)
        return 0;
    if (w > (uint64_t)SIZE_MAX / h)
        return 0;
    uint64_t wh = w * h;
    if (wh > (uint64_t)SIZE_MAX / channels)
        return 0;
    *out = (size_t)(wh * channels);
    return 1;
}

/* Hard ceiling on width * height that the implementation can process correctly,
 * independent of any user-configured max_pixels.
 *
 * Pixel *indexing* is still done in `int` across the hot loops (`y * w + x` in
 * image/filters.c, image/orient.c, hashes/radial.c, hashes/whash.c, hashes/phash.c),
 * so an image with more than INT_MAX pixels overflows those index computations --
 * signed overflow, i.e. undefined behaviour. Rather than leaving that reachable in a
 * documented mode, the library refuses such images outright. The default max_pixels
 * (256 MP) is eight times below this ceiling, so the ceiling only ever comes into play
 * when a caller deliberately raises or disables the limit.
 *
 * Raising this ceiling means converting that index arithmetic to size_t everywhere,
 * SIMD paths included -- not done, since nothing currently needs images this large. */
#define PH_MAX_SUPPORTED_PIXELS ((uint64_t)INT_MAX)

/* Largest encoded (still compressed) input ph_decode_buffer() accepts, in bytes. stb_image
 * takes the length as an int, so a longer buffer would reach it truncated: a negative
 * length makes a valid image "unrecognized", and a length past 4 GiB wraps to a small
 * positive one and decodes the prefix as if it were the file. One limit for every build keeps the
 * answer independent of which decoder a format goes to. */
#define PH_MAX_ENCODED_SIZE ((size_t)INT_MAX)

/* Upper bound on a single image dimension, applied by every decode path.
 *
 * max_pixels bounds the *area*, which on its own permits an absurd aspect ratio: a
 * 268435456 x 1 image passes the default area limit exactly, yet makes a decoder size
 * a single row buffer of ~800 MB. libpng's own default per-dimension limit is 1000000,
 * so this matches it: libphash must only ever *lower* that limit, never raise it.
 *
 * The cap is deliberately *not* derived from max_pixels: raising or disabling the area
 * limit must not raise the aspect-ratio ceiling with it. It lives here, next to the
 * area check, so that the verdict and the error code (PH_ERR_IMAGE_TOO_LARGE) are the
 * same for every format and in every build configuration. */
#define PH_MAX_IMAGE_DIMENSION 1000000u

/* Returns 1 if either dimension is beyond what the library will decode. Same contract
 * as ph_exceeds_pixel_limit(): `w` and `h` must each fit in 32 bits. */
static inline int ph_exceeds_dimension_limit(uint64_t w, uint64_t h) {
    return w > PH_MAX_IMAGE_DIMENSION || h > PH_MAX_IMAGE_DIMENSION;
}

/* Returns 1 if decoding a w x h image is disallowed.
 *
 * Two limits apply, and the stricter one wins:
 *   - the caller's max_pixels, where 0 means "no limit of my own";
 *   - PH_MAX_SUPPORTED_PIXELS, which always applies -- including when max_pixels is 0
 *     or set above it. `0` therefore means "the implementation's limit", not "no limit".
 *
 * Contract: `w` and `h` must each fit in 32 bits. Every caller feeds it either an
 * `int` dimension (already made non-negative -- see ph_abs_dim()) or a `png_uint_32`,
 * so `w * h` is at most 2^64 - 2^33 + 1 and cannot wrap the uint64_t product. Do NOT
 * call this with values wider than 32 bits without adding an overflow check first. */
static inline int ph_exceeds_pixel_limit(uint64_t w, uint64_t h, uint64_t max_pixels) {
    uint64_t pixels = w * h;
    if (pixels > PH_MAX_SUPPORTED_PIXELS)
        return 1;
    if (max_pixels == 0)
        return 0;
    return pixels > max_pixels;
}

/* printf-style format checking for internal helpers; nothing on compilers without it. */
#if defined(__GNUC__) || defined(__clang__)
#define PH_PRINTF_FORMAT(fmt_idx, arg_idx) __attribute__((format(printf, fmt_idx, arg_idx)))
#else
#define PH_PRINTF_FORMAT(fmt_idx, arg_idx)
#endif

/* Where to cut `len` bytes of a string that was truncated to fit, so that the cut does
 * not split a UTF-8 sequence: if the last sequence started within the kept bytes is
 * incomplete, it is dropped whole. Bytes that were never valid UTF-8 are left as they
 * are -- a message in a legacy locale is not ours to repair; the rule is only that our
 * own truncation must not create invalid UTF-8. */
static inline size_t ph_utf8_cut(const char *s, size_t len) {
    size_t lead = len;
    int back = 0;
    while (lead > 0 && back < 4 && ((unsigned char)s[lead - 1] & 0xC0) == 0x80) {
        lead--;
        back++;
    }
    if (lead == 0)
        return len;
    unsigned char c = (unsigned char)s[lead - 1];
    size_t need = (c >= 0xF0 && c <= 0xF7) ? 4 : (c >= 0xE0) ? 3 : (c >= 0xC0) ? 2 : 1;
    if (c < 0xC0 || c > 0xF7)
        return len; /* ASCII, or not a lead byte at all: nothing of ours to trim */
    return (size_t)back + 1 < need ? lead - 1 : len;
}

/* Truncating copy into a fixed-size diagnostic buffer (err_msg may be NULL/zero-size,
 * meaning the caller isn't collecting a message). A truncation never splits a UTF-8
 * character (ph_utf8_cut()). Never allocates. */
static inline void ph_set_err_msg(char *err_msg, size_t err_msg_cap, const char *msg) {
    if (!err_msg || err_msg_cap == 0 || !msg)
        return;
    size_t i = 0;
    for (; i + 1 < err_msg_cap && msg[i] != '\0'; i++)
        err_msg[i] = msg[i];
    if (msg[i] != '\0')
        i = ph_utf8_cut(err_msg, i);
    err_msg[i] = '\0';
}

#endif /* PH_SAFETY_H */
