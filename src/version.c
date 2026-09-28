#include "libphash.h"
#include "phash_version.h"

PH_API const char *ph_version(void) { return PH_VERSION_STRING; }

PH_API int ph_version_number(void) { return PH_VERSION_NUMBER; }

/* Every piece is decided by the preprocessor, so the whole line is one string literal:
 * nothing to allocate, nothing to get wrong at run time. The PH_USE_* and
 * PH_ENABLE_* macros are the same ones that select the code, so the line cannot claim
 * a backend that was not compiled in. */
#if defined(PH_USE_LIBJPEG_TURBO)
#define PH_BUILD_JPEG "libjpeg-turbo"
#else
#define PH_BUILD_JPEG "stb"
#endif
#if defined(PH_USE_LIBPNG)
#define PH_BUILD_PNG "libpng"
#elif defined(PH_USE_SPNG)
#define PH_BUILD_PNG "spng"
#else
#define PH_BUILD_PNG "stb"
#endif
#if defined(PH_USE_WEBP)
#define PH_BUILD_WEBP "libwebp"
#else
#define PH_BUILD_WEBP "none"
#endif
#if defined(PH_USE_ZLIB_NG)
#define PH_BUILD_ZLIB "zlib-ng"
#elif defined(PH_USE_LIBPNG) || defined(PH_USE_SPNG)
#define PH_BUILD_ZLIB "zlib"
#else
#define PH_BUILD_ZLIB "none"
#endif
#if defined(PH_ENABLE_THREADS)
#define PH_BUILD_THREADS "on"
#else
#define PH_BUILD_THREADS "off"
#endif
#if defined(__AVX2__)
#define PH_BUILD_SIMD "avx2"
#elif defined(__SSE4_2__)
#define PH_BUILD_SIMD "sse4.2"
#elif defined(__ARM_NEON) || defined(__ARM_NEON__)
#define PH_BUILD_SIMD "neon"
#else
#define PH_BUILD_SIMD "none"
#endif
#if defined(PH_ENABLE_MOCK_BACKEND)
#define PH_BUILD_MOCK "on"
#else
#define PH_BUILD_MOCK "off"
#endif

/* The same macros select the backends, so neither these nor ph_get_build_info() can claim
 * a decoder that was not compiled in. */
PH_API int ph_can_use_jpeg(void) {
#if defined(PH_USE_LIBJPEG_TURBO)
    return 1;
#else
    return 0;
#endif
}

PH_API int ph_can_use_png(void) {
#if defined(PH_USE_LIBPNG) || defined(PH_USE_SPNG)
    return 1;
#else
    return 0;
#endif
}

PH_API int ph_can_use_webp(void) {
#if defined(PH_USE_WEBP)
    return 1;
#else
    return 0;
#endif
}

PH_API const char *ph_get_build_info(void) {
    return "version=" PH_VERSION_STRING " jpeg=" PH_BUILD_JPEG " png=" PH_BUILD_PNG
           " webp=" PH_BUILD_WEBP " zlib=" PH_BUILD_ZLIB " threads=" PH_BUILD_THREADS
           " simd=" PH_BUILD_SIMD " mock=" PH_BUILD_MOCK;
}
