#include "internal.h"
#include "libphash.h"
#include "test_macros.h"
#include <stdio.h>
#include <stdlib.h>

void test_hsv_classification_edges(void) {
    ph_hsv_result_t res;

    // Black
    res = ph_hsv_classify_pixel(0, 0, 0);
    if (res.category != PH_HSV_BLACK)
        exit(1);

    // White
    res = ph_hsv_classify_pixel(255, 255, 255);
    if (res.category != PH_HSV_GRAY)
        exit(1); // S=0, so it's Gray

    // Gray
    res = ph_hsv_classify_pixel(128, 128, 128);
    if (res.category != PH_HSV_GRAY)
        exit(1);

    // Faint vs Bright
    // S = 255 * (max-min)/max.
    // If R=255, G=100, B=100 -> max=255, min=100, d=155. S = 255 * 155 / 255 = 155.
    // 155 < 170 -> Faint.
    res = ph_hsv_classify_pixel(255, 100, 100);
    if (res.category != PH_HSV_FAINT)
        exit(1);

    // If R=255, G=50, B=50 -> max=255, min=50, d=205. S = 205.
    // 205 > 170 -> Bright.
    res = ph_hsv_classify_pixel(255, 50, 50);
    if (res.category != PH_HSV_BRIGHT)
        exit(1);

    PASS("test_hsv_classification_edges");
}

void test_color_moments_edges(void) {
    // Test with stride and different channel counts
    uint8_t data[12] = {
        255, 0,   0,   255, // Pixel 1: Red, Alpha=255
        0,   255, 0,   255, // Pixel 2: Green
        0,   0,   255, 255  // Pixel 3: Blue
    };
    ph_channel_moments_t m;

    // Compute for 3 pixels, 4 channels (RGBA), check Red channel (index 0)
    m = ph_compute_moments(data, 3, 4, 0);

    // Mean Red: (255+0+0)/3 = 85
    if (m.mean < 84.0 || m.mean > 86.0)
        exit(1);

    PASS("test_color_moments_edges");
}

void test_pack_3bit_clamping(void) {
    double vals[2] = {1.5, -0.5}; // Should clamp to 1.0 (7) and 0.0 (0)
    uint64_t hash = ph_pack_3bit_values(vals, 2);
    // 3 bits for 0.8+ (clamped to 7) = 111
    // 3 bits for 0.0- (clamped to 0) = 000
    // Total bits = 6.
    // Implementation shifts left from bit 41.
    // i=0: v=7 (111). bits at 41, 40, 39 set.
    // i=1: v=0 (000). bits at 38, 37, 36 unset.
    if (!(hash & (1ULL << 41)))
        exit(1);
    if (!(hash & (1ULL << 40)))
        exit(1);
    if (!(hash & (1ULL << 39)))
        exit(1);
    if (hash & (1ULL << 38))
        exit(1);

    PASS("test_pack_3bit_clamping");
}

int main(void) {
    test_hsv_classification_edges();
    test_color_moments_edges();
    test_pack_3bit_clamping();
    printf("\nAll image color extended tests passed.\n");
    return 0;
}
