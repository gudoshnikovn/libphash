#include "internal.h"
#include "libphash.h"
#include "test_macros.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

void test_haar_1d_unit() {
    float data[2] = {100.0f, 50.0f};
    float temp[2];
    ph_haar_1d_float(data, 2, temp);

    // (100+50)/sqrt(2) ≈ 106.066
    // (100-50)/sqrt(2) ≈ 35.355
    ASSERT_FLOAT_EQ(106.066, data[0], 0.01);
    ASSERT_FLOAT_EQ(35.355, data[1], 0.01);

    PASS("test_haar_1d_unit");
}

void test_whash_e2e() {
    ph_context_t *ctx = NULL;
    uint64_t hash1, hash2;

    ASSERT_OK(ph_create(&ctx));
    ASSERT_OK(ph_load_from_file(ctx, TEST_DATA_DIR "/photo.jpeg"));

    // FAST mode (default or explicit)
    ph_context_set_whash_mode(ctx, PH_WHASH_FAST);
    ASSERT_OK(ph_compute_whash(ctx, &hash1));

    uint64_t hash_copy;
    ph_context_t *ctx_copy = NULL;
    ASSERT_OK(ph_create(&ctx_copy));
    ASSERT_OK(ph_load_from_file(ctx_copy, TEST_DATA_DIR "/photo_copy.jpeg"));
    ph_context_set_whash_mode(ctx_copy, PH_WHASH_FAST);
    ASSERT_OK(ph_compute_whash(ctx_copy, &hash_copy));
    ASSERT_UINT64_EQ(hash1, hash_copy);

    // FULL mode
    ph_context_set_whash_mode(ctx, PH_WHASH_FULL);
    ASSERT_OK(ph_compute_whash(ctx, &hash1));

    ph_context_set_whash_mode(ctx_copy, PH_WHASH_FULL);
    ASSERT_OK(ph_compute_whash(ctx_copy, &hash2));
    ASSERT_UINT64_EQ(hash1, hash2);

    ph_free(ctx);
    ph_free(ctx_copy);
    PASS("test_whash_e2e");
}

/* R67. ImageHash zeroes the coarsest LL band before the working decomposition
 * (remove_max_haar_ll, on by default there) to keep overall brightness out of the hash.
 * These three tests are why ours defaults to off: the operation is the identity for a
 * hash thresholded at the median, so it cannot remove anything the median has not
 * already removed, and all it can contribute is rounding noise. */

/* Step one of the proof: zeroing the single coarsest LL coefficient and reconstructing
 * subtracts the image mean from every sample, and nothing else. */
void test_remove_max_haar_ll_subtracts_the_mean() {
    enum { N = 16 };
    float orig[N * N], d[N * N], temp_a[N], temp_b[N];

    unsigned seed = 7;
    for (int i = 0; i < N * N; i++) {
        seed = seed * 1103515245u + 12345u;
        orig[i] = (float)((seed >> 16) & 0xFF) / 255.0f;
    }
    memcpy(d, orig, sizeof(d));

    /* Full cascade down to a 1x1 LL, then back up -- with no zeroing this must round-trip. */
    for (int size = N; size > 1; size /= 2)
        ph_haar_2d_level(d, size, N, temp_a, temp_b);
    for (int size = 2; size <= N; size *= 2)
        ph_haar_2d_level_inverse(d, size, N, temp_a, temp_b);
    for (int i = 0; i < N * N; i++)
        ASSERT_FLOAT_EQ(orig[i], d[i], 1e-5);

    /* Now the same cascade with the coarsest coefficient zeroed. */
    memcpy(d, orig, sizeof(d));
    for (int size = N; size > 1; size /= 2)
        ph_haar_2d_level(d, size, N, temp_a, temp_b);
    d[0] = 0.0f;
    for (int size = 2; size <= N; size *= 2)
        ph_haar_2d_level_inverse(d, size, N, temp_a, temp_b);

    double mean = 0.0;
    for (int i = 0; i < N * N; i++)
        mean += orig[i];
    mean /= (double)(N * N);

    for (int i = 0; i < N * N; i++)
        ASSERT_FLOAT_EQ(orig[i] - (float)mean, d[i], 1e-5);

    PASS("test_remove_max_haar_ll_subtracts_the_mean");
}

/* Step two: a constant subtracted from every sample shifts every LL coefficient and the
 * median by that same constant, so the hash comes out unchanged. Measured on the real
 * fixtures, both modes, both settings. */
void test_remove_max_haar_ll_leaves_the_hash_alone() {
    static const char *files[] = {TEST_DATA_DIR "/photo.jpeg", TEST_DATA_DIR "/photo_complex.png",
                                  TEST_DATA_DIR "/photo_rotated_90.jpeg"};
    static const ph_whash_mode_t modes[] = {PH_WHASH_FAST, PH_WHASH_FULL};

    for (unsigned f = 0; f < sizeof(files) / sizeof(*files); f++) {
        for (unsigned m = 0; m < sizeof(modes) / sizeof(*modes); m++) {
            uint64_t hash[2] = {0, 0};
            for (int enable = 0; enable < 2; enable++) {
                ph_context_t *ctx = NULL;
                ASSERT_OK(ph_create(&ctx));
                ASSERT_OK(ph_context_set_whash_mode(ctx, modes[m]));
                ASSERT_OK(ph_context_set_whash_remove_max_haar_ll(ctx, enable));
                ASSERT_OK(ph_load_from_file(ctx, files[f]));
                ASSERT_OK(ph_compute_whash(ctx, &hash[enable]));
                ph_free(ctx);
            }
            ASSERT_UINT64_EQ(hash[0], hash[1]);
        }
    }

    PASS("test_remove_max_haar_ll_leaves_the_hash_alone");
}

/* The one numerical edge the removal was suspected of: on a solid fill every LL
 * coefficient lands on the median, and after the subtraction they are all exactly zero.
 * The hash must stay all-zero rather than turn into a readout of float noise, the way
 * pHash does on a constant image (see test_dct2_of_constant_image). */
void test_remove_max_haar_ll_on_a_solid_fill() {
    enum { W = 64 };
    uint8_t pixels[W * W * 3];
    memset(pixels, 137, sizeof(pixels));

    for (int enable = 0; enable < 2; enable++) {
        uint64_t hash = 0xdeadbeefdeadbeefULL;
        ph_context_t *ctx = NULL;
        ASSERT_OK(ph_create(&ctx));
        ASSERT_OK(ph_context_set_whash_remove_max_haar_ll(ctx, enable));
        ASSERT_OK(ph_load_from_pixels(ctx, pixels, W, W, 3, 0));
        ASSERT_OK(ph_compute_whash(ctx, &hash));
        ASSERT_UINT64_EQ(0ULL, hash);
        ph_free(ctx);
    }

    PASS("test_remove_max_haar_ll_on_a_solid_fill");
}

int main() {
    test_haar_1d_unit();
    test_whash_e2e();
    test_remove_max_haar_ll_subtracts_the_mean();
    test_remove_max_haar_ll_leaves_the_hash_alone();
    test_remove_max_haar_ll_on_a_solid_fill();
    return 0;
}
