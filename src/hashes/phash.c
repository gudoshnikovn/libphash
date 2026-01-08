#include "../internal.h"
#include <math.h>
#include <stdlib.h>

static void ph_dct_1d(const double *src, double *dst, int size) {
    for (int i = 0; i < size; i++) {
        double sum = 0;
        for (int j = 0; j < size; j++)
            sum += src[j] * cos(M_PI * i * (j + 0.5) / size);
        dst[i] = sum * ((i == 0) ? sqrt(1.0 / size) : sqrt(2.0 / size));
    }
}

PH_API ph_error_t ph_compute_phash(ph_context_t *ctx, uint64_t *out_hash) {
    if (!ctx || !ctx->is_loaded || !out_hash)
        return PH_ERR_INVALID_ARGUMENT;
    uint8_t gray32[1024];
    uint8_t *full_gray = malloc(ctx->width * ctx->height);
    if (!full_gray)
        return PH_ERR_ALLOCATION_FAILED;

    ph_to_grayscale(ctx->data, ctx->width, ctx->height, ctx->channels, full_gray);
    ph_resize_grayscale(full_gray, ctx->width, ctx->height, gray32, 32, 32);
    free(full_gray);

    double dct_in[1024], dct_out[1024], temp[1024];
    for (int i = 0; i < 1024; i++)
        dct_in[i] = (double)gray32[i];

    // 2D DCT
    for (int i = 0; i < 32; i++)
        ph_dct_1d(&dct_in[i * 32], &temp[i * 32], 32);
    for (int j = 0; j < 32; j++) {
        double col_in[32], col_out[32];
        for (int i = 0; i < 32; i++)
            col_in[i] = temp[i * 32 + j];
        ph_dct_1d(col_in, col_out, 32);
        for (int i = 0; i < 32; i++)
            dct_out[i * 32 + j] = col_out[i];
    }

    double sum = 0;
    for (int i = 0; i < 8; i++)
        for (int j = 0; j < 8; j++)
            if (i != 0 || j != 0)
                sum += dct_out[i * 32 + j];

    double avg = sum / 63.0;
    uint64_t hash = 0;
    for (int i = 0; i < 8; i++)
        for (int j = 0; j < 8; j++)
            if (dct_out[i * 32 + j] > avg)
                hash |= (1ULL << (i * 8 + j));

    *out_hash = hash;
    return PH_SUCCESS;
}
