#ifndef NT_GFX_CAPTURE_TEXT_H
#define NT_GFX_CAPTURE_TEXT_H

#include "graphics/nt_gfx.h"

#include <stddef.h>

/* Lossless named text of a capture (grammar in the render architecture spec, "Capture text").
 * Returns the bytes the text needs, without the terminator; writes at most cap - 1 of them plus a
 * terminator when cap > 0. Format with cap 0 first to size the buffer. */
size_t nt_gfx_capture_format(const nt_gfx_capture_view_t *view, char *out, size_t cap);

#endif /* NT_GFX_CAPTURE_TEXT_H */
