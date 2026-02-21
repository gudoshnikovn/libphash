#ifndef PH_LOADER_H
#define PH_LOADER_H

#include <setjmp.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

// --- Runtime Loading Status ---
typedef enum { PH_LIB_NOT_LOADED = 0, PH_LIB_LOADED, PH_LIB_FAILED } PhLibStatus;

// --- JPEG Definitions (Minimal ABI) ---
// We avoid including jpeglib.h to keep build zero-dependency.
// These must adhere to standard libjpeg ABI.

#define JPEG_LIB_VERSION 62 // Standard baseline

#ifndef boolean
typedef int boolean;
#endif

typedef struct jpeg_common_struct *j_common_ptr;
typedef struct jpeg_compress_struct *j_compress_ptr;
typedef struct jpeg_decompress_struct *j_decompress_ptr;
typedef struct jpeg_error_mgr *j_error_ptr;

// --- Struct Definitions (ABI Compatible with libjpeg v6b-v8) ---

struct jpeg_common_struct {
    struct jpeg_error_mgr *err;
    struct jpeg_memory_mgr *mem;
    struct jpeg_progress_mgr *progress;
    void *client_data;
    int is_decompressor;
    int global_state;
};

struct jpeg_error_mgr {
    void (*error_exit)(j_common_ptr cinfo);
    void (*emit_message)(j_common_ptr cinfo, int msg_level);
    void (*output_message)(j_common_ptr cinfo);
    void (*format_message)(j_common_ptr cinfo, char *buffer);
    void (*reset_error_mgr)(j_common_ptr cinfo);
    int msg_code;
    union {
        int i[8];
        char s[80];
    } msg_parm;
    int trace_level;
    long num_warnings;
    /* User defined data follows: */
    jmp_buf setjmp_buffer;
};

// Simplified view of decompress struct - sufficient for standard usage
// We rely on the library to handle the internals, we just need the size to match roughly
// and the public fields (image_width, etc) which are at the start.
// WARNING: This is a simplification. For robust prod use, we should be careful.
// However, jpeg_decompress_struct is quite standard. We pad it to be safe.
struct jpeg_decompress_struct {
    struct jpeg_error_mgr *err;
    struct jpeg_memory_mgr *mem;
    struct jpeg_progress_mgr *progress;
    void *client_data;
    int is_decompressor;
    int global_state;

    // Source
    struct jpeg_source_mgr *src;

    // Basic info
    unsigned int image_width;
    unsigned int image_height;
    int num_components;
    int jpeg_color_space;

    // Decompression processing parameters
    int out_color_space;
    unsigned int scale_num, scale_denom;
    double output_gamma;
    boolean buffered_image;
    boolean raw_data_out;
    int dct_method;
    boolean do_fancy_upsampling;
    boolean do_block_smoothing;
    boolean quantize_colors;

    // Description of actual output
    unsigned int output_width;
    unsigned int output_height;
    int out_color_components;
    int output_components;
    int rec_outbuf_height;
    int actual_number_of_colors;
    unsigned char **colormap;

    unsigned int output_scanline;

    // Padding to ensure we cover internal fields of standard libjpeg
    char padding[1024];
};

// --- Definitions ---
// JDIMENSION is unsigned int usually
// J_COLOR_SPACE: JCS_RGB usually 2

#define JCS_RGB 2

// Function definitions
typedef void (*t_jpeg_CreateDecompress)(j_decompress_ptr, int, size_t);
typedef void (*t_jpeg_destroy_decompress)(j_decompress_ptr);
typedef void (*t_jpeg_stdio_src)(j_decompress_ptr, FILE *);
typedef int (*t_jpeg_read_header)(j_decompress_ptr, bool);
typedef boolean (*t_jpeg_start_decompress)(j_decompress_ptr);
typedef unsigned int (*t_jpeg_read_scanlines)(j_decompress_ptr, unsigned char **, unsigned int);
typedef boolean (*t_jpeg_finish_decompress)(j_decompress_ptr);
typedef struct jpeg_error_mgr *(*t_jpeg_std_error)(struct jpeg_error_mgr *);

// Function Table
typedef struct {
    t_jpeg_CreateDecompress jpeg_CreateDecompress;
    t_jpeg_destroy_decompress jpeg_destroy_decompress;
    t_jpeg_stdio_src jpeg_stdio_src;
    t_jpeg_read_header jpeg_read_header;
    t_jpeg_start_decompress jpeg_start_decompress;
    t_jpeg_read_scanlines jpeg_read_scanlines;
    t_jpeg_finish_decompress jpeg_finish_decompress;
    t_jpeg_std_error jpeg_std_error;
} PhJpegFuncs;

// --- Globals ---
extern PhLibStatus g_jpeg_status;
extern PhJpegFuncs g_jpeg;

// --- PNG Definitions (Minimal ABI) ---
// png_struct and png_info are opaque in modern libpng, which is great for us.
typedef struct png_struct_def png_struct;
typedef struct png_info_def png_info;
typedef png_struct *png_structp;
typedef png_info *png_infop;

// Function definitions
typedef png_structp (*t_png_create_read_struct)(const char *user_png_ver, void *error_ptr,
                                                void *error_fn, void *warn_fn);
typedef png_infop (*t_png_create_info_struct)(png_structp png_ptr);
typedef void (*t_png_destroy_read_struct)(png_structp *png_ptr_ptr, png_infop *info_ptr_ptr,
                                          png_infop *end_info_ptr_ptr);
typedef void (*t_png_init_io)(png_structp png_ptr, FILE *fp);
typedef void (*t_png_read_info)(png_structp png_ptr, png_infop info_ptr);
typedef void (*t_png_read_update_info)(png_structp png_ptr, png_infop info_ptr);
typedef void (*t_png_read_image)(png_structp png_ptr, unsigned char **image);

typedef uint32_t (*t_png_get_IHDR)(png_structp png_ptr, png_infop info_ptr, uint32_t *width,
                                   uint32_t *height, int *bit_depth, int *color_type,
                                   int *interlace_method, int *compression_method,
                                   int *filter_method);

// Transforms
typedef void (*t_png_set_expand_gray_1_2_4_to_8)(png_structp png_ptr);
typedef void (*t_png_set_palette_to_rgb)(png_structp png_ptr);
typedef void (*t_png_set_tRNS_to_alpha)(png_structp png_ptr);
typedef void (*t_png_set_strip_16)(png_structp png_ptr);
typedef void (*t_png_set_packing)(png_structp png_ptr);
typedef void (*t_png_set_gray_to_rgb)(png_structp png_ptr);
typedef void (*t_png_set_filler)(png_structp png_ptr, uint32_t filler, int flags);

typedef size_t (*t_png_get_rowbytes)(png_structp png_ptr, png_infop info_ptr);

// Error handling - simplified for dynamic loading
// We might not use libpng's setjmp directly if we can avoid it or use set_error_fn
// But standard libpng usage requires setjmp.
// png_jmpbuf is usually a macro calling png_set_longjmp_fn.
// We will look up png_set_longjmp_fn.
typedef jmp_buf *(*t_png_set_longjmp_fn)(png_structp png_ptr, void (*longjmp_fn)(jmp_buf, int),
                                         size_t jmp_buf_size);

// Function Table
typedef struct {
    t_png_create_read_struct png_create_read_struct;
    t_png_create_info_struct png_create_info_struct;
    t_png_destroy_read_struct png_destroy_read_struct;
    t_png_init_io png_init_io;
    t_png_read_info png_read_info;
    t_png_read_update_info png_read_update_info;
    t_png_read_image png_read_image;
    t_png_get_IHDR png_get_IHDR;
    t_png_get_rowbytes png_get_rowbytes;
    t_png_set_longjmp_fn png_set_longjmp_fn;

    // Transforms
    t_png_set_expand_gray_1_2_4_to_8 png_set_expand_gray_1_2_4_to_8;
    t_png_set_palette_to_rgb png_set_palette_to_rgb;
    t_png_set_tRNS_to_alpha png_set_tRNS_to_alpha;
    t_png_set_strip_16 png_set_strip_16;
    t_png_set_packing png_set_packing;
    t_png_set_gray_to_rgb png_set_gray_to_rgb;
    t_png_set_filler png_set_filler;
} PhPngFuncs;

extern PhLibStatus g_png_status;
extern PhPngFuncs g_png;

// --- API ---
void ph_load_libs(void);
bool ph_can_use_libjpeg(void);
bool ph_can_use_libpng(void);

// Decodes a JPEG file using the loaded library. Returns NULL on failure.
unsigned char *ph_decode_jpeg_turbo(const char *filepath, int *width, int *height, int *channels);

// Decodes a PNG file using the loaded library. Returns NULL on failure.
unsigned char *ph_decode_png(const char *filepath, int *width, int *height, int *channels);

#endif // PH_LOADER_H
