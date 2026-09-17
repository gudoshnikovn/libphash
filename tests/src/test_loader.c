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

// The remaining formats CLAUDE.md, README.md and the ph_can_read_stb() comment claim
// stb_image gives us "for free": TGA, PNM/PPM (P5 and P6), HDR, PSD, PIC. None of these
// had any test coverage before -- each fixture below was hand-built against the exact
// parsing stb_image.h does for that format (stbi__tga_info/stbi__tga_test,
// stbi__hdr_load, stbi__psd_load, stbi__pic_load_core) and independently confirmed to
// decode with the vendored stb_image.h before being pasted in here.

// Minimal 2x2 uncompressed 24bpp TGA (image type 2, no colormap, no RLE). BGR pixel
// order, origin bottom-left (the TGA default -- image descriptor byte is 0).
static const uint8_t tga_2x2[] = {0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                                  0x00, 0x00, 0x02, 0x00, 0x02, 0x00, 0x18, 0x00, 0x00, 0x00,
                                  0xff, 0x00, 0xff, 0x00, 0xff, 0x00, 0x00, 0xff, 0xff, 0xff};

// Minimal 2x2 binary PGM (P5, grayscale): "P5\n2 2\n255\n" + 4 raw gray bytes.
static const uint8_t pgm_p5_2x2[] = {0x50, 0x35, 0x0a, 0x32, 0x20, 0x32, 0x0a, 0x32,
                                     0x35, 0x35, 0x0a, 0x00, 0x40, 0x80, 0xff};

// Minimal 2x2 binary PPM (P6, RGB): "P6\n2 2\n255\n" + 4 raw RGB triples.
static const uint8_t ppm_p6_2x2[] = {0x50, 0x36, 0x0a, 0x32, 0x20, 0x32, 0x0a, 0x32,
                                     0x35, 0x35, 0x0a, 0xff, 0x00, 0x00, 0x00, 0xff,
                                     0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff};

// Minimal 2x2 Radiance HDR (RGBE): "#?RADIANCE" signature, mandatory
// "FORMAT=32-bit_rle_rgbe" token, blank line, "-Y 2 +X 2" resolution string, then raw
// (non run-length-encoded) RGBE data -- stb_image only takes the RLE scanline path for
// width in [8, 32768), so a 2-pixel-wide image goes through the flat read.
static const uint8_t hdr_2x2[] = {
    0x23, 0x3f, 0x52, 0x41, 0x44, 0x49, 0x41, 0x4e, 0x43, 0x45, 0x0a, 0x46, 0x4f, 0x52, 0x4d, 0x41,
    0x54, 0x3d, 0x33, 0x32, 0x2d, 0x62, 0x69, 0x74, 0x5f, 0x72, 0x6c, 0x65, 0x5f, 0x72, 0x67, 0x62,
    0x65, 0x0a, 0x0a, 0x2d, 0x59, 0x20, 0x32, 0x20, 0x2b, 0x58, 0x20, 0x32, 0x0a, 0x80, 0x00, 0x00,
    0x80, 0x00, 0x80, 0x00, 0x80, 0x00, 0x00, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80};

// Minimal 2x2 uncompressed 8-bit RGB PSD: "8BPS" signature, version 1, 3 channels,
// zero-length color-mode/image-resources/layer-mask sections, compression = none, then
// raw per-channel planes (R plane, then G, then B -- PSD stores channels separately,
// not interleaved).
static const uint8_t psd_2x2[] = {0x38, 0x42, 0x50, 0x53, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00,
                                  0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x02,
                                  0x00, 0x08, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                                  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0x00, 0x80, 0x40,
                                  0x00, 0xff, 0x80, 0x40, 0x00, 0x00, 0x80, 0x40};

// Minimal 2x2 uncompressed RGB Softimage PIC: magic + 84-byte filler + "PICT" marker
// (that's the 92-byte block stbi__pic_test_core()/stbi__pic_load() both skip), 2x2
// resolution, then a single non-chained uncompressed packet covering the R/G/B
// channels (channel mask 0xE0 -- no alpha, so comp comes back 3).
static const uint8_t pic_2x2[] = {
    0x53, 0x80, 0xf6, 0x34, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x50, 0x49,
    0x43, 0x54, 0x00, 0x02, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x08, 0x00, 0xe0, 0xff, 0x00, 0x00, 0x00, 0xff, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff};

void test_stb_extended_fallback_formats() {
    struct {
        const char *name;
        const uint8_t *data;
        size_t len;
        int expect_w, expect_h, expect_min_ch;
    } cases[] = {
        {"tga", tga_2x2, sizeof(tga_2x2), 2, 2, 3},
        {"pgm (P5)", pgm_p5_2x2, sizeof(pgm_p5_2x2), 2, 2, 1},
        {"ppm (P6)", ppm_p6_2x2, sizeof(ppm_p6_2x2), 2, 2, 3},
        {"hdr", hdr_2x2, sizeof(hdr_2x2), 2, 2, 3},
        {"psd", psd_2x2, sizeof(psd_2x2), 2, 2, 3},
        {"pic", pic_2x2, sizeof(pic_2x2), 2, 2, 3},
    };

    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        ph_context_t *ctx = NULL;
        ASSERT_OK(ph_create(&ctx));

        ph_error_t err = ph_load_from_memory(ctx, cases[i].data, cases[i].len);
        if (err != PH_SUCCESS) {
            fprintf(stderr, "[FAIL] test_stb_extended_fallback_formats: %s failed with %d (%s)\n",
                    cases[i].name, err, ph_get_error_string(err));
            exit(1);
        }

        int w, h, ch;
        ph_context_get_dimensions(ctx, &w, &h, &ch);
        if (w != cases[i].expect_w || h != cases[i].expect_h || ch < cases[i].expect_min_ch) {
            fprintf(stderr,
                    "[FAIL] test_stb_extended_fallback_formats: %s decoded to %dx%d ch=%d, "
                    "expected %dx%d ch>=%d\n",
                    cases[i].name, w, h, ch, cases[i].expect_w, cases[i].expect_h,
                    cases[i].expect_min_ch);
            exit(1);
        }
        ASSERT_INT_EQ(1, ph_is_loaded(ctx));

        // Every one of these is a real color image, so a hash that needs color data
        // must succeed on it, the same contract exercised for BMP/GIF above.
        uint64_t hash = 0;
        ASSERT_OK(ph_compute_ahash(ctx, &hash));

        ph_free(ctx);
    }

    printf("test_stb_extended_fallback_formats: PASSED\n");
}

// Grayscale loading through the stb_image fallback, which is the one decoder path that
// cannot be configured away: BMP and GIF have no native backend in any build, so this
// exercises `*ch = req_comp` in ph_decode_stb_mem() (src/loader.c) whatever the native
// decoders are compiled in. The native JPEG/PNG grayscale paths are covered elsewhere in
// this file; in a stb-only build those tests hit this code too, and in a full CMake build
// nothing did before.
//
// Only the channel count is asserted against a reference: stb_image converts to gray with
// its own coefficients, so its pixel values are deliberately not compared with
// ph_to_grayscale()'s -- see check_png_backend_parity() for why that exactness is asked of
// libpng/spng and not of stb.
void test_grayscale_via_stb_fallback() {
    struct {
        const char *name;
        const uint8_t *data;
        size_t len;
        int w, h;
    } cases[] = {
        {"bmp bottom-up", bmp_bottomup, sizeof(bmp_bottomup), 2, 2},
        {"bmp top-down", bmp_topdown, sizeof(bmp_topdown), 2, 2},
        {"gif", mini_gif, sizeof(mini_gif), 1, 1},
    };

    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        ph_context_t *ctx = NULL;
        ASSERT_OK(ph_create(&ctx));
        ASSERT_OK(ph_context_set_load_grayscale(ctx, 1));

        ph_error_t err = ph_load_from_memory(ctx, cases[i].data, cases[i].len);
        if (err != PH_SUCCESS) {
            fprintf(stderr, "[FAIL] test_grayscale_via_stb_fallback: %s failed with %d (%s)\n",
                    cases[i].name, err, ph_get_error_string(err));
            exit(1);
        }

        int w, h, ch;
        ph_context_get_dimensions(ctx, &w, &h, &ch);
        ASSERT_INT_EQ(cases[i].w, w);
        ASSERT_INT_EQ(cases[i].h, h);
        ASSERT_INT_EQ(1, ch); // the whole point: the request was honoured
        ASSERT_INT_EQ(1, ph_is_loaded(ctx));

        // A single-channel image really is single-channel as far as the rest of the
        // library is concerned: grayscale hashes work, colour ones refuse.
        uint64_t hash = 0;
        ASSERT_OK(ph_compute_ahash(ctx, &hash));
        ph_digest_t d;
        ASSERT_INT_EQ(PH_ERR_REQUIRES_COLOR, ph_compute_color_hash(ctx, &d));

        // Same buffer without the flag must come back with colour channels, so that the
        // assertion above is about the request and not about the fixture. The exact count
        // is the format's business -- stb hands back 3 for the BMPs and 4 for the GIF --
        // so what matters here is only that it is not 1.
        ph_context_t *rgb_ctx = NULL;
        ASSERT_OK(ph_create(&rgb_ctx));
        ASSERT_OK(ph_context_set_load_grayscale(rgb_ctx, 0));
        ASSERT_OK(ph_load_from_memory(rgb_ctx, cases[i].data, cases[i].len));
        int rw, rh, rc;
        ph_context_get_dimensions(rgb_ctx, &rw, &rh, &rc);
        ASSERT(rc >= 3);

        ph_free(rgb_ctx);
        ph_free(ctx);
    }

    printf("test_grayscale_via_stb_fallback: PASSED\n");
}

// "Only the first frame is decoded" is an implicit contract this library has always
// had for animated GIF (and for animated WebP, when PH_USE_WEBP is compiled in) --
// documented on ph_load_from_memory() in include/libphash.h, but until now not backed
// by a test. gif_two_frames below has two *visibly different* solid-color frames (red,
// then blue); gif_frame1_only is a separately hand-built single-frame GIF holding just
// the first one. If ph_load_from_memory() on the animated file ever started decoding
// the wrong frame -- last frame instead of first, for instance -- the two would stop
// matching while this test kept passing on either wrong answer alone, which is why the
// comparison is against a real "frame 1 only" fixture and not just against a known RGBA
// tuple.
//
// LZW-encoded with a minimal general-purpose GIF/LZW encoder (Python, not committed);
// both fixtures were confirmed to decode with the vendored stb_image.h before being
// pasted in.
static const uint8_t gif_two_frames[] = {
    0x47, 0x49, 0x46, 0x38, 0x39, 0x61, 0x02, 0x00, 0x02, 0x00, 0x80, 0x00, 0x00, 0xff,
    0x00, 0x00, 0x00, 0x00, 0xff, 0x21, 0xf9, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x2c,
    0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x02, 0x00, 0x00, 0x02, 0x02, 0x84, 0x51, 0x00,
    0x21, 0xf9, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x2c, 0x00, 0x00, 0x00, 0x00, 0x02,
    0x00, 0x02, 0x00, 0x00, 0x02, 0x02, 0x8c, 0x53, 0x00, 0x3b};

static const uint8_t gif_frame1_only[] = {
    0x47, 0x49, 0x46, 0x38, 0x39, 0x61, 0x02, 0x00, 0x02, 0x00, 0x80, 0x00, 0x00, 0xff, 0x00,
    0x00, 0x00, 0x00, 0xff, 0x21, 0xf9, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x2c, 0x00, 0x00,
    0x00, 0x00, 0x02, 0x00, 0x02, 0x00, 0x00, 0x02, 0x02, 0x84, 0x51, 0x00, 0x3b};

// Same "first frame only" contract, for the native WebP decoder path: webp_anim_two_frames
// is a real animated WebP (built with `img2webp`, libwebp 1.6.0) holding a 4x4 red frame
// then a 4x4 blue one; webp_frame1_only is a standalone lossless WebP (built with `cwebp`)
// of just the red frame. The two encoders produce different containers -- VP8X+ANIM+ANMF
// for the animated file, a bare VP8L chunk for the standalone one -- which is deliberate:
// it means a passing comparison cannot be an accident of identical bytes, only of decoding
// to the same pixels.
static const uint8_t webp_anim_two_frames[] = {
    0x52, 0x49, 0x46, 0x46, 0x84, 0x00, 0x00, 0x00, 0x57, 0x45, 0x42, 0x50, 0x56, 0x50, 0x38, 0x58,
    0x0a, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x03, 0x00, 0x00, 0x41, 0x4e,
    0x49, 0x4d, 0x06, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff, 0x00, 0x00, 0x41, 0x4e, 0x4d, 0x46,
    0x28, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x03, 0x00, 0x00,
    0x64, 0x00, 0x00, 0x02, 0x56, 0x50, 0x38, 0x4c, 0x0f, 0x00, 0x00, 0x00, 0x2f, 0x03, 0xc0, 0x00,
    0x00, 0x07, 0x10, 0xe5, 0x8f, 0xfe, 0x07, 0x22, 0xa2, 0xff, 0x01, 0x00, 0x41, 0x4e, 0x4d, 0x46,
    0x28, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x03, 0x00, 0x00,
    0x64, 0x00, 0x00, 0x00, 0x56, 0x50, 0x38, 0x4c, 0x0f, 0x00, 0x00, 0x00, 0x2f, 0x03, 0xc0, 0x00,
    0x00, 0x07, 0x10, 0xd1, 0xfe, 0xfe, 0x07, 0x22, 0xa2, 0xff, 0x01, 0x00};

static const uint8_t webp_frame1_only[] = {0x52, 0x49, 0x46, 0x46, 0x1c, 0x00, 0x00, 0x00, 0x57,
                                           0x45, 0x42, 0x50, 0x56, 0x50, 0x38, 0x4c, 0x0f, 0x00,
                                           0x00, 0x00, 0x2f, 0x03, 0xc0, 0x00, 0x00, 0x07, 0x10,
                                           0xe5, 0x8f, 0xfe, 0x07, 0x22, 0xa2, 0xff, 0x01, 0x00};

static uint64_t hash_of_buffer(const uint8_t *data, size_t len) {
    ph_context_t *ctx = NULL;
    ASSERT_OK(ph_create(&ctx));
    ASSERT_OK(ph_load_from_memory(ctx, data, len));
    uint64_t hash = 0;
    ASSERT_OK(ph_compute_ahash(ctx, &hash));
    ph_free(ctx);
    return hash;
}

void test_animated_gif_first_frame_only() {
    ph_context_t *ctx = NULL;
    ASSERT_OK(ph_create(&ctx));
    ASSERT_OK(ph_load_from_memory(ctx, gif_two_frames, sizeof(gif_two_frames)));

    int w, h, ch;
    ph_context_get_dimensions(ctx, &w, &h, &ch);
    ASSERT_INT_EQ(2, w);
    ASSERT_INT_EQ(2, h);
    ph_free(ctx);

    // The hash of the two-frame file must equal the hash of the standalone
    // first-frame-only file, and not just "some" stable value.
    uint64_t hash_multi = hash_of_buffer(gif_two_frames, sizeof(gif_two_frames));
    uint64_t hash_frame1 = hash_of_buffer(gif_frame1_only, sizeof(gif_frame1_only));
    ASSERT_INT_EQ(0, ph_hamming_distance(hash_multi, hash_frame1));

    printf("test_animated_gif_first_frame_only: PASSED\n");
}

// Animated WebP was assumed to follow the same "first frame only" contract as animated
// GIF (both are what CLAUDE.md/README.md/ph_load_from_memory() document). It does not:
// this backend calls WebPGetInfo() + WebPDecodeRGBInto(), libwebp's *simple* decode API,
// which has no bitstream to decode at the RIFF top level for a VP8X+ANIM container --
// the actual pixels are one level down, in per-frame ANMF chunks, reachable only through
// the demux API (WebPAnimDecoder / WebPDemuxer), which this backend does not link. The
// dimension query still succeeds (VP8X carries the canvas size), but the decode call
// itself fails, so an animated WebP is refused outright rather than decoding its first
// frame. Confirmed against a real two-frame file built with `img2webp` (libwebp 1.6.0).
// This contradicts the documented contract; flagged as a real gap rather than silently
// worked around -- fixing it means linking libwebp's demux library and decoding through
// WebPAnimDecoder, which is a decoder-behavior change of its own, not a test-coverage one.
void test_webp_animated_first_frame_only_or_skip() {
    if (!ph_can_use_webp()) {
        printf("test_webp_animated_first_frame_only_or_skip: SKIPPED (no WebP decoder in "
               "this build)\n");
        return;
    }

    ph_context_t *ctx = NULL;
    ASSERT_OK(ph_create(&ctx));
    ph_error_t err = ph_load_from_memory(ctx, webp_anim_two_frames, sizeof(webp_anim_two_frames));
    ASSERT_INT_EQ(PH_ERR_CORRUPT_DATA, err);
    ph_free(ctx);

    // The standalone single-frame fixture (same pixels, no ANIM container) decodes fine,
    // confirming the failure above is about the container, not the fixture being broken.
    ph_context_t *ctx2 = NULL;
    ASSERT_OK(ph_create(&ctx2));
    ASSERT_OK(ph_load_from_memory(ctx2, webp_frame1_only, sizeof(webp_frame1_only)));
    ph_free(ctx2);

    printf("test_webp_animated_first_frame_only_or_skip: PASSED (documents a real gap: "
           "animated WebP is rejected, not decoded to its first frame -- see comment)\n");
}

// TIFF is claimed nowhere as supported (CLAUDE.md and README.md explicitly call it
// out as NOT covered, since stb_image has zero TIFF support), but that claim was never
// pinned by a test. Both byte orders reach ph_can_read_stb() -- which accepts every
// magic except WebP's -- so they fall through to stb_image, which recognizes neither
// signature and fails with its generic "unknown image type", landing on
// PH_ERR_UNSUPPORTED_FORMAT and not some other/generic error.
void test_tiff_unsupported() {
    ph_context_t *ctx = NULL;
    ASSERT_OK(ph_create(&ctx));

    static const uint8_t tiff_le[] = {0x49, 0x49, 0x2a, 0x00, 0x08, 0x00, 0x00, 0x00};
    ph_error_t err = ph_load_from_memory(ctx, tiff_le, sizeof(tiff_le));
    ASSERT_INT_EQ(PH_ERR_UNSUPPORTED_FORMAT, err);
    ASSERT(strlen(ph_get_last_error_message(ctx)) > 0);

    static const uint8_t tiff_be[] = {0x4d, 0x4d, 0x00, 0x2a, 0x00, 0x00, 0x00, 0x08};
    err = ph_load_from_memory(ctx, tiff_be, sizeof(tiff_be));
    ASSERT_INT_EQ(PH_ERR_UNSUPPORTED_FORMAT, err);
    ASSERT(strlen(ph_get_last_error_message(ctx)) > 0);

    ph_free(ctx);
    printf("test_tiff_unsupported: PASSED\n");
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

// The stb_image fallback reports failures as an English sentence, not a code, so
// src/loader.c has to recognize the sentence that means "nothing here looked like an
// image" and separate it from every sentence that means "recognized, but broken". That
// mapping is pinned to the literals in vendor/stb_image.h, and it fails silently: a
// reworded string in a vendored update would quietly turn every unsupported buffer into
// PH_ERR_CORRUPT_DATA.
//
// This test is therefore meant to break on a vendor bump. If it does, re-read the
// strings out of the new stb_image.h and update ph_stb_unsupported_reasons[] in
// src/loader.c -- do not relax the assertions.
void test_stb_failure_classification() {
    ph_context_t *ctx = NULL;
    ASSERT_OK(ph_create(&ctx));

    // Not any format stb_image tests for: "unknown image type" -> unsupported.
    const uint8_t junk[] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
                            0x88, 0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff};
    ASSERT_INT_EQ(PH_ERR_UNSUPPORTED_FORMAT, ph_load_from_memory(ctx, junk, sizeof(junk)));

    // A BMP whose DIB header size is nonsense stops looking like a BMP at all, so it
    // reaches the same "unknown image type" and must land on the same code.
    uint8_t bmp[sizeof(bmp_bottomup)];
    memcpy(bmp, bmp_bottomup, sizeof(bmp));
    bmp[14] = 0x99; // DIB header size
    ASSERT_INT_EQ(PH_ERR_UNSUPPORTED_FORMAT, ph_load_from_memory(ctx, bmp, sizeof(bmp)));

    // The other half of the mapping, which is what a silent degradation would collapse
    // into: a recognized format that cannot be decoded stays PH_ERR_CORRUPT_DATA. This
    // BMP is well-formed down to its bit depth, which is 7.
    memcpy(bmp, bmp_bottomup, sizeof(bmp));
    bmp[28] = 7;
    bmp[29] = 0;
    ASSERT_INT_EQ(PH_ERR_CORRUPT_DATA, ph_load_from_memory(ctx, bmp, sizeof(bmp)));

    // A failure always leaves a reason behind, whichever side of the mapping it took.
    ASSERT(ph_get_last_error_message(ctx) != NULL);
    ASSERT(ph_get_last_error_message(ctx)[0] != '\0');

    ph_free(ctx);
    printf("test_stb_failure_classification: PASSED\n");
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
    test_stb_extended_fallback_formats();
    test_animated_gif_first_frame_only();
    test_webp_animated_first_frame_only_or_skip();
    test_tiff_unsupported();
    test_grayscale_via_stb_fallback();
    test_bmp_negative_height_not_too_large();
    test_bmp_extreme_aspect_ratio_rejected();
    test_stb_failure_classification();
    return 0;
}
