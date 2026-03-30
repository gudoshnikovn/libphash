#include "internal.h"
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

// --- Cached DCT Matrix (N=32) ---
static float s_dct_matrix_32[32 * 32];
static atomic_flag s_dct_32_lock = ATOMIC_FLAG_INIT;
static atomic_bool s_dct_32_init = false;

void init_dct_matrix(void) {
    if (atomic_load(&s_dct_32_init))
        return;

    // Simple spinlock
    while (atomic_flag_test_and_set(&s_dct_32_lock)) {
    }

    if (!atomic_load(&s_dct_32_init)) {
        compute_dct_coefficients(s_dct_matrix_32, 32);
        atomic_store(&s_dct_32_init, true);
    }

    atomic_flag_clear(&s_dct_32_lock);
}

const float *ph_get_dct_matrix_32(void) {
    init_dct_matrix();
    return s_dct_matrix_32;
}

// --- NEON Helpers ---
#if defined(__ARM_NEON)
#include <arm_neon.h>

// Dot product of float[N] and uint8[N]
static float dot_product_f32_u8_neon(const float *f, const uint8_t *u, int n) {
    float32x4_t sum_vec = vdupq_n_f32(0.0f);
    int i = 0;
    for (; i <= n - 16; i += 16) {
        uint8x16_t u_val = vld1q_u8(&u[i]);

        // Convert u8 -> u16 -> u32 -> f32
        uint16x8_t u_low = vmovl_u8(vget_low_u8(u_val));
        uint16x8_t u_high = vmovl_u8(vget_high_u8(u_val));

        uint32x4_t u_ll = vmovl_u16(vget_low_u16(u_low));
        uint32x4_t u_lh = vmovl_u16(vget_high_u16(u_low));
        uint32x4_t u_hl = vmovl_u16(vget_low_u16(u_high));
        uint32x4_t u_hh = vmovl_u16(vget_high_u16(u_high));

        float32x4_t f_ll = vcvtq_f32_u32(u_ll);
        float32x4_t f_lh = vcvtq_f32_u32(u_lh);
        float32x4_t f_hl = vcvtq_f32_u32(u_hl);
        float32x4_t f_hh = vcvtq_f32_u32(u_hh);

        // Load floats
        float32x4_t f_val1 = vld1q_f32(&f[i]);
        float32x4_t f_val2 = vld1q_f32(&f[i + 4]);
        float32x4_t f_val3 = vld1q_f32(&f[i + 8]);
        float32x4_t f_val4 = vld1q_f32(&f[i + 12]);

        // Accumulate
        sum_vec = vmlaq_f32(sum_vec, f_val1, f_ll);
        sum_vec = vmlaq_f32(sum_vec, f_val2, f_lh);
        sum_vec = vmlaq_f32(sum_vec, f_val3, f_hl);
        sum_vec = vmlaq_f32(sum_vec, f_val4, f_hh);
    }

    // Horizontal reduce
    float sum = vaddvq_f32(sum_vec);

    // Fallback
    for (; i < n; i++) {
        sum += f[i] * u[i];
    }
    return sum;
}
#endif

PH_API ph_error_t ph_compute_phash(ph_context_t *ctx, uint64_t *out_hash) {
    if (!ctx || !ctx->image.is_loaded || !out_hash)
        return PH_ERR_INVALID_ARGUMENT;

    int dct_size = ctx->config.phash_dct_size;
    int reduction_size = ctx->config.phash_reduction_size;

    /* Ensure we can fit in 64-bit hash (max 8x8) */
    if (reduction_size > 8)
        reduction_size = 8;

    uint8_t *gray_full = ph_get_gray(ctx);
    if (!gray_full)
        return PH_ERR_ALLOCATION_FAILED;

    /* Allocate all needed buffers.
     * Optimization: If dct_size=32, we use static cached matrix.
     */
    bool use_cache = (dct_size == 32);

    size_t sz1 = (size_t)dct_size * dct_size;                             // dct_input
    size_t sz2 = use_cache ? 0 : (sz1 * sizeof(float));                   // dct_mat (if not cached)
    size_t sz3 = (size_t)reduction_size * reduction_size * sizeof(float); // dct_out

    size_t saved_offset = ctx->arena.offset;
    uint8_t *scratch = ph_get_scratchpad(ctx, sz1 + sz2 + sz3);
    if (!scratch)
        return PH_ERR_ALLOCATION_FAILED;

    uint8_t *dct_input = scratch;
    float *dct_mat;
    float *dct_out;

    if (use_cache) {
        init_dct_matrix();
        dct_mat = s_dct_matrix_32;
        dct_out = (float *)(scratch + sz1);
    } else {
        dct_mat = (float *)(scratch + sz1);
        dct_out = (float *)((uint8_t *)dct_mat + sz2);
        compute_dct_coefficients(dct_mat, dct_size);
    }

    ph_resize_box(gray_full, ctx->image.width, ctx->image.height, dct_input, dct_size, dct_size);

    ph_dct2_partial(dct_mat, dct_input, dct_size, reduction_size, dct_out);

    *out_hash = ph_median_bitpack(dct_out, reduction_size * reduction_size);

    ctx->arena.offset = saved_offset;
    return PH_SUCCESS;
}

void ph_dct2_partial(const float *dct_mat, const uint8_t *input, int dct_size, int reduction_size,
                     float *out) {
    // Temporary matrix for first pass: dct_size rows, reduction_size columns
    float temp[32 * 8]; // max dct_size=32, reduction_size=8
    if (dct_size > 32 || reduction_size > 8) {
        // Fallback for unexpected sizes (though in phash they are capped)
        return;
    }

    /* First pass: DCT of each row, but only compute first reduction_size columns */
    for (int i = 0; i < dct_size; i++) {
        for (int j = 0; j < reduction_size; j++) {
            float sum = 0;
            const float *coeffs = &dct_mat[j * dct_size];
            const uint8_t *in = &input[i * dct_size];

#if defined(__ARM_NEON)
            if (dct_size == 32) {
                sum = dot_product_f32_u8_neon(coeffs, in, 32);
            } else {
                for (int k = 0; k < dct_size; k++)
                    sum += coeffs[k] * in[k];
            }
#else
            for (int k = 0; k < dct_size; k++)
                sum += coeffs[k] * in[k];
#endif
            temp[i * reduction_size + j] = sum;
        }
    }

    /* Second pass: DCT of first reduction_size columns, but only first reduction_size rows */
    for (int j = 0; j < reduction_size; j++) {
        for (int i = 0; i < reduction_size; i++) {
            float sum = 0;
            for (int k = 0; k < dct_size; k++) {
                // temp col = j, variable k (temp is row-major: k * reduction_size + j)
                sum += dct_mat[i * dct_size + k] * temp[k * reduction_size + j];
            }
            out[i * reduction_size + j] = sum;
        }
    }
}

uint64_t ph_median_bitpack(const float *values, int n) {
    if (n <= 0 || n > 64)
        return 0;

    float sorted[64];
    for (int i = 0; i < n; i++) {
        sorted[i] = values[i];
    }

    // Sort to find median (insertion sort)
    for (int i = 1; i < n; i++) {
        float key = sorted[i];
        int j = i - 1;
        while (j >= 0 && sorted[j] > key) {
            sorted[j + 1] = sorted[j];
            j--;
        }
        sorted[j + 1] = key;
    }

    float median;
    if (n % 2 == 0) {
        median = (sorted[n / 2 - 1] + sorted[n / 2]) * 0.5f;
    } else {
        median = sorted[n / 2];
    }

    uint64_t hash = 0;
    for (int i = 0; i < n; i++) {
        if (values[i] > median) {
            hash |= (1ULL << i);
        }
    }

    return hash;
}
