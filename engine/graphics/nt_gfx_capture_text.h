#ifndef NT_GFX_CAPTURE_TEXT_H
#define NT_GFX_CAPTURE_TEXT_H

#include "graphics/nt_gfx.h"

#include <stddef.h>

/* Name of an issued-call detail, e.g. "glBindTexture"; NULL outside the table. */
const char *nt_gfx_gl_call_name(uint32_t call);

/* Lossless named text of a capture (grammar in the render architecture spec, "Capture text").
 * Returns the bytes the text needs, without the terminator; writes at most cap - 1 of them plus a
 * terminator when cap > 0. Format with cap 0 first to size the buffer. */
size_t nt_gfx_capture_format(const nt_gfx_capture_view_t *view, char *out, size_t cap);

typedef struct {
    nt_gfx_capture_view_t view; /* events NULL: the header was rejected; else its count records (a valid prefix when error is set) */
    size_t next;                /* offset of the next document in text, or len */
    size_t error_at;            /* byte offset of the error in text */
    const char *error;          /* static message; NULL when the document parsed whole */
} nt_gfx_capture_parsed_t;

/* Parses the document at the start of text (len bytes, not terminated) into events[0..cap), events non-NULL. The text is
 * untrusted: nothing in it asserts. A header error rejects the document; a record error, an unknown name
 * included, keeps the records before it. A file holds documents back to back: parse again at next.
 * Every record is one line, so the file's line count bounds the records of all its documents. */
nt_gfx_capture_parsed_t nt_gfx_capture_parse(const char *text, size_t len, nt_gfx_event_t *events, uint32_t cap);

#endif /* NT_GFX_CAPTURE_TEXT_H */
