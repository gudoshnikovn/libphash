#include "../internal.h"
#include <stdlib.h>

static void haar_1d_float(float *data, int n) {
    float temp[PH_CORE_HASH_SIZE]; /* Only valid for N=8 */
    int h = n / 2;
    float inv_haar = (float)(1.0 / PH_HAAR_SCALE);
    for (int i = 0; i < h; i++) {
        temp[i] = (data[2 * i] + data[2 * i + 1]) * inv_haar;
        temp[i + h] = (data[2 * i] - data[2 * i + 1]) * inv_haar;
    }
    for (int i = 0; i < n; i++)
        data[i] = temp[i];
}

static ph_error_t ph_compute_whash_fast(ph_context_t *ctx, uint64_t *out_hash) {
    int total_pixels = PH_CORE_HASH_SIZE * PH_CORE_HASH_SIZE;
    uint8_t hash_input[PH_CORE_HASH_SIZE * PH_CORE_HASH_SIZE];

    size_t gray_size = (size_t)ctx->width * ctx->height;
    if (!PH_SAFE_ALLOC_SIZE(ctx->width, ctx->height))
        return PH_ERR_ALLOCATION_FAILED;

    const uint8_t *full_gray;
    uint8_t *scratch = NULL;
    if (ctx->channels == 1) {
        full_gray = ctx->data;
    } else {
        scratch = ph_get_scratchpad(ctx, gray_size);
        if (!scratch)
            return PH_ERR_ALLOCATION_FAILED;
        ph_to_grayscale(ctx, ctx->data, ctx->width, ctx->height, ctx->channels, scratch);
        full_gray = scratch;
    }

    ph_resize_box(full_gray, ctx->width, ctx->height, hash_input, PH_CORE_HASH_SIZE,
                  PH_CORE_HASH_SIZE);

    float d[PH_CORE_HASH_SIZE * PH_CORE_HASH_SIZE];
    for (int i = 0; i < total_pixels; i++)
        d[i] = (float)hash_input[i];

    /* Horizontal passes */
    for (int i = 0; i < PH_CORE_HASH_SIZE; i++)
        haar_1d_float(&d[i * PH_CORE_HASH_SIZE], PH_CORE_HASH_SIZE);

    /* Vertical passes */
    for (int j = 0; j < PH_CORE_HASH_SIZE; j++) {
        float col[PH_CORE_HASH_SIZE];
        for (int i = 0; i < PH_CORE_HASH_SIZE; i++)
            col[i] = d[i * PH_CORE_HASH_SIZE + j];
        haar_1d_float(col, PH_CORE_HASH_SIZE);
        for (int i = 0; i < PH_CORE_HASH_SIZE; i++)
            d[i * PH_CORE_HASH_SIZE + j] = col[i];
    }

    /* Copy to sortable array for median */
    float sorted[PH_CORE_HASH_SIZE * PH_CORE_HASH_SIZE];
    for (int i = 0; i < total_pixels; i++) {
        sorted[i] = d[i];
    }

    /* Simple insertion sort for 64 elements */
    for (int i = 1; i < total_pixels; i++) {
        float key = sorted[i];
        int j = i - 1;
        while (j >= 0 && sorted[j] > key) {
            sorted[j + 1] = sorted[j];
            j = j - 1;
        }
        sorted[j + 1] = key;
    }
    float median = sorted[total_pixels / 2];

    uint64_t hash = 0;
    for (int i = 0; i < total_pixels; i++)
        if (d[i] > median)
            hash |= (1ULL << i);
    *out_hash = hash;
    return PH_SUCCESS;
}

static void haar_1d_float_dyn(float *data, int n, float *temp) {
    int h = n / 2;
    float inv_haar = (float)(1.0 / PH_HAAR_SCALE);
    for (int i = 0; i < h; i++) {
        temp[i] = (data[2 * i] + data[2 * i + 1]) * inv_haar;
        temp[i + h] = (data[2 * i] - data[2 * i + 1]) * inv_haar;
    }
    for (int i = 0; i < n; i++)
        data[i] = temp[i];
}

static void haar_2d_level(float *data, int size, int stride, float *temp_row, float *temp_col) {
    /* Horizontal passes */
    for (int i = 0; i < size; i++) {
        for (int j = 0; j < size; j++) {
            temp_row[j] = data[i * stride + j];
        }
        haar_1d_float_dyn(temp_row, size, temp_col);
        for (int j = 0; j < size; j++) {
            data[i * stride + j] = temp_row[j];
        }
    }

    /* Vertical passes */
    for (int j = 0; j < size; j++) {
        for (int i = 0; i < size; i++) {
            temp_col[i] = data[i * stride + j];
        }
        haar_1d_float_dyn(temp_col, size, temp_row);
        for (int i = 0; i < size; i++) {
            data[i * stride + j] = temp_col[i];
        }
    }
}

static ph_error_t ph_compute_whash_full(ph_context_t *ctx, uint64_t *out_hash) {
    int min_dim = ctx->width < ctx->height ? ctx->width : ctx->height;
    int log2_min = 0;
    while ((1 << log2_min) <= min_dim)
        log2_min++;
    log2_min--;

    int nat_scale = 1 << log2_min;
    int image_scale = nat_scale > PH_CORE_HASH_SIZE ? nat_scale : PH_CORE_HASH_SIZE;

    const uint8_t *full_gray;
    uint8_t *scratch = NULL;
    if (ctx->channels == 1) {
        full_gray = ctx->data;
    } else {
        scratch = ph_get_scratchpad(ctx, (size_t)ctx->width * ctx->height);
        if (!scratch)
            return PH_ERR_ALLOCATION_FAILED;
        ph_to_grayscale(ctx, ctx->data, ctx->width, ctx->height, ctx->channels, scratch);
        full_gray = scratch;
    }

    uint8_t *scaled_img = (uint8_t *)malloc(image_scale * image_scale * sizeof(uint8_t));
    if (!scaled_img)
        return PH_ERR_ALLOCATION_FAILED;

    ph_resize_bilinear(full_gray, ctx->width, ctx->height, scaled_img, image_scale, image_scale);

    float *d = (float *)malloc(image_scale * image_scale * sizeof(float));
    float *temp_a = (float *)malloc(image_scale * sizeof(float));
    float *temp_b = (float *)malloc(image_scale * sizeof(float));
    if (!d || !temp_a || !temp_b) {
        free(scaled_img);
        if (d)
            free(d);
        if (temp_a)
            free(temp_a);
        if (temp_b)
            free(temp_b);
        return PH_ERR_ALLOCATION_FAILED;
    }

    for (int i = 0; i < image_scale; i++) {
        for (int j = 0; j < image_scale; j++) {
            d[i * image_scale + j] = (float)scaled_img[i * image_scale + j] / 255.0f;
        }
    }
    free(scaled_img);

    int current_size = image_scale;
    // DWT cascade down to 8x8. We just call it on the top-left quadrant over and over.
    while (current_size > PH_CORE_HASH_SIZE) {
        haar_2d_level(d, current_size, image_scale, temp_a, temp_b);
        current_size /= 2;
    }

    // Extract the 8x8 LL block
    float ll_band[PH_CORE_HASH_SIZE * PH_CORE_HASH_SIZE];
    for (int i = 0; i < PH_CORE_HASH_SIZE; i++) {
        for (int j = 0; j < PH_CORE_HASH_SIZE; j++) {
            ll_band[i * PH_CORE_HASH_SIZE + j] = d[i * image_scale + j];
        }
    }
    free(d);
    free(temp_a);
    free(temp_b);

    int total_pixels = PH_CORE_HASH_SIZE * PH_CORE_HASH_SIZE;

    // Sort and calculate median
    float sorted[PH_CORE_HASH_SIZE * PH_CORE_HASH_SIZE];
    for (int i = 0; i < total_pixels; i++) {
        sorted[i] = ll_band[i];
    }
    for (int i = 1; i < total_pixels; i++) {
        float key = sorted[i];
        int j = i - 1;
        while (j >= 0 && sorted[j] > key) {
            sorted[j + 1] = sorted[j];
            j--;
        }
        sorted[j + 1] = key;
    }
    float median = sorted[total_pixels / 2];

    uint64_t hash = 0;
    for (int i = 0; i < total_pixels; i++) {
        if (ll_band[i] > median) {
            hash |= (1ULL << i);
        }
    }
    *out_hash = hash;
    return PH_SUCCESS;
}

PH_API ph_error_t ph_compute_whash(ph_context_t *ctx, uint64_t *out_hash) {
    if (!ctx || !ctx->is_loaded || !out_hash)
        return PH_ERR_INVALID_ARGUMENT;

    if (ctx->whash_mode == PH_WHASH_FULL) {
        return ph_compute_whash_full(ctx, out_hash);
    } else {
        return ph_compute_whash_fast(ctx, out_hash);
    }
}
