#include "internal.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>

#ifdef PH_USE_TURBOJPEG

#include "turbojpeg.h"

PH_API int ph_can_use_libjpeg(void) { return 1; }

int ph_can_read_jpeg(const uint8_t *magic, size_t len) {
    return (len >= 2 && magic[0] == 0xFF && magic[1] == 0xD8);
}

unsigned char *ph_decode_jpeg_tj(const unsigned char *buffer, unsigned long size, int *width,
                                 int *height, int *channels, int req_comp, uint64_t max_pixels,
                                 ph_error_t *out_err) {
    if (!buffer || size == 0)
        return NULL;

    tjhandle handle = tjInitDecompress();
    if (!handle)
        return NULL;

    int w, h, subsamp, colorspace;
    if (tjDecompressHeader3(handle, buffer, size, &w, &h, &subsamp, &colorspace) < 0) {
        tjDestroy(handle);
        return NULL;
    }

    if (ph_exceeds_pixel_limit((uint64_t)w, (uint64_t)h, max_pixels)) {
        if (out_err)
            *out_err = PH_ERR_IMAGE_TOO_LARGE;
        tjDestroy(handle);
        return NULL;
    }

    int pixelFormat = (req_comp == 1) ? TJPF_GRAY : TJPF_RGB;
    int out_channels = (req_comp == 1) ? 1 : 3;

    size_t pitch_size;
    if (!ph_safe_image_alloc_size((uint64_t)w, (uint64_t)out_channels, 1, &pitch_size) ||
        pitch_size > INT_MAX) {
        if (out_err)
            *out_err = PH_ERR_IMAGE_TOO_LARGE;
        tjDestroy(handle);
        return NULL;
    }
    int pitch = (int)pitch_size;

    size_t alloc_size;
    if (!ph_safe_image_alloc_size((uint64_t)pitch, (uint64_t)h, 1, &alloc_size)) {
        if (out_err)
            *out_err = PH_ERR_IMAGE_TOO_LARGE;
        tjDestroy(handle);
        return NULL;
    }

    unsigned char *output = (unsigned char *)malloc(alloc_size);
    if (!output) {
        tjDestroy(handle);
        return NULL;
    }

    int flags = TJFLAG_FASTDCT | TJFLAG_NOREALLOC;
    if (tjDecompress2(handle, buffer, size, output, w, pitch, h, pixelFormat, flags) < 0) {
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
