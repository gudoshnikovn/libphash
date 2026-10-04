#include "loader.h"
#include "loaders/backends.h"
#include "safety.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>

#ifdef PH_USE_LIBJPEG_TURBO

/* The vendored libjpeg-turbo, through its libjpeg API (the jpeg-static archive). The
 * TurboJPEG archive would bring its own copies of zlib and spng with global symbols -- for
 * tj3LoadImage()/tj3SaveImage(), which this library never calls -- and on macOS libpng
 * would bind to that zlib instead of the vendored zlib-ng. jpeg-static is the codec
 * alone.
 *
 * Decode settings: the fast integer IDCT, fancy (smooth) chroma upsampling, DCT-domain
 * scaling for decode_scale, and any libjpeg warning (a truncated stream, stray bytes
 * before a marker) treated as a failure. These settings fix the decoded pixels, so
 * changing any of them changes hash values. */
#    include <setjmp.h>
#    include <string.h>

/* jpeglib.h first: it pulls in jconfig.h, whose JPEG_LIB_VERSION jerror.h tests to
 * decide which message codes exist. Sorted, jerror.h would come first and see the macro
 * undefined (-Wundef), so the include sorter is kept off this pair. */
// clang-format off
#    include "jpeglib.h"
#    include "jerror.h"
// clang-format on

int ph_can_read_jpeg(const uint8_t *magic, size_t len) {
    return len >= 2 && magic[0] == 0xFF && magic[1] == 0xD8;
}

/* libjpeg reports errors by calling error_exit(), which must not return; the default one
 * calls exit(). This one records the message and jumps back into ph_decode_jpeg_mem(). */
typedef struct {
    struct jpeg_error_mgr pub;
    jmp_buf escape;
    char message[JMSG_LENGTH_MAX];
    int out_of_memory;
    /* The caller's outputs, read through here everywhere after setjmp(): a parameter used
     * past that point may live in a register that longjmp() does not restore (GCC:
     * -Wclobbered), while this struct is in memory, its address handed to libjpeg. */
    ph_error_t *out_err;
    char *err_msg;
    size_t err_msg_cap;
} ph_jpeg_error_t;

static void ph_jpeg_error_exit(j_common_ptr cinfo) {
    ph_jpeg_error_t *err = (ph_jpeg_error_t *)cinfo->err;
    err->pub.format_message(cinfo, err->message);
    /* By code, not by the wording of the message: libjpeg's memory manager reports every
     * failed allocation as JERR_OUT_OF_MEMORY. */
    err->out_of_memory = (err->pub.msg_code == JERR_OUT_OF_MEMORY);
    longjmp(err->escape, 1);
}

/* msg_level < 0 is a warning: libjpeg recovered from damaged data and would carry on,
 * handing back an image with made-up content. A warning is a failure here, and the first
 * one ends the decode -- nothing after it is worth decoding.
 * Trace messages (msg_level >= 0) are ignored; nothing is ever printed to stderr. */
static void ph_jpeg_emit_message(j_common_ptr cinfo, int msg_level) {
    if (msg_level < 0) {
        ph_jpeg_error_exit(cinfo);
    }
}

static void ph_jpeg_output_message(j_common_ptr cinfo) { (void)cinfo; }

/* decode_scale -> libjpeg's scale_num/scale_denom. libjpeg then picks the output size
 * itself (jpeg_calc_output_dimensions(): ceil(dimension * num / denom)), so an image with
 * a side that is not a multiple of the denominator rounds up, never down to zero. */
static unsigned int ph_jpeg_scale_denom(ph_decode_scale_t decode_scale) {
    switch (decode_scale) {
        case PH_DECODE_SCALE_HALF:
            return 2;
        case PH_DECODE_SCALE_QUARTER:
            return 4;
        case PH_DECODE_SCALE_EIGHTH:
            return 8;
        case PH_DECODE_SCALE_FULL:
        case PH_DECODE_SCALE_FORCE_INT32_: /* width spacer; the setter never stores it */
        default:
            return 1;
    }
}

unsigned char *ph_decode_jpeg_mem(const unsigned char *buffer, size_t size, int *width, int *height,
                                  int *channels, int req_comp, uint64_t max_pixels,
                                  ph_decode_scale_t decode_scale, ph_error_t *out_err,
                                  char *err_msg, size_t err_msg_cap) {
    if (!buffer || size == 0) {
        return NULL;
    }
    /* jpeg_mem_src() takes the size as unsigned long, 32 bits on Windows x64. The loader
     * refuses anything over PH_MAX_ENCODED_SIZE (INT_MAX) before a backend sees it, so the
     * cast below cannot truncate. */
    if (size > PH_MAX_ENCODED_SIZE) {
        if (out_err) {
            *out_err = PH_ERR_IMAGE_TOO_LARGE;
        }
        ph_set_err_msg(err_msg, err_msg_cap, "Encoded image is larger than 2 GiB - 1 byte");
        return NULL;
    }

    struct jpeg_decompress_struct cinfo;
    ph_jpeg_error_t jerr;
    memset(&cinfo, 0, sizeof(cinfo));
    memset(&jerr, 0, sizeof(jerr));

    /* Written between setjmp() and a possible longjmp(), read after it: volatile, or the
     * values seen on the error path are indeterminate. */
    unsigned char *volatile output = NULL;
    JSAMPROW *volatile rows = NULL;

    cinfo.err = jpeg_std_error(&jerr.pub);
    jerr.out_err = out_err;
    jerr.err_msg = err_msg;
    jerr.err_msg_cap = err_msg_cap;
    jerr.pub.error_exit = ph_jpeg_error_exit;
    jerr.pub.emit_message = ph_jpeg_emit_message;
    jerr.pub.output_message = ph_jpeg_output_message;
    if (setjmp(jerr.escape)) {
        jpeg_destroy_decompress(&cinfo);
        free(output);
        free(rows);
        if (jerr.out_err) {
            *jerr.out_err = jerr.out_of_memory ? PH_ERR_ALLOCATION_FAILED : PH_ERR_CORRUPT_DATA;
        }
        ph_set_err_msg(jerr.err_msg, jerr.err_msg_cap, jerr.message);
        return NULL;
    }

    jpeg_create_decompress(&cinfo);
    jpeg_mem_src(&cinfo, buffer, (unsigned long)size);
    if (jpeg_read_header(&cinfo, TRUE) != JPEG_HEADER_OK) {
        /* A tables-only stream: valid JPEG syntax, no image in it. */
        jpeg_destroy_decompress(&cinfo);
        if (jerr.out_err) {
            *jerr.out_err = PH_ERR_CORRUPT_DATA;
        }
        ph_set_err_msg(jerr.err_msg, jerr.err_msg_cap, "JPEG stream contains no image");
        return NULL;
    }

    /* The pixel-count limit is judged against the file's declared full-resolution
     * dimensions, not the requested decode size: it exists to reject decompression
     * bombs, which a scale request does not make safe -- the header can still claim an
     * enormous image regardless of what the caller asked to receive. */
    if (ph_exceeds_pixel_limit((uint64_t)cinfo.image_width, (uint64_t)cinfo.image_height,
                               max_pixels)) {
        jpeg_destroy_decompress(&cinfo);
        if (jerr.out_err) {
            *jerr.out_err = PH_ERR_IMAGE_TOO_LARGE;
        }
        ph_set_err_msg(jerr.err_msg, jerr.err_msg_cap,
                       "Image exceeds the configured maximum pixel count");
        return NULL;
    }

    const int out_channels = (req_comp == 1) ? 1 : 3;
    cinfo.out_color_space = (req_comp == 1) ? JCS_GRAYSCALE : JCS_RGB;
    cinfo.dct_method = JDCT_IFAST;
    cinfo.do_fancy_upsampling = TRUE;
    cinfo.scale_num = 1;
    cinfo.scale_denom = ph_jpeg_scale_denom(decode_scale);

    jpeg_start_decompress(&cinfo);
    if (cinfo.output_components != out_channels) {
        /* Should not happen for the two colour spaces requested above; checked because
         * the buffer below is sized by out_channels, not by what libjpeg writes. */
        jpeg_destroy_decompress(&cinfo);
        if (jerr.out_err) {
            *jerr.out_err = PH_ERR_CORRUPT_DATA;
        }
        ph_set_err_msg(jerr.err_msg, jerr.err_msg_cap, "Unexpected JPEG output component count");
        return NULL;
    }

    const JDIMENSION w = cinfo.output_width, h = cinfo.output_height;
    size_t stride, total;
    if (w > INT_MAX || h > INT_MAX ||
        !ph_safe_image_alloc_size((uint64_t)w, (uint64_t)out_channels, 1, &stride) ||
        !ph_safe_image_alloc_size((uint64_t)stride, (uint64_t)h, 1, &total)) {
        jpeg_destroy_decompress(&cinfo);
        if (jerr.out_err) {
            *jerr.out_err = PH_ERR_IMAGE_TOO_LARGE;
        }
        ph_set_err_msg(jerr.err_msg, jerr.err_msg_cap,
                       "Image exceeds the configured maximum pixel count");
        return NULL;
    }

    output = (unsigned char *)malloc(total);
    rows = (JSAMPROW *)malloc(sizeof(JSAMPROW) * (size_t)h);
    if (!output || !rows) {
        jpeg_destroy_decompress(&cinfo);
        free(output);
        free(rows);
        if (jerr.out_err) {
            *jerr.out_err = PH_ERR_ALLOCATION_FAILED;
        }
        ph_set_err_msg(jerr.err_msg, jerr.err_msg_cap, "Memory allocation failed");
        return NULL;
    }
    for (JDIMENSION y = 0; y < h; y++) {
        rows[y] = output + (size_t)y * stride;
    }

    /* Every row pointer at once: libjpeg writes straight into the output instead of
     * staging rows in a buffer of its own. */
    while (cinfo.output_scanline < h) {
        jpeg_read_scanlines(&cinfo, rows + cinfo.output_scanline, h - cinfo.output_scanline);
    }
    jpeg_finish_decompress(&cinfo);
    jpeg_destroy_decompress(&cinfo);
    free(rows);

    *width = (int)w;
    *height = (int)h;
    *channels = out_channels;
    return output;
}

#endif // PH_USE_LIBJPEG_TURBO
