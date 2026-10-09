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

/* The message every classification below writes; its contract is in fileio.h. */
void ph_format_path_msg(char *err_buf, size_t err_len, const char *verb, const char *path,
                        const char *reason_fmt, ...) {
    if (!err_buf || err_len == 0) {
        return;
    }
    char reason[128];
    va_list ap;
    va_start(ap, reason_fmt);
    int n = vsnprintf(reason, sizeof(reason), reason_fmt, ap);
    va_end(ap);
    if (n < 0) {
        reason[0] = '\0';
    } else if ((size_t)n >= sizeof(reason)) {
        reason[ph_utf8_cut(reason, sizeof(reason) - 1)] = '\0';
    }

    static const char ellipsis[] = "...";
    size_t path_len = strlen(path);
    size_t fixed = strlen(verb) + strlen(" '") + strlen("': ") + strlen(reason);
    if (fixed + path_len < err_len) {
        snprintf(err_buf, err_len, "%s '%s': %s", verb, path, reason);
        return;
    }
    /* Room for the path's two ends; a buffer too small for even that gets the plain
     * message, truncated. */
    if (fixed + sizeof(ellipsis) - 1 + 2 >= err_len) {
        snprintf(err_buf, err_len, "%s '%s': %s", verb, path, reason);
        err_buf[ph_utf8_cut(err_buf, strlen(err_buf))] = '\0';
        return;
    }
    size_t room = err_len - 1 - fixed - (sizeof(ellipsis) - 1);
    size_t head = ph_utf8_cut(path, room / 2);
    size_t tail_start = path_len - (room - room / 2);
    while (tail_start < path_len && ((unsigned char)path[tail_start] & 0xC0) == 0x80) {
        tail_start++; /* a continuation byte: start at the next character */
    }
    snprintf(err_buf, err_len, "%s '%.*s%s%s': %s", verb, (int)head, path, ellipsis,
             path + tail_start, reason);
}

/* Turns a failed open into PH_ERR_IO with a diagnostic message. `open_errno` must
 * be errno captured immediately after the failing open -- both POSIX and the
 * Windows CRT set it (ENOENT for a missing path or a dangling symlink, EACCES for
 * an unreadable one, and on Windows also for a directory). */
static ph_error_t ph_report_file_open_failure(const char *filepath, int open_errno, char *err_buf,
                                              size_t err_len) {
    ph_format_path_msg(err_buf, err_len, "Cannot open", filepath, "%s", strerror(open_errno));
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
    /* LCOV_EXCL_START -- excluded from coverage: fstat() on a descriptor just opened
     * does not fail outside a kernel or filesystem fault. */
    if (PH_FILE_FSTAT(fd, &st) != 0) {
        ph_format_path_msg(err_buf, err_len, "Cannot stat", filepath, "%s", strerror(errno));
        return PH_ERR_IO;
    }
    /* LCOV_EXCL_STOP */
    if (!S_ISREG(st.st_mode)) {
        ph_format_path_msg(err_buf, err_len, "Cannot read", filepath, "not a regular file");
        return PH_ERR_IO;
    }
    if (st.st_size <= 0) {
        ph_format_path_msg(err_buf, err_len, "Cannot read", filepath, "file is empty");
        return PH_ERR_IO;
    }
    if (out_size) {
        *out_size = st.st_size;
    }
    return PH_SUCCESS;
}

void ph_release_file_bytes(ph_file_bytes_t *fb) {
    if (!fb->data) {
        return;
    }
#ifdef PH_HAVE_MMAP
    if (fb->mapped) {
        /* The bytes are read-only to every reader; this layer owns them and drops the
         * const to release them. */
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
#define PH_READ_CHUNK ((size_t)16 * 1024 * 1024)

static ph_error_t ph_read_open_file(int fd, const char *filepath, size_t size, ph_file_bytes_t *out,
                                    char *err_buf, size_t err_len) {
    uint8_t *buf = malloc(size);
    if (!buf) {
        ph_format_path_msg(err_buf, err_len, "Cannot read", filepath,
                           "out of memory for %llu bytes", (unsigned long long)size);
        return PH_ERR_ALLOCATION_FAILED;
    }

    size_t got = 0;
    while (got < size) {
        size_t want = size - got;
        /* One chunk stays well inside the signed return type of read()/_read()
         * on every platform, including a 32-bit one. */
        if (want > PH_READ_CHUNK) {
            want = PH_READ_CHUNK;
        }
        long long n = PH_FILE_READ(fd, buf + got, want);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            ph_format_path_msg(err_buf, err_len, "Cannot read", filepath, "%s", strerror(errno));
            free(buf);
            return PH_ERR_IO;
        }
        if (n == 0) {
            break; /* EOF earlier than fstat() promised */
        }
        got += (size_t)n; /* n > 0 here */
    }

    if (got == 0) {
        ph_format_path_msg(err_buf, err_len, "Cannot read", filepath, "file is empty");
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
    /* LCOV_EXCL_START -- excluded from coverage: needs a 32-bit size_t and a file over 4 GiB. */
    if ((unsigned long long)size > (unsigned long long)SIZE_MAX) {
        ph_format_path_msg(err_buf, err_len, "Cannot read", filepath,
                           "file is too large to load into memory");
        PH_FILE_CLOSE(fd);
        return PH_ERR_IO;
    }
    /* LCOV_EXCL_STOP */
    const size_t length = (size_t)size; /* positive and at most SIZE_MAX, checked above */

#ifdef PH_HAVE_MMAP
    void *mapped = mmap(NULL, length, PROT_READ, MAP_PRIVATE, fd, 0);
    if (mapped != MAP_FAILED) {
#    if defined(__linux__) || defined(__APPLE__) || defined(__FreeBSD__)
        posix_madvise(mapped, length, POSIX_MADV_SEQUENTIAL);
#    endif
        /* The mapping keeps the file alive on its own; the descriptor is not
         * needed past this point. */
        PH_FILE_CLOSE(fd);
        out->data = mapped;
        out->length = length;
        out->mapped = 1;
        return PH_SUCCESS;
    }
#endif

    err = ph_read_open_file(fd, filepath, length, out, err_buf, err_len);
    PH_FILE_CLOSE(fd);
    return err;
}
