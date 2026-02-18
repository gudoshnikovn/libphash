#include "../internal.h"
#include <math.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdlib.h>

static double dct_matrix[PH_DCT_SIZE][PH_DCT_SIZE];
static atomic_bool dct_globally_initialized = false;

void init_dct_matrix(void) {
    if (atomic_exchange(&dct_globally_initialized, true)) {
        return;
    }
    double c = sqrt(1.0 / (double)PH_DCT_SIZE);
    for (int j = 0; j < PH_DCT_SIZE; j++)
        dct_matrix[0][j] = c;

    c = sqrt(2.0 / (double)PH_DCT_SIZE);
    for (int i = 1; i < PH_DCT_SIZE; i++) {
        for (int j = 0; j < PH_DCT_SIZE; j++) {
            dct_matrix[i][j] = c * cos(M_PI * i * (j + 0.5) / (double)PH_DCT_SIZE);
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

    uint8_t dct_input[PH_DCT_SIZE * PH_DCT_SIZE];
    ph_resize_bilinear(gray_full, ctx->width, ctx->height, dct_input, PH_DCT_SIZE, PH_DCT_SIZE);

    double temp[PH_DCT_SIZE * PH_DCT_SIZE], dct_out[PH_DCT_SIZE * PH_DCT_SIZE];

    for (int i = 0; i < PH_DCT_SIZE; i++) {
        for (int j = 0; j < PH_DCT_SIZE; j++) {
            double sum = 0;
            for (int k = 0; k < PH_DCT_SIZE; k++)
                sum += dct_matrix[j][k] * dct_input[i * PH_DCT_SIZE + k];
            temp[i * PH_DCT_SIZE + j] = sum;
        }
    }
    for (int j = 0; j < PH_DCT_SIZE; j++) {
        for (int i = 0; i < PH_DCT_SIZE; i++) {
            double sum = 0;
            for (int k = 0; k < PH_DCT_SIZE; k++)
                sum += dct_matrix[i][k] * temp[k * PH_DCT_SIZE + j];
            dct_out[i * PH_DCT_SIZE + j] = sum;
        }
    }

    double sum_dct = 0;
    for (int i = 0; i < PH_DCT_REDUCTION_SIZE; i++) {
        for (int j = 0; j < PH_DCT_REDUCTION_SIZE; j++) {
            if (i == 0 && j == 0)
                continue;
            sum_dct += dct_out[i * PH_DCT_SIZE + j];
        }
    }

    double avg = sum_dct / (double)(PH_DCT_REDUCTION_SIZE * PH_DCT_REDUCTION_SIZE - 1);
    uint64_t hash = 0;
    for (int i = 0; i < PH_DCT_REDUCTION_SIZE; i++) {
        for (int j = 0; j < PH_DCT_REDUCTION_SIZE; j++) {
            if (dct_out[i * PH_DCT_SIZE + j] > avg) {
                hash |= (1ULL << (i * PH_DCT_REDUCTION_SIZE + j));
            }
        }
    }

    *out_hash = hash;
    return PH_SUCCESS;
}
