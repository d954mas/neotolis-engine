#ifndef NT_SKELETAL_GPU_H
#define NT_SKELETAL_GPU_H

#include <stdint.h>

#include "core/nt_types.h"
#include "graphics/nt_gfx.h"
#include "skeletal/nt_skeletal.h"

/*
 * Frame-scoped upload of CPU skinning palettes into one shared RGBA32F
 * texture. Per frame: begin_frame -> reserve* -> flush -> all passes.
 *
 * Texel layout (skeletal spec, "Baked playback: runtime banks", texel layout):
 * a frame of P palette entries is 3*P contiguous texels in one texture row
 * starting at (x, y); texel (x + 3p + r, y) holds row r of the 3x4 matrix
 * B[p]. Frames never span a row; a frame that does not fit the current row
 * starts the next one.
 *
 * A binding is valid until the next begin_frame or a graphics invalidation.
 * The game's frame order keeps it current; nothing here stamps or checks a
 * frame counter.
 */

/* Borrowed texture + two frame origins; CPU palettes have x1 == x0, y1 == y0,
 * alpha 0; bank lookups fill both origins. Coordinates stay separate
 * integers: a linear offset may exceed 2^24 in float. */
typedef struct {
    nt_texture_t texture;
    uint16_t x0, y0, x1, y1;
    float alpha;
} nt_deformation_binding_t;

#ifndef __cplusplus
_Static_assert(sizeof(nt_deformation_binding_t) == 16, "nt_deformation_binding_t is 16 bytes");
#endif

typedef struct {
    uint16_t width;  /* texels per row; 0 = min(2048, gpu_caps.max_texture_size) */
    uint16_t height; /* rows; capacity = width * height texels, overflow asserts */
} nt_skeletal_gpu_desc_t;

/* 2048 x 16 texels = 512 KB: 96 frames of 100 joints. */
static inline nt_skeletal_gpu_desc_t nt_skeletal_gpu_desc_defaults(void) { return (nt_skeletal_gpu_desc_t){.width = 0, .height = 16}; }

/* Requires nt_gfx_init. Allocates the staging buffer and creates the texture;
 * no other allocation afterwards. */
nt_result_t nt_skeletal_gpu_init(const nt_skeletal_gpu_desc_t *desc);
void nt_skeletal_gpu_shutdown(void);
/* Destroys and recreates the texture after a context loss; staging survives
 * but the next frame rewrites it. Failure returns NT_ERR_INIT_FAILED: retry
 * before reserving, or shut down. Inactive module returns NT_OK. */
nt_result_t nt_skeletal_gpu_restore_gpu(void);

/* Resets the frame cursor; every earlier binding and reserved pointer is invalid. */
void nt_skeletal_gpu_begin_frame(void);

/* Reserves a frame of `count` palette entries and returns the staging pointer
 * to fill (nt_skin_palette_build with capacity = count). Writes the binding
 * of that frame. No GL call. Asserts: count > 0, 3*count <= width, capacity.
 * The pointer is valid until the next begin_frame. */
nt_skeletal_mat34_t *nt_skeletal_gpu_reserve(uint16_t count, nt_deformation_binding_t *out_binding);

/* Uploads every texel reserved since begin_frame: the complete rows as one
 * rectangle plus the partial last row as one h = 1 fragment. Call once per
 * frame after the last palette write and before the first pass; a second call
 * re-uploads the same bytes. */
void nt_skeletal_gpu_flush(void);

#endif /* NT_SKELETAL_GPU_H */
