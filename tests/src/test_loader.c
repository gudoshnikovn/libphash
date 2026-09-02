#include "internal.h"
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

// Task 14: stb_image is now a registered last-resort backend in ph_decode_buffer
// (src/loader.c), giving BMP/GIF/TGA/PSD/HDR/PIC/PNM support for free. These are
// hand-crafted minimal fixtures (not committed binary files) so the test doesn't
// depend on any external tool to regenerate them.

// Minimal valid 2x2 24-bit BMP, BITMAPINFOHEADER, bottom-up (positive height).
static const uint8_t bmp_bottomup[] = {
    0x42, 0x4d, 0x46, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x36, 0x00, 0x00, 0x00,
    0x28, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x01, 0x00,
    0x18, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0x00,
    0x00, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0x00, 0x00, 0xff, 0x00, 0x00, 0x00};

// Same image, but with a negative height in the DIB header -- a legitimate
// top-down BMP encoding. Regression fixture for a bug found while implementing
// this task: ph_load_from_file()/ph_load_from_memory() cast stbi_info's signed
// height straight to uint64_t for the max_pixels pre-check, so a negative
// height wrapped to a huge value and every top-down BMP was rejected as
// PH_ERR_IMAGE_TOO_LARGE regardless of its actual size.
static const uint8_t bmp_topdown[] = {
    0x42, 0x4d, 0x46, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x36, 0x00, 0x00, 0x00,
    0x28, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0xfe, 0xff, 0xff, 0xff, 0x01, 0x00,
    0x18, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0x00,
    0x00, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0x00, 0x00, 0xff, 0x00, 0x00, 0x00};

// Minimal valid 1x1 GIF89a (single static frame, no animation).
static const uint8_t mini_gif[] = {0x47, 0x49, 0x46, 0x38, 0x39, 0x61, 0x01, 0x00, 0x01, 0x00, 0x80,
                                   0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0x21, 0xf9, 0x04,
                                   0x01, 0x00, 0x00, 0x00, 0x00, 0x2c, 0x00, 0x00, 0x00, 0x00, 0x01,
                                   0x00, 0x01, 0x00, 0x00, 0x02, 0x02, 0x44, 0x01, 0x00, 0x3b};

void test_stb_fallback_formats() {
    ph_context_t *ctx = NULL;
    ASSERT_OK(ph_create(&ctx));

    ASSERT_OK(ph_load_from_memory(ctx, bmp_bottomup, sizeof(bmp_bottomup)));
    int w, h, ch;
    ph_context_get_dimensions(ctx, &w, &h, &ch);
    ASSERT_INT_EQ(2, w);
    ASSERT_INT_EQ(2, h);

    ASSERT_OK(ph_load_from_memory(ctx, bmp_topdown, sizeof(bmp_topdown)));
    ph_context_get_dimensions(ctx, &w, &h, &ch);
    ASSERT_INT_EQ(2, w);
    ASSERT_INT_EQ(2, h);

    ASSERT_OK(ph_load_from_memory(ctx, mini_gif, sizeof(mini_gif)));
    ph_context_get_dimensions(ctx, &w, &h, &ch);
    ASSERT_INT_EQ(1, w);
    ASSERT_INT_EQ(1, h);

    ph_free(ctx);
    printf("test_stb_fallback_formats: PASSED\n");
}

void test_bmp_negative_height_not_too_large() {
    ph_context_t *ctx = NULL;
    ASSERT_OK(ph_create(&ctx));

    // Default max_pixels (256 Mpix) must not reject a 2x2 top-down BMP just
    // because its header height is negative.
    ph_error_t err = ph_load_from_memory(ctx, bmp_topdown, sizeof(bmp_topdown));
    ASSERT_INT_EQ(PH_SUCCESS, err);

    // Same fixture via ph_load_from_file(), which since 2.0.0 reaches the very
    // same pre-check through the shared decode path.
    const char *tmp_path = "/tmp/libphash_test_topdown.bmp";
    FILE *f = fopen(tmp_path, "wb");
    if (f) {
        fwrite(bmp_topdown, 1, sizeof(bmp_topdown), f);
        fclose(f);
        err = ph_load_from_file(ctx, tmp_path);
        ASSERT_INT_EQ(PH_SUCCESS, err);
        remove(tmp_path);
    }

    ph_free(ctx);
    printf("test_bmp_negative_height_not_too_large: PASSED\n");
}

// The per-dimension cap is not a PNG matter: any format can declare an absurd aspect
// ratio that slips under the area limit and still asks the decoder for a single
// enormous row. BMP is the case reachable in every build -- it has no native backend,
// so it always goes through stb_image, which is exactly the path that used to have no
// dimension cap at all.
static void patch_bmp_dimensions(uint8_t *hdr, int32_t w, int32_t h) {
    for (int i = 0; i < 4; i++) {
        hdr[18 + i] = (uint8_t)(((uint32_t)w >> (8 * i)) & 0xff);
        hdr[22 + i] = (uint8_t)(((uint32_t)h >> (8 * i)) & 0xff);
    }
}

void test_bmp_extreme_aspect_ratio_rejected() {
    uint8_t bmp[sizeof(bmp_bottomup)];
    ph_context_t *ctx = NULL;
    ASSERT_OK(ph_create(&ctx));

    // 2000000 x 10 = 20 Mpix, comfortably inside the default 256 Mpix area limit,
    // yet twice the per-dimension cap.
    memcpy(bmp, bmp_bottomup, sizeof(bmp));
    patch_bmp_dimensions(bmp, 2000000, 10);
    ASSERT_INT_EQ(PH_ERR_IMAGE_TOO_LARGE, ph_load_from_memory(ctx, bmp, sizeof(bmp)));
    ASSERT_INT_EQ(0, ph_is_loaded(ctx));

    // Tall variant of the same shape.
    memcpy(bmp, bmp_bottomup, sizeof(bmp));
    patch_bmp_dimensions(bmp, 10, 2000000);
    ASSERT_INT_EQ(PH_ERR_IMAGE_TOO_LARGE, ph_load_from_memory(ctx, bmp, sizeof(bmp)));

    // Disabling the area limit must not lift the dimension cap with it.
    ph_context_set_max_pixels(ctx, 0);
    memcpy(bmp, bmp_bottomup, sizeof(bmp));
    patch_bmp_dimensions(bmp, 2000000, 10);
    ASSERT_INT_EQ(PH_ERR_IMAGE_TOO_LARGE, ph_load_from_memory(ctx, bmp, sizeof(bmp)));

    // A top-down BMP declares a negative height; its magnitude is what the cap is
    // applied to, so this must be rejected for its size and not for its sign.
    memcpy(bmp, bmp_bottomup, sizeof(bmp));
    patch_bmp_dimensions(bmp, 10, -2000000);
    ASSERT_INT_EQ(PH_ERR_IMAGE_TOO_LARGE, ph_load_from_memory(ctx, bmp, sizeof(bmp)));

    // Exactly at the cap is allowed through the size check; the load still fails,
    // on the truncated pixel data, which is a different and honest complaint.
    memcpy(bmp, bmp_bottomup, sizeof(bmp));
    patch_bmp_dimensions(bmp, 1000000, 1);
    ASSERT(ph_load_from_memory(ctx, bmp, sizeof(bmp)) != PH_ERR_IMAGE_TOO_LARGE);

    ph_free(ctx);
    printf("test_bmp_extreme_aspect_ratio_rejected: PASSED\n");
}

static unsigned char *read_whole_file(const char *path, size_t *out_size) {
    FILE *f = fopen(path, "rb");
    ASSERT_PTR_NOT_NULL(f);
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    ASSERT(size > 0);
    unsigned char *buf = (unsigned char *)malloc((size_t)size);
    ASSERT_PTR_NOT_NULL(buf);
    ASSERT(fread(buf, 1, (size_t)size, f) == (size_t)size);
    fclose(f);
    *out_size = (size_t)size;
    return buf;
}

// The PNG decoder is one of two interchangeable implementations (libpng or spng),
// chosen at configure time, and only one of them is ever linked into a given build.
// A cross-backend comparison therefore cannot be made inside a single binary; what
// this test does instead is pin BOTH backends to the same reference, which makes
// them equal to each other by construction. The reference is the library's own
// ph_to_grayscale() applied to the RGB decode of the same file.
//
// The regression this guards: the spng backend asked spng for SPNG_FMT_G8
// unconditionally, but spng only accepts that format for a color-type-0 PNG. For
// an ordinary truecolor file it answered SPNG_EFMT and the decode failed outright,
// so ph_context_set_load_grayscale(ctx, 1) turned every valid PNG into
// PH_ERR_CORRUPT_DATA -- while the libpng backend decoded the very same file.
static void check_png_backend_parity(const char *path) {
    size_t size = 0;
    unsigned char *buf = read_whole_file(path, &size);

    int rw = 0, rh = 0, rc = 0;
    ph_error_t err = PH_SUCCESS;
    uint8_t *rgb = ph_decode_buffer(buf, size, &rw, &rh, &rc, 0, 0, &err, NULL, 0);
    ASSERT_PTR_NOT_NULL(rgb);
    ASSERT_INT_EQ(PH_SUCCESS, err);
    ASSERT_INT_EQ(3, rc);

    int gw = 0, gh = 0, gc = 0;
    err = PH_SUCCESS;
    uint8_t *gray = ph_decode_buffer(buf, size, &gw, &gh, &gc, 1, 0, &err, NULL, 0);
    ASSERT_PTR_NOT_NULL(gray);
    ASSERT_INT_EQ(PH_SUCCESS, err);

    // Same geometry, and exactly the channel count that was asked for.
    ASSERT_INT_EQ(rw, gw);
    ASSERT_INT_EQ(rh, gh);
    ASSERT_INT_EQ(1, gc);

    if (ph_can_use_libpng()) {
        // Both native backends must reproduce the library's own conversion
        // byte for byte. (stb_image, the fallback backend, converts with its
        // own coefficients, so this exactness is only required of libpng/spng.)
        size_t num_pixels = (size_t)rw * (size_t)rh;
        uint8_t *reference = (uint8_t *)malloc(num_pixels);
        ASSERT_PTR_NOT_NULL(reference);
        ph_to_grayscale(NULL, rgb, rw, rh, rc, reference);
        for (size_t i = 0; i < num_pixels; i++) {
            if (gray[i] != reference[i]) {
                fprintf(stderr,
                        "[FAIL] %s: decoder grayscale differs from ph_to_grayscale at "
                        "pixel %zu (%d vs %d)\n",
                        path, i, (int)gray[i], (int)reference[i]);
                exit(1);
            }
        }
        free(reference);
    }

    ph_free_image(rgb);
    ph_free_image(gray);
    free(buf);
}

// Consequence of the byte-level parity above, checked at the level a user sees:
// a hash taken from the decoder's grayscale output equals the hash taken from the
// RGB decode, so switching PNG backends cannot move a stored hash.
static void check_png_gray_hash_parity(const char *path) {
    if (!ph_can_use_libpng())
        return; // stb_image uses different conversion coefficients; see above.

    ph_context_t *rgb_ctx = NULL;
    ph_context_t *gray_ctx = NULL;
    ASSERT_OK(ph_create(&rgb_ctx));
    ASSERT_OK(ph_create(&gray_ctx));
    ph_context_set_load_grayscale(rgb_ctx, 0);
    ph_context_set_load_grayscale(gray_ctx, 1);
    ASSERT_OK(ph_load_from_file(rgb_ctx, path));
    ASSERT_OK(ph_load_from_file(gray_ctx, path));

    uint64_t a_rgb = 0, a_gray = 0, d_rgb = 0, d_gray = 0, p_rgb = 0, p_gray = 0;
    ASSERT_OK(ph_compute_ahash(rgb_ctx, &a_rgb));
    ASSERT_OK(ph_compute_ahash(gray_ctx, &a_gray));
    ASSERT_OK(ph_compute_dhash(rgb_ctx, &d_rgb));
    ASSERT_OK(ph_compute_dhash(gray_ctx, &d_gray));
    ASSERT_OK(ph_compute_phash(rgb_ctx, &p_rgb));
    ASSERT_OK(ph_compute_phash(gray_ctx, &p_gray));

    ASSERT_INT_EQ(0, ph_hamming_distance(a_rgb, a_gray));
    ASSERT_INT_EQ(0, ph_hamming_distance(d_rgb, d_gray));
    ASSERT_INT_EQ(0, ph_hamming_distance(p_rgb, p_gray));

    ph_free(rgb_ctx);
    ph_free(gray_ctx);
}

void test_png_grayscale_backend_parity() {
    check_png_backend_parity(TEST_DATA_DIR "/photo.png");
    check_png_backend_parity(TEST_DATA_DIR "/photo_complex.png");
    check_png_gray_hash_parity(TEST_DATA_DIR "/photo.png");
    check_png_gray_hash_parity(TEST_DATA_DIR "/photo_complex.png");
    printf("test_png_grayscale_backend_parity: PASSED\n");
}

// A broken PNG must be reported with the decoder's own reason, not swallowed.
// The spng backend used to discard its return code entirely, which is what made
// the grayscale failure above so hard to read: every cause came out as a bare -8.
void test_png_decode_error_is_reported() {
    size_t size = 0;
    unsigned char *buf = read_whole_file(TEST_DATA_DIR "/photo.png", &size);
    ASSERT(size > 64);

    ph_context_t *ctx = NULL;
    ASSERT_OK(ph_create(&ctx));

    for (int gray = 0; gray <= 1; gray++) {
        ph_context_set_load_grayscale(ctx, gray);
        // Valid signature and IHDR, truncated body: recognized as PNG, undecodable.
        ph_error_t err = ph_load_from_memory(ctx, buf, 40);
        ASSERT_INT_EQ(PH_ERR_CORRUPT_DATA, err);
        ASSERT(strlen(ph_get_last_error_message(ctx)) > 0);
    }

    ph_free(ctx);
    free(buf);
    printf("test_png_decode_error_is_reported: PASSED\n");
}

int main() {
    test_jpeg_loading();
    test_png_loading();
    test_webp_loading_or_unavailable();
    test_corrupted_loading();
    test_grayscale_loading();
    test_png_grayscale_backend_parity();
    test_png_decode_error_is_reported();
    test_memory_loading();
    test_loader_edge_cases();
    test_stb_fallback_formats();
    test_bmp_negative_height_not_too_large();
    test_bmp_extreme_aspect_ratio_rejected();
    return 0;
}
