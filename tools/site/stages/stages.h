/* site_stages, the documentation site's measuring tool: what its modes share.
 *
 * Each algorithm has its own file (ahash.c … color_moments.c) with the modes that show
 * its stages; measure.c compares images, timing.c times them, main.c dispatches. The
 * helpers here load images, write what a mode produces, and check a result recomputed
 * from the stages against the library's own. tools/site/README.md describes the tool as
 * a whole and how to add a mode.
 */
#ifndef SITE_STAGES_H
#define SITE_STAGES_H

#include "context.h"
#include "hashes/hashes.h"
#include "image/image.h"
#include "libphash.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#define COUNT(a) (sizeof(a) / sizeof((a)[0]))

/* ---- Errors, images, files (util.c) ---------------------------------------------- */

/* Prints "site_stages: <what>[: <detail>]" to stderr and returns 1, the exit status. */
int fail(const char *what, const char *detail);

/* Loads `path` into a new context, with every setting at its default. Returns 0, or 1
 * with *ctx NULL after saying why. */
int load_image(ph_context_t **ctx, const char *path);

/* The same with ph_context_set_load_grayscale() on: the decoder converts to grayscale,
 * which every stage mode shows beside the library's own conversion. */
int load_decoder_gray(ph_context_t **ctx, const char *path);

/* When `arg` is `--load=<settings>`, makes every later load_image() load with them and
 * returns 1 (setting *status on a setting it does not know); otherwise returns 0. The
 * settings, comma-separated: alpha=grey|white|black|ignore,
 * scale=full|half|quarter|eighth, orient=off, gray=decoder, weights=R/G/B/SHIFT (a
 * grayscale the tool computes), or default. A mode that takes a list of images accepts
 * them between its images, and each replaces the last. */
int take_load_settings(const char *arg, int *status);

/* Writes <dir>/<name> as binary PGM (channels 1) or PPM (channels 3). Returns 0 or 1. */
int write_pnm(const char *dir, const char *name, const uint8_t *px, int w, int h, int channels);

/* Writes n floats to <dir>/<name>, raw, in the machine's byte order. Returns 0 or 1. */
int write_f32(const char *dir, const char *name, const float *v, size_t n);

/* Opens <dir>/<name> for writing, or says why not and returns NULL. */
FILE *open_out(const char *dir, const char *name);

/* Writes original.ppm (when the image is RGB) and gray.pgm into `outdir`: the first two
 * stages of every grayscale hash. Returns the grayscale buffer, owned by `ctx`, or NULL;
 * a failed write sets *status. */
const uint8_t *write_gray_stages(ph_context_t *ctx, const char *outdir, int *status);

/* Sorts n floats in ascending order, as the library's own insertion sorts do. */
void sort_floats(float *v, int n);

/* How many bits of two byte strings differ. */
int bits_apart(const uint8_t *a, const uint8_t *b, int bytes);

/* Every stage mode recomputes its hash from the stages it shows and checks it against
 * the library's: a mismatch prints both and returns 1, so a picture of the steps is
 * always a picture of what the library does. `what` names the computation. */
int check_hash64(const char *what, uint64_t stages, uint64_t lib);
int check_digest(const char *what, const uint8_t *stages, int bytes, const ph_digest_t *lib);

/* ---- JSON output (json.c) --------------------------------------------------------- */

/* A JSON value written piece by piece: `{"a": 1, "b": [2, 3]}`. The site's readers
 * (tools/site/measure/tool.py) take what these helpers write, so a mode only names its
 * fields. Inside an object every value has a key; inside an array the key is NULL. */
typedef struct {
    FILE *f;
    int fields;
} json_t;

json_t json_begin(FILE *f); /* a top-level object */
void json_end(json_t *j);   /* closes it, and ends the line */
json_t json_object(json_t *j, const char *key);
void json_close_object(json_t *o);
json_t json_array(json_t *j, const char *key);
void json_close_array(json_t *a);

void json_null(json_t *j, const char *key);
void json_int(json_t *j, const char *key, long long v);
void json_double(json_t *j, const char *key, double v); /* six decimals */
void json_float(json_t *j, const char *key, float v);   /* exactly the float, for ties */
void json_string(json_t *j, const char *key, const char *v);
void json_hex64(json_t *j, const char *key, uint64_t v);
void json_hexbytes(json_t *j, const char *key, const uint8_t *v, int n);
void json_u8s(json_t *j, const char *key, const uint8_t *v, int n);
void json_u64s(json_t *j, const char *key, const uint64_t *v, int n);
void json_floats(json_t *j, const char *key, const float *v, int n);
void json_doubles(json_t *j, const char *key, const double *v, int n);
void json_doubles_fixed(json_t *j, const char *key, const double *v, int n, int decimals);
/* The n low bits of `bits` as 0/1: from the most significant one down (aHash, dHash
 * pack the first cell into the top bit), or from the least significant one up (pHash,
 * wHash: bit i is coefficient i). */
void json_bits_msb(json_t *j, const char *key, uint64_t bits, int n);
void json_bits_lsb(json_t *j, const char *key, uint64_t bits, int n);

/* Runs `row` on every image of argv, each loaded with the default settings, and prints
 * one JSON line per image: {"file": <path>, …what `row` writes}. The modes that take a
 * list of images (the corpora) are built on it. Returns 0, or 1 on the first failure. */
typedef int (*image_row_fn)(ph_context_t *ctx, json_t *row, const char *path);
int for_each_image(int argc, char **argv, image_row_fn row);

/* ---- Shared between modes ----------------------------------------------------------- */

/* The Radial settings `site_stages radial-variants` hashes every image with, and the
 * time cases of the non-default ones (radial.c, timing.c): the default first, then one
 * setting changed at a time. */
typedef struct {
    const char *name;
    int projections, samples;
    float sigma, gamma;
} radial_variant_t;

enum {
    RADIAL_DEFAULT,
    RADIAL_SIGMA_1,
    RADIAL_SIGMA_8,
    RADIAL_GAMMA_05,
    RADIAL_GAMMA_2,
    RADIAL_GRID_40X32,
    RADIAL_GRID_90X64,
    RADIAL_GRID_360X256,
    RADIAL_GRID_1440X1024,
    RADIAL_GRID_4096X4096,
    RADIAL_VARIANTS,
};

extern const radial_variant_t radial_variants[RADIAL_VARIANTS];

/* Sets Radial's parameters and the context's gamma; returns 1 on success. */
int radial_settings(ph_context_t *ctx, const radial_variant_t *v);

/* The image at `path` loaded through the decoder's grayscale, which a color hash refuses:
 * the error `compute` returns, which must be PH_ERR_REQUIRES_COLOR (color_hash.c). Returns
 * 0 when it is, 1 otherwise, after saying so under `name`. */
int color_refuses_gray(const char *path, ph_error_t (*compute)(ph_context_t *, ph_digest_t *),
                       const char *name, ph_error_t *err);

/* ---- The modes (main.c dispatches; each is documented where it is defined) --------- */

int mode_ahash(int argc, char **argv);
int mode_ahash_variants(int argc, char **argv);
int mode_dhash(int argc, char **argv);
int mode_dhash_variants(int argc, char **argv);
int mode_phash(int argc, char **argv);
int mode_phash_variants(int argc, char **argv);
int mode_whash(int argc, char **argv);
int mode_whash_modes(int argc, char **argv);
int mode_mhash(int argc, char **argv);
int mode_mhash_direct(int argc, char **argv);
int mode_bmh(int argc, char **argv);
int mode_bmh_variants(int argc, char **argv);
int mode_radial(int argc, char **argv);
int mode_radial_profiles(int argc, char **argv);
int mode_radial_variants(int argc, char **argv);
int mode_color_hash(int argc, char **argv);
int mode_color_moments(int argc, char **argv);
int mode_color_moments_digests(int argc, char **argv);
int mode_measure(int argc, char **argv);
int mode_pairs(int argc, char **argv);
int mode_sizes(int argc, char **argv);
int mode_corpus(int argc, char **argv);
int mode_time(int argc, char **argv);
int mode_scan(int argc, char **argv);
int mode_loaded(int argc, char **argv);
int mode_area(int argc, char **argv);

#endif /* SITE_STAGES_H */
