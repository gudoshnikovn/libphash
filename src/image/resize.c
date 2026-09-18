#include "internal.h"
#include <stdint.h>
#include <string.h>

/* Declarations only. The stb_image_resize2 implementation is instantiated in
 * src/image/stb_resize_impl.c, which is compiled with -fno-sanitize=alignment
 * because of deliberate unaligned 64-bit moves inside stb (see the
 * comment at the top of that file). Keeping the implementation out of this TU
 * keeps our own code fully sanitizer-instrumented. */
#include "../vendor/stb_image_resize2.h"

int ph_resize_box(const uint8_t *src, int sw, int sh, uint8_t *dst, int dw, int dh) {
    if (dw <= 0 || dh <= 0 || sw <= 0 || sh <= 0)
        return 0;
    void *result = stbir_resize(src, sw, sh, 0, dst, dw, dh, 0, STBIR_1CHANNEL, STBIR_TYPE_UINT8,
                                STBIR_EDGE_CLAMP, STBIR_FILTER_BOX);
    return result != NULL;
}

int ph_resize_mitchell(const uint8_t *src, int sw, int sh, uint8_t *dst, int dw, int dh) {
    if (dw <= 0 || dh <= 0 || sw <= 0 || sh <= 0)
        return 0;

    void *result = stbir_resize(src, sw, sh, 0, dst, dw, dh, 0, STBIR_1CHANNEL, STBIR_TYPE_UINT8,
                                STBIR_EDGE_CLAMP, STBIR_FILTER_MITCHELL);
    return result != NULL;
}
