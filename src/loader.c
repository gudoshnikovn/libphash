#include "loader.h"
#include "../vendor/stb_image.h"
#include "loaders/internal.h"
#include <stddef.h>

#ifdef PH_TESTING
// Mock backend for testing the dispatcher loop without real libraries
static int ph_mock_can_read(const uint8_t *magic, size_t len) {
    if (len >= 4 && magic[0] == 0xDE && magic[1] == 0xAD) return 1;
    return 0;
}
static uint8_t *ph_mock_decode(const uint8_t *data, size_t len, int *w, int *h, int *ch, int req) {
    (void)data; (void)len; (void)req;
    *w = 1; *h = 1; *ch = 3;
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
#ifdef PH_TESTING
    {ph_mock_can_read, ph_mock_decode},
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
