#include "libphash.h"
#include "loader.h"
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

// Branches on the runtime capability check rather than the PH_USE_WEBP compile-time
// macro: under CMake, that macro is a PRIVATE define on the `phash` target and isn't
// visible here, so it wouldn't reliably reflect how the library itself was built.
void test_webp_loading_or_unavailable() {
    ph_context_t *ctx = NULL;
    ASSERT_OK(ph_create(&ctx));

    ph_error_t err = ph_load_from_file(ctx, TEST_DATA_DIR "/photo.webp");

    if (ph_can_use_webp()) {
        ASSERT_INT_EQ(PH_SUCCESS, err);
        int w, h, ch;
        ph_context_get_dimensions(ctx, &w, &h, &ch);
        printf("WebP Loader stats: w=%d, h=%d, ch=%d, webp_active=1\n", w, h, ch);
        ASSERT_INT_EQ(3, ch); // WebP decodes to RGB by default in our implementation
    } else {
        // A real WebP file is recognized by its RIFF/WEBP magic, but with no WebP
        // decoder compiled in (and stb_image having no WebP support to fall back
        // to), this must be reported as "decoder unavailable", not a generic or
        // corrupt-data failure.
        ASSERT_INT_EQ(PH_ERR_DECODER_UNAVAILABLE, err);
        ASSERT(strlen(ph_get_last_error_message(ctx)) > 0);
    }

    ph_free(ctx);
    printf("test_webp_loading_or_unavailable: PASSED\n");
}

void test_corrupted_loading() {
    ph_context_t *ctx = NULL;
    ASSERT_OK(ph_create(&ctx));

    // Loading a truncated/garbled-but-recognizable file must be reported as
    // corrupt data specifically, not a generic decode failure, and should leave
    // a non-empty diagnostic message behind.
    ph_error_t err = ph_load_from_file(ctx, TEST_DATA_DIR "/corrupted.jpg");
    ASSERT_INT_EQ(PH_ERR_CORRUPT_DATA, err);
    ASSERT(strlen(ph_get_last_error_message(ctx)) > 0);

    // A missing file is an I/O problem, distinct from a decode failure.
    err = ph_load_from_file(ctx, TEST_DATA_DIR "/non_existent.jpg");
    ASSERT_INT_EQ(PH_ERR_IO, err);
    ASSERT(strlen(ph_get_last_error_message(ctx)) > 0);

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

void test_loader_edge_cases() {
    ph_context_t *ctx = NULL;
    ASSERT_OK(ph_create(&ctx));

    // 1. NULL/Empty buffer
    ph_error_t err = ph_load_from_memory(ctx, NULL, 0);
    if (err == PH_SUCCESS) {
        fprintf(stderr, "[FAIL] test_loader_edge_cases - NULL buffer should fail\n");
        exit(1);
    }

    err = ph_load_from_memory(ctx, (const uint8_t *)"not empty", 0);
    if (err == PH_SUCCESS) {
        fprintf(stderr, "[FAIL] test_loader_edge_cases - Zero length should fail\n");
        exit(1);
    }

    // 2. Unknown format (magic not matching any backend, and not recognized by
    // stb_image's fallback either) must report PH_ERR_UNSUPPORTED_FORMAT.
    uint8_t garbage[10] = "garbage!!!";
    err = ph_load_from_memory(ctx, garbage, 10);
    ASSERT_INT_EQ(PH_ERR_UNSUPPORTED_FORMAT, err);

    // 3. ph_free_image(NULL) check (internal call coverage)
    ph_free_image(NULL);

    ph_free(ctx);
    printf("test_loader_edge_cases: PASSED\n");
}

int main() {
    test_jpeg_loading();
    test_png_loading();
    test_webp_loading_or_unavailable();
    test_corrupted_loading();
    test_grayscale_loading();
    test_memory_loading();
    test_loader_edge_cases();
    return 0;
}
