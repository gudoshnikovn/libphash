#include "../internal.h"
#include <stdlib.h>

PH_API ph_error_t ph_compute_ahash(ph_context_t *ctx, uint64_t *out_hash) {
    if (!ctx || !ctx->is_loaded || !out_hash) {
        return PH_ERR_INVALID_ARGUMENT;
    }

    const uint8_t *gray_input;
    uint8_t *scratch = NULL;
    if (ctx->channels == 1) {
        gray_input = ctx->data;
    } else {
        size_t gray_size = (size_t)ctx->width * ctx->height;
        scratch = ph_get_scratchpad(ctx, gray_size);
        if (!scratch) {
            return PH_ERR_ALLOCATION_FAILED;
        }
        ph_to_grayscale(ctx, ctx->data, ctx->width, ctx->height, ctx->channels, scratch);
        gray_input = scratch;
    }

    uint8_t hash_input[PH_CORE_HASH_SIZE * PH_CORE_HASH_SIZE];
    uint8_t temp_input[PH_CORE_HASH_SIZE * PH_CORE_HASH_SIZE];

    ph_resize_box(gray_input, ctx->width, ctx->height, temp_input, PH_CORE_HASH_SIZE,
                  PH_CORE_HASH_SIZE);

    // Apply 3x3 Laplacian sharpening to mimic Lanczos filter edge preservation
    int w = PH_CORE_HASH_SIZE;
    int h = PH_CORE_HASH_SIZE;
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            if (x == 0 || y == 0 || x == w - 1 || y == h - 1) {
                hash_input[y * w + x] = temp_input[y * w + x];
            } else {
                int val = 5 * temp_input[y * w + x] - temp_input[(y - 1) * w + x] -
                          temp_input[(y + 1) * w + x] - temp_input[y * w + (x - 1)] -
                          temp_input[y * w + (x + 1)];
                if (val < 0)
                    val = 0;
                if (val > 255)
                    val = 255;
                hash_input[y * w + x] = (uint8_t)val;
            }
        }
    }

    uint64_t total_sum = 0;
    int num_pixels = PH_CORE_HASH_SIZE * PH_CORE_HASH_SIZE;
    for (int i = 0; i < num_pixels; i++) {
        total_sum += hash_input[i];
    }
    uint8_t avg = (uint8_t)(total_sum / num_pixels);

    uint64_t hash = 0;
    for (int i = 0; i < num_pixels; i++) {
        if (hash_input[i] >= avg) {
            hash |= (1ULL << i);
        }
    }

    *out_hash = hash;
    return PH_SUCCESS;
}
