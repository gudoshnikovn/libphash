#include "internal.h"
#include "libphash.h"
#include "test_macros.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

void test_moments_unit() {
    uint8_t data[4 * 3]; // 4 pixels, RGB
    ph_channel_moments_t m;

    // Test 1: Uniform channel (all R=128)
    memset(data, 0, sizeof(data));
    for (int i = 0; i < 4; i++)
        data[i * 3] = 128;
    m = ph_compute_moments(data, 4, 3, 0);
    ASSERT_FLOAT_EQ(128.0, m.mean, 0.001);
    ASSERT_FLOAT_EQ(0.0, m.std_dev, 0.001);
    ASSERT_FLOAT_EQ(0.0, m.skew, 0.001);

    // Test 2: Two-value channel [0, 255, 0, 255]
    memset(data, 0, sizeof(data));
    data[0 * 3] = 0;
    data[1 * 3] = 255;
    data[2 * 3] = 0;
    data[3 * 3] = 255;
    m = ph_compute_moments(data, 4, 3, 0);
    ASSERT_FLOAT_EQ(127.5, m.mean, 0.001);
    ASSERT_FLOAT_EQ(127.5, m.std_dev, 0.001);
    ASSERT_FLOAT_EQ(0.0, m.skew, 0.001);

    // Test 3: Asymmetric channel [0, 0, 0, 255]
    memset(data, 0, sizeof(data));
    data[3 * 3] = 255;
    m = ph_compute_moments(data, 4, 3, 0);
    ASSERT_FLOAT_EQ(63.75, m.mean, 0.001);
    // skew should be positive (outlier on the right)
    if (m.skew <= 0) {
        fprintf(stderr, "Expected positive skew for [0,0,0,255], got %f\n", m.skew);
        exit(1);
    }

    PASS("test_moments_unit");
}

void test_color_moments_e2e() {
    ph_context_t *ctx = NULL;
    ph_digest_t digest1, digest2;

    ASSERT_OK(ph_create(&ctx));

    // Identical images
    ASSERT_OK(ph_load_from_file(ctx, TEST_DATA_DIR "/photo.jpeg"));
    ASSERT_OK(ph_compute_color_moments_hash(ctx, &digest1));
    ASSERT_INT_EQ(9, digest1.size);

    ASSERT_OK(ph_load_from_file(ctx, TEST_DATA_DIR "/photo_copy.jpeg"));
    ASSERT_OK(ph_compute_color_moments_hash(ctx, &digest2));

    ASSERT_INT_EQ(0, ph_hamming_distance_digest(&digest1, &digest2));

    ph_free(ctx);
    PASS("test_color_moments_e2e");
}

/* R08: with a grayscale image all three "color" moments used to come out of the same
 * byte -- three identical channels reported as PH_SUCCESS. Refuse, and leave the
 * caller's digest untouched. */
void test_color_moments_requires_color() {
    ph_context_t *ctx = NULL;
    ph_digest_t digest;

    memset(&digest, 0xAB, sizeof(digest));

    ASSERT_OK(ph_create(&ctx));
    ASSERT_OK(ph_context_set_load_grayscale(ctx, 1));
    ASSERT_OK(ph_load_from_file(ctx, TEST_DATA_DIR "/photo.jpeg"));

    ASSERT_INT_EQ(PH_ERR_REQUIRES_COLOR, ph_compute_color_moments_hash(ctx, &digest));

    /* Not a single byte of the digest was written. */
    for (size_t i = 0; i < sizeof(digest); i++)
        ASSERT_UINT8_EQ(0xAB, ((const uint8_t *)&digest)[i]);

    ph_free(ctx);
    PASS("test_color_moments_requires_color");
}

int main() {
    test_moments_unit();
    test_color_moments_e2e();
    test_color_moments_requires_color();
    return 0;
}
