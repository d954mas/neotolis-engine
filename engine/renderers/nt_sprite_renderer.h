#ifndef NT_SPRITE_RENDERER_H
#define NT_SPRITE_RENDERER_H

#include "core/nt_types.h"
#include "render/nt_render_defs.h"

/* Staging buffers for one flush. uint16 indices cap MAX_VERTICES at 65536.
 * Default index ratio (9/4) sized for 8-vertex polygon worst case (18 idx /
 * 8 verts). Pure-rect content needs only 6/4 = 1.5×; polygon-heavy 16-v
 * needs 42/16 ≈ 2.6×. Override either to match the game's content profile. */
#ifndef NT_SPRITE_RENDERER_MAX_VERTICES
#define NT_SPRITE_RENDERER_MAX_VERTICES 16384
#endif

#ifndef NT_SPRITE_RENDERER_MAX_INDICES
#define NT_SPRITE_RENDERER_MAX_INDICES (NT_SPRITE_RENDERER_MAX_VERTICES * 9 / 4)
#endif

_Static_assert(NT_SPRITE_RENDERER_MAX_VERTICES <= 65536, "MAX_VERTICES must fit uint16 index range");

#define NT_SPRITE_RENDERER_MAX_PIPELINES_HARDCAP 64

#ifndef NT_SPRITE_RENDERER_MAX_DRAW_CMDS
#define NT_SPRITE_RENDERER_MAX_DRAW_CMDS 256
#endif

/* ---- Vertex format — 24 bytes ---- */

typedef struct {
    float position[3]; /* 12 B */
    float texcoord[2]; /*  8 B */
    uint8_t color[4];  /*  4 B */
} nt_sprite_vertex_t;
_Static_assert(sizeof(nt_sprite_vertex_t) == 24, "sprite vertex must be 24 bytes");

/* ---- Init descriptor ---- */

typedef struct {
    uint16_t max_pipelines; /* default 16 */
} nt_sprite_renderer_desc_t;

static inline nt_sprite_renderer_desc_t nt_sprite_renderer_desc_defaults(void) {
    return (nt_sprite_renderer_desc_t){
        .max_pipelines = 16,
    };
}

/* ---- Lifecycle ---- */

nt_result_t nt_sprite_renderer_init(const nt_sprite_renderer_desc_t *desc);
void nt_sprite_renderer_shutdown(void);
void nt_sprite_renderer_restore_gpu(void);

/* Contract: atlas page texture always binds to slot 0. Material may declare
 * a slot-0 binding to override sampler / set uniform name. */
void nt_sprite_renderer_draw_list(const nt_render_item_t *items, uint32_t count);
void nt_sprite_renderer_flush(void);

/* ---- Test access (compiled only when NT_SPRITE_RENDERER_TEST_ACCESS is defined) ---- */

#ifdef NT_SPRITE_RENDERER_TEST_ACCESS
uint32_t nt_sprite_renderer_test_pipeline_cache_count(void);
/* Per-renderer test counter (separate from nt_gfx_get_frame_draw_calls). */
uint32_t nt_sprite_renderer_test_draw_call_count(void);
/* Current staging vertex_count (resets on flush). */
uint32_t nt_sprite_renderer_test_vertex_count(void);
/* Captured at end of emit_one — survives flush; lets tests assert per-emit
 * counts after draw_list completes. */
uint32_t nt_sprite_renderer_test_last_emit_vertex_count(void);
uint32_t nt_sprite_renderer_test_last_emit_index_count(void);
bool nt_sprite_renderer_test_initialized(void);
#endif

#endif /* NT_SPRITE_RENDERER_H */
