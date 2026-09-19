#include "internal.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>

#ifdef PH_USE_TURBOJPEG

#include "turbojpeg.h"
#include <string.h>

PH_API int ph_can_use_libjpeg(void) { return 1; }

/* Two distinct, independently-worded OOM messages can reach here:
 *   - libjpeg-turbo's own memory manager (jmemmgr.c) reports a failed internal
 *     malloc via ERREXIT1(cinfo, JERR_OUT_OF_MEMORY, which), whose message text
 *     (vendor/libjpeg-turbo/src/jerror.h) is "Insufficient memory (case %d)" --
 *     the %d varies, hence a substring match, not exact -- an earlier version of
 *     this check used strncmp() with a hardcoded length one byte too long (20,
 *     not strlen("Insufficient memory") == 19), which always compared the
 *     literal's NUL terminator against the real message's following space and
 *     therefore never matched at all.
 *   - the TurboJPEG API wrapper's own tj3Decompress8() reports its own allocation
 *     failure (a buffer it allocates itself, distinct from libjpeg's memory
 *     manager) as "tj3Decompress8(): Memory allocation failure" -- unrelated
 *     wording, needs its own check.
 * Same idea as ph_stb_reason_is_oom() in src/loader.c for the stb_image backend.
 * Without this, an injected/real allocation failure inside
 * tjDecompressHeader3()/tjDecompress2() was indistinguishable from actual corrupt
 * JPEG data -- both returned < 0 and got mapped to PH_ERR_CORRUPT_DATA below.
 *
 * Not every allocation failure surfaces with recognizable wording, though: a
 * malloc failing inside jpeg_read_header()'s marker-processing tables can leave
 * libjpeg with stale/zeroed state that it then misreports as a substantively
 * different, memory-silent error ("Could not determine subsampling level of JPEG
 * image") -- a genuine libjpeg-turbo limitation this wrapper has no way to see
 * through, since the message it hands back carries no indication that the root
 * cause was an allocation failure at all. */
static int ph_tj_message_is_oom(const char *msg) {
    return msg && (strstr(msg, "Insufficient memory") != NULL ||
                   strstr(msg, "Memory allocation failure") != NULL);
}

int ph_can_read_jpeg(const uint8_t *magic, size_t len) {
    return (len >= 2 && magic[0] == 0xFF && magic[1] == 0xD8);
}

/* decode_scale -> a libjpeg-turbo scaling factor. TJSCALED() below picks the nearest
 * scaling factor the JPEG's DCT actually supports at or under the requested size, same
 * as libjpeg-turbo does for any other caller of this API -- eighths are just the
 * factors this library exposes as a stable, documented contract (see
 * ph_context_set_decode_scale()), not a hard restriction of the underlying decoder. */
static tjscalingfactor ph_jpeg_scaling_factor(ph_decode_scale_t decode_scale) {
    switch (decode_scale) {
        case PH_DECODE_SCALE_HALF:
            return (tjscalingfactor){1, 2};
        case PH_DECODE_SCALE_QUARTER:
            return (tjscalingfactor){1, 4};
        case PH_DECODE_SCALE_EIGHTH:
            return (tjscalingfactor){1, 8};
        case PH_DECODE_SCALE_FULL:
        default:
            return (tjscalingfactor){1, 1};
    }
}

unsigned char *ph_decode_jpeg_tj(const unsigned char *buffer, unsigned long size, int *width,
                                 int *height, int *channels, int req_comp, uint64_t max_pixels,
                                 ph_decode_scale_t decode_scale, ph_error_t *out_err, char *err_msg,
                                 size_t err_msg_cap) {
    if (!buffer || size == 0)
        return NULL;

    tjhandle handle = tjInitDecompress();
    if (!handle) {
        /* tjInitDecompress()'s only failure mode is its own internal allocation
         * failing; leaving *out_err untouched here used to fall through to
         * ph_decode_buffer()'s PH_SUCCESS-turned-PH_ERR_CORRUPT_DATA fallback
         * (src/loader.c), misreporting an OOM as corrupt image data. */
        if (out_err)
            *out_err = PH_ERR_ALLOCATION_FAILED;
        ph_set_err_msg(err_msg, err_msg_cap, "Memory allocation failed");
        return NULL;
    }

    int w, h, subsamp, colorspace;
    if (tjDecompressHeader3(handle, buffer, size, &w, &h, &subsamp, &colorspace) < 0) {
        const char *tj_err = tjGetErrorStr2(handle);
        if (out_err)
            *out_err =
                ph_tj_message_is_oom(tj_err) ? PH_ERR_ALLOCATION_FAILED : PH_ERR_CORRUPT_DATA;
        ph_set_err_msg(err_msg, err_msg_cap, tj_err);
        tjDestroy(handle);
        return NULL;
    }

    /* The pixel-count limit is judged against the file's declared full-resolution
     * dimensions, not the requested decode size: it exists to reject decompression
     * bombs, which a scale request does not make safe -- the header can still claim an
     * enormous image regardless of what the caller asked to receive. */
    if (ph_exceeds_pixel_limit((uint64_t)w, (uint64_t)h, max_pixels)) {
        if (out_err)
            *out_err = PH_ERR_IMAGE_TOO_LARGE;
        ph_set_err_msg(err_msg, err_msg_cap, "Image exceeds the configured maximum pixel count");
        tjDestroy(handle);
        return NULL;
    }

    tjscalingfactor sf = ph_jpeg_scaling_factor(decode_scale);
    w = TJSCALED(w, sf);
    h = TJSCALED(h, sf);

    int pixelFormat = (req_comp == 1) ? TJPF_GRAY : TJPF_RGB;
    int out_channels = (req_comp == 1) ? 1 : 3;

    size_t pitch_size;
    if (!ph_safe_image_alloc_size((uint64_t)w, (uint64_t)out_channels, 1, &pitch_size) ||
        pitch_size > INT_MAX) {
        if (out_err)
            *out_err = PH_ERR_IMAGE_TOO_LARGE;
        ph_set_err_msg(err_msg, err_msg_cap, "Image exceeds the configured maximum pixel count");
        tjDestroy(handle);
        return NULL;
    }
    int pitch = (int)pitch_size;

    size_t alloc_size;
    if (!ph_safe_image_alloc_size((uint64_t)pitch, (uint64_t)h, 1, &alloc_size)) {
        if (out_err)
            *out_err = PH_ERR_IMAGE_TOO_LARGE;
        ph_set_err_msg(err_msg, err_msg_cap, "Image exceeds the configured maximum pixel count");
        tjDestroy(handle);
        return NULL;
    }

    unsigned char *output = (unsigned char *)malloc(alloc_size);
    if (!output) {
        if (out_err)
            *out_err = PH_ERR_ALLOCATION_FAILED;
        ph_set_err_msg(err_msg, err_msg_cap, "Memory allocation failed");
        tjDestroy(handle);
        return NULL;
    }

    int flags = TJFLAG_FASTDCT | TJFLAG_NOREALLOC;
    if (tjDecompress2(handle, buffer, size, output, w, pitch, h, pixelFormat, flags) < 0) {
        const char *tj_err = tjGetErrorStr2(handle);
        if (out_err)
            *out_err =
                ph_tj_message_is_oom(tj_err) ? PH_ERR_ALLOCATION_FAILED : PH_ERR_CORRUPT_DATA;
        ph_set_err_msg(err_msg, err_msg_cap, tj_err);
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
