#include "../internal.h"
#include <math.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdlib.h>

static void compute_dct_coefficients(float *matrix, int n) {
    float c = (float)sqrt(1.0 / (double)n);
    for (int j = 0; j < n; j++)
        matrix[j] = c;

    c = (float)sqrt(2.0 / (double)n);
    for (int i = 1; i < n; i++) {
        for (int j = 0; j < n; j++) {
            matrix[i * n + j] = (float)(c * cos(M_PI * i * (j + 0.5) / (double)n));
        }
    }
}

PH_API ph_error_t ph_compute_phash(ph_context_t *ctx, uint64_t *out_hash) {
    if (!ctx || !ctx->is_loaded || !out_hash)
        return PH_ERR_INVALID_ARGUMENT;

    int dct_size = ctx->phash_dct_size;
    int reduction_size = ctx->phash_reduction_size;

    /* Ensure we can fit in 64-bit hash (max 8x8) */
    if (reduction_size > 8)
        reduction_size = 8;

    uint8_t *gray_full = ph_get_gray(ctx);
    if (!gray_full)
        return PH_ERR_ALLOCATION_FAILED;

    /* Allocate all needed buffers from scratchpad:
     * 1. dct_input: dct_size * dct_size (bytes)
     * 2. dct_matrix: dct_size * dct_size (floats)
     * 3. temp_matrix: dct_size * reduction_size (floats)
     * 4. dct_out: reduction_size * reduction_size (floats)
     */
    size_t sz1 = (size_t)dct_size * dct_size;
    size_t sz2 = sz1 * sizeof(float);
    size_t sz3 = (size_t)dct_size * reduction_size * sizeof(float);
    size_t sz4 = (size_t)reduction_size * reduction_size * sizeof(float);

    uint8_t *scratch = ph_get_scratchpad(ctx, sz1 + sz2 + sz3 + sz4);
    if (!scratch)
        return PH_ERR_ALLOCATION_FAILED;

    uint8_t *dct_input = scratch;
    float *dct_mat = (float *)(scratch + sz1);
    float *temp = (float *)(scratch + sz1 + sz2);
    float *dct_out = (float *)(scratch + sz1 + sz2 + sz3);

    ph_resize_bilinear(gray_full, ctx->width, ctx->height, dct_input, dct_size, dct_size);
    compute_dct_coefficients(dct_mat, dct_size);

    /* First pass: DCT of each row, but only compute first reduction_size columns */
    for (int i = 0; i < dct_size; i++) {
        for (int j = 0; j < reduction_size; j++) {
            float sum = 0;
            for (int k = 0; k < dct_size; k++)
                sum += dct_mat[j * dct_size + k] * dct_input[i * dct_size + k];
            temp[i * reduction_size + j] = sum;
        }
    }

    /* Second pass: DCT of first reduction_size columns, but only first reduction_size rows */
    for (int j = 0; j < reduction_size; j++) {
        for (int i = 0; i < reduction_size; i++) {
            float sum = 0;
            for (int k = 0; k < dct_size; k++)
                sum += dct_mat[i * dct_size + k] * temp[k * reduction_size + j];
            dct_out[i * reduction_size + j] = sum;
        }
    }

    float sum_dct = 0;
    for (int i = 0; i < reduction_size; i++) {
        for (int j = 0; j < reduction_size; j++) {
            if (i == 0 && j == 0)
                continue;
            sum_dct += dct_out[i * reduction_size + j];
        }
    }

    float avg = sum_dct / (float)(reduction_size * reduction_size - 1);
    uint64_t hash = 0;
    for (int i = 0; i < reduction_size; i++) {
        for (int j = 0; j < reduction_size; j++) {
            if (dct_out[i * reduction_size + j] > avg) {
                hash |= (1ULL << (i * reduction_size + j));
            }
        }
    }

    *out_hash = hash;
    return PH_SUCCESS;
}
