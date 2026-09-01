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

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#include <sys/stat.h>
#include <sys/types.h>
#else
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

/* One spelling of "open a file read-only, stat the descriptor and read from it"
 * for both platforms, so the file-loading path below is a single piece of code
 * rather than a POSIX implementation and a Windows one that can drift apart. The
 * Windows CRT (_open/_fstat64/_read/_close) is used rather than the Win32 API on
 * purpose: it sets errno the same way, which is what the diagnostic message is
 * built from. */
#ifdef _WIN32
typedef struct __stat64 ph_file_stat_t;
#define PH_FILE_OPEN_RDONLY(path) _open((path), _O_RDONLY | _O_BINARY)
#define PH_FILE_FSTAT(fd, st) _fstat64((fd), (st))
#define PH_FILE_READ(fd, buf, n) _read((fd), (buf), (unsigned int)(n))
#define PH_FILE_CLOSE(fd) _close(fd)
#ifndef S_ISREG
#define S_ISREG(m) (((m) & _S_IFMT) == _S_IFREG)
#endif
#else
typedef struct stat ph_file_stat_t;
#define PH_FILE_OPEN_RDONLY(path) open((path), O_RDONLY)
#define PH_FILE_FSTAT(fd, st) fstat((fd), (st))
#define PH_FILE_READ(fd, buf, n) read((fd), (buf), (n))
#define PH_FILE_CLOSE(fd) close(fd)
#endif

/* Whether the file can be mapped instead of copied. Mapping is what makes a load
 * cost one open() and no read() at all; the read-into-heap fallback in
 * ph_open_file_bytes() covers the platforms that have no mmap (Windows) and the
 * filesystems that refuse to map. Nothing outside these two helpers knows which
 * of the two produced the bytes -- in particular, the mmap block is no longer
 * tied to the PH_USE_* decoder macros, which used to make a Windows build with
 * any native decoder enabled fail to compile on <sys/mman.h>. */
#if !defined(_WIN32) && defined(_POSIX_MAPPED_FILES)
#define PH_HAVE_MMAP 1
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
        case PH_ERR_REQUIRES_COLOR:
            return "Algorithm requires a color image, but the loaded image is grayscale";
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

/* --- Classifying a path that cannot serve as an image source -------------------
 *
 * These three helpers are deliberately split so that a caller which already holds
 * an open descriptor (or which opens the file once for reading and mapping) can
 * reuse the classification without opening the path a second time: the failure
 * branch takes an errno, the success branch takes a descriptor, and only the
 * convenience wrapper does an open()/close() of its own. */

/* Turns a failed open into PH_ERR_IO with a diagnostic message. `open_errno` must
 * be errno captured immediately after the failing open -- both POSIX and the
 * Windows CRT set it (ENOENT for a missing path or a dangling symlink, EACCES for
 * an unreadable one, and on Windows also for a directory). */
static ph_error_t ph_report_file_open_failure(const char *filepath, int open_errno, char *err_buf,
                                              size_t err_len) {
    char msg[PH_LAST_ERROR_MAX];
    snprintf(msg, sizeof(msg), "Cannot open '%s': %s", filepath, strerror(open_errno));
    ph_set_err_msg(err_buf, err_len, msg);
    return PH_ERR_IO;
}

/* Checks a descriptor that opened successfully. An image source has to be a
 * regular file with at least one byte in it: a directory opens fine on POSIX, and
 * so do character devices and FIFOs, but none of them is something a decoder can
 * be handed, and an empty file is an I/O-level fact rather than an unrecognized
 * image format. Returns PH_SUCCESS otherwise, reporting the size through
 * `out_size` when it is non-NULL. */
static ph_error_t ph_check_open_file(int fd, const char *filepath, long long *out_size,
                                     char *err_buf, size_t err_len) {
    ph_file_stat_t st;
    char msg[PH_LAST_ERROR_MAX];
    if (PH_FILE_FSTAT(fd, &st) != 0) {
        snprintf(msg, sizeof(msg), "Cannot stat '%s': %s", filepath, strerror(errno));
        ph_set_err_msg(err_buf, err_len, msg);
        return PH_ERR_IO;
    }
    if (!S_ISREG(st.st_mode)) {
        snprintf(msg, sizeof(msg), "Cannot read '%s': not a regular file", filepath);
        ph_set_err_msg(err_buf, err_len, msg);
        return PH_ERR_IO;
    }
    if (st.st_size <= 0) {
        snprintf(msg, sizeof(msg), "Cannot read '%s': file is empty", filepath);
        ph_set_err_msg(err_buf, err_len, msg);
        return PH_ERR_IO;
    }
    if (out_size)
        *out_size = (long long)st.st_size;
    return PH_SUCCESS;
}

/* --- Getting the encoded bytes of a file, with exactly one open() -------------
 *
 * A read-only view of a file's contents: either a mapping of the file (the normal
 * case) or a heap copy of it (the fallback). `mapped` says which one it is, i.e.
 * how it has to be released; nothing above this layer needs to care. */
typedef struct {
    const uint8_t *data;
    size_t length;
    int mapped;
} ph_file_bytes_t;

static void ph_release_file_bytes(ph_file_bytes_t *fb) {
    if (!fb->data)
        return;
#ifdef PH_HAVE_MMAP
    if (fb->mapped) {
        munmap((void *)(uintptr_t)fb->data, fb->length);
        fb->data = NULL;
        fb->length = 0;
        return;
    }
#endif
    free((void *)(uintptr_t)fb->data);
    fb->data = NULL;
    fb->length = 0;
}

/* Fallback when the file cannot be mapped: read all of it through the descriptor
 * that is already open, so this still costs no extra open(). A short read is not
 * an error -- the file may legitimately have shrunk since the fstat() above, and
 * whatever bytes did arrive are handed to the decoder, which is the one that gets
 * to say whether they form an image. */
static ph_error_t ph_read_open_file(int fd, const char *filepath, size_t size, ph_file_bytes_t *out,
                                    char *err_buf, size_t err_len) {
    char msg[PH_LAST_ERROR_MAX];
    uint8_t *buf = (uint8_t *)malloc(size);
    if (!buf) {
        snprintf(msg, sizeof(msg), "Cannot read '%s': out of memory for %llu bytes", filepath,
                 (unsigned long long)size);
        ph_set_err_msg(err_buf, err_len, msg);
        return PH_ERR_ALLOCATION_FAILED;
    }

    size_t got = 0;
    while (got < size) {
        size_t want = size - got;
        /* One chunk stays well inside the signed return type of read()/_read()
         * on every platform, including a 32-bit one. */
        if (want > (size_t)16 * 1024 * 1024)
            want = (size_t)16 * 1024 * 1024;
        long long n = (long long)PH_FILE_READ(fd, buf + got, want);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            snprintf(msg, sizeof(msg), "Cannot read '%s': %s", filepath, strerror(errno));
            ph_set_err_msg(err_buf, err_len, msg);
            free(buf);
            return PH_ERR_IO;
        }
        if (n == 0)
            break; /* EOF earlier than fstat() promised */
        got += (size_t)n;
    }

    if (got == 0) {
        snprintf(msg, sizeof(msg), "Cannot read '%s': file is empty", filepath);
        ph_set_err_msg(err_buf, err_len, msg);
        free(buf);
        return PH_ERR_IO;
    }

    out->data = buf;
    out->length = got;
    out->mapped = 0;
    return PH_SUCCESS;
}

/* Opens the path exactly ONCE and hands back all of its bytes.
 *
 * This used to be spread over as many as six openings of the same path -- a
 * probe, a magic-byte sniff, an mmap attempt, stbi_info(), stbi_load() and a
 * final full re-read just to scan for an EXIF tag. Besides being pure I/O
 * overhead on the hottest path in the library, that meant every one of those
 * steps looked at a potentially different file (TOCTOU): the path could be
 * replaced between the probe that accepted it and the read that decoded it.
 * One open, one set of bytes, and everything downstream -- format dispatch,
 * pixel-limit check, decode, orientation scan -- works on that one snapshot.
 *
 * Everything that makes a path unusable as an image source is classified here,
 * before any decoder sees it, so "I could not read this file" is reported as
 * PH_ERR_IO instead of surfacing later as an unrecognized image format. */
static ph_error_t ph_open_file_bytes(const char *filepath, ph_file_bytes_t *out, char *err_buf,
                                     size_t err_len) {
    out->data = NULL;
    out->length = 0;
    out->mapped = 0;

    int fd = PH_FILE_OPEN_RDONLY(filepath);
    if (fd < 0)
        return ph_report_file_open_failure(filepath, errno, err_buf, err_len);

    long long size = 0;
    ph_error_t err = ph_check_open_file(fd, filepath, &size, err_buf, err_len);
    if (err != PH_SUCCESS) {
        PH_FILE_CLOSE(fd);
        return err;
    }

    /* Only reachable where size_t is narrower than off_t (a 32-bit build looking
     * at a >4 GB file). Neither mapping nor reading it can work. */
    if ((unsigned long long)size > (unsigned long long)SIZE_MAX) {
        char msg[PH_LAST_ERROR_MAX];
        snprintf(msg, sizeof(msg), "Cannot read '%s': file is too large to load into memory",
                 filepath);
        ph_set_err_msg(err_buf, err_len, msg);
        PH_FILE_CLOSE(fd);
        return PH_ERR_IO;
    }

#ifdef PH_HAVE_MMAP
    void *mapped = mmap(NULL, (size_t)size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (mapped != MAP_FAILED) {
#if defined(__linux__) || defined(__APPLE__) || defined(__FreeBSD__)
        posix_madvise(mapped, (size_t)size, POSIX_MADV_SEQUENTIAL);
#endif
        /* The mapping keeps the file alive on its own; the descriptor is not
         * needed past this point. */
        PH_FILE_CLOSE(fd);
        out->data = (const uint8_t *)mapped;
        out->length = (size_t)size;
        out->mapped = 1;
        return PH_SUCCESS;
    }
#endif

    err = ph_read_open_file(fd, filepath, (size_t)size, out, err_buf, err_len);
    PH_FILE_CLOSE(fd);
    return err;
}

/* Drops whatever image the context is holding. Both load entry points call this
 * first, so a failed load never leaves the previously loaded image visible. */
static void ph_reset_loaded_image(ph_context_t *ctx) {
    ctx->last_error[0] = '\0';
    if (ctx->image.raw_rgb)
        ph_free_image(ctx->image.raw_rgb);
    ctx->image.raw_rgb = NULL;
    ctx->image.is_loaded = 0;
    if (ctx->image.gray_cache) {
        free(ctx->image.gray_cache);
        ctx->image.gray_cache = NULL;
    }
}

/* The one decode path. Both ph_load_from_file() and ph_load_from_memory() reach
 * the decoders through this, which is what makes backend dispatch, the
 * pixel-count limit, error classification and EXIF auto-orientation identical for
 * a file and for a buffer -- auto-orientation in particular used to be applied on
 * the file path only in some build configurations. */
static ph_error_t ph_load_encoded_bytes(ph_context_t *ctx, const uint8_t *data, size_t length) {
    int req_comp = ctx->config.load_grayscale ? 1 : 0;
    int w = 0, h = 0, ch = 0;
    ph_error_t decode_err = PH_SUCCESS;

    uint8_t *decoded = ph_decode_buffer(data, length, &w, &h, &ch, req_comp, ctx->config.max_pixels,
                                        &decode_err, ctx->last_error, sizeof(ctx->last_error));
    if (!decoded) {
        /* ph_decode_buffer() resolves a non-empty buffer to either decoded data
         * or a specific error (its last-resort stb_image backend claims anything
         * not already claimed, except WebP without PH_USE_WEBP, which it reports
         * itself), so the fallback below is belt-and-braces: never report success
         * without an image. */
        return (decode_err != PH_SUCCESS) ? decode_err : PH_ERR_CORRUPT_DATA;
    }

    ctx->image.raw_rgb = decoded;
    ctx->image.width = w;
    ctx->image.height = h;
    ctx->image.channels = ch;
    ctx->image.is_loaded = 1;

    if (ctx->config.auto_orient) {
        int orientation = ph_scan_orientation(data, length);
        if (orientation != 1)
            ph_apply_exif_orientation(&ctx->image.raw_rgb, &ctx->image.width, &ctx->image.height,
                                      ctx->image.channels, orientation);
    }
    return PH_SUCCESS;
}

PH_API ph_error_t ph_load_from_file(ph_context_t *ctx, const char *filepath) {
    if (!ctx || !filepath)
        return PH_ERR_INVALID_ARGUMENT;
    ph_reset_loaded_image(ctx);

    ph_file_bytes_t bytes;
    ph_error_t err = ph_open_file_bytes(filepath, &bytes, ctx->last_error, sizeof(ctx->last_error));
    if (err != PH_SUCCESS)
        return err;

    err = ph_load_encoded_bytes(ctx, bytes.data, bytes.length);
    ph_release_file_bytes(&bytes);
    return err;
}

PH_API ph_error_t ph_load_from_memory(ph_context_t *ctx, const uint8_t *buffer, size_t length) {
    if (!ctx || !buffer || length == 0)
        return PH_ERR_INVALID_ARGUMENT;
    ph_reset_loaded_image(ctx);
    return ph_load_encoded_bytes(ctx, buffer, length);
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
