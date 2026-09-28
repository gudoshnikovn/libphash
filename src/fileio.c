#ifndef _DEFAULT_SOURCE
#    define _DEFAULT_SOURCE
#endif
#ifndef _POSIX_C_SOURCE
#    define _POSIX_C_SOURCE 200809L
#endif

#include "fileio.h"

#include "safety.h"

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#    include <fcntl.h>
#    include <io.h>
#    include <sys/stat.h>
#    include <sys/types.h>
#else
#    include <fcntl.h>
#    include <sys/mman.h>
#    include <sys/stat.h>
#    include <unistd.h>
#endif

/* One spelling of "open a file read-only, stat the descriptor and read from it"
 * for both platforms, so the file-loading path below is a single piece of code
 * rather than a POSIX implementation and a Windows one that can drift apart. The
 * Windows CRT (_open/_fstat64/_read/_close) is used rather than the Win32 API on
 * purpose: it sets errno the same way, which is what the diagnostic message is
 * built from. */
#ifdef _WIN32
typedef struct __stat64 ph_file_stat_t;
#    define PH_FILE_OPEN_RDONLY(path) _open((path), _O_RDONLY | _O_BINARY)
#    define PH_FILE_FSTAT(fd, st)     _fstat64((fd), (st))
#    define PH_FILE_READ(fd, buf, n)  _read((fd), (buf), (unsigned int)(n))
#    define PH_FILE_CLOSE(fd)         _close(fd)
#    ifndef S_ISREG
#        define S_ISREG(m) (((m) & _S_IFMT) == _S_IFREG)
#    endif
#else
typedef struct stat ph_file_stat_t;
#    define PH_FILE_OPEN_RDONLY(path) open((path), O_RDONLY)
#    define PH_FILE_FSTAT(fd, st)     fstat((fd), (st))
#    define PH_FILE_READ(fd, buf, n)  read((fd), (buf), (n))
#    define PH_FILE_CLOSE(fd)         close(fd)
#endif

/* Whether the file can be mapped instead of copied. Mapping is what makes a load
 * cost one open() and no read() at all; the read-into-heap fallback in
 * ph_open_file_bytes() covers the platforms that have no mmap (Windows) and the
 * filesystems that refuse to map. Nothing outside these two helpers knows which
 * of the two produced the bytes. It does not depend on the PH_USE_* decoder
 * macros: <sys/mman.h> must stay out of Windows builds whatever decoders are
 * enabled. */
#if !defined(_WIN32) && defined(_POSIX_MAPPED_FILES)
#    define PH_HAVE_MMAP 1
#endif

/* --- Classifying a path that cannot serve as an image source -------------------
 *
 * These three helpers are deliberately split so that a caller which already holds
 * an open descriptor (or which opens the file once for reading and mapping) can
 * reuse the classification without opening the path a second time: the failure
 * branch takes an errno, the success branch takes a descriptor, and only the
 * convenience wrapper does an open()/close() of its own. */

/* printf into a fixed-size diagnostic buffer, with ph_set_err_msg()'s guarantee that a
 * truncation never splits a UTF-8 character. The file messages quote a caller's path, and
 * a path in Cyrillic or CJK reaches the buffer's end in about 75 characters. */
static void ph_format_err_msg(char *err_buf, size_t err_len, const char *fmt, ...)
    PH_PRINTF_FORMAT(3, 4);

static void ph_format_err_msg(char *err_buf, size_t err_len, const char *fmt, ...) {
    if (!err_buf || err_len == 0) {
        return;
    }
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(err_buf, err_len, fmt, ap);
    va_end(ap);
    if (n < 0) {
        err_buf[0] = '\0';
        return;
    }
    if ((size_t)n >= err_len) {
        size_t kept = err_len - 1;
        err_buf[ph_utf8_cut(err_buf, kept)] = '\0';
    }
}

/* Turns a failed open into PH_ERR_IO with a diagnostic message. `open_errno` must
 * be errno captured immediately after the failing open -- both POSIX and the
 * Windows CRT set it (ENOENT for a missing path or a dangling symlink, EACCES for
 * an unreadable one, and on Windows also for a directory). */
static ph_error_t ph_report_file_open_failure(const char *filepath, int open_errno, char *err_buf,
                                              size_t err_len) {
    ph_format_err_msg(err_buf, err_len, "Cannot open '%s': %s", filepath, strerror(open_errno));
    return PH_ERR_IO;
}

/* Checks a descriptor that opened successfully. An image source has to be a
 * regular file with at least one byte in it: a directory opens fine on POSIX, and
 * so do character devices and FIFOs, but none of them is something a decoder can
 * be handed, and an empty file is an I/O-level fact rather than an unrecognized
 * image format. Returns PH_SUCCESS otherwise, reporting the size through
 * `out_size` when it is non-NULL. */
static ph_error_t ph_check_open_file(int fd, const char *filepath, long long *out_size,
                                     char *err_buf, size_t err_len) {
    ph_file_stat_t st;
    if (PH_FILE_FSTAT(fd, &st) != 0) {
        ph_format_err_msg(err_buf, err_len, "Cannot stat '%s': %s", filepath, strerror(errno));
        return PH_ERR_IO;
    }
    if (!S_ISREG(st.st_mode)) {
        ph_format_err_msg(err_buf, err_len, "Cannot read '%s': not a regular file", filepath);
        return PH_ERR_IO;
    }
    if (st.st_size <= 0) {
        ph_format_err_msg(err_buf, err_len, "Cannot read '%s': file is empty", filepath);
        return PH_ERR_IO;
    }
    if (out_size) {
        *out_size = (long long)st.st_size;
    }
    return PH_SUCCESS;
}

void ph_release_file_bytes(ph_file_bytes_t *fb) {
    if (!fb->data) {
        return;
    }
#ifdef PH_HAVE_MMAP
    if (fb->mapped) {
        munmap((void *)(uintptr_t)fb->data, fb->length);
        fb->data = NULL;
        fb->length = 0;
        return;
    }
#endif
    free((void *)(uintptr_t)fb->data);
    fb->data = NULL;
    fb->length = 0;
}

/* Fallback when the file cannot be mapped: read all of it through the descriptor
 * that is already open, so this still costs no extra open(). A short read is not
 * an error -- the file may legitimately have shrunk since the fstat() above, and
 * whatever bytes did arrive are handed to the decoder, which is the one that gets
 * to say whether they form an image. */
static ph_error_t ph_read_open_file(int fd, const char *filepath, size_t size, ph_file_bytes_t *out,
                                    char *err_buf, size_t err_len) {
    uint8_t *buf = (uint8_t *)malloc(size);
    if (!buf) {
        ph_format_err_msg(err_buf, err_len, "Cannot read '%s': out of memory for %llu bytes",
                          filepath, (unsigned long long)size);
        return PH_ERR_ALLOCATION_FAILED;
    }

    size_t got = 0;
    while (got < size) {
        size_t want = size - got;
        /* One chunk stays well inside the signed return type of read()/_read()
         * on every platform, including a 32-bit one. */
        if (want > (size_t)16 * 1024 * 1024) {
            want = (size_t)16 * 1024 * 1024;
        }
        long long n = (long long)PH_FILE_READ(fd, buf + got, want);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            ph_format_err_msg(err_buf, err_len, "Cannot read '%s': %s", filepath, strerror(errno));
            free(buf);
            return PH_ERR_IO;
        }
        if (n == 0) {
            break; /* EOF earlier than fstat() promised */
        }
        got += (size_t)n;
    }

    if (got == 0) {
        ph_format_err_msg(err_buf, err_len, "Cannot read '%s': file is empty", filepath);
        free(buf);
        return PH_ERR_IO;
    }

    out->data = buf;
    out->length = got;
    out->mapped = 0;
    return PH_SUCCESS;
}

/* Opens the path exactly ONCE and hands back all of its bytes.
 *
 * Opening the path more than once -- to probe it, sniff its magic bytes, map it,
 * decode it, scan it for an EXIF tag -- would cost I/O on the hottest path in the
 * library, and every step could see a different file (TOCTOU): the path can be
 * replaced between a probe that accepts it and the read that decodes it.
 * So there is one open, one set of bytes, and everything downstream -- format dispatch,
 * pixel-limit check, decode, orientation scan -- works on that one snapshot.
 *
 * Everything that makes a path unusable as an image source is classified here,
 * before any decoder sees it, so "I could not read this file" is reported as
 * PH_ERR_IO instead of surfacing later as an unrecognized image format. */
ph_error_t ph_open_file_bytes(const char *filepath, ph_file_bytes_t *out, char *err_buf,
                              size_t err_len) {
    out->data = NULL;
    out->length = 0;
    out->mapped = 0;

    int fd = PH_FILE_OPEN_RDONLY(filepath);
    if (fd < 0) {
        return ph_report_file_open_failure(filepath, errno, err_buf, err_len);
    }

    long long size = 0;
    ph_error_t err = ph_check_open_file(fd, filepath, &size, err_buf, err_len);
    if (err != PH_SUCCESS) {
        PH_FILE_CLOSE(fd);
        return err;
    }

    /* Only reachable where size_t is narrower than off_t (a 32-bit build looking
     * at a >4 GB file). Neither mapping nor reading it can work. */
    if ((unsigned long long)size > (unsigned long long)SIZE_MAX) {
        ph_format_err_msg(err_buf, err_len,
                          "Cannot read '%s': file is too large to load into memory", filepath);
        PH_FILE_CLOSE(fd);
        return PH_ERR_IO;
    }

#ifdef PH_HAVE_MMAP
    void *mapped = mmap(NULL, (size_t)size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (mapped != MAP_FAILED) {
#    if defined(__linux__) || defined(__APPLE__) || defined(__FreeBSD__)
        posix_madvise(mapped, (size_t)size, POSIX_MADV_SEQUENTIAL);
#    endif
        /* The mapping keeps the file alive on its own; the descriptor is not
         * needed past this point. */
        PH_FILE_CLOSE(fd);
        out->data = (const uint8_t *)mapped;
        out->length = (size_t)size;
        out->mapped = 1;
        return PH_SUCCESS;
    }
#endif

    err = ph_read_open_file(fd, filepath, (size_t)size, out, err_buf, err_len);
    PH_FILE_CLOSE(fd);
    return err;
}
