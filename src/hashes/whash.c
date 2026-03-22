#include "../internal.h"
#include <stdlib.h>

static void haar_1d_float_dyn(float *data, int n, float *temp);

static ph_error_t ph_compute_whash_fast(ph_context_t *ctx, uint64_t *out_hash) {
    int hash_size = PH_CORE_HASH_SIZE;
    int image_scale = hash_size * 2; // 16
    int total_pixels = hash_size * hash_size; // 64
    uint8_t hash_input[256]; // image_scale * image_scale

    uint8_t *full_gray = ph_get_gray(ctx);
    if (!full_gray)
        return PH_ERR_ALLOCATION_FAILED;

    ph_resize_box(full_gray, ctx->image.width, ctx->image.height, hash_input, image_scale, image_scale);

    float d[256];
    for (int i = 0; i < 256; i++)
        d[i] = (float)hash_input[i] / 255.0f;

    float temp_haar[16];
    /* Horizontal passes */
    for (int i = 0; i < image_scale; i++)
        haar_1d_float_dyn(&d[i * image_scale], image_scale, temp_haar);

    /* Vertical passes */
    for (int j = 0; j < image_scale; j++) {
        float col[16];
        for (int i = 0; i < image_scale; i++)
            col[i] = d[i * image_scale + j];
        haar_1d_float_dyn(col, image_scale, temp_haar);
        for (int i = 0; i < image_scale; i++)
            d[i * image_scale + j] = col[i];
    }

    /* Extract top-left 8x8 (LL band) */
    float ll_band[64];
    for (int i = 0; i < hash_size; i++) {
        for (int j = 0; j < hash_size; j++) {
            ll_band[i * hash_size + j] = d[i * image_scale + j];
        }
    }

    /* Copy to sortable array for median */
    float sorted[64];
    for (int i = 0; i < total_pixels; i++) {
        sorted[i] = ll_band[i];
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
        if (ll_band[i] > median)
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
    int min_dim = ctx->image.width < ctx->image.height ? ctx->image.width : ctx->image.height;
    int log2_min = 0;
    while ((1 << log2_min) <= min_dim)
        log2_min++;
    log2_min--;

    int nat_scale = 1 << log2_min;
    int image_scale = nat_scale > PH_CORE_HASH_SIZE ? nat_scale : PH_CORE_HASH_SIZE;

    uint8_t *full_gray = ph_get_gray(ctx);
    if (!full_gray)
        return PH_ERR_ALLOCATION_FAILED;

    size_t sz_scaled = (size_t)image_scale * image_scale;
    size_t sz_d = (size_t)image_scale * image_scale * sizeof(float);
    size_t sz_temps = (size_t)image_scale * 2 * sizeof(float);

    size_t saved_offset = ctx->arena.offset;
    uint8_t *scratch_mem = ph_get_scratchpad(ctx, sz_scaled + sz_d + sz_temps);
    if (!scratch_mem)
        return PH_ERR_ALLOCATION_FAILED;

    uint8_t *scaled_img = scratch_mem;
    float *d = (float *)(scratch_mem + sz_scaled);
    float *temp_a = (float *)((uint8_t *)d + sz_d);
    float *temp_b = temp_a + image_scale;

    ph_resize_bilinear(full_gray, ctx->image.width, ctx->image.height, scaled_img, image_scale, image_scale);

    for (int i = 0; i < image_scale; i++) {
        for (int j = 0; j < image_scale; j++) {
            d[i * image_scale + j] = (float)scaled_img[i * image_scale + j] / 255.0f;
        }
    }

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
    ctx->arena.offset = saved_offset;
    return PH_SUCCESS;
}

PH_API ph_error_t ph_compute_whash(ph_context_t *ctx, uint64_t *out_hash) {
    if (!ctx || !ctx->image.is_loaded || !out_hash)
        return PH_ERR_INVALID_ARGUMENT;

    if (ctx->config.whash_mode == PH_WHASH_FULL) {
        return ph_compute_whash_full(ctx, out_hash);
    } else {
        return ph_compute_whash_fast(ctx, out_hash);
    }
}
