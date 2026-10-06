#ifndef NT_MESH_RUN_INTERNAL_H
#define NT_MESH_RUN_INTERNAL_H

#include "renderers/nt_mesh_renderer.h"
#include "renderers/nt_renderer_shared.h"

/* Internal to the mesh renderers -- not a public header, not installed. */

/* Executes prepared runs in order: binds only what changed, never merges or reorders. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static inline void nt_mesh_runs_draw(const nt_mesh_run_t *runs, uint32_t run_count) {
    if (run_count == 0) {
        return;
    }
    const nt_buffer_t instances = nt_gfx_frame_buffer(NT_GFX_FRAME_VERTEX);
    nt_renderer_bound_t bound = {0};
    uint32_t textured_supplied = 0;
    for (uint32_t r = 0; r < run_count; r++) {
        const nt_mesh_run_t *run = &runs[r];
        nt_renderer_bind_pipeline(&bound, run->pipeline);
        /* A pipeline change clears bound.material, replaying textures with uniforms. */
        if (run->material.id != bound.material || run->supplied_texture.id != textured_supplied) {
            const nt_material_info_t *mat_info = nt_material_get_info(run->material);
            NT_ASSERT(mat_info != NULL && "mesh run: material destroyed after prepare");
            nt_renderer_material_view_t view = nt_renderer_material_view(mat_info);
            nt_sampler_t samplers[NT_MATERIAL_MAX_TEXTURES];
            if (run->supplied_slot < view.tex_count) {
                memcpy(samplers, mat_info->tex_samplers, sizeof(nt_sampler_t) * view.tex_count);
                samplers[run->supplied_slot] = NT_SAMPLER_DEFAULT;
                view.tex_samplers = samplers;
                view.resolved_tex[run->supplied_slot] = run->supplied_texture.id;
            }
            nt_renderer_apply_material_uniforms(&bound, run->material.id, &view);
            nt_renderer_apply_texture_slots(&view);
            textured_supplied = run->supplied_texture.id;
        }
        nt_renderer_bind_vertex_input(&bound, run->vertex_input);
        nt_gfx_bind_instance_buffer(instances, run->offset);
        if (run->index_count > 0) {
            nt_gfx_draw_indexed_instanced(0, run->index_count, run->vertex_count, run->instance_count);
        } else {
            nt_gfx_draw_instanced(0, run->vertex_count, run->instance_count);
        }
    }
}

#endif /* NT_MESH_RUN_INTERNAL_H */
