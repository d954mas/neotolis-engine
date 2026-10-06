#ifndef NT_SPRITE_TEST_EMIT_H
#define NT_SPRITE_TEST_EMIT_H

/* Readers of the last sprite emit or draw_list item in frame storage staging. Read them before
 * the next nt_gfx_begin_frame, which empties the storage. */

#include <string.h>

#include "core/nt_assert.h"
#include "graphics/nt_gfx.h"
#include "renderers/nt_sprite_renderer.h"

static inline nt_sprite_test_emit_t nt_sprite_test_last_emit(void) {
    nt_sprite_test_emit_t e;
    nt_sprite_renderer_test_last_emit(&e);
    return e;
}

static inline const nt_sprite_vertex_t *nt_sprite_test_last_vertex(uint32_t v_idx) {
    const nt_sprite_test_emit_t e = nt_sprite_test_last_emit();
    NT_ASSERT(v_idx < e.vertex_count && "last emit: vertex index out of range");
    return (const nt_sprite_vertex_t *)(e.vertices + ((size_t)v_idx * e.stride));
}

static inline void nt_sprite_renderer_test_last_emit_position(uint32_t v_idx, float out[3]) { memcpy(out, nt_sprite_test_last_vertex(v_idx)->position, 3U * sizeof(float)); }

/* Raw uint16 0..65535 units (the shader normalizes to [0,1]). */
static inline void nt_sprite_renderer_test_last_emit_texcoord(uint32_t v_idx, uint16_t out[2]) { memcpy(out, nt_sprite_test_last_vertex(v_idx)->texcoord, 2U * sizeof(uint16_t)); }

/* Raw [R,G,B,A] bytes. */
static inline void nt_sprite_renderer_test_last_emit_color(uint32_t v_idx, uint8_t out[4]) { memcpy(out, nt_sprite_test_last_vertex(v_idx)->color, 4U); }

/* Custom attr block after the 20 B base of an absolute frame-storage vertex, at the last emit's stride. */
static inline void nt_sprite_renderer_test_batch_custom(uint32_t vertex, float *out, uint8_t float_count) {
    const nt_sprite_test_emit_t e = nt_sprite_test_last_emit();
    NT_ASSERT((uint32_t)float_count * sizeof(float) + 20U <= e.stride && "batch_custom: float_count exceeds the custom block");
    memcpy(out, g_nt_gfx_frame_storage[NT_GFX_FRAME_VERTEX].staging + ((size_t)vertex * e.stride) + 20U, (size_t)float_count * sizeof(float));
}

static inline void nt_sprite_renderer_test_last_emit_radial(uint32_t v_idx, float *out, uint8_t float_count) {
    const nt_sprite_test_emit_t e = nt_sprite_test_last_emit();
    NT_ASSERT(v_idx < e.vertex_count && "last_emit_radial: index out of range");
    nt_sprite_renderer_test_batch_custom(e.first_vertex + v_idx, out, float_count);
}

#endif /* NT_SPRITE_TEST_EMIT_H */
