/* wHash -- Haar wavelet hash.
 *
 * NO PRIMARY SOURCE. This algorithm has no paper. It is Johannes Buchner's `whash` from
 * the ImageHash library (https://github.com/JohannesBuchner/imagehash), whose only
 * reference is a blog post by Alexander Petrov. That implementation is the closest
 * thing to a specification there is, and it is a third-party implementation, not one.
 *
 * It is NOT the algorithm of Venkatesan, Koon, Jakubowski and Moulin, "Robust Image
 * Hashing", ICIP 2000 (doi:10.1109/ICIP.2000.899541), which is often named as the
 * origin of wavelet hashing: that one builds a hash from statistics of randomly tiled
 * subbands under a secret key, followed by error-correction decoding. Related work,
 * not an attribution.
 *
 * Consequently wHash is judged by measurable properties -- robustness, discrimination,
 * separability -- rather than by conformance, and under this library's threat model that
 * is the right instrument rather than a fallback: no paper describes an unkeyed
 * deterministic wavelet hash because the security literature has nothing to prove about
 * one. Measured separability on the synthetic corpus is third best of the nine (OBSERVED
 * in tests/src/test_hash_properties.c). Kept deliberately for that reason; see the
 * premise section of docs/algorithm-provenance.md.
 *
 * Only the final LL band reaches the hash. After k levels an LL value is 2^k times the
 * mean of its 2^k x 2^k block, so the band is the image averaged over an 8x8 grid, and
 * both modes come close to aHash's grid thresholded at its median; docs/theory/whash.md
 * has the measurement.
 *
 * One documented difference from the reference implementation: ImageHash zeroes the
 * coarsest LL band by default (remove_max_haar_ll=True) so the hash describes local
 * structure rather than overall brightness. This code implements the operation but leaves
 * it off, because under a median threshold it can change nothing but ties: zeroing that
 * single coefficient and reconstructing subtracts the image mean from every sample, a
 * constant subtraction shifts the working LL band and its median alike, and only a value
 * equal to the median can land on the other side of it, by rounding error. See
 * ph_context_set_whash_remove_max_haar_ll().
 *
 * The transform itself is the orthonormal Haar wavelet: sums and differences of
 * adjacent samples, both divided by sqrt(2).
 *
 * Bit order: ph_median_bitpack_from()/ph_median_bitpack() (shared with pHash) packs
 * LSB first -- `1ULL << i` for coefficient i in row-major order of the low band. Not
 * verified against ImageHash's own layout, and not specifiable against a primary
 * source that does not exist; a choice, not a conformance claim.
 */
#include "context.h"
#include "hashes/hashes.h"
#include "image/image.h"
#include "safety.h"

#include <stdlib.h>
#include <string.h>

void ph_haar_1d_float(float *data, int n, float *temp) {
    int h = n / 2;
    float inv_haar = (float)(1.0 / PH_HAAR_SCALE);
    for (int i = 0; i < h; i++) {
        temp[i] = (data[2 * i] + data[2 * i + 1]) * inv_haar;
        temp[i + h] = (data[2 * i] - data[2 * i + 1]) * inv_haar;
    }
    for (int i = 0; i < n; i++) {
        data[i] = temp[i];
    }
}

void ph_haar_2d_level(float *data, int size, int stride, float *temp_row, float *temp_col) {
    /* Horizontal passes */
    for (int i = 0; i < size; i++) {
        for (int j = 0; j < size; j++) {
            temp_row[j] = data[i * stride + j];
        }
        ph_haar_1d_float(temp_row, size, temp_col);
        for (int j = 0; j < size; j++) {
            data[i * stride + j] = temp_row[j];
        }
    }

    /* Vertical passes */
    for (int j = 0; j < size; j++) {
        for (int i = 0; i < size; i++) {
            temp_col[i] = data[i * stride + j];
        }
        ph_haar_1d_float(temp_col, size, temp_row);
        for (int i = 0; i < size; i++) {
            data[i * stride + j] = temp_col[i];
        }
    }
}

void ph_haar_1d_inverse_float(float *data, int n, float *temp) {
    int h = n / 2;
    float inv_haar = (float)(1.0 / PH_HAAR_SCALE);
    for (int i = 0; i < h; i++) {
        temp[2 * i] = (data[i] + data[i + h]) * inv_haar;
        temp[2 * i + 1] = (data[i] - data[i + h]) * inv_haar;
    }
    for (int i = 0; i < n; i++) {
        data[i] = temp[i];
    }
}

void ph_haar_2d_level_inverse(float *data, int size, int stride, float *temp_row, float *temp_col) {
    /* Mirror of ph_haar_2d_level: it went horizontal then vertical, so undo vertical first. */
    for (int j = 0; j < size; j++) {
        for (int i = 0; i < size; i++) {
            temp_col[i] = data[i * stride + j];
        }
        ph_haar_1d_inverse_float(temp_col, size, temp_row);
        for (int i = 0; i < size; i++) {
            data[i * stride + j] = temp_col[i];
        }
    }

    for (int i = 0; i < size; i++) {
        for (int j = 0; j < size; j++) {
            temp_row[j] = data[i * stride + j];
        }
        ph_haar_1d_inverse_float(temp_row, size, temp_col);
        for (int j = 0; j < size; j++) {
            data[i * stride + j] = temp_row[j];
        }
    }
}

/* One level of ph_haar_2d_level() reduced to the part the cascade reads: the low band of
 * both passes, written to the top-left size/2 x size/2 corner. Each output is the same two
 * float operations in the same order -- the horizontal low pass of rows 2i and 2i+1, then
 * the vertical low pass of those two results -- so the LL band is bit for bit the one the
 * full level leaves there. Skipping the three detail bands drops three quarters of the
 * stores, and walking rows instead of columns keeps the reads sequential; a column pass
 * over a 2048-wide float image strides 8 KiB per sample. temp holds size/2 floats. */
static void ph_haar_2d_level_ll(float *data, int size, int stride, float *temp) {
    const int h = size / 2;
    const float inv_haar = (float)(1.0 / PH_HAAR_SCALE);
    for (int i = 0; i < h; i++) {
        const float *a = data + ph_size(2 * i) * ph_size(stride);
        const float *b = a + stride;
        for (int j = 0; j < h; j++) {
            float ra = (a[2 * j] + a[2 * j + 1]) * inv_haar;
            float rb = (b[2 * j] + b[2 * j + 1]) * inv_haar;
            temp[j] = (ra + rb) * inv_haar;
        }
        /* Row i's only reader is output row i/2: this one (read above) or one done. */
        memcpy(data + ph_size(i) * ph_size(stride), temp, ph_size(h) * sizeof(float));
    }
}

/* ImageHash's remove_max_haar_ll: decompose all the way down to a 1x1 LL, zero that single
 * coefficient, and reconstruct. See the note at the top of this file for what this is worth. */
static void ph_whash_remove_max_haar_ll(float *d, int size, int stride, float *temp_a,
                                        float *temp_b) {
    int current_size = size;
    while (current_size > 1) {
        ph_haar_2d_level(d, current_size, stride, temp_a, temp_b);
        current_size /= 2;
    }

    d[0] = 0.0f;

    while (current_size < size) {
        current_size *= 2;
        ph_haar_2d_level_inverse(d, current_size, stride, temp_a, temp_b);
    }
}

static ph_error_t ph_compute_whash_fast(ph_context_t *ctx, uint64_t *out_hash) {
    int hash_size = PH_CORE_HASH_SIZE;
    int image_scale = hash_size * 2; // 16
    uint8_t hash_input[256];         // image_scale * image_scale

    if (!ph_area_downscale(ctx, image_scale, image_scale, hash_input)) {
        return PH_ERR_ALLOCATION_FAILED;
    }

    float d[256];
    for (int i = 0; i < 256; i++) {
        d[i] = hash_input[i] / 255.0f;
    }

    float temp_haar[16];
    float temp_haar_b[16];
    if (ctx->config.whash_remove_max_haar_ll) {
        ph_whash_remove_max_haar_ll(d, image_scale, image_scale, temp_haar, temp_haar_b);
    }

    /* Horizontal passes */
    for (int i = 0; i < image_scale; i++) {
        ph_haar_1d_float(&d[i * image_scale], image_scale, temp_haar);
    }

    /* Vertical passes */
    for (int j = 0; j < image_scale; j++) {
        float col[16];
        for (int i = 0; i < image_scale; i++) {
            col[i] = d[i * image_scale + j];
        }
        ph_haar_1d_float(col, image_scale, temp_haar);
        for (int i = 0; i < image_scale; i++) {
            d[i * image_scale + j] = col[i];
        }
    }

    /* Extract top-left 8x8 (LL band) */
    float ll_band[64];
    for (int i = 0; i < hash_size; i++) {
        for (int j = 0; j < hash_size; j++) {
            ll_band[i * hash_size + j] = d[i * image_scale + j];
        }
    }

    *out_hash = ph_median_bitpack(ll_band, 64);
    return PH_SUCCESS;
}

static ph_error_t ph_compute_whash_full(ph_context_t *ctx, uint64_t *out_hash) {
    int min_dim = ctx->image.width < ctx->image.height ? ctx->image.width : ctx->image.height;
    int log2_min = 0;
    while ((1 << log2_min) <= min_dim) {
        log2_min++;
    }
    log2_min--;

    int nat_scale = 1 << log2_min;
    int image_scale = nat_scale > PH_CORE_HASH_SIZE ? nat_scale : PH_CORE_HASH_SIZE;

    uint8_t *full_gray = ph_get_gray(ctx);
    if (!full_gray) {
        return PH_ERR_ALLOCATION_FAILED;
    }

    /* Rounded up so the float buffers behind the byte one stay aligned. */
    const size_t scale = ph_size(image_scale);
    size_t sz_scaled = ph_arena_align_up(scale * scale);
    size_t sz_d = scale * scale * sizeof(float);
    size_t sz_temps = scale * 2 * sizeof(float);

    ph_arena_mark_t arena_mark = ph_arena_mark(ctx);
    uint8_t *scratch_mem = ph_get_scratchpad(ctx, sz_scaled + sz_d + sz_temps);
    if (!scratch_mem) {
        return PH_ERR_ALLOCATION_FAILED;
    }

    uint8_t *scaled_img = scratch_mem;
    float *d = ph_arena_at(scratch_mem, sz_scaled);
    float *temp_a = ph_arena_at(scratch_mem, sz_scaled + sz_d);
    float *temp_b = temp_a + image_scale;

    if (!ph_resize_box(full_gray, ctx->image.width, ctx->image.height, scaled_img, image_scale,
                       image_scale)) {
        ph_arena_release(ctx, arena_mark);
        return PH_ERR_ALLOCATION_FAILED;
    }

    for (int i = 0; i < image_scale; i++) {
        for (int j = 0; j < image_scale; j++) {
            d[i * image_scale + j] = scaled_img[i * image_scale + j] / 255.0f;
        }
    }

    if (ctx->config.whash_remove_max_haar_ll) {
        ph_whash_remove_max_haar_ll(d, image_scale, image_scale, temp_a, temp_b);
    }

    int current_size = image_scale;
    // DWT cascade down to 8x8, each level on the previous level's LL band.
    while (current_size > PH_CORE_HASH_SIZE) {
        ph_haar_2d_level_ll(d, current_size, image_scale, temp_a);
        current_size /= 2;
    }

    // Extract the 8x8 LL block
    float ll_band[PH_CORE_HASH_SIZE * PH_CORE_HASH_SIZE];
    for (int i = 0; i < PH_CORE_HASH_SIZE; i++) {
        for (int j = 0; j < PH_CORE_HASH_SIZE; j++) {
            ll_band[i * PH_CORE_HASH_SIZE + j] = d[i * image_scale + j];
        }
    }

    *out_hash = ph_median_bitpack(ll_band, 64);
    ph_arena_release(ctx, arena_mark);
    return PH_SUCCESS;
}

PH_API ph_error_t ph_compute_whash(ph_context_t *ctx, uint64_t *out_hash) {
    if (!ctx || !out_hash) {
        return PH_ERR_INVALID_ARGUMENT;
    }
    if (!ctx->image.is_loaded) {
        return PH_ERR_EMPTY_IMAGE;
    }

    if (ctx->config.whash_mode == PH_WHASH_FULL) {
        return ph_compute_whash_full(ctx, out_hash);
    } else {
        return ph_compute_whash_fast(ctx, out_hash);
    }
}
