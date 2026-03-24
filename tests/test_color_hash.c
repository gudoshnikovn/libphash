#include "libphash.h"
#include "test_macros.h"
#include "../src/internal.h"
#include <stdio.h>
#include <string.h>

void test_hsv_classify_unit() {
    ph_hsv_result_t res;

    // Test 1: Black (intensity < 32)
    res = ph_hsv_classify_pixel(5.0f, 5.0f, 5.0f);
    ASSERT_INT_EQ(PH_HSV_BLACK, res.category);

    // Test 2: Gray (s < 85)
    res = ph_hsv_classify_pixel(200.0f, 200.0f, 200.0f);
    ASSERT_INT_EQ(PH_HSV_GRAY, res.category);

    // Test 3: Bright Red
    res = ph_hsv_classify_pixel(255.0f, 0.0f, 0.0f);
    ASSERT_INT_EQ(PH_HSV_BRIGHT, res.category);
    ASSERT_INT_EQ(0, res.hue_bin);

    // Test 4: Bright Green
    res = ph_hsv_classify_pixel(0.0f, 255.0f, 0.0f);
    ASSERT_INT_EQ(PH_HSV_BRIGHT, res.category);
    ASSERT_INT_EQ(2, res.hue_bin);

    // Test 5: Blue (must be bright enough to exceed intensity=32 threshold)
    // (60, 60, 255) -> intensity ≈ 82.2 > 32. S = 255*(255-60)/255 = 195 > 170 -> BRIGHT.
    res = ph_hsv_classify_pixel(60.0f, 60.0f, 255.0f);
    ASSERT_INT_EQ(PH_HSV_BRIGHT, res.category);
    ASSERT_INT_EQ(4, res.hue_bin);

    // Test 6: Faint color (85 <= s < 170)
    res = ph_hsv_classify_pixel(200.0f, 120.0f, 120.0f);
    ASSERT_INT_EQ(PH_HSV_FAINT, res.category);
    ASSERT_INT_EQ(0, res.hue_bin);

    PASS("test_hsv_classify_unit");
}

void test_pack_3bit_unit() {
    double values[14] = {0};

    // Test 1: All zeros
    uint64_t hash = ph_pack_3bit_values(values, 14);
    ASSERT_UINT64_EQ(0, hash);

    // Test 2: All ones (clamped to 7)
    for (int i = 0; i < 14; i++)
        values[i] = 1.0;
    hash = ph_pack_3bit_values(values, 14);
    // 14 groups * 3 bits = 42 bits total. Each value is 7 (0b111).
    ASSERT_UINT64_EQ(0x3FFFFFFFFFFULL, hash);

    // Test 3: Only first value non-zero (0.5 -> v=4, 0b100)
    memset(values, 0, sizeof(values));
    values[0] = 0.5;
    hash = ph_pack_3bit_values(values, 14);
    // First 3 bits at index 41, 40, 39 are 1, 0, 0. bit 41 set.
    ASSERT_UINT64_EQ(1ULL << 41, hash);

    PASS("test_pack_3bit_unit");
}

void test_color_hash_e2e() {
    ph_context_t *ctx = NULL;
    uint64_t hash1, hash2;

    ASSERT_OK(ph_create(&ctx));

    // Identical images
    ASSERT_OK(ph_load_from_file(ctx, "tests/photo.jpeg"));
    ASSERT_OK(ph_compute_color_hash(ctx, &hash1));

    ASSERT_OK(ph_load_from_file(ctx, "tests/photo_copy.jpeg"));
    ASSERT_OK(ph_compute_color_hash(ctx, &hash2));
    ASSERT_UINT64_EQ(hash1, hash2);

    ph_free(ctx);
    PASS("test_color_hash_e2e");
}

int main() {
    test_hsv_classify_unit();
    test_pack_3bit_unit();
    test_color_hash_e2e();
    return 0;
}
