#include "libphash.h"
#include "test_macros.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void test_jpeg_loading() {
    ph_context_t *ctx = NULL;
    ASSERT_OK(ph_create(&ctx));

    // Test loading valid JPEG
    ASSERT_OK(ph_load_from_file(ctx, TEST_DATA_DIR "/photo.jpeg"));
    ASSERT_PTR_NOT_NULL(ctx);
    ASSERT_INT_EQ(1, ph_is_loaded(ctx));

    int w, h, ch;
    ph_context_get_dimensions(ctx, &w, &h, &ch);
    printf("JPEG Loader stats: w=%d, h=%d, ch=%d, turbo_active=%d\n", w, h, ch,
           ph_can_use_libjpeg());

    ph_free(ctx);
    printf("test_jpeg_loading: PASSED\n");
}

void test_png_loading() {
    ph_context_t *ctx = NULL;
    ASSERT_OK(ph_create(&ctx));

    // Test loading valid PNG (newly created)
    ASSERT_OK(ph_load_from_file(ctx, TEST_DATA_DIR "/photo.png"));
    ASSERT_PTR_NOT_NULL(ctx);

    int w, h, ch;
    ph_context_get_dimensions(ctx, &w, &h, &ch);
    ASSERT_INT_EQ(100, w);
    ASSERT_INT_EQ(100, h);

    printf("PNG Loader stats: w=%d, h=%d, ch=%d, png_active=%d\n", w, h, ch, ph_can_use_libpng());

    ph_free(ctx);
    printf("test_png_loading: PASSED\n");
}

#ifdef PH_USE_WEBP
void test_webp_loading() {
    ph_context_t *ctx = NULL;
    ASSERT_OK(ph_create(&ctx));

    // Test loading valid WebP
    ASSERT_OK(ph_load_from_file(ctx, TEST_DATA_DIR "/photo.webp"));
    ASSERT_PTR_NOT_NULL(ctx);

    int w, h, ch;
    ph_context_get_dimensions(ctx, &w, &h, &ch);
    printf("WebP Loader stats: w=%d, h=%d, ch=%d, webp_active=%d\n", w, h, ch, ph_can_use_webp());
    ASSERT_INT_EQ(3, ch); // WebP decodes to RGB by default in our implementation

    ph_free(ctx);
    printf("test_webp_loading: PASSED\n");
}
#endif

void test_corrupted_loading() {
    ph_context_t *ctx = NULL;
    ASSERT_OK(ph_create(&ctx));

    // Loading corrupted file should return error
    ph_error_t err = ph_load_from_file(ctx, TEST_DATA_DIR "/corrupted.jpg");
    if (err == PH_SUCCESS) {
        fprintf(stderr,
                "[FAIL] test_corrupted_loading - Should have failed to load corrupted.jpg\n");
        exit(1);
    }

    // Loading non-existent file
    err = ph_load_from_file(ctx, TEST_DATA_DIR "/non_existent.jpg");
    if (err == PH_SUCCESS) {
        fprintf(stderr,
                "[FAIL] test_corrupted_loading - Should have failed to load non_existent.jpg\n");
        exit(1);
    }

    ph_free(ctx);
    printf("test_corrupted_loading: PASSED\n");
}

void test_grayscale_loading() {
    ph_context_t *ctx = NULL;
    ASSERT_OK(ph_create(&ctx));

    // Enable grayscale loading
    ph_context_set_load_grayscale(ctx, 1);

    // Load JPEG
    ASSERT_OK(ph_load_from_file(ctx, TEST_DATA_DIR "/photo.jpeg"));
    int w, h, ch;
    ph_context_get_dimensions(ctx, &w, &h, &ch);
    ASSERT_INT_EQ(1, ch); // Should be 1 despite image being RGB

    // Load PNG
    ASSERT_OK(ph_load_from_file(ctx, TEST_DATA_DIR "/photo.png"));
    ph_context_get_dimensions(ctx, &w, &h, &ch);
    ASSERT_INT_EQ(1, ch);

    ph_free(ctx);
    printf("test_grayscale_loading: PASSED\n");
}

void test_memory_loading() {
    ph_context_t *ctx = NULL;
    ASSERT_OK(ph_create(&ctx));

    // Read photo.png into memory manually
    FILE *f = fopen(TEST_DATA_DIR "/photo.png", "rb");
    ASSERT_PTR_NOT_NULL(f);
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    unsigned char *buf = malloc(size);
    fread(buf, 1, size, f);
    fclose(f);

    // Load from memory
    ASSERT_OK(ph_load_from_memory(ctx, buf, size));
    int w, h, ch;
    ph_context_get_dimensions(ctx, &w, &h, &ch);
    ASSERT_INT_EQ(100, w);
    ASSERT_INT_EQ(100, h);

    free(buf);
    ph_free(ctx);
    printf("test_memory_loading: PASSED\n");
}

int main() {
    test_jpeg_loading();
    test_png_loading();
#ifdef PH_USE_WEBP
    test_webp_loading();
#endif
    test_corrupted_loading();
    test_grayscale_loading();
    test_memory_loading();
    return 0;
}
