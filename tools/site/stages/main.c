/* site_stages: the documentation site's measuring tool (tools/site/README.md).
 *
 * Every number and picture on the site's algorithm pages comes from this program, run
 * against the library the site documents, so a page cannot describe a computation the
 * code does not perform. It reads internal state (the grayscale image, the reduced
 * grid), so it links like the tests do.
 *
 * Three kinds of mode, each documented where it is defined:
 *   - `<algo> <image> <outdir>` writes the stages of one algorithm on one image (PNM
 *     images and <algo>.json), every result recomputed from its stages and checked
 *     against the library: a mismatch exits 1.
 *   - `<algo>-<what> <image>...` prints one JSON line per image, for the measurements
 *     over a corpus that one page makes (block sizes, settings, modes).
 *   - `measure`, `pairs`, `time`, `scan` and `corpus` serve every page.
 */
#include "stages.h"

#include <string.h>

/* The modes: name, arguments, the least number of them (a mode taking a list accepts
 * more), and what it is for, as `site_stages` without arguments prints them. */
static const struct {
    const char *name;
    const char *args;
    int min_args;
    int variadic;
    int (*run)(int argc, char **argv);
    const char *what;
} modes[] = {
    {"ahash", "<image> <outdir>", 2, 0, mode_ahash, "aHash's stages, three reductions"},
    {"ahash-variants", "<image>...", 1, 1, mode_ahash_variants,
     "aHash with other reductions and a rounded mean"},
    {"dhash", "<image> <outdir>", 2, 0, mode_dhash, "dHash's stages, and an area grid"},
    {"dhash-variants", "<image>...", 1, 1, mode_dhash_variants, "dHash on both grids"},
    {"phash", "<image> <outdir>", 2, 0, mode_phash, "pHash's stages, block and DCT sizes"},
    {"phash-variants", "<image>...", 1, 1, mode_phash_variants, "pHash at each block and DCT size"},
    {"whash", "<image> <outdir>", 2, 0, mode_whash, "wHash's stages, both modes"},
    {"whash-modes", "<image>...", 1, 1, mode_whash_modes, "wHash's modes against aHash's grid"},
    {"mhash", "<image> <outdir>", 2, 0, mode_mhash, "mHash's stages, scales and sizes"},
    {"mhash-direct", "<image>...", 1, 1, mode_mhash_direct, "mHash against its definition"},
    {"bmh", "<image> <outdir>", 2, 0, mode_bmh, "BMH's stages, four block sizes"},
    {"bmh-variants", "<image>...", 1, 1, mode_bmh_variants, "BMH at each block size"},
    {"radial", "<image> <outdir>", 2, 0, mode_radial, "Radial's stages, sigmas and gammas"},
    {"radial-profiles", "<reference> <image>...", 1, 1, mode_radial_profiles,
     "Radial's profiles against a reference"},
    {"radial-variants", "<image>...", 1, 1, mode_radial_variants, "Radial at each setting"},
    {"color_hash", "<image> <outdir>", 2, 0, mode_color_hash, "ColorHash's stages"},
    {"color_moments", "<image> <outdir>", 2, 0, mode_color_moments, "ColorMoments' stages"},
    {"color_moments-digests", "<image>...", 1, 1, mode_color_moments_digests,
     "ColorMoments' digests"},
    {"measure", "<reference> <variant>...", 2, 1, mode_measure,
     "every algorithm, variants against a reference"},
    {"pairs", "<image> <image>...", 2, 1, mode_pairs, "every algorithm, every pair of images"},
    {"sizes", "<image>", 1, 0, mode_sizes, "every algorithm's digest size and kind"},
    {"corpus", "<outdir>", 1, 0, mode_corpus, "the tests' synthetic corpus as PPM"},
    {"time", "<image>", 1, 0, mode_time, "decoding and every hash, timed"},
    {"scan", "", 0, 0, mode_scan, "one comparison in a linear search, timed"},
    {"loaded", "<outdir> <image>...", 2, 1, mode_loaded, "each image as loaded, and its gray"},
    {"area", "<width> <height> <image>", 3, 0, mode_area, "the area average onto a grid"},
};

int main(int argc, char **argv) {
    for (size_t m = 0; argc >= 2 && m < COUNT(modes); m++) {
        int n = argc - 2;
        if (strcmp(argv[1], modes[m].name) == 0 &&
            (n == modes[m].min_args || (modes[m].variadic && n > modes[m].min_args))) {
            return modes[m].run(n, argv + 2);
        }
    }
    for (size_t m = 0; m < COUNT(modes); m++) {
        char call[64];
        snprintf(call, sizeof(call), "%s %s", modes[m].name, modes[m].args);
        fprintf(stderr, "%s site_stages %-38s %s\n", m ? "      " : "usage:", call, modes[m].what);
    }
    return 2;
}
