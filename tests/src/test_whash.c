#include "hashes/hashes.h"
#include "image/image.h"
#include "libphash.h"
#include "test_macros.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

/* ImageHash zeroes the coarsest LL band before the working decomposition
 * (remove_max_haar_ll, on by default there) to keep overall brightness out of the hash.
 * These three tests are why ours defaults to off: the operation is the identity for a
 * hash thresholded at the median, so it cannot remove anything the median has not
 * already removed, and all it can contribute is rounding noise. */

/* Step one of the proof: zeroing the single coarsest LL coefficient and reconstructing
 * subtracts the image mean from every sample, and nothing else. */
void test_remove_max_haar_ll_subtracts_the_mean() {
    enum {
        N = 16,
    };

    float orig[N * N], d[N * N], temp_a[N], temp_b[N];

    unsigned seed = 7;
    for (int i = 0; i < N * N; i++) {
        seed = seed * 1103515245u + 12345u;
        orig[i] = (float)((seed >> 16) & 0xFF) / 255.0f;
    }
    memcpy(d, orig, sizeof(d));

    /* Full cascade down to a 1x1 LL, then back up -- with no zeroing this must round-trip. */
    for (int size = N; size > 1; size /= 2) {
        ph_haar_2d_level(d, size, N, temp_a, temp_b);
    }
    for (int size = 2; size <= N; size *= 2) {
        ph_haar_2d_level_inverse(d, size, N, temp_a, temp_b);
    }
    for (int i = 0; i < N * N; i++) {
        ASSERT_FLOAT_EQ(orig[i], d[i], 1e-5);
    }

    /* Now the same cascade with the coarsest coefficient zeroed. */
    memcpy(d, orig, sizeof(d));
    for (int size = N; size > 1; size /= 2) {
        ph_haar_2d_level(d, size, N, temp_a, temp_b);
    }
    d[0] = 0.0f;
    for (int size = 2; size <= N; size *= 2) {
        ph_haar_2d_level_inverse(d, size, N, temp_a, temp_b);
    }

    double mean = 0.0;
    for (int i = 0; i < N * N; i++) {
        mean += orig[i];
    }
    mean /= (double)(N * N);

    for (int i = 0; i < N * N; i++) {
        ASSERT_FLOAT_EQ(orig[i] - (float)mean, d[i], 1e-5);
    }

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
    enum {
        W = 64,
    };

    uint8_t pixels[W * W * 3];
    memset(pixels, 137, sizeof(pixels));

    for (int enable = 0; enable < 2; enable++) {
        uint64_t hash = 0xDEADBEEFDEADBEEFULL;
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

/* The FULL-mode cascade against an independent calculation of what it should be.
 *
 * After L levels of the orthonormal Haar transform, the LL coefficient at (i,j) is the
 * mean of the 2^L x 2^L block of samples under it, times 2^L. The hash thresholds those
 * coefficients against their own median, and a median threshold is blind to a positive
 * constant factor -- so the *block means themselves* predict the hash exactly, with no
 * wavelet code involved. That makes this a real check of the cascade rather than a
 * restatement of it: it verifies the level count, the stride handling and the scale
 * factor at once, and it would fail if the cascade stopped one level early or ran one
 * level too far.
 *
 * The sides cover both sides of every power of two that matters. Below 16 the chosen
 * scale is 8, which is already the hash size, so FULL mode performs *no wavelet
 * decomposition at all* and degrades to a median-thresholded 8x8 box reduction. That is
 * inherited from the reference implementation (its level count is
 * log2(scale) - log2(hash_size), i.e. zero there) and is asserted here rather than
 * discovered by someone hashing thumbnails.
 *
 * Ties are excluded from the comparison: when several LL coefficients land exactly on the
 * median, which one clears a `>` is decided by the rounding of the cascade, and the block
 * means computed in double do not reproduce that. Side 32 of this fixture has a seven-way
 * tie and is exactly the case in question. */
static uint64_t whash_full_from_block_means(const uint8_t *px, int w, int h,
                                            uint64_t *out_decided) {
    int min_dim = w < h ? w : h;
    int log2_min = 0;
    while ((1 << (log2_min + 1)) <= min_dim) {
        log2_min++;
    }
    int scale = 1 << log2_min;
    if (scale < PH_CORE_HASH_SIZE) {
        scale = PH_CORE_HASH_SIZE;
    }

    uint8_t *scaled = (uint8_t *)malloc((size_t)scale * scale);
    ASSERT_PTR_NOT_NULL(scaled);
    ph_resize_box(px, w, h, scaled, scale, scale);

    const int block = scale / PH_CORE_HASH_SIZE;
    double ll[64];
    for (int i = 0; i < PH_CORE_HASH_SIZE; i++) {
        for (int j = 0; j < PH_CORE_HASH_SIZE; j++) {
            double sum = 0.0;
            for (int y = 0; y < block; y++) {
                for (int x = 0; x < block; x++) {
                    sum += scaled[(size_t)(i * block + y) * scale + (j * block + x)];
                }
            }
            ll[i * PH_CORE_HASH_SIZE + j] = sum / (double)(block * block);
        }
    }
    free(scaled);

    double sorted[64];
    memcpy(sorted, ll, sizeof(sorted));
    for (int i = 1; i < 64; i++) {
        double key = sorted[i];
        int j = i - 1;
        while (j >= 0 && sorted[j] > key) {
            sorted[j + 1] = sorted[j];
            j--;
        }
        sorted[j + 1] = key;
    }
    const double median = (sorted[31] + sorted[32]) * 0.5;

    uint64_t hash = 0, decided = 0;
    for (int i = 0; i < 64; i++) {
        if (ll[i] > median) {
            hash |= 1ULL << i;
        }
        /* A coefficient a long way from the median cannot be flipped by the cascade's
         * rounding; one sitting on it can. Only the former are compared. */
        if (fabs(ll[i] - median) > 1e-6) {
            decided |= 1ULL << i;
        }
    }
    *out_decided = decided;
    return hash;
}

void test_whash_full_ll_band_is_the_block_mean() {
    static const int sides[] = {8, 9, 12, 15, 16, 17, 23, 31, 32, 33, 40, 47, 63, 64, 65, 96, 128};
    for (unsigned s = 0; s < sizeof(sides) / sizeof(sides[0]); s++) {
        const int side = sides[s];
        uint8_t *px = (uint8_t *)malloc((size_t)side * side);
        ASSERT_PTR_NOT_NULL(px);
        for (int y = 0; y < side; y++) {
            for (int x = 0; x < side; x++) {
                px[(size_t)y * side + x] = (uint8_t)((x * 37 + y * 91 + x * y) % 256);
            }
        }

        ph_context_t *ctx = NULL;
        ASSERT_OK(ph_create(&ctx));
        ASSERT_OK(ph_load_from_pixels(ctx, px, side, side, 1, 0));
        ASSERT_OK(ph_context_set_whash_mode(ctx, PH_WHASH_FULL));
        uint64_t got = 0;
        ASSERT_OK(ph_compute_whash(ctx, &got));
        ph_free(ctx);

        uint64_t decided = 0;
        uint64_t want = whash_full_from_block_means(px, side, side, &decided);
        free(px);

        if ((got & decided) != (want & decided)) {
            fprintf(stderr,
                    "[FAIL] whash FULL at %dx%d: got %016llx, block means predict %016llx "
                    "(decided mask %016llx)\n",
                    side, side, (unsigned long long)got, (unsigned long long)want,
                    (unsigned long long)decided);
            exit(1);
        }
    }

    PASS("test_whash_full_ll_band_is_the_block_mean");
}

/* The FULL cascade written with the complete ph_haar_2d_level() at every level -- all four
 * bands, column passes included -- from the same box-resized gray image. The library keeps
 * only each level's LL band; this is the contract that it is the same LL band, bit for bit,
 * so the reference is compared with ==, not with a tolerance. */
static uint64_t whash_full_with_complete_levels(ph_context_t *ctx, int remove_max_haar_ll) {
    int w = 0, h = 0, c = 0;
    ph_context_get_dimensions(ctx, &w, &h, &c);
    const int min_dim = w < h ? w : h;
    int scale = 1;
    while (scale * 2 <= min_dim) {
        scale *= 2;
    }
    if (scale < PH_CORE_HASH_SIZE) {
        scale = PH_CORE_HASH_SIZE;
    }

    uint8_t *gray = ph_get_gray(ctx);
    ASSERT_PTR_NOT_NULL(gray);
    uint8_t *img = (uint8_t *)malloc((size_t)scale * scale);
    float *d = (float *)malloc((size_t)scale * scale * sizeof(float));
    float *ta = (float *)malloc((size_t)scale * sizeof(float));
    float *tb = (float *)malloc((size_t)scale * sizeof(float));
    ASSERT(img && d && ta && tb);
    ASSERT(ph_resize_box(gray, w, h, img, scale, scale));
    for (size_t i = 0; i < (size_t)scale * scale; i++) {
        d[i] = img[i] / 255.0f;
    }

    if (remove_max_haar_ll) {
        int cur = scale;
        while (cur > 1) {
            ph_haar_2d_level(d, cur, scale, ta, tb);
            cur /= 2;
        }
        d[0] = 0.0f;
        while (cur < scale) {
            cur *= 2;
            ph_haar_2d_level_inverse(d, cur, scale, ta, tb);
        }
    }
    for (int cur = scale; cur > PH_CORE_HASH_SIZE; cur /= 2) {
        ph_haar_2d_level(d, cur, scale, ta, tb);
    }

    float ll[PH_CORE_HASH_SIZE * PH_CORE_HASH_SIZE];
    for (int y = 0; y < PH_CORE_HASH_SIZE; y++) {
        for (int x = 0; x < PH_CORE_HASH_SIZE; x++) {
            ll[y * PH_CORE_HASH_SIZE + x] = d[(size_t)y * scale + x];
        }
    }
    free(img);
    free(d);
    free(ta);
    free(tb);
    return ph_median_bitpack(ll, PH_CORE_HASH_SIZE * PH_CORE_HASH_SIZE);
}

void test_whash_full_matches_complete_levels() {
    /* Sides around every power of two the cascade starts from (8 ... 512), non-square and
     * odd, 1, 3 and 4 channels, pseudo-random and structured content. */
    static const int dims[][2] = {{1, 1},     {7, 5},     {8, 8},     {9, 13},
                                  {16, 17},   {33, 31},   {64, 100},  {127, 129},
                                  {200, 256}, {257, 300}, {511, 513}, {640, 480}};
    static const int channels[] = {1, 3, 4};
    ph_test_rng_t rng = ph_test_rng(0x5EED1u);
    for (unsigned di = 0; di < sizeof(dims) / sizeof(dims[0]); di++) {
        for (unsigned ci = 0; ci < sizeof(channels) / sizeof(channels[0]); ci++) {
            for (int pattern = 0; pattern < 3; pattern++) {
                const int w = dims[di][0], h = dims[di][1], ch = channels[ci];
                const size_t n = (size_t)w * h * ch;
                uint8_t *px = (uint8_t *)malloc(n);
                ASSERT_PTR_NOT_NULL(px);
                for (size_t i = 0; i < n; i++) {
                    const size_t p = i / (size_t)ch, x = p % (size_t)w, y = p / (size_t)w;
                    px[i] = pattern == 0   ? ph_test_rng_byte(&rng)
                            : pattern == 1 ? (uint8_t)((x * 37 + y * 91 + x * y) % 256)
                                           : (uint8_t)(((x / 3 + y / 5) % 2) ? 255 : 0);
                }
                for (int remove = 0; remove <= 1; remove++) {
                    ph_context_t *ctx = NULL;
                    ASSERT_OK(ph_create(&ctx));
                    ASSERT_OK(ph_load_from_pixels(ctx, px, w, h, ch, 0));
                    ASSERT_OK(ph_context_set_whash_mode(ctx, PH_WHASH_FULL));
                    ASSERT_OK(ph_context_set_whash_remove_max_haar_ll(ctx, remove));
                    uint64_t got = 0;
                    ASSERT_OK(ph_compute_whash(ctx, &got));
                    const uint64_t want = whash_full_with_complete_levels(ctx, remove);
                    ASSERT_MSG(got == want,
                               "whash FULL %dx%d c=%d pattern %d remove_max_haar_ll %d: got "
                               "%016llx, complete levels give %016llx",
                               w, h, ch, pattern, remove, (unsigned long long)got,
                               (unsigned long long)want);
                    ph_free(ctx);
                }
                free(px);
            }
        }
    }
    PASS("test_whash_full_matches_complete_levels");
}

int main() {
    test_whash_full_ll_band_is_the_block_mean();
    test_whash_full_matches_complete_levels();
    test_whash_e2e();
    test_remove_max_haar_ll_subtracts_the_mean();
    test_remove_max_haar_ll_leaves_the_hash_alone();
    test_remove_max_haar_ll_on_a_solid_fill();
    return 0;
}
