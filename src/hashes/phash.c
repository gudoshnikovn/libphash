/* pHash -- DCT-based perceptual hash.
 *
 * Christoph Zauner, "Implementation and Benchmarking of Perceptual Image Hash
 * Functions", Diplomarbeit, FH Hagenberg, July 2010, sections 3.1.1 and 3.2.1.
 * https://www.phash.org/docs/pubs/thesis_zauner.pdf
 * The coefficient selection originates in B. Coskun and B. Sankur, "Robust video hash
 * extraction", IEEE SIU 2004, pp. 292-295 (Zauner's reference [9]). Neal Krawetz
 * describes the same construction independently in "Looks Like It" (2011).
 *
 * The DCT matrix below is Zauner's definition 3.3, and the two-dimensional transform is
 * his equation 3.4 computed as two passes of matrix multiplication.
 *
 * The DC coefficient does not decide anything. The block taken is DCT(0,0)..DCT(7,7),
 * but the median that thresholds it is taken over the 63 AC coefficients only, which is
 * what pHash's ph_dct_imagehash() does: it crops the 8x8 block at (0,0), then takes the
 * median of that block's elements 1 through 63 and thresholds all 64 against it.
 *
 * DC is the image mean and runs 10-100x larger than any AC term, so leaving it in the
 * median drags the threshold that decides the other 63 bits -- which is what this code
 * used to do, and what ImageHash still does, which is why comparing against ImageHash
 * could never have caught it.
 *
 * The written descriptions of pHash go further than its code and disagree with each
 * other about how far. Zauner 3.2.1 reads the block as starting at DCT(1,1) -- "64
 * low-frequency DCT coefficients, omitting the lowest frequency coefficients" -- and
 * Starkweather of pHash, quoted by Krawetz, says the hash is "based on the low 2D DCT
 * coefficients starting at the second from lowest, leaving out the first DC term". The
 * reference implementation does neither literally: DC keeps its bit, and since it is
 * always above a median taken without it, that bit is always 1 and carries nothing. The
 * code is followed here rather than the prose, and the cost -- one dead bit, an
 * effective width of 63 -- is measured in docs/algorithm-provenance.md section 3.
 *
 * Zauner's equation 3.10 thresholds with >=; pHash's code, and this code, use >. With
 * float coefficients that differs only on degenerate input such as a solid colour.
 */
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

    /* Defensive bounds check. ph_context_set_phash_params() already rejects
     * out-of-range values; this catches any other way they could get here.
     * Out-of-range parameters are an error, never silently clamped: the hash
     * must fit into 64 bits (reduction_size^2 <= 64) and ph_dct2_partial()
     * has a fixed 32*8 scratch buffer. */
    if (dct_size <= 0 || dct_size > PH_DCT_MAX_SIZE || reduction_size <= 0 ||
        reduction_size > PH_DCT_MAX_REDUCTION_SIZE || reduction_size > dct_size)
        return PH_ERR_INVALID_ARGUMENT;

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

    ph_error_t err = ph_dct2_partial(dct_mat, dct_input, dct_size, reduction_size, dct_out);
    if (err != PH_SUCCESS) {
        ctx->arena.offset = saved_offset;
        return err;
    }

    /* median_from = 1: the DC coefficient is thresholded like the others but takes no
     * part in choosing the threshold. See the note at the top of this file. */
    *out_hash = ph_median_bitpack_from(dct_out, reduction_size * reduction_size, 1);

    ctx->arena.offset = saved_offset;
    return PH_SUCCESS;
}

ph_error_t ph_dct2_partial(const float *dct_mat, const uint8_t *input, int dct_size,
                           int reduction_size, float *out) {
    // Temporary matrix for first pass: dct_size rows, reduction_size columns
    float temp[PH_DCT_MAX_SIZE * PH_DCT_MAX_REDUCTION_SIZE];

    if (!dct_mat || !input || !out)
        return PH_ERR_INVALID_ARGUMENT;
    /* Hard bounds: `temp` is a fixed-size stack buffer and the caller expects
     * every element of `out` to be written. Never return without writing it. */
    if (dct_size <= 0 || dct_size > PH_DCT_MAX_SIZE || reduction_size <= 0 ||
        reduction_size > PH_DCT_MAX_REDUCTION_SIZE || reduction_size > dct_size)
        return PH_ERR_INVALID_ARGUMENT;

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

    return PH_SUCCESS;
}

uint64_t ph_median_bitpack(const float *values, int n) {
    return ph_median_bitpack_from(values, n, 0);
}

uint64_t ph_median_bitpack_from(const float *values, int n, int median_from) {
    if (n <= 0 || n > 64 || median_from < 0 || median_from >= n)
        return 0;

    /* Every value gets a bit; only values[median_from..n-1] get a say in the median. */
    int m = n - median_from;
    float sorted[64];
    for (int i = 0; i < m; i++) {
        sorted[i] = values[median_from + i];
    }

    // Sort to find median (insertion sort)
    for (int i = 1; i < m; i++) {
        float key = sorted[i];
        int j = i - 1;
        while (j >= 0 && sorted[j] > key) {
            sorted[j + 1] = sorted[j];
            j--;
        }
        sorted[j + 1] = key;
    }

    float median;
    if (m % 2 == 0) {
        median = (sorted[m / 2 - 1] + sorted[m / 2]) * 0.5f;
    } else {
        median = sorted[m / 2];
    }

    uint64_t hash = 0;
    for (int i = 0; i < n; i++) {
        if (values[i] > median) {
            hash |= (1ULL << i);
        }
    }

    return hash;
}
