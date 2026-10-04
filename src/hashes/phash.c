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
 * DC is the image mean and runs 10-100x larger than any AC term. ImageHash leaves it in
 * the median, so it cannot serve as a reference for this detail; a median is not dragged
 * by one outlier, and the two thresholds differ only on an exact tie between the two
 * central coefficients.
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
 *
 * Bit order: ph_median_bitpack_from() (shared with wHash) packs LSB first --
 * `1ULL << i` for coefficient i in row-major DCT block order. Not specified by either
 * source (neither Zauner nor Krawetz's post says how to lay out the 64 bits); this is
 * simply what this library's implementation does, unverified against pHash's own code.
 */
#include "context.h"
#include "hashes/hashes.h"
#include "image/image.h"
#include "safety.h"

#include <math.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdlib.h>

static void compute_dct_coefficients(float *matrix, int n) {
    float c = (float)sqrt(1.0 / n);
    for (int j = 0; j < n; j++) {
        matrix[j] = c;
    }

    c = (float)sqrt(2.0 / n);
    for (int i = 1; i < n; i++) {
        for (int j = 0; j < n; j++) {
            matrix[i * n + j] = (float)((double)c * cos(M_PI * i * (j + 0.5) / n));
        }
    }
}

// --- Cached DCT Matrix (N=32) ---
static float s_dct_matrix_32[32 * 32];
static atomic_flag s_dct_32_lock = ATOMIC_FLAG_INIT;
static atomic_bool s_dct_32_init = false;

static void ph_init_dct_matrix(void) {
    if (atomic_load(&s_dct_32_init)) {
        return;
    }

    // Simple spinlock. Waiting, and finding the matrix built once the lock is ours,
    // happen only when two threads race for the first pHash; whether a run takes them is
    // the scheduler's choice, so their branches are not counted (the TSan tests exercise
    // the race).
    while (atomic_flag_test_and_set(&s_dct_32_lock)) { // LCOV_EXCL_BR_LINE
    }

    if (!atomic_load(&s_dct_32_init)) { // LCOV_EXCL_BR_LINE
        compute_dct_coefficients(s_dct_matrix_32, 32);
        atomic_store(&s_dct_32_init, true);
    }

    atomic_flag_clear(&s_dct_32_lock);
}

const float *ph_get_dct_matrix_32(void) {
    ph_init_dct_matrix();
    return s_dct_matrix_32;
}

PH_API ph_error_t ph_compute_phash(ph_context_t *ctx, uint64_t *out_hash) {
    if (!ctx || !out_hash) {
        return PH_ERR_INVALID_ARGUMENT;
    }
    if (!ctx->image.is_loaded) {
        return PH_ERR_EMPTY_IMAGE;
    }

    int dct_size = ctx->config.phash_dct_size;
    int reduction_size = ctx->config.phash_reduction_size;

    /* Defensive bounds check. ph_context_set_phash_params() already rejects
     * out-of-range values; this catches any other way they could get here.
     * Out-of-range parameters are an error, never silently clamped: the hash
     * must fit into 64 bits (reduction_size^2 <= 64) and ph_dct2_partial()
     * has a fixed 32*8 scratch buffer. */
    if (dct_size <= 0 || dct_size > PH_DCT_MAX_SIZE || reduction_size <= 0 ||
        reduction_size > PH_DCT_MAX_REDUCTION_SIZE || reduction_size > dct_size) {
        return PH_ERR_INVALID_ARGUMENT;
    }

    /* Allocate all needed buffers.
     * Optimization: If dct_size=32, we use static cached matrix.
     */
    bool use_cache = (dct_size == 32);

    /* One block, three buffers. The byte buffer comes first and its size is rounded up so
     * that the float buffers behind it stay aligned: dct_size^2 is odd for an odd
     * dct_size. */
    const size_t dct_n = ph_size(dct_size), red_n = ph_size(reduction_size);
    size_t sz1 = ph_arena_align_up(dct_n * dct_n);              // dct_input
    size_t sz2 = use_cache ? 0 : dct_n * dct_n * sizeof(float); // dct_mat
    size_t sz3 = red_n * red_n * sizeof(float);                 // dct_out

    ph_arena_mark_t arena_mark = ph_arena_mark(ctx);
    uint8_t *scratch = ph_get_scratchpad(ctx, sz1 + sz2 + sz3);
    if (!scratch) {
        return PH_ERR_ALLOCATION_FAILED;
    }

    uint8_t *dct_input = scratch;
    float *dct_mat;
    float *dct_out;

    if (use_cache) {
        ph_init_dct_matrix();
        dct_mat = s_dct_matrix_32;
        dct_out = ph_arena_at(scratch, sz1);
    } else {
        dct_mat = ph_arena_at(scratch, sz1);
        dct_out = ph_arena_at(scratch, sz1 + sz2);
        compute_dct_coefficients(dct_mat, dct_size);
    }

    if (!ph_area_downscale(ctx, dct_size, dct_size, dct_input)) {
        ph_arena_release(ctx, arena_mark);
        return PH_ERR_ALLOCATION_FAILED;
    }

    ph_error_t err = ph_dct2_partial(dct_mat, dct_input, dct_size, reduction_size, dct_out);
    if (err != PH_SUCCESS) {
        ph_arena_release(ctx, arena_mark);
        return err;
    }

    /* median_from = 1: the DC coefficient is thresholded like the others but takes no
     * part in choosing the threshold. See the note at the top of this file. */
    *out_hash = ph_median_bitpack_margin(dct_out, reduction_size * reduction_size, 1,
                                         PH_PHASH_MEDIAN_MARGIN);

    ph_arena_release(ctx, arena_mark);
    return PH_SUCCESS;
}

/* Plain sequential sums in a fixed order, and no vector path: the DCT is the step whose
 * last-bit rounding pHash can turn into flipped bits, so it is computed the same way on
 * every architecture, and with -ffp-contract=off every build gives the same coefficients
 * bit for bit. It runs on a 32x32 input, where a SIMD version would save nothing that
 * shows. */
ph_error_t ph_dct2_partial(const float *dct_mat, const uint8_t *input, int dct_size,
                           int reduction_size, float *out) {
    // Temporary matrix for first pass: dct_size rows, reduction_size columns
    float temp[PH_DCT_MAX_SIZE * PH_DCT_MAX_REDUCTION_SIZE];

    if (!dct_mat || !input || !out) {
        return PH_ERR_INVALID_ARGUMENT;
    }
    /* Hard bounds: `temp` is a fixed-size stack buffer and the caller expects
     * every element of `out` to be written. Never return without writing it. */
    if (dct_size <= 0 || dct_size > PH_DCT_MAX_SIZE || reduction_size <= 0 ||
        reduction_size > PH_DCT_MAX_REDUCTION_SIZE || reduction_size > dct_size) {
        return PH_ERR_INVALID_ARGUMENT;
    }

    /* First pass: DCT of each row, but only compute first reduction_size columns */
    for (int i = 0; i < dct_size; i++) {
        for (int j = 0; j < reduction_size; j++) {
            float sum = 0;
            const float *coeffs = &dct_mat[j * dct_size];
            const uint8_t *in = &input[i * dct_size];

            for (int k = 0; k < dct_size; k++) {
                sum += coeffs[k] * in[k];
            }
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
