#ifndef NT_FRAME_ARENA_H
#define NT_FRAME_ARENA_H

#include "core/nt_types.h"
#include "graphics/nt_gfx.h"

/* Shared STREAM vertex buffer for prepared instance attributes.
 * The game owns begin_frame -> reserve* -> upload -> consuming draws.
 * Upload before any draw reads this buffer, including draws using the previous upload. */

/* Reserve offsets are multiples of this: a whole RGBA32F texel, so the same
 * offsets can index a data texture. */
#define NT_FRAME_ARENA_ALIGN 16U

typedef struct {
    uint32_t capacity; /* bytes per frame, multiple of NT_FRAME_ARENA_ALIGN; overflow asserts */
} nt_frame_arena_desc_t;

/* Requires nt_gfx_init. Allocates the staging copy and creates the buffer;
 * no other allocation afterwards. */
nt_result_t nt_frame_arena_init(const nt_frame_arena_desc_t *desc);
void nt_frame_arena_shutdown(void);
/* Recreates the buffer after context loss; staging and offsets survive. Upload again before taking the buffer.
 * Failure: NT_ERR_INIT_FAILED; retry before uploading or shut down. Inactive: NT_OK. */
nt_result_t nt_frame_arena_restore_gpu(void);

/* Resets the cursor and invalidates every earlier offset and reserved pointer.
 * Once per gfx frame after nt_gfx_begin_frame; a second call asserts.
 * Skip this call to reuse the previous upload for the whole gfx frame. */
void nt_frame_arena_begin_frame(void);

/* Reserves size bytes at an aligned offset, rounded up to NT_FRAME_ARENA_ALIGN.
 * Returns staging and writes the byte offset to *out_offset; no GL call. Padding must not be read.
 * Asserts: size > 0, capacity (logs first), called before upload. */
void *nt_frame_arena_reserve(uint32_t size, uint32_t *out_offset);

/* Uploads the reserved prefix once per gfx frame after the last write and before any draw reads this buffer.
 * A second call asserts; GPU restore permits re-upload. */
void nt_frame_arena_upload(void);

/* The buffer to bind at a reserved offset. Requires a completed upload;
 * begin_frame or GPU restore makes it unavailable until upload. */
nt_buffer_t nt_frame_arena_buffer(void);

/* Most bytes any frame uploaded since init: size the capacity from it. */
uint32_t nt_frame_arena_peak(void);

#endif /* NT_FRAME_ARENA_H */
