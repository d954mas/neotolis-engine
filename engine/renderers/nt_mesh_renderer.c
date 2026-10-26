#include "renderers/nt_mesh_renderer.h"

#include "comp_storage/nt_comp_storage.h"
#include "core/nt_assert.h"
#include "drawable_comp/nt_drawable_comp.h"
#include "graphics/nt_gfx.h"
#include "material/nt_material.h"
#include "material_comp/nt_material_comp.h"
#include "mesh_comp/nt_mesh_comp.h"
#include "nt_vertex_locations.h"
#include "renderers/nt_renderer_shared.h"
#include "resource/nt_resource.h"
#include "transform_comp/nt_transform_comp.h"
#if NT_MESH_RENDERER_SKINNED
#include "hash/nt_hash.h"
#include "skin_comp/nt_skin_comp.h"
#endif

#include <string.h>

static struct {
    nt_instance_layout_t layout;
#if NT_MESH_RENDERER_SKINNED
    nt_instance_layout_t skinned_layout;
    uint32_t skin_name_hash;
#endif
} s_mesh_renderer; /* layout.id != 0 between init and shutdown */

/* clang-format off */
const nt_vertex_layout_t nt_mesh_instance_layout = {
    .attr_count = 4,
    .stride = sizeof(nt_mesh_instance_t),
    .attrs = {
        {.location = NT_INST_LOC_WORLD_ROW0, .type = NT_VERTEX_FLOAT, .count = 4, .offset = 0},
        {.location = NT_INST_LOC_WORLD_ROW1, .type = NT_VERTEX_FLOAT, .count = 4, .offset = 16},
        {.location = NT_INST_LOC_WORLD_ROW2, .type = NT_VERTEX_FLOAT, .count = 4, .offset = 32},
        {.location = NT_INST_LOC_COLOR, .type = NT_VERTEX_UINT8, .count = 4, .normalized = true, .offset = offsetof(nt_mesh_instance_t, color)},
    },
};
#if NT_MESH_RENDERER_SKINNED
const nt_vertex_layout_t nt_skinned_mesh_instance_layout = {
    .attr_count = 6,
    .stride = sizeof(nt_skinned_mesh_instance_t),
    .attrs = {
        {.location = NT_INST_LOC_WORLD_ROW0, .type = NT_VERTEX_FLOAT, .count = 4, .offset = 0},
        {.location = NT_INST_LOC_WORLD_ROW1, .type = NT_VERTEX_FLOAT, .count = 4, .offset = 16},
        {.location = NT_INST_LOC_WORLD_ROW2, .type = NT_VERTEX_FLOAT, .count = 4, .offset = 32},
        {.location = NT_INST_LOC_SKIN_FRAMES, .type = NT_VERTEX_UINT16, .count = 4, .offset = offsetof(nt_skinned_mesh_instance_t, skin_origins)},
        {.location = NT_INST_LOC_SKIN_ALPHA, .type = NT_VERTEX_FLOAT, .count = 1, .offset = offsetof(nt_skinned_mesh_instance_t, skin_alpha)},
        {.location = NT_INST_LOC_COLOR, .type = NT_VERTEX_UINT8, .count = 4, .normalized = true, .offset = offsetof(nt_skinned_mesh_instance_t, color)},
    },
};
#endif
/* clang-format on */

/* ---- Lifecycle ---- */

void nt_mesh_renderer_init(void) {
    NT_ASSERT(s_mesh_renderer.layout.id == 0);
    s_mesh_renderer.layout = nt_gfx_make_instance_layout(&nt_mesh_instance_layout);
#if NT_MESH_RENDERER_SKINNED
    s_mesh_renderer.skinned_layout = nt_gfx_make_instance_layout(&nt_skinned_mesh_instance_layout);
    s_mesh_renderer.skin_name_hash = nt_hash32_str("u_skin_matrices").value;
#endif
}

void nt_mesh_renderer_shutdown(void) { memset(&s_mesh_renderer, 0, sizeof(s_mesh_renderer)); }

// #region run
/* One call's state: a pass or foreign pipeline between calls invalidates it. The draw texture's
 * name and sampler are fixed within a call, so its texture id identifies what was applied: a run
 * of the same material and draw texture applies nothing again. */
typedef struct {
    nt_material_t material;
    nt_mesh_t mesh;
    nt_pipeline_t pipeline;
    nt_vertex_input_t vertex_input;
    nt_instance_layout_t instance_layout;
    nt_buffer_t instances;
    uint32_t applied_material;
    uint32_t applied_draw;
} mesh_draw_t;

/* Resolves pipeline and vertex input, reusing the previous run's on equal handles. False skips
 * the run: the program is not ready (linking or lost) or a create met a loss (retried by the next
 * run). The mesh's vertex input for this call's buffer does not depend on the material. */
static bool resolve(mesh_draw_t *d, nt_material_t material, const nt_material_info_t *mi, nt_mesh_t mesh) {
    if (!nt_gfx_program_ready(mi->program)) {
        NT_GFX_NOTE("skip", NT_NOTE_STR("reason", "program not ready"));
        return false;
    }
    if (material.id != d->material.id) {
        /* A mesh feeds every stream at its pack location; an attr_map would name locations no mesh uses. */
        NT_ASSERT(mi->attr_map_count == 0 && "mesh material: attr_map is sprite-only");
        /* Program x render state; layouts live on the meshes' vertex inputs. */
        const nt_pipeline_desc_t desc = nt_renderer_material_pipeline_desc(mi, "mesh_renderer");
        d->pipeline = nt_gfx_make_pipeline(&desc);
    }
    if (mesh.id != d->mesh.id) {
        d->vertex_input = nt_gfx_mesh_vertex_input(mesh, d->instances);
    }
    if (d->pipeline.id == 0 || d->vertex_input.id == 0) {
        d->material = (nt_material_t){0};
        d->mesh = (nt_mesh_t){0};
        return false;
    }
    d->material = material;
    d->mesh = mesh;
    return true;
}

/* Pipeline first: the material's block and texture set land on its program. */
static void record(mesh_draw_t *d, const nt_gfx_mesh_info_t *mesh_info, const nt_gfx_texture_binding_t *draw, uint32_t offset, uint32_t count) {
    nt_gfx_bind_pipeline(d->pipeline);
    if (d->material.id != d->applied_material || (draw != NULL && draw->texture.id != d->applied_draw)) {
        nt_material_apply(d->material, draw, draw != NULL ? 1U : 0U);
        d->applied_draw = draw != NULL ? draw->texture.id : 0U;
    }
    d->applied_material = d->material.id;
    nt_gfx_bind_vertex_input_instanced(d->vertex_input, d->instance_layout, d->instances, offset);
    if (mesh_info->index_count > 0) {
        nt_gfx_draw_indexed_instanced(0, mesh_info->index_count, mesh_info->vertex_count, count);
    } else {
        nt_gfx_draw_instanced(0, mesh_info->vertex_count, count);
    }
}
// #endregion

/* ---- Draw ---- */

/* The caller has checked the lost context. */
static void draw_one(nt_mesh_t mesh, nt_material_t material, nt_instance_layout_t layout, nt_buffer_t instances, uint32_t offset, uint32_t count, const nt_gfx_texture_binding_t *draw_texture) {
    NT_ASSERT(count > 0);
    const nt_material_info_t *mat_info = nt_material_get_info(material);
    const nt_gfx_mesh_info_t *mesh_info = nt_gfx_get_mesh_info(mesh);
    NT_ASSERT(mat_info != NULL && mesh_info != NULL && "mesh draw references a destroyed material or mesh");
    NT_GFX_NOTE("mesh", NT_NOTE_U32("material", material.id), NT_NOTE_U32("mesh", mesh.id));
    mesh_draw_t draw = {.instance_layout = layout, .instances = instances};
    if (resolve(&draw, material, mat_info, mesh)) {
        record(&draw, mesh_info, draw_texture, offset, count);
    }
}

void nt_mesh_renderer_draw(nt_mesh_t mesh, nt_material_t material, nt_instance_layout_t layout, nt_buffer_t instances, uint32_t offset, uint32_t count) {
    /* A lost context can leave a game-activated mesh 0; nothing would draw. */
    if (g_nt_gfx.context_lost) {
        return;
    }
    draw_one(mesh, material, layout, instances, offset, count, NULL);
}

static uint32_t find_run_end(const nt_render_item_t *items, uint32_t run_start, uint32_t count) {
    uint32_t run_end = run_start + 1;
    while (run_end < count && items[run_end].batch_key == items[run_start].batch_key) {
        run_end++;
    }
    return run_end;
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void nt_mesh_renderer_draw_list(uint32_t stream, const nt_render_item_t *items, uint32_t count) {
    NT_ASSERT(s_mesh_renderer.layout.id != 0);
    /* A lost context can leave a game-activated mesh 0; nothing would draw. */
    if (g_nt_gfx.context_lost) {
        return;
    }
    NT_ASSERT(stream >= NT_GFX_FRAME_VERTEX && stream < NT_GFX_FRAME_STREAM_COUNT && "draw_list: not a frame vertex stream");
    NT_ASSERT(count == 0 || items != NULL);
    /* Inline sparse reads, as the sprite emit does: no per-instance accessor call or liveness assert. */
    const nt_transform_comp_view_t transform_view = nt_transform_comp_view();
    const nt_drawable_comp_view_t drawable_view = nt_drawable_comp_view();
    mesh_draw_t draw = {.instance_layout = s_mesh_renderer.layout, .instances = nt_gfx_frame_buffer(stream)};
    for (uint32_t run_start = 0, run_end = 0; run_start < count; run_start = run_end) {
        run_end = find_run_end(items, run_start, count);
        const nt_entity_t leader = {.id = items[run_start].entity};
        const nt_material_t material = *nt_material_comp_handle(leader);
        const nt_mesh_t mesh = *nt_mesh_comp_handle(leader);
        const nt_material_info_t *mat_info = nt_material_get_info(material);
        const nt_gfx_mesh_info_t *mesh_info = nt_gfx_get_mesh_info(mesh);
        NT_ASSERT(mat_info != NULL && mesh_info != NULL && "mesh render item references a destroyed material or mesh");
        NT_GFX_NOTE("mesh", NT_NOTE_U32("material", material.id), NT_NOTE_U32("mesh", mesh.id));
        if (!resolve(&draw, material, mat_info, mesh)) {
            continue;
        }

        const uint32_t instance_count = run_end - run_start;
        NT_ASSERT(instance_count <= UINT32_MAX / sizeof(nt_mesh_instance_t) && "mesh draw_list: run exceeds the frame storage address range");
        uint32_t offset = 0;
        nt_mesh_instance_t *dst = (nt_mesh_instance_t *)nt_gfx_frame_alloc(stream, instance_count * (uint32_t)sizeof(nt_mesh_instance_t), 4, &offset);
        for (uint32_t i = run_start; i < run_end; i++, dst++) {
            const uint16_t entity_index = nt_entity_index((nt_entity_t){.id = items[i].entity});
            const uint16_t transform_index = transform_view.sparse_indices[entity_index];
            const uint16_t drawable_index = drawable_view.sparse_indices[entity_index];
            NT_ASSERT(transform_index != NT_INVALID_COMP_INDEX && "mesh render item: entity has no transform component");
            NT_ASSERT(drawable_index != NT_INVALID_COMP_INDEX && "mesh render item: entity has no drawable component");
            nt_mesh_instance_world_rows(dst->world_rows, transform_view.world_matrices[transform_index]);
            dst->color = drawable_view.colors_packed[drawable_index];
        }
        record(&draw, mesh_info, NULL, offset, instance_count);
    }
}

#if NT_MESH_RENDERER_SKINNED
// #region skinned
void nt_mesh_renderer_draw_skinned(nt_mesh_t mesh, nt_material_t material, nt_texture_t deformation, nt_instance_layout_t layout, nt_buffer_t instances, uint32_t offset, uint32_t count) {
    NT_ASSERT(s_mesh_renderer.layout.id != 0);
    /* A lost context can leave the deformation texture or a mesh 0; nothing would draw. */
    if (g_nt_gfx.context_lost) {
        return;
    }
    NT_ASSERT(deformation.id != 0 && "skinned draw requires a deformation texture");
    const nt_gfx_texture_binding_t skin = {.name = {.value = s_mesh_renderer.skin_name_hash}, .texture = deformation, .sampler = NT_SAMPLER_DEFAULT};
    draw_one(mesh, material, layout, instances, offset, count, &skin);
}

static uint32_t find_skinned_run_end(const nt_render_item_t *items, uint32_t leader, uint32_t end, uint32_t texture_id, nt_skin_comp_view_t skin_view) {
    uint32_t run_end = leader + 1;
    while (run_end < end && items[run_end].batch_key == items[leader].batch_key) {
        const uint16_t skin_index = skin_view.sparse_indices[nt_entity_index((nt_entity_t){.id = items[run_end].entity})];
        NT_ASSERT(skin_index != NT_INVALID_COMP_INDEX && "skinned render item: entity has no skin component");
        if (skin_view.bindings[skin_index].texture.id != texture_id) {
            break;
        }
        run_end++;
    }
    return run_end;
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void nt_mesh_renderer_draw_list_skinned(uint32_t stream, const nt_render_item_t *items, uint32_t count) {
    NT_ASSERT(s_mesh_renderer.layout.id != 0);
    /* A lost context can leave the deformation texture or a mesh 0; nothing would draw. */
    if (g_nt_gfx.context_lost) {
        return;
    }
    NT_ASSERT(stream >= NT_GFX_FRAME_VERTEX && stream < NT_GFX_FRAME_STREAM_COUNT && "draw_list: not a frame vertex stream");
    NT_ASSERT(count == 0 || items != NULL);
    /* Inline sparse reads per instance; each run leader goes through the asserting accessors. */
    const nt_transform_comp_view_t transform_view = nt_transform_comp_view();
    const nt_drawable_comp_view_t drawable_view = nt_drawable_comp_view();
    const nt_skin_comp_view_t skin_view = nt_skin_comp_view();
    mesh_draw_t draw = {.instance_layout = s_mesh_renderer.skinned_layout, .instances = nt_gfx_frame_buffer(stream)};
    nt_gfx_texture_binding_t skin = {.name = {.value = s_mesh_renderer.skin_name_hash}, .sampler = NT_SAMPLER_DEFAULT};
    for (uint32_t run_start = 0, run_end = 0; run_start < count; run_start = run_end) {
        const nt_entity_t leader = {.id = items[run_start].entity};
        skin.texture = nt_skin_comp_handle(leader)->texture;
        const nt_material_t material = *nt_material_comp_handle(leader);
        const nt_mesh_t mesh = *nt_mesh_comp_handle(leader);
        NT_ASSERT(skin.texture.id != 0 && "skinned draw requires a deformation texture");
        run_end = find_skinned_run_end(items, run_start, count, skin.texture.id, skin_view); /* the run shares the leader's texture */
        const nt_material_info_t *mat_info = nt_material_get_info(material);
        const nt_gfx_mesh_info_t *mesh_info = nt_gfx_get_mesh_info(mesh);
        NT_ASSERT(mat_info != NULL && mesh_info != NULL && "skinned render item references a destroyed material or mesh");
        NT_GFX_NOTE("mesh", NT_NOTE_U32("material", material.id), NT_NOTE_U32("mesh", mesh.id));
        if (!resolve(&draw, material, mat_info, mesh)) {
            continue;
        }

        const uint32_t instance_count = run_end - run_start;
        NT_ASSERT(instance_count <= UINT32_MAX / sizeof(nt_skinned_mesh_instance_t) && "skinned draw_list: run exceeds the frame storage address range");
        uint32_t offset = 0;
        nt_skinned_mesh_instance_t *dst = (nt_skinned_mesh_instance_t *)nt_gfx_frame_alloc(stream, instance_count * (uint32_t)sizeof(nt_skinned_mesh_instance_t), 4, &offset);
        for (uint32_t i = run_start; i < run_end; i++, dst++) {
            const uint16_t entity_index = nt_entity_index((nt_entity_t){.id = items[i].entity});
            const uint16_t transform_index = transform_view.sparse_indices[entity_index];
            const uint16_t drawable_index = drawable_view.sparse_indices[entity_index];
            NT_ASSERT(transform_index != NT_INVALID_COMP_INDEX && "skinned render item: entity has no transform component");
            NT_ASSERT(drawable_index != NT_INVALID_COMP_INDEX && "skinned render item: entity has no drawable component");
            /* The leader passed its accessor and every follower the run scan's skin assert. */
            const nt_deformation_binding_t binding = skin_view.bindings[skin_view.sparse_indices[entity_index]];
            nt_mesh_instance_world_rows(dst->world_rows, transform_view.world_matrices[transform_index]);
            dst->skin_origins[0] = binding.x0;
            dst->skin_origins[1] = binding.y0;
            dst->skin_origins[2] = binding.x1;
            dst->skin_origins[3] = binding.y1;
            dst->skin_alpha = binding.alpha;
            dst->color = drawable_view.colors_packed[drawable_index];
        }
        record(&draw, mesh_info, &skin, offset, instance_count);
    }
}
// #endregion
#endif
