#include "libphash.h"
#include "../src/internal.h" // For access to internal fields (gray_data)
#include "test_macros.h"
#include <string.h>

void test_context_reuse_clears_gray_data() {
    ph_context_t *ctx = NULL;
    ASSERT_OK(ph_create(&ctx));

    const char *img_path = "tests/photo.jpeg";

    // 1. Load first image
    ASSERT_OK(ph_load_from_file(ctx, img_path));

    // Force gray allocation
    uint8_t *gray1 = ph_get_gray(ctx);
    ASSERT_PTR_NOT_NULL(gray1);
    
    // Fill with a marker pattern to detect stale data
    size_t len1 = ctx->width * ctx->height;
    memset(gray1, 0xAB, len1);

    // 2. Reload same image (or different one)
    ASSERT_OK(ph_load_from_file(ctx, img_path));

    // 3. Get gray again
    uint8_t *gray2 = ph_get_gray(ctx);
    ASSERT_PTR_NOT_NULL(gray2);

    // 4. Verify content
    // If bug exists, gray2 == gray1 and content is 0xAB.
    // If fixed, content should be overwritten by actual grayscale data.
    // We check if at least one byte is NOT 0xAB (highly likely for a real image).
    
    int is_stale = 1;
    size_t len2 = ctx->width * ctx->height;
    
    // Check a few points or verify entire buffer isn't just 0xAB
    // Since ph_to_grayscale calculates values, it's very unlikely to be 0xAB everywhere.
    if (gray2[0] != 0xAB || gray2[len2 / 2] != 0xAB || gray2[len2 - 1] != 0xAB) {
        is_stale = 0;
    }

    if (is_stale) {
        fprintf(stderr, "[FAIL] Data is stale! ph_to_grayscale was NOT called on reload.\n");
        exit(1);
    }

    printf("test_context_reuse_clears_gray_data: PASSED\n");
    ph_free(ctx);
}

int main() {
    test_context_reuse_clears_gray_data();
    return 0;
}
