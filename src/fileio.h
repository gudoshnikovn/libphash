#ifndef PH_FILEIO_H
#define PH_FILEIO_H

/* A file's encoded bytes, from exactly one open(): mapped where the platform allows it,
 * read into the heap otherwise. Every reason a path cannot serve as an image source is
 * classified here, as PH_ERR_IO with a diagnostic message, before any decoder sees it. */

#include "libphash.h"
#include "safety.h"

#include <stddef.h>
#include <stdint.h>

/* A read-only view of a file's contents: either a mapping of the file (the normal
 * case) or a heap copy of it (the fallback). `mapped` says which one it is, i.e.
 * how it has to be released; nothing above this layer needs to care. */
typedef struct {
    const uint8_t *data;
    size_t length;
    int mapped;
} ph_file_bytes_t;

/* Opens `filepath` once and hands back all of its bytes in *out; release them with
 * ph_release_file_bytes(). On failure *out is empty and err_buf (may be NULL) holds the
 * reason. */
ph_error_t ph_open_file_bytes(const char *filepath, ph_file_bytes_t *out, char *err_buf,
                              size_t err_len);

void ph_release_file_bytes(ph_file_bytes_t *fb);

/* Writes "<verb> '<path>': <reason>" into a diagnostic buffer, the reason printf-style
 * and cut to 127 bytes. A context's buffer (PH_LAST_ERROR_MAX) holds any path the
 * operating system opens, so the message is whole. A path longer still -- one the system
 * refuses as too long, or a Windows long path -- keeps its start and its end around "...",
 * so the reason, the one part the caller does not already know, always survives; a
 * buffer too small even for that gets the plain message, truncated. No cut splits a UTF-8
 * character: a path in Cyrillic or CJK is two or three bytes a character. err_buf may be
 * NULL or err_len 0, and then nothing is written. */
void ph_format_path_msg(char *err_buf, size_t err_len, const char *verb, const char *path,
                        const char *reason_fmt, ...) PH_PRINTF_FORMAT(5, 6);

#endif /* PH_FILEIO_H */
