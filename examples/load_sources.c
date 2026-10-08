/* One picture loaded three ways -- as pixels already in memory, as an encoded file in a
 * memory buffer, and as a file on disk -- and the same pHash from each. Build/run:
 *   cc load_sources.c -o load_sources $(pkg-config --cflags --libs libphash)
 *   ./load_sources frame.ppm        (the file it writes and then loads)
 */
#include <libphash.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Each row is padded to 512 bytes: the stride is the distance from one row to the next,
 * which need not be the width times the channels. */
enum {
    WIDTH = 150,
    HEIGHT = 100,
    STRIDE = 512,
};

/* Hashes what ctx holds and prints it with the name of the source. */
static int print_phash(ph_context_t *ctx, const char *source, uint64_t *out) {
    char hex[17];
    if (ph_compute_phash(ctx, out) != PH_SUCCESS ||
        ph_hash_to_hex(*out, hex, sizeof(hex)) != PH_SUCCESS) {
        fprintf(stderr, "%s: hashing failed\n", source);
        return 1;
    }
    printf("%-16s pHash %s\n", source, hex);
    return 0;
}

static int report(ph_context_t *ctx, const char *source, ph_error_t err) {
    fprintf(stderr, "%s: %s (%s)\n", source, ph_get_error_string(err),
            ph_get_last_error_message(ctx));
    return 1;
}

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "usage: %s <file to write>\n", argv[0]);
        return 1;
    }

    /* A frame some other code produced: RGB, a gradient with a bright disc. */
    static uint8_t frame[HEIGHT * STRIDE];
    for (int y = 0; y < HEIGHT; y++) {
        for (int x = 0; x < WIDTH; x++) {
            int dx = x - 100, dy = y - 40, disc = dx * dx + dy * dy < 30 * 30;
            uint8_t *p = frame + y * STRIDE + x * 3;
            p[0] = (uint8_t)(disc ? 250 : x);
            p[1] = (uint8_t)(disc ? 220 : y * 2);
            p[2] = (uint8_t)(disc ? 90 : 160);
        }
    }

    ph_context_t *ctx = NULL;
    if (ph_create(&ctx) != PH_SUCCESS) {
        fprintf(stderr, "ph_create failed\n");
        return 1;
    }
    uint64_t from_pixels = 0, from_memory = 0, from_file = 0;
    ph_error_t err;
    int failed = 0;

    /* --8<-- [start:pixels] */
    /* Pixels: width, height, channels, and the bytes from one row to the next. */
    err = ph_load_from_pixels(ctx, frame, WIDTH, HEIGHT, 3, STRIDE);
    if (err != PH_SUCCESS) {
        failed = report(ctx, "pixels", err);
    } else {
        failed = print_phash(ctx, "pixels", &from_pixels);
    }
    /* --8<-- [end:pixels] */

    /* The same frame encoded as a binary PPM, whole, in one buffer. */
    char header[32];
    int header_len = snprintf(header, sizeof(header), "P6\n%d %d\n255\n", WIDTH, HEIGHT);
    size_t length = (size_t)header_len + (size_t)WIDTH * HEIGHT * 3;
    uint8_t *encoded = malloc(length);
    if (!encoded) {
        ph_free(ctx);
        return 1;
    }
    memcpy(encoded, header, (size_t)header_len);
    for (int y = 0; y < HEIGHT; y++) {
        memcpy(encoded + header_len + (size_t)y * WIDTH * 3, frame + y * STRIDE, WIDTH * 3);
    }

    /* --8<-- [start:memory] */
    /* Memory: an encoded file -- from a network, a database, an archive -- as bytes. */
    err = ph_load_from_memory(ctx, encoded, length);
    if (err != PH_SUCCESS) {
        failed |= report(ctx, "memory", err);
    } else {
        failed |= print_phash(ctx, "memory", &from_memory);
    }
    /* --8<-- [end:memory] */

    FILE *f = fopen(argv[1], "wb");
    if (!f || fwrite(encoded, 1, length, f) != length || fclose(f) != 0) {
        fprintf(stderr, "cannot write %s\n", argv[1]);
        free(encoded);
        ph_free(ctx);
        return 1;
    }
    free(encoded);

    /* --8<-- [start:file] */
    /* File: the same bytes on disk. */
    err = ph_load_from_file(ctx, argv[1]);
    if (err != PH_SUCCESS) {
        failed |= report(ctx, "file", err);
    } else {
        failed |= print_phash(ctx, "file", &from_file);
    }
    /* --8<-- [end:file] */

    ph_free(ctx);
    if (failed) {
        return 1;
    }
    if (from_pixels != from_memory || from_memory != from_file) {
        printf("the three sources disagree\n");
        return 1;
    }
    printf("one picture, one hash\n");
    return 0;
}
