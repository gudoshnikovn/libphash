#ifndef PH_CONTEXT_H
#define PH_CONTEXT_H

/* The context: struct ph_context and its configuration. */

#include "arena.h"
#include "libphash.h"
#include <stddef.h>
#include <stdint.h>

/* Max length (including NUL) of the diagnostic message stashed by a failed load. */
#define PH_LAST_ERROR_MAX 160

/* Internal Context Structure */
struct ph_context {
    // Uploaded image data
    struct {
        uint8_t *raw_rgb;
        uint8_t *gray_cache;
        int width;
        int height;
        int channels;
        int is_loaded;
    } image;

    // Diagnostic message for the most recent failed load; empty string if none.
    char last_error[PH_LAST_ERROR_MAX];

    // User-defined configuration parameters. Plain values only, no pointers: the batch
    // API copies it by value from a caller's template context into each worker's.
    struct ph_context_config {
        // gamma is applied per image, normalised by the blurred buffer's own maximum;
        // see ph_apply_gamma(), src/image/color.c.
        float gamma;
        int gray_r, gray_g, gray_b;
        int load_grayscale;
        int auto_orient; // On by default: see ph_context_set_auto_orient().

        // Various tunings for hashes
        float mhash_alpha;
        float mhash_level;
        int mhash_size;
        int phash_dct_size;
        int phash_reduction_size;
        int radial_projections;
        int radial_samples;
        float radial_sigma;
        int block_size;
        ph_whash_mode_t whash_mode;
        int whash_remove_max_haar_ll;

        // 0 = no caller limit; PH_MAX_SUPPORTED_PIXELS still applies. Otherwise the max
        // allowed width*height before decoding a pixel buffer.
        uint64_t max_pixels;

        // See ph_context_set_decode_scale(). Only the JPEG backend honors this; other
        // backends decode at full resolution regardless.
        ph_decode_scale_t decode_scale;
    } config;

    // Scratch arena for the algorithms' working buffers; its fields are src/arena.c's.
    ph_arena_t arena;
};

/* Fills a configuration with the defaults of a freshly created context (src/core.c). */
void ph_config_init_defaults(struct ph_context_config *config);

#endif /* PH_CONTEXT_H */
