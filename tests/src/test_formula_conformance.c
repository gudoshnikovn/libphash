/*
 * test_formula_conformance.c
 *
 * Checks the library's maths against the formulas its sources state, on synthetic input
 * whose correct answer is known independently -- not against hash bits of a photograph.
 *
 * The distinction is the whole point. A wrong bit in the hash of a photo could be the
 * resampler, the grayscale weights, rounding, or the algorithm; there is no way to tell.
 * A wrong DCT coefficient on a known matrix is unambiguous.
 *
 * Sources, cited in full in docs/references.md:
 *   [Z10] Zauner, "Implementation and Benchmarking of Perceptual Image Hash Functions",
 *         FH Hagenberg, 2010 -- definitions 3.1-3.3 (DCT), 3.9 (block mean threshold),
 *         3.10 (DCT hash threshold), section 3.1.4 (block mean method 1).
 *   [SO95] Stricker & Orengo, "Similarity of color images", SPIE 2420, 1995 -- the three
 *         colour moments, via the restatement named in docs/references.md.
 *
 * Where the code contradicts a source, the test says so with a KNOWN DIVERGENCE comment
 * naming the defect, and pins today's behaviour. That is deliberate: the fix must then
 * change a test, which makes it a visible decision instead of a silent drift.
 *
 * These tests exercise the real functions. tests/src/test_dct.c and test_haar.c check
 * properties of hand-copied replicas of the same maths, which cannot catch a change in
 * the shipped code; this file complements them, it does not replace them.
 */

#include "internal.h"
#include "libphash.h"
#include "test_macros.h"
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Coefficients are sums of up to 1024 products of a float matrix with byte data, so the
 * accumulated error is compared relative to the magnitude of the terms, not absolutely. */
static void assert_close(double got, double want, double tol, const char *what) {
    double diff = fabs(got - want);
    if (diff > tol) {
        fprintf(stderr, "[FAIL] %s: got %.6f, want %.6f (|diff| %.6g > tol %.6g)\n", what, got,
                want, diff, tol);
        exit(1);
    }
}

/* ============================================================================
 * DCT -- [Z10] definitions 3.2 and 3.3
 * ========================================================================= */

/* [Z10] definition 3.3: c[n,m] = sqrt(2/N) * cos((2m+1) * n * pi / 2N), with row 0
 * scaled by 1/sqrt(2) so the matrix is orthonormal. Written out here from the thesis
 * rather than copied from phash.c: a transcription of the source is the reference, a
 * copy of the code would only prove the code equals itself. */
static double dct_matrix_entry(int n, int m, int N) {
    double scale = (n == 0) ? sqrt(1.0 / N) : sqrt(2.0 / N);
    return scale * cos((2.0 * m + 1.0) * n * M_PI / (2.0 * N));
}

static void test_dct_matrix_matches_definition(void) {
    const float *m = ph_get_dct_matrix_32();
    ASSERT_PTR_NOT_NULL((void *)m);

    for (int n = 0; n < 32; n++) {
        for (int col = 0; col < 32; col++) {
            assert_close(m[n * 32 + col], dct_matrix_entry(n, col, 32), 1e-6,
                         "DCT matrix entry vs definition 3.3");
        }
    }
    printf("test_dct_matrix_matches_definition: PASSED\n");
}

static void test_dct_matrix_is_orthonormal(void) {
    const float *m = ph_get_dct_matrix_32();

    /* The 2-D transform in [Z10] equation 3.4 is M * I * M', which only inverts and only
     * preserves energy if M is orthonormal. */
    for (int i = 0; i < 32; i++) {
        for (int j = i; j < 32; j++) {
            double dot = 0.0;
            for (int k = 0; k < 32; k++)
                dot += (double)m[i * 32 + k] * (double)m[j * 32 + k];
            assert_close(dot, (i == j) ? 1.0 : 0.0, 1e-5, "DCT row orthonormality");
        }
    }
    printf("test_dct_matrix_is_orthonormal: PASSED\n");
}

/* Direct O(N^4) evaluation of the separable 2-D transform, straight from the definition:
 *   X[u][v] = sum_k sum_l c[u,k] * c[v,l] * x[k][l]
 * Slow on purpose -- it shares no structure with the two-pass implementation, so an
 * error in the latter's index arithmetic cannot hide in it. */
static double dct2_direct(const uint8_t *input, int N, int u, int v) {
    double sum = 0.0;
    for (int k = 0; k < N; k++) {
        for (int l = 0; l < N; l++) {
            sum += dct_matrix_entry(u, k, N) * dct_matrix_entry(v, l, N) * (double)input[k * N + l];
        }
    }
    return sum;
}

static void test_dct2_partial_matches_direct_definition(void) {
    const int N = 32, R = 8;
    uint8_t input[32 * 32];

    /* Deterministic pseudo-random input: no seed, no dependence on the platform's RNG. */
    uint32_t state = 0x9E3779B9u;
    for (int i = 0; i < N * N; i++) {
        state = state * 1664525u + 1013904223u;
        input[i] = (uint8_t)(state >> 24);
    }

    float out[8 * 8];
    ASSERT_OK(ph_dct2_partial(ph_get_dct_matrix_32(), input, N, R, out));

    for (int u = 0; u < R; u++) {
        for (int v = 0; v < R; v++) {
            /* Single-precision accumulation over 32 terms of magnitude ~255, twice.
             * A tolerance of 0.05 is far below the spacing between coefficients that
             * decides any hash bit. */
            assert_close(out[u * R + v], dct2_direct(input, N, u, v), 0.05,
                         "ph_dct2_partial vs the 2-D definition");
        }
    }
    printf("test_dct2_partial_matches_direct_definition: PASSED\n");
}

static void test_dct2_of_constant_image(void) {
    const int N = 32, R = 8;
    const uint8_t value = 200;
    uint8_t input[32 * 32];
    memset(input, value, sizeof(input));

    float out[8 * 8];
    ASSERT_OK(ph_dct2_partial(ph_get_dct_matrix_32(), input, N, R, out));

    /* A constant image has all its energy in DC. With an orthonormal matrix,
     * X[0][0] = (1/sqrt(N))^2 * N^2 * v = N * v, and every other coefficient is zero
     * because every row but the first sums to zero. */
    assert_close(out[0], (double)N * value, 0.05, "DC coefficient of a constant image");
    for (int i = 1; i < R * R; i++)
        assert_close(out[i], 0.0, 0.05, "AC coefficient of a constant image");

    /* The degenerate case, at its most extreme: DC is 6400 and all 63 AC coefficients
     * are 0, so the median -- taken over the AC terms alone, as pHash does -- is 0, and
     * every AC bit is then decided by float rounding noise rather than by the image.
     * Leaving DC out of the median does not help here and is not meant to: a solid
     * colour has no structure to hash, which is Starkweather's "completely flat image
     * information" as quoted in docs/references.md. The DC bit is set, as always. */
    uint64_t hash = ph_median_bitpack_from(out, R * R, 1);
    ASSERT(hash & 1u); /* bit 0 is DC, and it is set */

    printf("test_dct2_of_constant_image: PASSED\n");
}

static void test_dct_dc_coefficient_dominates_and_its_bit_is_constant(void) {
    /* DC is sqrt(N)*N times the mean brightness, so it is non-negative and, for any image
     * that is not pathological, larger in magnitude than every AC coefficient. It is
     * therefore above the median every time and its bit is 1 every time: one of the 64
     * bits carries no information, and the hash is effectively 63 bits wide.
     *
     * That is pHash's behaviour too -- it crops the block at (0,0) and thresholds all 64
     * values -- so it is a property of the algorithm rather than a divergence, and the
     * two ways of removing the dead bit were both measured before this was left alone.
     * See test_dct_median_ignores_dc_without_changing_the_hash() below and
     * docs/algorithm-provenance.md section 3.
     *
     * Asserted over three unrelated synthetic images. */
    const int N = 32, R = 8;
    uint8_t input[32 * 32];

    for (int variant = 0; variant < 3; variant++) {
        for (int y = 0; y < N; y++) {
            for (int x = 0; x < N; x++) {
                int v;
                switch (variant) {
                    case 0:
                        v = (x * 255) / (N - 1);
                        break; /* gradient */
                    case 1:
                        v = ((x / 4) + (y / 4)) % 2 ? 230 : 25;
                        break; /* checkerboard */
                    default:
                        v = (x * y * 7 + 13) & 0xFF;
                        break; /* texture */
                }
                input[y * N + x] = (uint8_t)v;
            }
        }

        float out[8 * 8];
        ASSERT_OK(ph_dct2_partial(ph_get_dct_matrix_32(), input, N, R, out));

        ASSERT(out[0] > 0.0f);
        for (int i = 1; i < R * R; i++) {
            if (fabs(out[i]) >= (double)out[0]) {
                fprintf(stderr,
                        "[FAIL] variant %d: AC coefficient %d (%.3f) is not below DC (%.3f)\n",
                        variant, i, (double)out[i], (double)out[0]);
                exit(1);
            }
        }

        uint64_t hash = ph_median_bitpack_from(out, R * R, 1);
        ASSERT(hash & 1u);
    }

    printf("test_dct_dc_coefficient_dominates_and_its_bit_is_constant: PASSED\n");
}

static void test_dct_median_ignores_dc_without_changing_the_hash(void) {
    /* pHash takes the median over the 63 AC coefficients, not over all 64, and this code
     * now does the same. This pins how little that changes, because the usual argument
     * for it -- that including DC "drags the median" -- is false. A median is not dragged
     * by an outlier.
     *
     * With the 64 values sorted ascending as v0..v63 and DC the largest, the median of
     * all 64 is (v31 + v32) / 2 and the median of the 63 without DC is v31. Nothing lies
     * strictly between them, so the same values clear the threshold and the hash is
     * identical -- unless v31 and v32 are so close that the float average of the two
     * rounds to v32 itself, at which point v32 clears the lower threshold and not the
     * higher one. That is the only way the two can disagree, and it costs one bit.
     *
     * Measured on the real fixtures in tests/data: identical on all of them. It shows up
     * here only on the symmetric checkerboard, where two coefficients tie to within a
     * float ulp. */
    const int N = 32, R = 8;
    uint8_t input[32 * 32];

    for (int variant = 0; variant < 4; variant++) {
        uint32_t state = 0x9E3779B9u + (uint32_t)variant * 2654435761u;
        for (int i = 0; i < N * N; i++) {
            state = state * 1664525u + 1013904223u;
            switch (variant) {
                case 0:
                    input[i] = (uint8_t)(state >> 24);
                    break; /* noise */
                case 1:
                    input[i] = (uint8_t)((i % N) * 255 / (N - 1));
                    break; /* gradient */
                case 2:
                    input[i] = (uint8_t)(((i / N) / 3 + (i % N) / 3) % 2 ? 240 : 12);
                    break; /* symmetric checkerboard -- ties */
                default:
                    input[i] = (uint8_t)(128 + (int)(60.0 * sin(i * 0.21)));
                    break;
            }
        }

        float out[8 * 8];
        ASSERT_OK(ph_dct2_partial(ph_get_dct_matrix_32(), input, N, R, out));

        uint64_t with_dc = ph_median_bitpack_from(out, R * R, 0);
        uint64_t without_dc = ph_median_bitpack_from(out, R * R, 1);

        uint64_t diff = with_dc ^ without_dc;
        int changed = 0;
        for (uint64_t d = diff; d; d &= d - 1)
            changed++;
        if (changed > 1) {
            fprintf(stderr,
                    "[FAIL] variant %d: leaving DC out of the median moved %d bits (%016llx vs "
                    "%016llx). At most one bit can move, and only on a tie between the two "
                    "middle coefficients -- check whether DC is still the maximum\n",
                    variant, changed, (unsigned long long)with_dc, (unsigned long long)without_dc);
            exit(1);
        }
        /* Whatever moved, it was not the DC bit: DC is above either threshold. */
        ASSERT((diff & 1u) == 0);
    }

    printf("test_dct_median_ignores_dc_without_changing_the_hash: PASSED\n");
}

static void test_dct2_concentrates_a_single_cosine(void) {
    const int N = 32, R = 8;
    const int freq = 3;
    uint8_t input[32 * 32];

    /* A pure horizontal cosine at frequency `freq`: constant down each column, so all
     * the energy must land in row 0 of the output, at column `freq`. Quantising to bytes
     * makes the value inexact, so the assertion is on where the energy is, not on how
     * much -- which is what "the transform separates frequencies" actually means. */
    for (int y = 0; y < N; y++) {
        for (int x = 0; x < N; x++) {
            double v = 127.5 + 127.0 * cos((2.0 * x + 1.0) * freq * M_PI / (2.0 * N));
            input[y * N + x] = (uint8_t)(v + 0.5);
        }
    }

    float out[8 * 8];
    ASSERT_OK(ph_dct2_partial(ph_get_dct_matrix_32(), input, N, R, out));

    double target = fabs(out[0 * R + freq]);
    for (int u = 0; u < R; u++) {
        for (int v = 0; v < R; v++) {
            if (u == 0 && v == freq)
                continue;
            if (u == 0 && v == 0)
                continue; /* DC carries the offset of the cosine */
            if (fabs(out[u * R + v]) > target * 0.01) {
                fprintf(stderr, "[FAIL] energy leaked to coefficient (%d,%d): %.3f vs %.3f\n", u, v,
                        (double)out[u * R + v], target);
                exit(1);
            }
        }
    }
    printf("test_dct2_concentrates_a_single_cosine: PASSED\n");
}

/* ============================================================================
 * Haar -- no primary source, so this checks the transform's defining properties
 * ========================================================================= */

/* ---------------------------------------------------------------------------
 * The 1-D DCT the radial hash applies to its variance vector
 * ------------------------------------------------------------------------ */

static double dct1_direct(const double *x, int n, int k) {
    double sum = 0.0;
    for (int i = 0; i < n; i++)
        sum += x[i] * cos(M_PI * (2.0 * i + 1.0) * k / (2.0 * n));
    return sum * (k == 0 ? 1.0 / sqrt((double)n) : sqrt(2.0 / (double)n));
}

static void test_dct1_partial_matches_direct_definition(void) {
    /* The transform the source calls for -- "applying the DCT to the radial variance
     * vector", of which "the first 40 coefficients ... form the radial hash vector"
     * ([Z10] 3.1.3) -- against DCT-II written straight out, on a vector the size the
     * algorithm actually uses. */
    const int N = 180, K = 40;
    double in[180];

    uint32_t state = 0x9E3779B9u;
    for (int i = 0; i < N; i++) {
        state = state * 1664525u + 1013904223u;
        in[i] = (double)(state >> 8) / 4096.0; /* non-negative, like a variance */
    }

    double out[40];
    ASSERT_OK(ph_dct1d_partial(in, N, K, out));
    for (int k = 0; k < K; k++)
        assert_close(out[k], dct1_direct(in, N, k), 1e-9, "ph_dct1d_partial vs the definition");

    printf("test_dct1_partial_matches_direct_definition: PASSED\n");
}

static void test_dct1_of_a_constant_vector(void) {
    /* All the energy in coefficient 0, nothing anywhere else -- the property that makes
     * a flat image produce an all-zero radial digest rather than quantisation noise. */
    const int N = 180, K = 40;
    double in[180];
    for (int i = 0; i < N; i++)
        in[i] = 7.5;

    double out[40];
    ASSERT_OK(ph_dct1d_partial(in, N, K, out));
    assert_close(out[0], 7.5 * sqrt((double)N), 1e-9, "coefficient 0 of a constant vector");
    for (int k = 1; k < K; k++)
        assert_close(out[k], 0.0, 1e-9, "AC coefficient of a constant vector");

    printf("test_dct1_of_a_constant_vector: PASSED\n");
}

static void test_dct1_rejects_more_coefficients_than_it_has(void) {
    /* A DCT of an n-element vector has n coefficients; asking for more is refused rather
     * than answered with whatever is past the end of the input. */
    double in[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    double out[16] = {0};
    ASSERT_INT_EQ(PH_ERR_INVALID_ARGUMENT, ph_dct1d_partial(in, 8, 9, out));
    ASSERT_INT_EQ(PH_ERR_INVALID_ARGUMENT, ph_dct1d_partial(in, 8, 0, out));
    ASSERT_INT_EQ(PH_ERR_INVALID_ARGUMENT, ph_dct1d_partial(NULL, 8, 4, out));
    ASSERT_FLOAT_EQ(0.0, out[0], 1e-12); /* nothing written on any of them */

    printf("test_dct1_rejects_more_coefficients_than_it_has: PASSED\n");
}

static void test_haar_is_orthonormal(void) {
    /* An orthonormal transform preserves energy. For the normalisation used here --
     * both the sum and the difference divided by sqrt(2) -- this holds exactly:
     * ((a+b)^2 + (a-b)^2) / 2 == a^2 + b^2. */
    float data[16], temp[16];
    double before = 0.0, after = 0.0;

    uint32_t state = 0x12345678u;
    for (int i = 0; i < 16; i++) {
        state = state * 1664525u + 1013904223u;
        data[i] = (float)((state >> 24) / 255.0);
        before += (double)data[i] * data[i];
    }

    ph_haar_1d_float(data, 16, temp);
    for (int i = 0; i < 16; i++)
        after += (double)data[i] * data[i];

    assert_close(after, before, 1e-4, "Haar energy preservation");
    printf("test_haar_is_orthonormal: PASSED\n");
}

static void test_haar_on_a_step_signal(void) {
    /* [0,0,0,0,1,1,1,1]: the low half must be the scaled averages and the high half the
     * differences, which are zero everywhere because each pair is constant. */
    float data[8] = {0, 0, 0, 0, 1, 1, 1, 1};
    float temp[8];
    const double s = 1.0 / sqrt(2.0);

    ph_haar_1d_float(data, 8, temp);

    assert_close(data[0], 0.0, 1e-6, "Haar step: average of (0,0)");
    assert_close(data[1], 0.0, 1e-6, "Haar step: average of (0,0)");
    assert_close(data[2], 2.0 * s, 1e-6, "Haar step: average of (1,1)");
    assert_close(data[3], 2.0 * s, 1e-6, "Haar step: average of (1,1)");
    for (int i = 4; i < 8; i++)
        assert_close(data[i], 0.0, 1e-6, "Haar step: detail within a constant pair");

    printf("test_haar_on_a_step_signal: PASSED\n");
}

/* ============================================================================
 * Block means -- [Z10] section 3.1.4, method 1
 * ========================================================================= */

static void test_block_means_on_an_exact_multiple(void) {
    /* The source normalises the image to a preset size and averages non-overlapping
     * blocks. Resampling straight to the block grid equals that only when the dimensions
     * are an exact multiple -- which is why this test uses 64x64 into 8x8, and why it is
     * the check that pins defect 8. */
    const int W = 64, H = 64, G = 8, B = W / G;
    uint8_t *src = (uint8_t *)malloc((size_t)W * H);
    ASSERT_PTR_NOT_NULL(src);

    uint32_t state = 0xDEADBEEFu;
    for (int i = 0; i < W * H; i++) {
        state = state * 1664525u + 1013904223u;
        src[i] = (uint8_t)(state >> 24);
    }

    uint8_t got[64];
    ph_resize_box(src, W, H, got, G, G);

    for (int by = 0; by < G; by++) {
        for (int bx = 0; bx < G; bx++) {
            unsigned sum = 0;
            for (int y = 0; y < B; y++)
                for (int x = 0; x < B; x++)
                    sum += src[(by * B + y) * W + (bx * B + x)];
            double want = (double)sum / (B * B);
            /* One level of slack for the resampler's rounding of the same mean. */
            assert_close(got[by * G + bx], want, 1.0, "box resample equals the block mean");
        }
    }

    free(src);
    printf("test_block_means_on_an_exact_multiple: PASSED\n");
}

static void test_bmh_thresholds_on_the_mean_not_the_median(void) {
    /* KNOWN DIVERGENCE (docs/algorithm-provenance.md, defect 3): [Z10] equation 3.9
     * thresholds each block against the MEDIAN of the block means, which makes the bit
     * distribution balanced by construction -- half the bits set, whatever the image.
     * This library uses the arithmetic mean.
     *
     * The input below is what makes the two rules differ: mostly dark, with a few very
     * bright blocks. The median rule would set exactly half the bits; the mean is
     * dragged upwards by the bright blocks, so far fewer are set. This test asserts the
     * present, divergent behaviour so that fixing it must change this test. */
    const int W = 64, H = 64;
    uint8_t *pixels = (uint8_t *)malloc((size_t)W * H * 3);
    ASSERT_PTR_NOT_NULL(pixels);

    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            /* The top 8 rows are white, everything else near-black. */
            uint8_t v = (y < 8) ? 255 : 10;
            size_t o = ((size_t)y * W + x) * 3;
            pixels[o] = pixels[o + 1] = pixels[o + 2] = v;
        }
    }

    ph_context_t *ctx = NULL;
    ASSERT_OK(ph_create(&ctx));
    ASSERT_OK(ph_context_set_block_params(ctx, 8));
    ASSERT_OK(ph_load_from_pixels(ctx, pixels, W, H, 3, 0));

    ph_digest_t d;
    ASSERT_OK(ph_compute_bmh(ctx, &d));

    int bits = 0;
    for (int i = 0; i < d.size * 8; i++)
        bits += (d.data[i / 8] >> (i % 8)) & 1;

    /* 8 of the 64 blocks are white. Mean = (8*255 + 56*10) / 64 ~= 40, so only the 8
     * white blocks clear it. The median would be 10, and every block with value >= 10 --
     * all 64 of them -- would be set. Either way the hash is degenerate here; the point
     * is that the two rules disagree, and which one this code follows. */
    ASSERT_INT_EQ(8, bits);

    ph_free(ctx);
    free(pixels);
    printf("test_bmh_thresholds_on_the_mean_not_the_median: PASSED (divergence pinned)\n");
}

/* ============================================================================
 * Colour moments -- [SO95] via the restatement in docs/references.md
 * ========================================================================= */

static void test_colour_moments_match_the_definitions(void) {
    /* Four values in one channel: {0, 0, 0, 100}. By hand:
     *   mean = 25
     *   variance = (3*(-25)^2 + 75^2) / 4 = (1875 + 5625) / 4 = 1875
     *   sd = sqrt(1875) = 43.301270...
     *   third central moment = (3*(-25)^3 + 75^3) / 4 = (-46875 + 421875) / 4 = 93750
     *   skew = cbrt(93750) = 45.428835...  (positive: the tail is to the right) */
    uint8_t rgb[4 * 3] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 100, 0, 0};

    ph_channel_moments_t m = ph_compute_moments(rgb, 4, 3, 0);
    assert_close(m.mean, 25.0, 1e-9, "colour moment 1 (mean)");
    assert_close(m.std_dev, sqrt(1875.0), 1e-9, "colour moment 2 (standard deviation)");
    assert_close(m.skew, cbrt(93750.0), 1e-9, "colour moment 3 (skewness)");

    /* The mirrored distribution {100, 100, 100, 0} must give the same magnitude of skew
     * with the opposite sign. cbrt(), not pow(x, 1/3), is what makes this work. */
    uint8_t mirrored[4 * 3] = {100, 0, 0, 100, 0, 0, 100, 0, 0, 0, 0, 0};
    ph_channel_moments_t n = ph_compute_moments(mirrored, 4, 3, 0);
    assert_close(n.mean, 75.0, 1e-9, "mirrored mean");
    assert_close(n.std_dev, sqrt(1875.0), 1e-9, "mirrored standard deviation");
    assert_close(n.skew, -cbrt(93750.0), 1e-9, "mirrored skewness is negative");

    printf("test_colour_moments_match_the_definitions: PASSED\n");
}

static void test_colour_moments_digest_discards_the_skew_sign(void) {
    /* KNOWN DIVERGENCE (docs/algorithm-provenance.md, defect 6): ph_compute_moments()
     * computes the signed third moment correctly, and then the digest stores fabs() of
     * it. Two images whose channel distributions are mirror images therefore produce
     * byte-identical digests, and the direction of the asymmetry -- half of what the
     * third moment says -- is unrecoverable.
     *
     * Built here as two 2x2 images that differ only in that mirroring. */
    uint8_t a[4 * 3] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 100, 100, 100};
    uint8_t b[4 * 3] = {100, 100, 100, 100, 100, 100, 100, 100, 100, 0, 0, 0};

    ph_context_t *ctx = NULL;
    ASSERT_OK(ph_create(&ctx));

    ph_digest_t da, db;
    ASSERT_OK(ph_load_from_pixels(ctx, a, 2, 2, 3, 0));
    ASSERT_OK(ph_compute_color_moments_hash(ctx, &da));
    ASSERT_OK(ph_load_from_pixels(ctx, b, 2, 2, 3, 0));
    ASSERT_OK(ph_compute_color_moments_hash(ctx, &db));

    /* The means differ (25 vs 75), so the digests are not equal overall -- but the skew
     * bytes, which carry opposite signs, are identical. That is the information loss. */
    for (int c = 0; c < 3; c++) {
        int skew_byte = c * 3 + 2;
        ASSERT_INT_EQ(da.data[skew_byte], db.data[skew_byte]);
    }
    ASSERT(da.data[0] != db.data[0]); /* the means do still distinguish them */

    ph_free(ctx);
    printf("test_colour_moments_digest_discards_the_skew_sign: PASSED (divergence pinned)\n");
}

int main(void) {
    test_dct_matrix_matches_definition();
    test_dct_matrix_is_orthonormal();
    test_dct2_partial_matches_direct_definition();
    test_dct2_of_constant_image();
    test_dct_dc_coefficient_dominates_and_its_bit_is_constant();
    test_dct_median_ignores_dc_without_changing_the_hash();
    test_dct2_concentrates_a_single_cosine();
    test_dct1_partial_matches_direct_definition();
    test_dct1_of_a_constant_vector();
    test_dct1_rejects_more_coefficients_than_it_has();
    test_haar_is_orthonormal();
    test_haar_on_a_step_signal();
    test_block_means_on_an_exact_multiple();
    test_bmh_thresholds_on_the_mean_not_the_median();
    test_colour_moments_match_the_definitions();
    test_colour_moments_digest_discards_the_skew_sign();
    printf("ALL FORMULA CONFORMANCE TESTS PASSED\n");
    return 0;
}
