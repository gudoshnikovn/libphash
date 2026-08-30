#include "libphash.h"
#include "test_macros.h"
#include <stdio.h>
#include <stdlib.h>

/* tests/data/decode_bomb.png is a 45-byte PNG whose IHDR declares a 100000x100000
 * (1e10 pixel) image with no real pixel data behind it — a classic decompression
 * bomb. Loading it must fail fast with PH_ERR_IMAGE_TOO_LARGE, never attempt the
 * multi-gigabyte allocation implied by the header. */
#define BOMB_PATH TEST_DATA_DIR "/decode_bomb.png"
#define NORMAL_PATH TEST_DATA_DIR "/photo.jpeg" /* 400x400 = 160000 pixels */

void test_default_limit_rejects_bomb_from_file() {
    ph_context_t *ctx = NULL;
    ASSERT_OK(ph_create(&ctx));

    ph_error_t err = ph_load_from_file(ctx, BOMB_PATH);
    ASSERT_INT_EQ(PH_ERR_IMAGE_TOO_LARGE, err);
    ASSERT_INT_EQ(0, ph_is_loaded(ctx));

    ph_free(ctx);
    printf("test_default_limit_rejects_bomb_from_file: PASSED\n");
}

void test_default_limit_rejects_bomb_from_memory() {
    FILE *f = fopen(BOMB_PATH, "rb");
    ASSERT_PTR_NOT_NULL(f);
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *buf = malloc((size_t)size);
    ASSERT_PTR_NOT_NULL(buf);
    ASSERT_INT_EQ((int)size, (int)fread(buf, 1, (size_t)size, f));
    fclose(f);

    ph_context_t *ctx = NULL;
    ASSERT_OK(ph_create(&ctx));

    ph_error_t err = ph_load_from_memory(ctx, buf, (size_t)size);
    ASSERT_INT_EQ(PH_ERR_IMAGE_TOO_LARGE, err);
    ASSERT_INT_EQ(0, ph_is_loaded(ctx));

    free(buf);
    ph_free(ctx);
    printf("test_default_limit_rejects_bomb_from_memory: PASSED\n");
}

void test_default_limit_allows_normal_image() {
    ph_context_t *ctx = NULL;
    ASSERT_OK(ph_create(&ctx));
    ASSERT_OK(ph_load_from_file(ctx, NORMAL_PATH));
    ASSERT_INT_EQ(1, ph_is_loaded(ctx));
    ph_free(ctx);
    printf("test_default_limit_allows_normal_image: PASSED\n");
}

void test_custom_lower_limit_rejects_normal_image() {
    ph_context_t *ctx = NULL;
    ASSERT_OK(ph_create(&ctx));

    /* photo.jpeg is 400x400 = 160000 pixels; cap below that. */
    ph_context_set_max_pixels(ctx, 100000);

    ph_error_t err = ph_load_from_file(ctx, NORMAL_PATH);
    ASSERT_INT_EQ(PH_ERR_IMAGE_TOO_LARGE, err);
    ASSERT_INT_EQ(0, ph_is_loaded(ctx));

    ph_free(ctx);
    printf("test_custom_lower_limit_rejects_normal_image: PASSED\n");
}

void test_custom_higher_limit_allows_normal_image() {
    ph_context_t *ctx = NULL;
    ASSERT_OK(ph_create(&ctx));

    ph_context_set_max_pixels(ctx, 200000);
    ASSERT_OK(ph_load_from_file(ctx, NORMAL_PATH));
    ASSERT_INT_EQ(1, ph_is_loaded(ctx));

    ph_free(ctx);
    printf("test_custom_higher_limit_allows_normal_image: PASSED\n");
}

int main() {
    test_default_limit_rejects_bomb_from_file();
    test_default_limit_rejects_bomb_from_memory();
    test_default_limit_allows_normal_image();
    test_custom_lower_limit_rejects_normal_image();
    test_custom_higher_limit_allows_normal_image();
    printf("ALL DECODE LIMIT TESTS PASSED\n");
    return 0;
}
