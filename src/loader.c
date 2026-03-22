#include "loader.h"
#include "../vendor/stb_image.h"
#include "loaders/internal.h"
#include <stddef.h>

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
    {NULL, NULL}};

uint8_t *ph_decode_buffer(const uint8_t *buffer, size_t length, int *width, int *height,
                          int *channels, int req_comp) {
    if (!buffer || length == 0)
        return NULL;
    for (int i = 0; backends[i].can_read != NULL; i++) {
        if (backends[i].can_read(buffer, length)) {
            uint8_t *data = backends[i].decode(buffer, length, width, height, channels, req_comp);
            if (data)
                return data;
        }
    }
    return NULL;
}

void ph_free_image(uint8_t *data) {
    if (data) {
        stbi_image_free(data);
    }
}
