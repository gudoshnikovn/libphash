/* ph_load_from_file() and ph_load_from_memory() must produce the same image.
 *
 * They used not to. The two entry points reached the decoders by different
 * routes: the buffer path went through ph_decode_buffer(), while the file path
 * had its own sequence of stbi_info(path)/stbi_load(path) and its own separate
 * re-read of the file for the EXIF orientation scan. Anything that lived on one
 * route and not the other -- most visibly auto-orientation -- silently depended
 * on *how* the caller had loaded the image, which is exactly the kind of
 * difference a stored hash cannot survive.
 *
 * There is one decode path now, and this test pins the observable consequence:
 * the same bytes hash the same whether they arrived as a path or as a buffer,
 * for every combination of the two settings that used to differ between the
 * routes, and failures come back with the same error code on both. */

#include "test_macros.h"
#include <libphash.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* --- fixtures ------------------------------------------------------------- */

static uint8_t *read_file(const char *path, size_t *out_len) {
    FILE *f = fopen(path, "rb");
    ASSERT_PTR_NOT_NULL(f);
    ASSERT_INT_EQ(0, fseek(f, 0, SEEK_END));
    long sz = ftell(f);
    ASSERT(sz > 0);
    rewind(f);
    uint8_t *buf = (uint8_t *)malloc((size_t)sz);
    ASSERT_PTR_NOT_NULL(buf);
    ASSERT(fread(buf, 1, (size_t)sz, f) == (size_t)sz);
    fclose(f);
    *out_len = (size_t)sz;
    return buf;
}

static void write_file(const char *path, const uint8_t *data, size_t len) {
    FILE *f = fopen(path, "wb");
    ASSERT_PTR_NOT_NULL(f);
    ASSERT(fwrite(data, 1, len, f) == len);
    fclose(f);
}

/* A JPEG carrying a little-endian APP1/Exif segment whose IFD0 holds a single
 * Orientation tag. Spliced right after the SOI of a real fixture, so the pixel
 * data is a genuine image and only the metadata is synthetic. */
static uint8_t *make_jpeg_with_orientation(const uint8_t *jpeg, size_t jpeg_len, uint16_t value,
                                           size_t *out_len) {
    uint8_t tiff[26];
    size_t t = 0;
    tiff[t++] = 'I';
    tiff[t++] = 'I';
    tiff[t++] = 42;
    tiff[t++] = 0;
    tiff[t++] = 8; /* IFD0 offset = 8 */
    tiff[t++] = 0;
    tiff[t++] = 0;
    tiff[t++] = 0;
    tiff[t++] = 1; /* one entry */
    tiff[t++] = 0;
    tiff[t++] = 0x12; /* tag 0x0112, Orientation */
    tiff[t++] = 0x01;
    tiff[t++] = 3; /* type SHORT */
    tiff[t++] = 0;
    tiff[t++] = 1; /* count */
    tiff[t++] = 0;
    tiff[t++] = 0;
    tiff[t++] = 0;
    tiff[t++] = (uint8_t)(value & 0xFF);
    tiff[t++] = (uint8_t)(value >> 8);
    tiff[t++] = 0; /* value field padding */
    tiff[t++] = 0;
    tiff[t++] = 0; /* no next IFD */
    tiff[t++] = 0;
    tiff[t++] = 0;
    tiff[t++] = 0;

    size_t payload = 6 + t;        /* "Exif\0\0" + TIFF block */
    size_t seg = 2 + 2 + payload;  /* marker + length field + payload */
    size_t total = jpeg_len + seg; /* the SOI of the original is kept */
    uint8_t *out = (uint8_t *)malloc(total);
    ASSERT_PTR_NOT_NULL(out);

    size_t o = 0;
    out[o++] = jpeg[0]; /* SOI */
    out[o++] = jpeg[1];
    out[o++] = 0xFF;
    out[o++] = 0xE1;
    out[o++] = (uint8_t)((payload + 2) >> 8); /* segment length includes itself */
    out[o++] = (uint8_t)((payload + 2) & 0xFF);
    memcpy(out + o, "Exif\0\0", 6);
    o += 6;
    memcpy(out + o, tiff, t);
    o += t;
    memcpy(out + o, jpeg + 2, jpeg_len - 2);
    o += jpeg_len - 2;

    *out_len = o;
    return out;
}

/* --- the comparison ------------------------------------------------------- */

/* Everything a caller can observe about a loaded image, so that "the two paths
 * agree" is not just about one hash value. */
typedef struct {
    ph_error_t err;
    int width, height, channels;
    uint64_t ahash, dhash, phash;
    ph_digest_t mhash, color;
} load_result_t;

static load_result_t describe(ph_context_t *ctx, ph_error_t err) {
    load_result_t r;
    memset(&r, 0, sizeof(r));
    r.err = err;
    if (err != PH_SUCCESS)
        return r;
    ph_context_get_dimensions(ctx, &r.width, &r.height, &r.channels);
    ASSERT_OK(ph_compute_ahash(ctx, &r.ahash));
    ASSERT_OK(ph_compute_dhash(ctx, &r.dhash));
    ASSERT_OK(ph_compute_phash(ctx, &r.phash));
    ASSERT_OK(ph_compute_mhash(ctx, &r.mhash));
    /* A digest, so it is compared by bytes below rather than by the CHECK macro. */
    if (r.channels >= 3)
        ASSERT_OK(ph_compute_color_hash(ctx, &r.color));
    return r;
}

static load_result_t load_via_file(ph_context_t *ctx, const char *path) {
    return describe(ctx, ph_load_from_file(ctx, path));
}

static load_result_t load_via_memory(ph_context_t *ctx, const uint8_t *buf, size_t len) {
    return describe(ctx, ph_load_from_memory(ctx, buf, len));
}

static void expect_same(const load_result_t *a, const load_result_t *b, const char *what) {
#define CHECK(field, fmt)                                                                          \
    if (a->field != b->field) {                                                                    \
        fprintf(stderr,                                                                            \
                "[FAIL] %s: file and memory disagree on " #field " (" fmt " vs " fmt ")\n", what,  \
                a->field, b->field);                                                               \
        exit(1);                                                                                   \
    }
    CHECK(err, "%d")
    CHECK(width, "%d")
    CHECK(height, "%d")
    CHECK(channels, "%d")
    CHECK(ahash, "%llu")
    CHECK(dhash, "%llu")
    CHECK(phash, "%llu")
    if (a->mhash.size != b->mhash.size ||
        memcmp(a->mhash.data, b->mhash.data, a->mhash.size) != 0) {
        fprintf(stderr, "[FAIL] %s: file and memory disagree on mhash\n", what);
        exit(1);
    }
    if (a->color.size != b->color.size ||
        memcmp(a->color.data, b->color.data, a->color.size) != 0) {
        fprintf(stderr, "[FAIL] %s: file and memory disagree on color\n", what);
        exit(1);
    }
#undef CHECK
}

/* Runs one fixture through both entry points in all four combinations of the two
 * settings that the old split path handled differently. */
static void check_parity(const char *path, const char *what) {
    size_t len = 0;
    uint8_t *buf = read_file(path, &len);

    ph_context_t *ctx = NULL;
    ASSERT_OK(ph_create(&ctx));
    for (int orient = 0; orient <= 1; orient++) {
        for (int gray = 0; gray <= 1; gray++) {
            ph_context_set_auto_orient(ctx, orient);
            ph_context_set_load_grayscale(ctx, gray);
            load_result_t from_file = load_via_file(ctx, path);
            load_result_t from_mem = load_via_memory(ctx, buf, len);
            expect_same(&from_file, &from_mem, what);
        }
    }
    ph_free(ctx);
    free(buf);
    printf("  %-38s file == memory\n", what);
}

/* The parity above would also hold if auto-orientation were dead on both paths.
 * This is the case that makes it meaningful: a JPEG whose only difference from
 * the fixture is an Orientation=6 tag must come out rotated -- through a path and
 * through a buffer alike -- and must come out unrotated when the feature is off. */
static void test_exif_orientation_parity(void) {
    size_t plain_len = 0;
    uint8_t *plain = read_file(TEST_DATA_DIR "/photo.jpeg", &plain_len);
    size_t tagged_len = 0;
    uint8_t *tagged = make_jpeg_with_orientation(plain, plain_len, 6, &tagged_len);

    const char *tmp_path = "ph_parity_orientation.jpeg";
    write_file(tmp_path, tagged, tagged_len);

    ph_context_t *ctx = NULL;
    ASSERT_OK(ph_create(&ctx));

    ph_context_set_auto_orient(ctx, 1);
    load_result_t tagged_file = load_via_file(ctx, tmp_path);
    load_result_t tagged_mem = load_via_memory(ctx, tagged, tagged_len);
    expect_same(&tagged_file, &tagged_mem, "EXIF-tagged JPEG, auto-orient on");

    ph_context_set_auto_orient(ctx, 0);
    load_result_t raw_file = load_via_file(ctx, tmp_path);
    load_result_t raw_mem = load_via_memory(ctx, tagged, tagged_len);
    expect_same(&raw_file, &raw_mem, "EXIF-tagged JPEG, auto-orient off");

    /* Orientation 6 is a 90-degree rotation: it has to move the hash a long way,
     * on both paths, or the parity assertions above prove nothing. */
    int dist_file = ph_hamming_distance(tagged_file.ahash, raw_file.ahash);
    int dist_mem = ph_hamming_distance(tagged_mem.ahash, raw_mem.ahash);
    if (dist_file < 10 || dist_mem < 10) {
        fprintf(stderr,
                "[FAIL] the orientation tag was not applied: aHash moved by %d bits via the "
                "file path and %d bits via the buffer path\n",
                dist_file, dist_mem);
        exit(1);
    }
    printf("  %-38s applied on both paths (%d / %d bits)\n", "EXIF orientation", dist_file,
           dist_mem);

    ph_free(ctx);
    remove(tmp_path);
    free(tagged);
    free(plain);
}

/* Failures have to match too: the file path used to classify some of them with
 * its own copy of the logic (stbi_failure_reason() on the file, a magic sniff for
 * WebP) while the buffer path used the decoder dispatcher's. */
static void test_failure_parity(void) {
    static const uint8_t garbage[] = "not an image at all, really";
    const char *tmp_path = "ph_parity_garbage.bin";
    write_file(tmp_path, garbage, sizeof(garbage));

    ph_context_t *ctx = NULL;
    ASSERT_OK(ph_create(&ctx));

    load_result_t f = load_via_file(ctx, tmp_path);
    load_result_t m = load_via_memory(ctx, garbage, sizeof(garbage));
    ASSERT_INT_EQ(PH_ERR_UNSUPPORTED_FORMAT, f.err);
    expect_same(&f, &m, "unrecognized bytes");
    remove(tmp_path);

    size_t len = 0;
    uint8_t *corrupt = read_file(TEST_DATA_DIR "/corrupted.jpg", &len);
    f = load_via_file(ctx, TEST_DATA_DIR "/corrupted.jpg");
    m = load_via_memory(ctx, corrupt, len);
    ASSERT_INT_EQ(PH_ERR_CORRUPT_DATA, f.err);
    expect_same(&f, &m, "corrupt JPEG");
    free(corrupt);

    /* The pixel-count limit is one check now instead of two implementations of
     * it (stbi_info(path) on one side, the backend's own on the other). */
    size_t jlen = 0;
    uint8_t *jpeg = read_file(TEST_DATA_DIR "/photo.jpeg", &jlen);
    ph_context_set_max_pixels(ctx, 16);
    f = load_via_file(ctx, TEST_DATA_DIR "/photo.jpeg");
    m = load_via_memory(ctx, jpeg, jlen);
    ASSERT_INT_EQ(PH_ERR_IMAGE_TOO_LARGE, f.err);
    expect_same(&f, &m, "image over max_pixels");
    ph_context_set_max_pixels(ctx, 0);
    free(jpeg);

    printf("  %-38s file == memory\n", "failure codes");
    ph_free(ctx);
}

int main(void) {
    printf("test_load_path_parity:\n");
    check_parity(TEST_DATA_DIR "/photo.jpeg", "photo.jpeg");
    check_parity(TEST_DATA_DIR "/photo.png", "photo.png");
    check_parity(TEST_DATA_DIR "/photo_complex.png", "photo_complex.png");
    if (ph_can_use_webp())
        check_parity(TEST_DATA_DIR "/photo.webp", "photo.webp");
    test_exif_orientation_parity();
    test_failure_parity();
    PASS("test_load_path_parity");
    return 0;
}
