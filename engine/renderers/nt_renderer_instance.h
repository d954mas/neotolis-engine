#ifndef NT_RENDERER_INSTANCE_H
#define NT_RENDERER_INSTANCE_H

#include "core/nt_assert.h"
#include "core/nt_types.h"
#include "graphics/nt_gfx.h"

#include <stdint.h>

/* Internal to the renderers -- not a public header, not installed. */

// #region instance block
/* Per-instance data reaches shaders through the NtInstances uniform block
 * (assets/shaders/common/instance.glsl): a flat vec4 array the renderer fills
 * one chunk at a time. Each draw sets nt_instance_base to its first instance
 * inside the bound chunk; the shader reads nt_instance_base + gl_InstanceID. */
#define NT_INSTANCE_BLOCK_NAME "NtInstances"
#define NT_INSTANCE_BLOCK_SLOT 15U
#define NT_INSTANCE_BASE_UNIFORM "nt_instance_base"
/* WebGL2's guaranteed MAX_UNIFORM_BLOCK_SIZE; the shader declares exactly this size. */
#define NT_INSTANCE_BLOCK_SIZE 16384U
_Static_assert(NT_INSTANCE_BLOCK_SLOT < NT_GFX_MAX_UBO_SLOTS, "instance block slot must be a valid UBO slot");

/* One uniform buffer per renderer; chunks are bound as ranges of it. */
typedef struct {
    nt_buffer_t buffer;
    uint32_t size;
    uint32_t cursor;
    uint32_t align;
} nt_renderer_instance_ring_t;

static inline uint32_t nt_renderer_align_up(uint32_t value, uint32_t align) { return (value + align - 1U) / align * align; }

/* Holds max_instances packed at stride per wrap, plus the alignment padding of
 * the chunks they split into and one full block, so a range bound at any
 * reserved offset fits. The alignment is re-read here, so restore recreates. */
static inline nt_result_t nt_renderer_instance_ring_create(nt_renderer_instance_ring_t *ring, uint32_t max_instances, uint32_t stride, const char *label) {
    const uint32_t align = nt_gfx_gpu_caps()->uniform_buffer_offset_alignment;
    NT_ASSERT(align != 0 && "instance ring needs a GL context: create it after nt_gfx_init");
    const uint32_t per_chunk = NT_INSTANCE_BLOCK_SIZE / stride;
    const uint32_t chunks = (max_instances + per_chunk - 1U) / per_chunk;
    const uint32_t size = nt_renderer_align_up((max_instances * stride) + (chunks * (align - 1U)) + NT_INSTANCE_BLOCK_SIZE, align);
    *ring = (nt_renderer_instance_ring_t){
        .buffer = nt_gfx_make_buffer(&(nt_buffer_desc_t){.type = NT_BUFFER_UNIFORM, .usage = NT_USAGE_STREAM, .size = size, .label = label}),
        .size = size,
        .align = align,
    };
    return ring->buffer.id != 0 ? NT_OK : NT_ERR_INIT_FAILED;
}

static inline void nt_renderer_instance_ring_destroy(nt_renderer_instance_ring_t *ring) {
    nt_gfx_destroy_buffer(ring->buffer);
    *ring = (nt_renderer_instance_ring_t){0};
}

/* Uploads one chunk and binds a full block at its offset: WebGL rejects a draw
 * whose bound range is smaller than the block, though only the packed bytes are
 * written. A wrap overwrites data an earlier draw may still read; the driver
 * orders it. */
static inline void nt_renderer_instance_ring_push(nt_renderer_instance_ring_t *ring, const void *data, uint32_t bytes) {
    NT_ASSERT(bytes > 0 && bytes <= NT_INSTANCE_BLOCK_SIZE);
    uint32_t offset = nt_renderer_align_up(ring->cursor, ring->align);
    if (offset + NT_INSTANCE_BLOCK_SIZE > ring->size) {
        offset = 0;
    }
    ring->cursor = offset + bytes;
    nt_gfx_update_buffer(ring->buffer, offset, data, bytes);
    nt_gfx_bind_uniform_buffer_range(ring->buffer, NT_INSTANCE_BLOCK_SLOT, offset, NT_INSTANCE_BLOCK_SIZE);
}

// #endregion

#endif /* NT_RENDERER_INSTANCE_H */
