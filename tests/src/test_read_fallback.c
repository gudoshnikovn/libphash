/* The read() fallback of a file load: what happens when the file cannot be mapped.
 *
 * On POSIX a load maps the file, and mmap() of a regular file does not fail in practice,
 * so the fallback -- the main path on Windows -- would never run here. This binary defines
 * mmap() and read() itself; because libphash is linked as a static archive, the library's
 * calls resolve to these definitions (the technique alloc_shim.h uses for malloc). Each
 * forwards to the C library's own symbol, found with dlsym(RTLD_NEXT), unless a test has
 * armed it to fail file mappings or to misbehave in a chosen way.
 *
 * What is held:
 *   - the fallback hands the decoder the same bytes the mapping does (same hashes);
 *   - read() is called in chunks of at most 16 MiB, and a read that returns less than
 *     asked is continued;
 *   - EINTR is retried;
 *   - a read error is PH_ERR_IO with a message;
 *   - a file that reads as empty after fstat() reported a size is PH_ERR_IO;
 *   - a file that ends earlier than fstat() said (it shrank) is not an I/O error: the
 *     bytes that arrived go to the decoder, and a truncated image fails as corrupt data;
 *   - the buffer's allocation failing is PH_ERR_ALLOCATION_FAILED, with nothing leaked.
 *
 * POSIX only, and not under AddressSanitizer, whose own read()/mmap() interceptors would
 * sit in the same place. */
#if defined(__linux__)
#    define _GNU_SOURCE /* RTLD_NEXT */
#endif

#include "alloc_shim.h"
#include "libphash.h"
#include "test_macros.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(__SANITIZE_ADDRESS__)
#    define PH_READ_SHIM 0
#elif defined(__has_feature)
#    if __has_feature(address_sanitizer)
#        define PH_READ_SHIM 0
#    endif
#endif
#ifndef PH_READ_SHIM
#    if defined(__linux__) || defined(__APPLE__)
#        define PH_READ_SHIM 1
#    else
#        define PH_READ_SHIM 0
#    endif
#endif

#if PH_READ_SHIM

#    include <dlfcn.h>
#    include <errno.h>
#    include <sys/mman.h>
#    include <sys/types.h>
#    include <unistd.h>

typedef enum {
    READ_PASS,      /* forward unchanged */
    READ_SMALL,     /* at most 1000 bytes per call */
    READ_EINTR,     /* the first call fails with EINTR, then forward */
    READ_EIO,       /* every call fails with EIO */
    READ_EMPTY,     /* every call returns 0 */
    READ_TRUNCATED, /* forward the first 100 bytes, then 0 */
} read_mode_t;

static struct {
    int fail_file_mmap;
    read_mode_t mode;
    long calls;
    size_t largest_request;
    size_t delivered;
} g_shim;

typedef void *(*mmap_fn)(void *, size_t, int, int, int, off_t);
typedef ssize_t (*read_fn)(int, void *, size_t);
static mmap_fn real_mmap;
static read_fn real_read;

static void resolve_real(void) {
    real_mmap = (mmap_fn)dlsym(RTLD_NEXT, "mmap");
    real_read = (read_fn)dlsym(RTLD_NEXT, "read");
    ASSERT_PTR_NOT_NULL((void *)real_mmap);
    ASSERT_PTR_NOT_NULL((void *)real_read);
}

void *mmap(void *addr, size_t len, int prot, int flags, int fd, off_t off) {
    if (g_shim.fail_file_mmap && fd >= 0) {
        errno = ENODEV; /* what a filesystem that cannot map files reports */
        return MAP_FAILED;
    }
    return real_mmap(addr, len, prot, flags, fd, off);
}

ssize_t read(int fd, void *buf, size_t n) {
    if (!g_shim.fail_file_mmap) {
        return real_read(fd, buf, n);
    }
    g_shim.calls++;
    if (n > g_shim.largest_request) {
        g_shim.largest_request = n;
    }
    ssize_t r;
    switch (g_shim.mode) {
        case READ_SMALL:
            r = real_read(fd, buf, n < 1000 ? n : 1000);
            break;
        case READ_EINTR:
            if (g_shim.calls == 1) {
                errno = EINTR;
                return -1;
            }
            r = real_read(fd, buf, n);
            break;
        case READ_EIO:
            errno = EIO;
            return -1;
        case READ_EMPTY:
            return 0;
        case READ_TRUNCATED:
            if (g_shim.delivered >= 100) {
                return 0;
            }
            r = real_read(fd, buf, n < 100 - g_shim.delivered ? n : 100 - g_shim.delivered);
            break;
        default:
            r = real_read(fd, buf, n);
            break;
    }
    if (r > 0) {
        g_shim.delivered += (size_t)r;
    }
    return r;
}

#    if defined(__GLIBC__)
/* With _FORTIFY_SOURCE, a read() whose buffer size the compiler can see is compiled as
 * a call to __read_chk(). */
ssize_t __read_chk(int fd, void *buf, size_t n, size_t buflen);

ssize_t __read_chk(int fd, void *buf, size_t n, size_t buflen) {
    if (n > buflen) {
        abort();
    }
    return read(fd, buf, n);
}
#    endif

static void arm(read_mode_t mode) {
    memset(&g_shim, 0, sizeof(g_shim));
    g_shim.fail_file_mmap = 1;
    g_shim.mode = mode;
}

static void disarm(void) { g_shim.fail_file_mmap = 0; }

#    define HASH_FLAGS (PH_HASH_AHASH | PH_HASH_DHASH | PH_HASH_PHASH | PH_HASH_WHASH)

static ph_error_t load_hashes(const char *path, uint64_t out[4], char *msg, size_t msg_len) {
    ph_context_t *ctx = NULL;
    ASSERT_OK(ph_create(&ctx));
    ph_error_t err = ph_load_from_file(ctx, path);
    if (err == PH_SUCCESS) {
        ASSERT_OK(ph_compute_multi(ctx, HASH_FLAGS, out));
    }
    if (msg) {
        snprintf(msg, msg_len, "%s", ph_get_last_error_message(ctx));
    }
    ph_free(ctx);
    return err;
}

static void expect_same_as_mapped(const char *path, read_mode_t mode, const char *what) {
    uint64_t mapped[4] = {0}, readback[4] = {0};
    disarm();
    ASSERT_OK(load_hashes(path, mapped, NULL, 0));
    arm(mode);
    ph_error_t err = load_hashes(path, readback, NULL, 0);
    long calls = g_shim.calls;
    disarm();
    ASSERT_MSG(calls > 0, "%s: %s: read() was never called -- the mapping was not refused", what,
               path);
    ASSERT_MSG(err == PH_SUCCESS, "%s: %s: load returned %d", what, path, err);
    ASSERT_MSG(memcmp(mapped, readback, sizeof(mapped)) == 0,
               "%s: %s: hashes differ from the mapped load", what, path);
}

static void test_read_path_matches_mapping(void) {
    expect_same_as_mapped(TEST_DATA_DIR "/photo.jpeg", READ_PASS, "whole reads");
    expect_same_as_mapped(TEST_DATA_DIR "/photo_complex.png", READ_PASS, "whole reads");
    expect_same_as_mapped(TEST_DATA_DIR "/photo.png", READ_SMALL, "1000-byte reads");
    expect_same_as_mapped(TEST_DATA_DIR "/photo.jpeg", READ_EINTR, "EINTR first");
    PASS("test_read_path_matches_mapping");
}

/* A file larger than one chunk: photo.jpeg followed by zeros past its end-of-image
 * marker, which the decoders do not read. */
static void test_large_file_is_read_in_chunks(void) {
    const size_t chunk = (size_t)16 * 1024 * 1024;
    FILE *in = fopen(TEST_DATA_DIR "/photo.jpeg", "rb");
    ASSERT_PTR_NOT_NULL(in);
    char tmp_path[] = "/tmp/ph_read_fallback.XXXXXX";
    int fd = mkstemp(tmp_path);
    ASSERT(fd >= 0);
    FILE *out = fdopen(fd, "wb");
    ASSERT_PTR_NOT_NULL(out);
    char buf[4096];
    size_t n, total = 0;
    while ((n = fread(buf, 1, sizeof(buf), in)) > 0) {
        ASSERT(fwrite(buf, 1, n, out) == n);
        total += n;
    }
    fclose(in);
    memset(buf, 0, sizeof(buf));
    while (total < chunk + 4096) {
        ASSERT(fwrite(buf, 1, sizeof(buf), out) == sizeof(buf));
        total += sizeof(buf);
    }
    fclose(out);

    uint64_t want[4] = {0}, got[4] = {0};
    disarm();
    ASSERT_OK(load_hashes(TEST_DATA_DIR "/photo.jpeg", want, NULL, 0));
    arm(READ_PASS);
    ph_error_t err = load_hashes(tmp_path, got, NULL, 0);
    size_t largest = g_shim.largest_request;
    long calls = g_shim.calls;
    disarm();
    remove(tmp_path);

    ASSERT_MSG(err == PH_SUCCESS, "padded JPEG of %zu bytes: load returned %d", total, err);
    ASSERT_MSG(memcmp(want, got, sizeof(want)) == 0, "padded JPEG hashes differ");
    ASSERT_MSG(largest == chunk, "largest read() request %zu bytes, expected %zu", largest, chunk);
    ASSERT_MSG(calls >= 2, "%ld read() call(s) for a file over one chunk", calls);
    PASS("test_large_file_is_read_in_chunks");
}

static void test_read_failures(void) {
    uint64_t h[4];
    char msg[256];

    arm(READ_EIO);
    ph_error_t err = load_hashes(TEST_DATA_DIR "/photo.png", h, msg, sizeof(msg));
    disarm();
    ASSERT_MSG(err == PH_ERR_IO, "read() error: load returned %d", err);
    ASSERT_MSG(strstr(msg, "Cannot read") != NULL, "read() error: message '%s'", msg);

    arm(READ_EMPTY);
    err = load_hashes(TEST_DATA_DIR "/photo.png", h, msg, sizeof(msg));
    disarm();
    ASSERT_MSG(err == PH_ERR_IO, "empty read: load returned %d", err);
    ASSERT_MSG(strstr(msg, "empty") != NULL, "empty read: message '%s'", msg);

    /* The file shrank between fstat() and read(): its first 100 bytes are a PNG cut off
     * mid-stream, and that is what the decoder is given and rejects. */
    arm(READ_TRUNCATED);
    err = load_hashes(TEST_DATA_DIR "/photo.png", h, msg, sizeof(msg));
    size_t delivered = g_shim.delivered;
    disarm();
    ASSERT_MSG(delivered == 100, "truncated read delivered %zu bytes", delivered);
    ASSERT_MSG(err == PH_ERR_CORRUPT_DATA, "truncated read: load returned %d (%s)", err, msg);

    PASS("test_read_failures");
}

static void test_read_buffer_allocation_failure(void) {
#    if PH_SHIM_SUPPORTED
    ph_context_t *ctx = NULL;
    ASSERT_OK(ph_create(&ctx));
    arm(READ_PASS);
    /* The read buffer is the load's first allocation once the mapping is refused. */
    ph_shim_arm(1);
    ph_error_t err = ph_load_from_file(ctx, TEST_DATA_DIR "/photo.png");
    ph_shim_disarm();
    long calls = g_shim.calls, leaked = ph_shim_live();
    disarm();
    ph_shim_reset();
    ASSERT_MSG(err == PH_ERR_ALLOCATION_FAILED, "load returned %d", err);
    ASSERT_MSG(calls == 0, "read() called %ld time(s) without a buffer", calls);
    ASSERT_MSG(leaked == 0, "%ld allocation(s) leaked", leaked);
    ph_free(ctx);
    PASS("test_read_buffer_allocation_failure");
#    else
    printf("test_read_buffer_allocation_failure: SKIPPED (no allocator shim here)\n");
#    endif
}

int main(void) {
    resolve_real();
    test_read_path_matches_mapping();
    test_large_file_is_read_in_chunks();
    test_read_failures();
    test_read_buffer_allocation_failure();
    printf("All read-fallback tests passed.\n");
    return 0;
}

#else /* !PH_READ_SHIM */

int main(void) {
    printf("test_read_fallback: SKIPPED (needs POSIX symbol interposition, without ASan)\n");
    return 0;
}

#endif
