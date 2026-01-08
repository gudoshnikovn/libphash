#include "../internal.h"
#include <stdlib.h>

static void haar_1d(double *data, int n) {
    double temp[64];
    int h = n / 2;
    for (int i = 0; i < h; i++) {
        temp[i] = (data[2 * i] + data[2 * i + 1]) / 1.4142;
        temp[i + h] = (data[2 * i] - data[2 * i + 1]) / 1.4142;
    }
    for (int i = 0; i < n; i++)
        data[i] = temp[i];
}

PH_API ph_error_t ph_compute_whash(ph_context_t *ctx, uint64_t *out_hash) {
    if (!ctx || !ctx->is_loaded || !out_hash)
        return PH_ERR_INVALID_ARGUMENT;
    uint8_t gray[64];
    uint8_t *full_gray = malloc(ctx->width * ctx->height);
    if (!full_gray)
        return PH_ERR_ALLOCATION_FAILED;

    ph_to_grayscale(ctx->data, ctx->width, ctx->height, ctx->channels, full_gray);
    ph_resize_grayscale(full_gray, ctx->width, ctx->height, gray, 8, 8);
    free(full_gray);

    double d[64];
    for (int i = 0; i < 64; i++)
        d[i] = gray[i];

    for (int i = 0; i < 8; i++)
        haar_1d(&d[i * 8], 8);
    for (int j = 0; j < 8; j++) {
        double col[8];
        for (int i = 0; i < 8; i++)
            col[i] = d[i * 8 + j];
        haar_1d(col, 8);
        for (int i = 0; i < 8; i++)
            d[i * 8 + j] = col[i];
    }

    double sum = 0;
    for (int i = 0; i < 64; i++)
        sum += d[i];
    double avg = sum / 64.0;

    uint64_t hash = 0;
    for (int i = 0; i < 64; i++)
        if (d[i] > avg)
            hash |= (1ULL << i);
    *out_hash = hash;
    return PH_SUCCESS;
}
