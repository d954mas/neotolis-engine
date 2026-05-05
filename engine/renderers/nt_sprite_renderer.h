#ifndef NT_SPRITE_RENDERER_H
#define NT_SPRITE_RENDERER_H

#include "core/nt_types.h"
#include "render/nt_render_defs.h"

/* ---- Compile-time limits (D-17) ----
 *
 * Buffers are sized for the worst case (rect: 4 verts + 6 indices per sprite).
 * Index buffer uses uint32 so the renderer can pack everything emitted in
 * one frame into a single VBO/IBO upload (Defold-style: collect → upload
 * once → multi-draw with offsets). Auto-flush on overflow keeps per-cmd
 * state and re-opens after the upload. */

#ifndef NT_SPRITE_RENDERER_MAX_SPRITES
#define NT_SPRITE_RENDERER_MAX_SPRITES 65535
#endif

#define NT_SPRITE_RENDERER_MAX_VERTICES (NT_SPRITE_RENDERER_MAX_SPRITES * 4)
#define NT_SPRITE_RENDERER_MAX_INDICES (NT_SPRITE_RENDERER_MAX_SPRITES * 6)

/* Hard cap on pipeline cache size (kept in static array to avoid heap). */
#define NT_SPRITE_RENDERER_MAX_PIPELINES_HARDCAP 64

/* Recorded draw commands per flush — one per state change (pipeline/material
 * /textures). Bunnymark uses 1; richer scenes typically stay well under 64. */
#ifndef NT_SPRITE_RENDERER_MAX_DRAW_CMDS
#define NT_SPRITE_RENDERER_MAX_DRAW_CMDS 256
#endif

/* ---- Vertex format (D-16, SPRITE-05) — 24 bytes ---- */

typedef struct {
    float position[3]; /* 12 B */
    float texcoord[2]; /*  8 B */
    uint8_t color[4];  /*  4 B */
} nt_sprite_vertex_t;
_Static_assert(sizeof(nt_sprite_vertex_t) == 24, "sprite vertex must be 24 bytes (SPRITE-05)");

/* ---- Init descriptor (D-19) ---- */

typedef struct {
    uint16_t max_pipelines; /* default 16 */
} nt_sprite_renderer_desc_t;

static inline nt_sprite_renderer_desc_t nt_sprite_renderer_desc_defaults(void) { return (nt_sprite_renderer_desc_t){.max_pipelines = 16}; }

/* ---- Lifecycle ---- */

nt_result_t nt_sprite_renderer_init(const nt_sprite_renderer_desc_t *desc);
void nt_sprite_renderer_shutdown(void);
void nt_sprite_renderer_restore_gpu(void);

/* ---- Draw API (list-based, D-01) ---- */

void nt_sprite_renderer_draw_list(const nt_render_item_t *items, uint32_t count);
void nt_sprite_renderer_flush(void);

/* ---- Test access (compiled only when NT_SPRITE_RENDERER_TEST_ACCESS is defined) ---- */

#ifdef NT_SPRITE_RENDERER_TEST_ACCESS
uint32_t nt_sprite_renderer_test_pipeline_cache_count(void);
/* Per-renderer test counter (separate from nt_gfx_get_frame_draw_calls per CONTEXT D-39). */
uint32_t nt_sprite_renderer_test_draw_call_count(void);
/* Current staging vertex_count (resets on flush). */
uint32_t nt_sprite_renderer_test_vertex_count(void);
/* Captured at end of emit_one — survives flush; lets tests assert per-emit
 * counts after draw_list completes (Issue 7 fix). */
uint32_t nt_sprite_renderer_test_last_emit_vertex_count(void);
uint32_t nt_sprite_renderer_test_last_emit_index_count(void);
bool nt_sprite_renderer_test_initialized(void);
#endif

#endif /* NT_SPRITE_RENDERER_H */
