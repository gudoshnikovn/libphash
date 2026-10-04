#include "context.h"
#include "image/image.h"
#include "safety.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* Declarations only. The stb_image_resize2 implementation is instantiated in
 * src/image/stb_resize_impl.c, which is compiled with -fno-sanitize=alignment
 * because of deliberate unaligned 64-bit moves inside stb (see the
 * comment at the top of that file). Keeping the implementation out of this TU
 * keeps our own code fully sanitizer-instrumented. */
#include "../../vendor/stb_image_resize2.h"

int ph_resize_box(const uint8_t *src, int sw, int sh, uint8_t *dst, int dw, int dh) {
    if (dw <= 0 || dh <= 0 || sw <= 0 || sh <= 0) {
        return 0;
    }
    void *result = stbir_resize(src, sw, sh, 0, dst, dw, dh, 0, STBIR_1CHANNEL, STBIR_TYPE_UINT8,
                                STBIR_EDGE_CLAMP, STBIR_FILTER_BOX);
    return result != NULL;
}

int ph_resize_mitchell(const uint8_t *src, int sw, int sh, uint8_t *dst, int dw, int dh) {
    if (dw <= 0 || dh <= 0 || sw <= 0 || sh <= 0) {
        return 0;
    }

    void *result = stbir_resize(src, sw, sh, 0, dst, dw, dh, 0, STBIR_1CHANNEL, STBIR_TYPE_UINT8,
                                STBIR_EDGE_CLAMP, STBIR_FILTER_MITCHELL);
    return result != NULL;
}

/* Area averaging on one axis, in integer units: the axis is src_len * dst_len units long,
 * source pixel i covers [i * dst_len, (i + 1) * dst_len) and output cell k covers
 * [k * src_len, (k + 1) * src_len). Cell k takes `first` with weight w_first, every pixel
 * strictly between `first` and `last` with the full weight dst_len, and `last` (when it is
 * not `first`) with weight w_last. The weights of a cell add up to src_len. */
typedef struct {
    size_t first, last;
    uint64_t w_first, w_last;
} ph_area_span_t;

static ph_area_span_t ph_area_span(size_t k, size_t src_len, size_t dst_len) {
    const size_t lo = k * src_len, hi = (k + 1) * src_len;
    ph_area_span_t sp;
    sp.first = lo / dst_len;
    sp.last = (hi - 1) / dst_len;
    if (sp.first == sp.last) {
        sp.w_first = src_len;
        sp.w_last = 0;
    } else {
        sp.w_first = (sp.first + 1) * dst_len - lo;
        sp.w_last = hi - sp.last * dst_len;
    }
    return sp;
}

/* Area sums of a whole gray image onto a dw x dh grid: out[r * dw + c] is the sum of every
 * source pixel times its coverage weight, and the cell's mean is that over sw * sh. One
 * pass over the source; the full-weight runs inside a cell are plain byte sums. */
static int ph_area_sums(const uint8_t *src, size_t sw, size_t sh, size_t dw, size_t dh,
                        uint64_t *out) {
    ph_area_span_t *cols = malloc(dw * sizeof(*cols));
    uint64_t *row = malloc(dw * sizeof(*row));
    if (!cols || !row) {
        free(cols);
        free(row);
        return 0;
    }
    for (size_t c = 0; c < dw; c++) {
        cols[c] = ph_area_span(c, sw, dw);
    }
    memset(out, 0, dw * dh * sizeof(*out));

    for (size_t y = 0; y < sh; y++) {
        const uint8_t *p = src + y * sw;
        for (size_t c = 0; c < dw; c++) {
            const ph_area_span_t sp = cols[c];
            uint64_t sum = sp.w_first * p[sp.first];
            if (sp.last > sp.first) {
                uint64_t run = 0; /* a strip image can be ~2^31 pixels wide */
                for (size_t x = sp.first + 1; x < sp.last; x++) {
                    run += p[x];
                }
                sum += (uint64_t)dw * run + sp.w_last * p[sp.last];
            }
            row[c] = sum;
        }
        /* Source row y covers [y * dh, (y + 1) * dh) of the rows' axis; add it to every
         * output row it overlaps, by the overlap. */
        const size_t lo = y * dh, hi = (y + 1) * dh;
        for (size_t r = lo / sh; r <= (hi - 1) / sh; r++) {
            const size_t cell_lo = r * sh, cell_hi = (r + 1) * sh;
            const uint64_t w =
                (uint64_t)((hi < cell_hi ? hi : cell_hi) - (lo > cell_lo ? lo : cell_lo));
            uint64_t *o = out + r * dw;
            for (size_t c = 0; c < dw; c++) {
                o[c] += w * row[c];
            }
        }
    }
    free(cols);
    free(row);
    return 1;
}

/* sum / den rounded half up: exact, the same on every platform. */
static uint8_t ph_area_round(uint64_t sum, uint64_t den) {
    return (uint8_t)((2 * sum + den) / (2 * den));
}

int ph_area_downscale(ph_context_t *ctx, int dw, int dh, uint8_t *dst) {
    if (dw <= 0 || dh <= 0) {
        return 0;
    }
    uint8_t *gray = ph_get_gray(ctx);
    if (!gray) {
        return 0;
    }
    const size_t sw = ph_size(ctx->image.width), sh = ph_size(ctx->image.height);
    const size_t w = ph_size(dw), h = ph_size(dh);
    const uint64_t den = (uint64_t)sw * sh; /* in 64 bits: it can overflow a 32-bit size_t */

    if (sw >= PH_AREA_GRID && sh >= PH_AREA_GRID && PH_AREA_GRID % dw == 0 &&
        PH_AREA_GRID % dh == 0) {
        if (!ctx->image.area_grid_valid) {
            if (!ph_area_sums(gray, sw, sh, PH_AREA_GRID, PH_AREA_GRID, ctx->image.area_grid)) {
                return 0;
            }
            ctx->image.area_grid_valid = 1;
        }
        const size_t kx = PH_AREA_GRID / w, ky = PH_AREA_GRID / h;
        for (size_t r = 0; r < h; r++) {
            for (size_t c = 0; c < w; c++) {
                uint64_t sum = 0;
                for (size_t y = r * ky; y < (r + 1) * ky; y++) {
                    for (size_t x = c * kx; x < (c + 1) * kx; x++) {
                        sum += ctx->image.area_grid[y * PH_AREA_GRID + x];
                    }
                }
                dst[r * w + c] = ph_area_round(sum, den * kx * ky);
            }
        }
        return 1;
    }

    uint64_t *sums = malloc(w * h * sizeof(*sums));
    if (!sums || !ph_area_sums(gray, sw, sh, w, h, sums)) {
        free(sums);
        return 0;
    }
    for (size_t i = 0; i < w * h; i++) {
        dst[i] = ph_area_round(sums[i], den);
    }
    free(sums);
    return 1;
}
