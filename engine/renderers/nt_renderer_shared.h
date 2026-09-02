#ifndef NT_RENDERER_SHARED_H
#define NT_RENDERER_SHARED_H

#include "core/nt_assert.h"
#include "graphics/nt_gfx.h"
#include "log/nt_log.h"
#include "material/nt_material.h"

#include <stdbool.h>
#include <stdint.h>

/* Internal to the renderers -- not a public header, not installed. */

/* The key is identity: hash the program handle and every descriptor field the renderer varies.
 * Descriptors are not compared on a hit. */
typedef struct {
    uint64_t key;
    nt_pipeline_t pipeline;
} nt_renderer_pipeline_entry_t;

/* Validate matched pipelines because program generations can wrap; dead matches return invalid.
 * Misses also return invalid. Cleanup is deferred to insertion. */
static inline nt_pipeline_t nt_renderer_pipeline_cache_find(const nt_renderer_pipeline_entry_t *entries, uint16_t count, uint64_t key) {
    for (uint16_t i = 0; i < count; i++) {
        if (entries[i].key == key) {
            return nt_gfx_pipeline_valid(entries[i].pipeline) ? entries[i].pipeline : (nt_pipeline_t){0};
        }
    }
    return (nt_pipeline_t){0};
}

/* Reap dead entries before checking capacity; exhaustion asserts.
 * Leave failed pipeline creation uncached so a later miss retries. */
static inline nt_pipeline_t nt_renderer_pipeline_cache_insert(nt_renderer_pipeline_entry_t *entries, uint16_t *count, uint16_t cap, uint64_t key, const nt_pipeline_desc_t *desc, bool *warned) {
    for (uint16_t i = 0; i < *count;) {
        if (!nt_gfx_pipeline_valid(entries[i].pipeline)) {
            entries[i] = entries[--(*count)];
            continue;
        }
        i++;
    }
    /* TRAP omits assert text; the log identifies the renderer that exhausted its cache. */
    if (*count >= cap) {
        NT_LOG_ERROR("pipeline cache exhausted building '%s' -- raise that renderer's cap", (desc->label != NULL) ? desc->label : "(unlabeled)");
    }
    NT_ASSERT(*count < cap && "pipeline cache exhausted -- raise this renderer's cap (desc.max_pipelines or NT_*_RENDERER_MAX_PIPELINES)");
    nt_pipeline_t pip = nt_gfx_make_pipeline(desc);
    if (pip.id == 0) {
        return pip;
    }
    entries[*count].key = key;
    entries[*count].pipeline = pip;
    (*count)++;
    *warned = false;
    return pip;
}

// #region bound state
/* Borrowed from nt_material_info_t, or from a sprite cmd whose slot 0 is the atlas page.
 * Hashes are captured so a cmd whose material died still replays its sampler units. */
typedef struct {
    uint8_t tex_count;
    const uint32_t *tex_name_hashes;      /* [tex_count] */
    const uint32_t *resolved_tex;         /* [tex_count]; 0 = unresolved */
    const nt_sampler_t *resolved_sampler; /* [tex_count]; .id == 0 = texture default */
    uint8_t param_count;
    const uint32_t *param_name_hashes; /* [param_count] */
    const float (*params)[4];
} nt_renderer_material_view_t;

/* Zero-init = nothing bound; lives for ONE draw_list or flush. Material uniforms replay on
 * a material change or a pipeline change. */
typedef struct {
    uint32_t pipeline;
    uint32_t vertex_input;
    uint32_t material;
    uint32_t tex[NT_MATERIAL_MAX_TEXTURES];
    uint32_t sampler[NT_MATERIAL_MAX_TEXTURES];
} nt_renderer_bound_t;

static inline void nt_renderer_bind_pipeline(nt_renderer_bound_t *b, nt_pipeline_t p) {
    if (p.id == b->pipeline) {
        return;
    }
    nt_gfx_bind_pipeline(p);
    b->pipeline = p.id;
    /* Uniforms are program state and the new pipeline may sit on another program,
     * so the same material has to write them again. */
    b->material = 0;
}

static inline void nt_renderer_bind_vertex_input(nt_renderer_bound_t *b, nt_vertex_input_t vi) {
    if (vi.id == b->vertex_input) {
        return;
    }
    nt_gfx_bind_vertex_input(vi);
    b->vertex_input = vi.id;
}

/* Stateless program state: sampler unit for every declared slot (written whether or not
 * the texture resolved -- shared with other materials on the program) + every vec4 param. */
static inline void nt_renderer_set_material_uniforms(const nt_renderer_material_view_t *v) {
    for (uint8_t t = 0; t < v->tex_count; t++) {
        nt_gfx_set_uniform_int_h((nt_hash32_t){.value = v->tex_name_hashes[t]}, (int)t);
    }
    for (uint8_t p = 0; p < v->param_count; p++) {
        nt_gfx_set_uniform_vec4_h((nt_hash32_t){.value = v->param_name_hashes[p]}, v->params[p]);
    }
}

static inline void nt_renderer_apply_material_uniforms(nt_renderer_bound_t *b, uint32_t material_id, const nt_renderer_material_view_t *v) {
    if (material_id == b->material) {
        return;
    }
    nt_renderer_set_material_uniforms(v);
    b->material = material_id;
}

/* Context state: texture + effective sampler per slot, only where the slot's binding
 * changed. Unresolved slots are left alone (the unit keeps whatever it held). */
static inline void nt_renderer_apply_texture_slots(nt_renderer_bound_t *b, const nt_renderer_material_view_t *v) {
    for (uint8_t t = 0; t < v->tex_count; t++) {
        if (v->resolved_tex[t] == 0) {
            continue;
        }
        const nt_texture_t tex = {.id = v->resolved_tex[t]};
        uint32_t want = v->resolved_sampler[t].id;
        if (v->resolved_tex[t] != b->tex[t]) {
            const uint32_t def = nt_gfx_get_texture_default_sampler(tex).id;
            nt_gfx_bind_texture(tex, t);
            b->tex[t] = v->resolved_tex[t];
            b->sampler[t] = def; /* bind_texture also installed the asset-baked default */
            if (want == 0) {
                want = def;
            }
        } else if (want == 0) {
            want = nt_gfx_get_texture_default_sampler(tex).id;
        }
        if (want != b->sampler[t]) {
            nt_gfx_bind_sampler((nt_sampler_t){.id = want}, t);
            b->sampler[t] = want;
        }
    }
}

static inline nt_renderer_material_view_t nt_renderer_material_view(const nt_material_info_t *mi) {
    return (nt_renderer_material_view_t){
        .tex_count = mi->tex_count,
        .tex_name_hashes = mi->tex_name_hashes,
        .resolved_tex = mi->resolved_tex,
        .resolved_sampler = mi->resolved_sampler,
        .param_count = mi->param_count,
        .param_name_hashes = mi->param_name_hashes,
        .params = mi->params,
    };
}
// #endregion

/* Warn once to explain skipped draws without per-frame spam; pipeline insertion re-arms the flag. */
static inline void nt_renderer_warn_program_not_ready(bool *warned, const nt_material_info_t *mat_info) {
    if (*warned) {
        return;
    }
    NT_LOG_WARN("skipping '%s': its program is not ready -- assign one with nt_material_set_program, and after a context loss invalidate NT_ASSET_SHADER_CODE so the stages come back",
                (mat_info != NULL && mat_info->label != NULL) ? mat_info->label : "(unlabeled)");
    *warned = true;
}

#endif /* NT_RENDERER_SHARED_H */
