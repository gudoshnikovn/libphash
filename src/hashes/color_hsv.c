#include "../internal.h"
#include <math.h>
#include <stdlib.h>

#define MIN3(a, b, c) ((a) < (b) ? ((a) < (c) ? (a) : (c)) : ((b) < (c) ? (b) : (c)))
#define MAX3(a, b, c) ((a) > (b) ? ((a) > (c) ? (a) : (c)) : ((b) > (c) ? (b) : (c)))

PH_API ph_error_t ph_compute_color_hash(ph_context_t *ctx, uint64_t *out_hash) {
    if (!ctx || !ctx->is_loaded || !out_hash) {
        return PH_ERR_INVALID_ARGUMENT;
    }

    int total_pixels = ctx->width * ctx->height;
    int count_black = 0;
    int count_gray = 0;
    int faint_counts[6] = {0};
    int bright_counts[6] = {0};
    int count_colors = 0;

    const uint8_t *src = ctx->data;
    int channels = ctx->channels;

    for (int i = 0; i < total_pixels; i++) {
        float r = src[i * channels];
        float g = (channels >= 3) ? src[i * channels + 1] : r;
        float b = (channels >= 3) ? src[i * channels + 2] : r;

        // Intensity (PIL L conversion)
        float intensity = (r * 299.0f + g * 587.0f + b * 114.0f) / 1000.0f;

        if (intensity < 32.0f) { // 256 // 8 = 32
            count_black++;
            continue;
        }

        float maxc = MAX3(r, g, b);
        float minc = MIN3(r, g, b);
        float s = 0.0f;
        float h = 0.0f;

        if (maxc != minc) {
            float d = maxc - minc;
            s = 255.0f * d / maxc;
            if (r == maxc) {
                h = (g - b) / d;
            } else if (g == maxc) {
                h = 2.0f + (b - r) / d;
            } else {
                h = 4.0f + (r - g) / d;
            }
            h *= 42.5f;
            if (h < 0.0f) {
                h += 255.0f;
            }
        }

        if (s < 85.0f) { // 256 // 3 = 85
            count_gray++;
            continue;
        }

        count_colors++;

        // hue_bins = numpy.linspace(0, 255, 6+1)
        // [0, 42.5, 85, 127.5, 170, 212.5, 255]
        int hue_bin = (int)(h / 42.5f);
        if (hue_bin > 5)
            hue_bin = 5;
        if (hue_bin < 0)
            hue_bin = 0;

        if (s < 170.0f) { // 256 * 2 // 3 = 170
            faint_counts[hue_bin]++;
        } else {
            bright_counts[hue_bin]++;
        }
    }

    double frac_black = (double)count_black / total_pixels;
    double frac_gray = (double)count_gray / total_pixels;
    double c_denom = (count_colors > 0) ? count_colors : 1.0;

    double values[14];
    values[0] = frac_black;
    values[1] = frac_gray;

    for (int i = 0; i < 6; i++) {
        values[2 + i] = (double)faint_counts[i] / c_denom;
        values[8 + i] = (double)bright_counts[i] / c_denom;
    }

    uint64_t hash = 0;
    int binbits = 3;
    int maxvalue = 1 << binbits; // 8

    int shift = 0;
    // We must mimic Python `bitarray += [v // ... % 2 > 0]`
    // ImageHash constructs array of bits. `numpy.packbits` doesn't happen.
    // Wait, let's see how `ImageHash(bitarray.reshape((-1, binbits)))` produces the hash
    // representation. If it's a bitarray, its integer value is `sum([bit * 2^(length - 1 - i)])` or
    // something. Let's match the exact C integer layout:
    for (int i = 0; i < 14; i++) {
        int v = (int)(values[i] * maxvalue);
        if (v > maxvalue - 1)
            v = maxvalue - 1;

        for (int b = 0; b < binbits; b++) {
            int m = binbits - b - 1;
            int bit = ((v >> m) % (1 << (m + 1))) > 0 ? 1 : 0;
            if (bit) {
                // To pack it correctly so that bin(hash) matches sequence of bits
                // MSB is the first bit added (i=0, b=0)
                int bit_idx = 41 - shift;
                hash |= (1ULL << bit_idx);
            }
            shift++;
        }
    }

    *out_hash = hash;
    return PH_SUCCESS;
}
