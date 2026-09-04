#include "internal.h"
#include "libphash.h"
#include "test_macros.h"
#include <stdio.h>
#include <stdlib.h>

/* The HSV classifier and the 3-bit packer that used to live here went with the ImageHash
 * port in 2.0.0; ColorHash is a colour histogram now, and its quantisation is checked in
 * tests/src/test_color_hash.c. What is left in this file is the colour moments. */

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

int main(void) {
    test_color_moments_edges();
    printf("\nAll image color extended tests passed.\n");
    return 0;
}
