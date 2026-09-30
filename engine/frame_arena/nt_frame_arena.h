#ifndef NT_FRAME_ARENA_H
#define NT_FRAME_ARENA_H

#include "core/nt_types.h"
#include "graphics/nt_gfx.h"

/*
 * One frame's prepared per-draw vertex data (instance attributes) in a single
 * STREAM vertex buffer. Renderers reserve ranges while preparing; the game
 * uploads once before the first draw that reads them; draws bind the buffer at
 * the reserved offset. Nothing writes the buffer after a draw of the frame read
 * it, which is the stall Mali/ANGLE charge for (see Dynamic data lifetime in
 * the render architecture spec).
 *
 * Frame order, owned by the game, once per gfx frame after nt_gfx_begin_frame:
 *   begin_frame -> reserve ... -> upload -> passes that draw reserved ranges.
 * Reserve, upload and buffer access assert that begin_frame ran in this gfx frame.
 * An offset is valid until the next begin_frame or restore.
 */

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
/* Destroys and recreates the buffer after a context loss; staging survives and
 * the next frame rewrites it. Draws assert until the next upload. Failure
 * returns NT_ERR_INIT_FAILED: retry before uploading, or shut down. Inactive
 * module returns NT_OK. */
nt_result_t nt_frame_arena_restore_gpu(void);

/* Resets the cursor; every earlier offset and reserved pointer is invalid.
 * Asserts a second call inside one gfx frame. */
void nt_frame_arena_begin_frame(void);

/* Reserves `size` bytes (rounded up to NT_FRAME_ARENA_ALIGN) and returns the
 * staging pointer to fill; *out_offset receives its byte offset in the buffer.
 * Alignment padding has unspecified contents and must not be read by consumers.
 * No GL call. Asserts: size > 0, fits the capacity (logs the need first),
 * called before this frame's upload. */
void *nt_frame_arena_reserve(uint32_t size, uint32_t *out_offset);

/* Uploads every reserved byte in one call. Once per frame, after the last
 * reserve and before the first draw that reads the buffer; a second call asserts. */
void nt_frame_arena_upload(void);

/* The buffer to bind at a reserved offset. Asserts the frame was uploaded:
 * drawing before the upload would read the previous frame's bytes. */
nt_buffer_t nt_frame_arena_buffer(void);

/* Most bytes any frame uploaded since init: size the capacity from it. */
uint32_t nt_frame_arena_peak(void);

#endif /* NT_FRAME_ARENA_H */
