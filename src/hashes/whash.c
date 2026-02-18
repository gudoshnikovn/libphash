#include "../internal.h"
#include <stdlib.h>

static void haar_1d(double *data, int n) {
    double temp[PH_CORE_HASH_SIZE];
    int h = n / 2;
    for (int i = 0; i < h; i++) {
        temp[i] = (data[2 * i] + data[2 * i + 1]) / PH_HAAR_SCALE;
        temp[i + h] = (data[2 * i] - data[2 * i + 1]) / PH_HAAR_SCALE;
    }
    for (int i = 0; i < n; i++)
        data[i] = temp[i];
}

PH_API ph_error_t ph_compute_whash(ph_context_t *ctx, uint64_t *out_hash) {
    if (!ctx || !ctx->is_loaded || !out_hash)
        return PH_ERR_INVALID_ARGUMENT;

    int total_pixels = PH_CORE_HASH_SIZE * PH_CORE_HASH_SIZE;
    uint8_t hash_input[PH_CORE_HASH_SIZE * PH_CORE_HASH_SIZE];
    if (!PH_SAFE_ALLOC_SIZE(ctx->width, ctx->height))
        return PH_ERR_ALLOCATION_FAILED;

    uint8_t *full_gray = malloc(ctx->width * ctx->height);
    if (!full_gray)
        return PH_ERR_ALLOCATION_FAILED;

    ph_to_grayscale(ctx->data, ctx->width, ctx->height, ctx->channels, full_gray);
    ph_resize_box(full_gray, ctx->width, ctx->height, hash_input, PH_CORE_HASH_SIZE, PH_CORE_HASH_SIZE);
    free(full_gray);

    double d[PH_CORE_HASH_SIZE * PH_CORE_HASH_SIZE];
    for (int i = 0; i < total_pixels; i++)
        d[i] = hash_input[i];

    for (int i = 0; i < PH_CORE_HASH_SIZE; i++)
        haar_1d(&d[i * PH_CORE_HASH_SIZE], PH_CORE_HASH_SIZE);
    for (int j = 0; j < PH_CORE_HASH_SIZE; j++) {
        double col[PH_CORE_HASH_SIZE];
        for (int i = 0; i < PH_CORE_HASH_SIZE; i++)
            col[i] = d[i * PH_CORE_HASH_SIZE + j];
        haar_1d(col, PH_CORE_HASH_SIZE);
        for (int i = 0; i < PH_CORE_HASH_SIZE; i++)
            d[i * PH_CORE_HASH_SIZE + j] = col[i];
    }

    double sum = 0;
    for (int i = 0; i < total_pixels; i++)
        sum += d[i];
    double avg = sum / (double)total_pixels;

    uint64_t hash = 0;
    for (int i = 0; i < total_pixels; i++)
        if (d[i] > avg)
            hash |= (1ULL << i);
    *out_hash = hash;
    return PH_SUCCESS;
}
