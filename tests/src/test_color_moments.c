#include "hashes/hashes.h"
#include "libphash.h"
#include "test_macros.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

void test_moments_unit() {
    uint8_t data[4 * 3]; // 4 pixels, RGB
    ph_channel_moments_t all[PH_COLOR_CHANNELS];
    ph_channel_moments_t m;

    // Test 1: Uniform channel (all R=128)
    memset(data, 0, sizeof(data));
    for (int i = 0; i < 4; i++) {
        data[i * 3] = 128;
    }
    ph_compute_moments(data, 4, 3, all);
    m = all[0];
    ASSERT_FLOAT_EQ(128.0, m.mean, 0.001);
    ASSERT_FLOAT_EQ(0.0, m.std_dev, 0.001);
    ASSERT_FLOAT_EQ(0.0, m.skew, 0.001);

    // Test 2: Two-value channel [0, 255, 0, 255]
    memset(data, 0, sizeof(data));
    data[0 * 3] = 0;
    data[1 * 3] = 255;
    data[2 * 3] = 0;
    data[3 * 3] = 255;
    ph_compute_moments(data, 4, 3, all);
    m = all[0];
    ASSERT_FLOAT_EQ(127.5, m.mean, 0.001);
    ASSERT_FLOAT_EQ(127.5, m.std_dev, 0.001);
    ASSERT_FLOAT_EQ(0.0, m.skew, 0.001);

    // Test 3: Asymmetric channel [0, 0, 0, 255]
    memset(data, 0, sizeof(data));
    data[3 * 3] = 255;
    ph_compute_moments(data, 4, 3, all);
    m = all[0];
    ASSERT_FLOAT_EQ(63.75, m.mean, 0.001);
    ASSERT_MSG(m.skew > 0, "skew of [0,0,0,255] is %f, expected positive (outlier on the right)",
               m.skew);

    // Test 4: RGBA input -- the channel stride is 4 and alpha takes no part.
    // Red over [red, green, blue] pixels: (255 + 0 + 0) / 3 = 85.
    uint8_t rgba[12] = {255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255};
    ph_compute_moments(rgba, 3, 4, all);
    m = all[0];
    ASSERT_FLOAT_EQ(85.0, m.mean, 0.001);

    PASS("test_moments_unit");
}

/* photo_color_changed.jpeg is photo.jpeg with its colours shifted and its structure
 * untouched: the grey-level hashes must call it the same image and the colour moments a
 * different one. Observed (stb_image and libjpeg-turbo): 0-2 bits for the four grey
 * hashes, and a colour-moments L2 of ~62 against 0 for the image with itself. The
 * bounds keep a wide margin either side: 4 bits is well inside "same image", and 30 is
 * half the observed colour distance. */
void test_structure_kept_colour_changed() {
    ph_context_t *orig = NULL, *shifted = NULL;
    ASSERT_OK(ph_create(&orig));
    ASSERT_OK(ph_create(&shifted));
    ASSERT_OK(ph_load_from_file(orig, TEST_DATA_DIR "/photo.jpeg"));
    ASSERT_OK(ph_load_from_file(shifted, TEST_DATA_DIR "/photo_color_changed.jpeg"));

    static const char *const names[] = {"aHash", "dHash", "pHash", "wHash"};
    ph_error_t (*const fns[])(ph_context_t *, uint64_t *) = {ph_compute_ahash, ph_compute_dhash,
                                                             ph_compute_phash, ph_compute_whash};
    for (int i = 0; i < 4; i++) {
        uint64_t a = 0, b = 0;
        ASSERT_OK(fns[i](orig, &a));
        ASSERT_OK(fns[i](shifted, &b));
        int dist = ph_hamming_distance(a, b);
        ASSERT_MSG(dist <= 4, "%s: distance %d bits on a colour-only change, bound 4", names[i],
                   dist);
    }

    ph_digest_t da, db;
    ASSERT_OK(ph_compute_color_moments_hash(orig, &da));
    ASSERT_OK(ph_compute_color_moments_hash(shifted, &db));
    double colour = ph_l2_distance(&da, &db);
    ASSERT_MSG(colour > 30.0, "colour moments L2 %.2f on a colour change, bound > 30", colour);

    ph_free(orig);
    ph_free(shifted);
    PASS("test_structure_kept_colour_changed");
}

void test_color_moments_e2e() {
    ph_context_t *ctx = NULL;
    ph_digest_t digest1, digest2;

    ASSERT_OK(ph_create(&ctx));

    // Identical images
    ASSERT_OK(ph_load_from_file(ctx, TEST_DATA_DIR "/photo.jpeg"));
    ASSERT_OK(ph_compute_color_moments_hash(ctx, &digest1));
    ASSERT_INT_EQ(PH_COLOR_MOMENTS_DIGEST_BYTES, digest1.size);
    ASSERT_INT_EQ((uint8_t)PH_DIGEST_KIND_VECTOR16, digest1.kind);

    ASSERT_OK(ph_load_from_file(ctx, TEST_DATA_DIR "/photo_copy.jpeg"));
    ASSERT_OK(ph_compute_color_moments_hash(ctx, &digest2));

    /* Colour moments are nine real-valued features, not a bit vector: the digest is
     * tagged PH_DIGEST_KIND_VECTOR16 -- two bytes a feature, signed -- and
     * Hamming distance over it is refused. L2 is the metric, and it is 0 for an identical
     * image. */
    ASSERT_INT_EQ(-1, ph_hamming_distance_digest(&digest1, &digest2));
    ASSERT_FLOAT_EQ(0.0, ph_l2_distance(&digest1, &digest2), 1e-9);

    ph_free(ctx);
    PASS("test_color_moments_e2e");
}

/* With a grayscale image all three "color" moments would come out of the same byte.
 * Refuse, and leave the caller's digest untouched. */
void test_color_moments_requires_color() {
    ph_context_t *ctx = NULL;
    ph_digest_t digest;

    memset(&digest, 0xAB, sizeof(digest));

    ASSERT_OK(ph_create(&ctx));
    ASSERT_OK(ph_context_set_load_grayscale(ctx, 1));
    ASSERT_OK(ph_load_from_file(ctx, TEST_DATA_DIR "/photo.jpeg"));

    ASSERT_INT_EQ(PH_ERR_REQUIRES_COLOR, ph_compute_color_moments_hash(ctx, &digest));

    /* Not a single byte of the digest was written. */
    for (size_t i = 0; i < sizeof(digest); i++) {
        ASSERT_UINT8_EQ(0xAB, ((const uint8_t *)&digest)[i]);
    }

    ph_free(ctx);
    PASS("test_color_moments_requires_color");
}

/* The moments of one channel, one channel at a time: a double sum of the bytes for the
 * mean, then one pass adding the squared and cubed deviations. ph_compute_moments() takes
 * all three channels together and sums the mean in integers; the contract is that each of
 * its nine doubles is this one bit for bit, so they are compared with memcmp(). */
static ph_channel_moments_t moments_one_channel(const uint8_t *data, size_t n, int ch, int c) {
    ph_channel_moments_t m = {0, 0, 0};
    for (size_t i = 0; i < n; i++) {
        m.mean += data[i * (size_t)ch + (size_t)c];
    }
    m.mean /= (double)n;
    for (size_t i = 0; i < n; i++) {
        double diff = data[i * (size_t)ch + (size_t)c] - m.mean;
        m.std_dev += diff * diff;
        m.skew += diff * diff * diff;
    }
    m.std_dev = sqrt(m.std_dev / (double)n);
    m.skew = cbrt(m.skew / (double)n);
    return m;
}

void test_moments_match_one_channel_at_a_time() {
    static const size_t counts[] = {1, 2, 3, 7, 64, 1000, 65537, 1 << 20};
    static const int channels[] = {3, 4};
    ph_test_rng_t rng = ph_test_rng(0xC0102u);
    for (unsigned ni = 0; ni < sizeof(counts) / sizeof(counts[0]); ni++) {
        for (unsigned ci = 0; ci < sizeof(channels) / sizeof(channels[0]); ci++) {
            for (int pattern = 0; pattern < 4; pattern++) {
                const size_t n = counts[ni];
                const int ch = channels[ci];
                uint8_t *px = (uint8_t *)malloc(n * (size_t)ch);
                ASSERT_PTR_NOT_NULL(px);
                for (size_t i = 0; i < n * (size_t)ch; i++) {
                    const size_t p = i / (size_t)ch;
                    px[i] = pattern == 0   ? ph_test_rng_byte(&rng)
                            : pattern == 1 ? (uint8_t)((p % 2) ? 255 : 0)
                            : pattern == 2 ? (uint8_t)200
                                           : (uint8_t)(p % 97 == 0 ? 255 : (p * 7) % 31);
                }
                ph_channel_moments_t got[PH_COLOR_CHANNELS];
                ph_compute_moments(px, n, ch, got);
                for (int c = 0; c < PH_COLOR_CHANNELS; c++) {
                    const ph_channel_moments_t want = moments_one_channel(px, n, ch, c);
                    ASSERT_MSG(memcmp(&got[c].mean, &want.mean, sizeof(double)) == 0 &&
                                   memcmp(&got[c].std_dev, &want.std_dev, sizeof(double)) == 0 &&
                                   memcmp(&got[c].skew, &want.skew, sizeof(double)) == 0,
                               "n=%zu ch=%d pattern %d channel %d: got (%.17g, %.17g, %.17g), "
                               "one channel at a time (%.17g, %.17g, %.17g)",
                               n, ch, pattern, c, got[c].mean, got[c].std_dev, got[c].skew,
                               want.mean, want.std_dev, want.skew);
                }
                free(px);
            }
        }
    }

    /* Fewer than three channels, no pixels or no data: all zero. */
    uint8_t px4[4] = {1, 2, 3, 4};

    const struct {
        const uint8_t *data;
        size_t n;
        int ch;
    } refused[] = {{px4, 2, 2}, {px4, 0, 3}, {NULL, 1, 3}};

    for (unsigned r = 0; r < sizeof(refused) / sizeof(refused[0]); r++) {
        ph_channel_moments_t z[PH_COLOR_CHANNELS];
        memset(z, 0xFF, sizeof(z));
        ph_compute_moments(refused[r].data, refused[r].n, refused[r].ch, z);
        for (int c = 0; c < PH_COLOR_CHANNELS; c++) {
            ASSERT(z[c].mean == 0.0 && z[c].std_dev == 0.0 && z[c].skew == 0.0);
        }
    }
    PASS("test_moments_match_one_channel_at_a_time");
}

int main() {
    test_moments_unit();
    test_moments_match_one_channel_at_a_time();
    test_color_moments_e2e();
    test_structure_kept_colour_changed();
    test_color_moments_requires_color();
    return 0;
}
