#include "../internal.h"
#include <math.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdlib.h>

static double dct_matrix[32][32];
static atomic_bool dct_globally_initialized = false;

void init_dct_matrix(void) {
    if (atomic_exchange(&dct_globally_initialized, true)) {
        return;
    }
    double c = sqrt(1.0 / 32.0);
    for (int j = 0; j < 32; j++)
        dct_matrix[0][j] = c;

    c = sqrt(2.0 / 32.0);
    for (int i = 1; i < 32; i++) {
        for (int j = 0; j < 32; j++) {
            dct_matrix[i][j] = c * cos(M_PI * i * (j + 0.5) / 32.0);
        }
    }
    dct_globally_initialized = true;
}

PH_API ph_error_t ph_compute_phash(ph_context_t *ctx, uint64_t *out_hash) {
    if (!ctx || !ctx->is_loaded || !out_hash)
        return PH_ERR_INVALID_ARGUMENT;

    uint8_t *gray_full = ph_get_gray(ctx);
    if (!gray_full)
        return PH_ERR_ALLOCATION_FAILED;

    uint8_t gray32[1024];
    ph_resize_bilinear(gray_full, ctx->width, ctx->height, gray32, 32, 32);

    double temp[1024], dct_out[1024];

    for (int i = 0; i < 32; i++) {
        for (int j = 0; j < 32; j++) {
            double sum = 0;
            for (int k = 0; k < 32; k++)
                sum += dct_matrix[j][k] * gray32[i * 32 + k];
            temp[i * 32 + j] = sum;
        }
    }
    for (int j = 0; j < 32; j++) {
        for (int i = 0; i < 32; i++) {
            double sum = 0;
            for (int k = 0; k < 32; k++)
                sum += dct_matrix[i][k] * temp[k * 32 + j];
            dct_out[i * 32 + j] = sum;
        }
    }

    double sum_dct = 0;
    for (int i = 0; i < 8; i++) {
        for (int j = 0; j < 8; j++) {
            if (i == 0 && j == 0)
                continue;
            sum_dct += dct_out[i * 32 + j];
        }
    }

    double avg = sum_dct / 63.0;
    uint64_t hash = 0;
    for (int i = 0; i < 8; i++) {
        for (int j = 0; j < 8; j++) {
            if (dct_out[i * 32 + j] > avg) {
                hash |= (1ULL << (i * 8 + j));
            }
        }
    }

    *out_hash = hash;
    return PH_SUCCESS;
}
