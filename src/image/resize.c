#include "internal.h"
#include <stdint.h>
#include <string.h>

#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include "../vendor/stb_image_resize2.h"

#if defined(__ARM_NEON)
#include <arm_neon.h>
#elif defined(__SSE4_1__)
#include <smmintrin.h>
#endif

void ph_resize_box(const uint8_t *src, int sw, int sh, uint8_t *dst, int dw, int dh) {
    if (dw <= 0 || dh <= 0 || sw <= 0 || sh <= 0)
        return;
    stbir_resize(src, sw, sh, 0, dst, dw, dh, 0, STBIR_1CHANNEL, STBIR_TYPE_UINT8, STBIR_EDGE_CLAMP,
                 STBIR_FILTER_BOX);
}

void ph_resize_lanczos(const uint8_t *src, int sw, int sh, uint8_t *dst, int dw, int dh) {
    if (dw <= 0 || dh <= 0 || sw <= 0 || sh <= 0)
        return;

    stbir_resize_uint8_linear(src, sw, sh, 0, dst, dw, dh, 0, STBIR_1CHANNEL);
}
