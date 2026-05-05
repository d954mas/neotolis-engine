#include "renderers/nt_sprite_renderer.h"

#include <string.h>

#include "atlas/nt_atlas.h"
#include "core/nt_assert.h"
#include "drawable_comp/nt_drawable_comp.h"
#include "graphics/nt_gfx.h"
#include "log/nt_log.h"
#include "material/nt_material.h"
#include "material_comp/nt_material_comp.h"
#include "render/nt_render_defs.h"
#include "sprite_comp/nt_sprite_comp.h"
#include "transform_comp/nt_transform_comp.h"

// #region module state
typedef struct {
    uint64_t key;
    nt_pipeline_t pipeline;
} nt_sprite_pipeline_entry_t;

static struct {
    bool initialized;
    uint16_t max_pipelines;
    nt_sprite_pipeline_entry_t entries[NT_SPRITE_RENDERER_MAX_PIPELINES_HARDCAP];
    uint16_t count;

    nt_buffer_t vbo; /* dynamic, sized for NT_SPRITE_RENDERER_MAX_VERTICES * 24 */
    nt_buffer_t ibo; /* dynamic, sized for NT_SPRITE_RENDERER_MAX_INDICES * 2 */
    nt_sprite_vertex_t vertices[NT_SPRITE_RENDERER_MAX_VERTICES];
    uint16_t indices[NT_SPRITE_RENDERER_MAX_INDICES];
    uint32_t vertex_count;
    uint32_t index_count;

    /* Per-batch state (set inside draw_list, consumed by flush) */
    nt_pipeline_t current_pipeline;
    uint32_t frame_draw_calls; /* test counter; SEPARATE from nt_gfx_get_frame_draw_calls per CONTEXT D-39 */
#ifdef NT_SPRITE_RENDERER_TEST_ACCESS
    /* Test-only: last emit_one() vertex/index counts captured BEFORE flush
     * resets s_sprite.vertex_count. Read by polygon-emit test to verify
     * region.vertex_count==N polygons emit N vertices (Issue 7 fix). */
    uint32_t last_emit_vertex_count;
    uint32_t last_emit_index_count;
#endif
} s_sprite;
// #endregion

// #region pack helper
/* pack_u8 is consumed by emit_one() in draw_list — declared here to keep the
 * helper close to module state. Marked unused while draw_list is a stub
 * (Task 1); the marker becomes a no-op as soon as Task 2 wires emit_one. */
__attribute__((unused)) static inline uint8_t pack_u8(float c) {
    if (c <= 0.0F) {
        return 0;
    }
    if (c >= 1.0F) {
        return 255;
    }
    return (uint8_t)((c * 255.0F) + 0.5F);
}
// #endregion

// #region lifecycle
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
nt_result_t nt_sprite_renderer_init(const nt_sprite_renderer_desc_t *desc) {
    NT_ASSERT(!s_sprite.initialized);
    nt_sprite_renderer_desc_t d = (desc != NULL) ? *desc : nt_sprite_renderer_desc_defaults();
    NT_ASSERT(d.max_pipelines > 0 && d.max_pipelines <= NT_SPRITE_RENDERER_MAX_PIPELINES_HARDCAP);

    memset(&s_sprite, 0, sizeof(s_sprite));
    s_sprite.max_pipelines = d.max_pipelines;

    /* Dynamic VBO and IBO. Pattern: nt_text_renderer.c init code. */
    s_sprite.vbo = nt_gfx_make_buffer(&(nt_buffer_desc_t){
        .type = NT_BUFFER_VERTEX,
        .usage = NT_USAGE_DYNAMIC,
        .size = NT_SPRITE_RENDERER_MAX_VERTICES * (uint32_t)sizeof(nt_sprite_vertex_t),
        .label = "sprite_vbo",
    });
    NT_ASSERT(s_sprite.vbo.id != 0);

    s_sprite.ibo = nt_gfx_make_buffer(&(nt_buffer_desc_t){
        .type = NT_BUFFER_INDEX,
        .usage = NT_USAGE_DYNAMIC,
        .size = NT_SPRITE_RENDERER_MAX_INDICES * (uint32_t)sizeof(uint16_t),
        .index_type = NT_INDEX_UINT16,
        .label = "sprite_ibo",
    });
    NT_ASSERT(s_sprite.ibo.id != 0);

    s_sprite.initialized = true;
    return NT_OK;
}

void nt_sprite_renderer_shutdown(void) {
    if (!s_sprite.initialized) {
        return;
    }
    /* Destroy pipelines in cache */
    for (uint16_t i = 0; i < s_sprite.count; i++) {
        nt_gfx_destroy_pipeline(s_sprite.entries[i].pipeline);
        s_sprite.entries[i] = (nt_sprite_pipeline_entry_t){0};
    }
    s_sprite.count = 0;
    nt_gfx_destroy_buffer(s_sprite.vbo);
    nt_gfx_destroy_buffer(s_sprite.ibo);
    memset(&s_sprite, 0, sizeof(s_sprite));
}

void nt_sprite_renderer_restore_gpu(void) {
    if (!s_sprite.initialized) {
        return;
    }
    uint16_t saved_pip = s_sprite.max_pipelines;
    nt_sprite_renderer_shutdown();
    nt_sprite_renderer_desc_t desc = {.max_pipelines = saved_pip};
    NT_ASSERT(nt_sprite_renderer_init(&desc) == NT_OK);
}
// #endregion

// #region draw_list (Task 2)
void nt_sprite_renderer_draw_list(const nt_render_item_t *items, uint32_t count) {
    /* Filled in Task 2 */
    (void)items;
    (void)count;
}

void nt_sprite_renderer_flush(void) { /* Filled in Task 2 */ }
// #endregion

// #region test accessors
#ifdef NT_SPRITE_RENDERER_TEST_ACCESS
uint32_t nt_sprite_renderer_test_pipeline_cache_count(void) { return s_sprite.count; }
uint32_t nt_sprite_renderer_test_draw_call_count(void) { return s_sprite.frame_draw_calls; }
uint32_t nt_sprite_renderer_test_vertex_count(void) { return s_sprite.vertex_count; }
uint32_t nt_sprite_renderer_test_last_emit_vertex_count(void) { return s_sprite.last_emit_vertex_count; }
uint32_t nt_sprite_renderer_test_last_emit_index_count(void) { return s_sprite.last_emit_index_count; }
bool nt_sprite_renderer_test_initialized(void) { return s_sprite.initialized; }
#endif
// #endregion
