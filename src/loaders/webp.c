#include "internal.h"
#include <stdio.h>
#include <stdlib.h>

#ifdef PH_USE_WEBP

#include <webp/decode.h>

PH_API int ph_can_use_webp(void) { return 1; }

int ph_can_read_webp(const uint8_t *magic, size_t len) {
    return (len >= 12 && magic[0] == 'R' && magic[1] == 'I' && magic[2] == 'F' && magic[3] == 'F' &&
            magic[8] == 'W' && magic[9] == 'E' && magic[10] == 'B' && magic[11] == 'P');
}

unsigned char *ph_decode_webp_mem(const unsigned char *buffer, unsigned long size, int *width,
                                  int *height, int *channels, int req_comp) {
    (void)req_comp;
    if (!buffer || size < 12)
        return NULL;

    int w, h;
    if (!WebPGetInfo(buffer, size, &w, &h))
        return NULL;

    /* libwebp has no native grayscale decode — always decode to RGB.
     * Grayscale conversion is handled later by ph_to_grayscale. */
    int out_channels = 3;
    size_t stride = (size_t)w * out_channels;
    size_t out_size = stride * h;

    unsigned char *output = (unsigned char *)malloc(out_size);
    if (!output)
        return NULL;

    /* Decode into our buffer so we can free() it normally */
    if (!WebPDecodeRGBInto(buffer, size, output, out_size, (int)stride)) {
        free(output);
        return NULL;
    }

    *width = w;
    *height = h;
    *channels = out_channels;
    return output;
}

#else
PH_API int ph_can_use_webp(void) { return 0; }
#endif // PH_USE_WEBP
