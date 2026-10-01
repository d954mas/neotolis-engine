#include "renderers/nt_ui_shape_renderer.h"

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "core/nt_assert.h"
#include "graphics/nt_gfx.h"
#include "log/nt_log.h"
#include "renderers/nt_renderer_shared.h"

/* Two triangles per instance; the vertex shader maps gl_VertexID 0..5 to TL,TR,BR,TL,BR,BL. */
#define NT_UI_SHAPE_VERTICES_PER_INSTANCE 6U

// #region module state
typedef struct {
    nt_pipeline_t pipeline;
    nt_material_t material;
    uint32_t first_instance;
    uint32_t instance_count;
} nt_ui_shape_draw_cmd_t;

static struct {
    bool initialized;
    bool warned_program_not_ready;
    nt_renderer_pipeline_entry_t pipelines[NT_UI_SHAPE_RENDERER_MAX_PIPELINES];
    uint16_t pipeline_count;
    nt_buffer_t instance_buf;
    nt_vertex_input_t vertex_input;
    nt_ui_shape_instance_t *staging;
    uint32_t max_instances;
    uint32_t instance_count;
    nt_ui_shape_draw_cmd_t cmds[NT_UI_SHAPE_RENDERER_MAX_DRAW_CMDS];
    uint32_t cmd_count;
    /* The open command's material and program: a flat program replace keeps the handle. */
    nt_material_t current_mat;
    nt_program_t current_program;
#ifdef NT_TEST_ACCESS
    nt_ui_shape_instance_t test_emitted[256];
    uint32_t test_emit_count;
    uint32_t test_draw_count;
#endif
} s_ui_shape;
// #endregion

// #region lifecycle
#define UI_SHAPE_ATTR(loc, field, kind, n, norm) {.location = (loc), .type = (kind), .count = (n), .normalized = (norm), .offset = (uint16_t)offsetof(nt_ui_shape_instance_t, field)}

static const nt_vertex_layout_t s_instance_layout = {
    .stride = sizeof(nt_ui_shape_instance_t),
    .attr_count = 10,
    .attrs =
        {
            UI_SHAPE_ATTR(0, origin, NT_VERTEX_FLOAT, 4, false),
            UI_SHAPE_ATTR(1, axis_x, NT_VERTEX_FLOAT, 4, false),
            UI_SHAPE_ATTR(2, axis_y, NT_VERTEX_FLOAT, 4, false),
            UI_SHAPE_ATTR(3, geometry, NT_VERTEX_FLOAT, 4, false),
            UI_SHAPE_ATTR(4, widths, NT_VERTEX_FLOAT, 4, false),
            UI_SHAPE_ATTR(5, user, NT_VERTEX_FLOAT, 4, false),
            UI_SHAPE_ATTR(6, color, NT_VERTEX_UINT8, 4, true),
            UI_SHAPE_ATTR(7, endpoint, NT_VERTEX_UINT8, 4, true),
            UI_SHAPE_ATTR(8, border, NT_VERTEX_UINT8, 4, true),
            UI_SHAPE_ATTR(9, control, NT_VERTEX_UINT8, 4, false),
        },
};

static nt_result_t create_gpu_resources(void) {
    s_ui_shape.instance_buf = nt_gfx_make_buffer(&(nt_buffer_desc_t){
        .type = NT_BUFFER_VERTEX,
        .usage = NT_USAGE_DYNAMIC,
        .size = s_ui_shape.max_instances * (uint32_t)sizeof(nt_ui_shape_instance_t),
        .label = "ui_shape_instances",
    });
    if (s_ui_shape.instance_buf.id == 0) {
        return NT_ERR_INIT_FAILED;
    }
    s_ui_shape.vertex_input = nt_gfx_make_vertex_input(&(nt_vertex_input_desc_t){.instance_layout = s_instance_layout, .label = "ui_shape_vi"});
    if (s_ui_shape.vertex_input.id == 0) {
        nt_gfx_destroy_buffer(s_ui_shape.instance_buf);
        s_ui_shape.instance_buf = (nt_buffer_t){0};
        return NT_ERR_INIT_FAILED;
    }
    return NT_OK;
}

static void destroy_gpu_resources(void) {
    for (uint16_t i = 0; i < s_ui_shape.pipeline_count; i++) {
        nt_gfx_destroy_pipeline(s_ui_shape.pipelines[i].pipeline);
    }
    s_ui_shape.pipeline_count = 0;
    nt_gfx_destroy_vertex_input(s_ui_shape.vertex_input);
    nt_gfx_destroy_buffer(s_ui_shape.instance_buf);
    s_ui_shape.vertex_input = (nt_vertex_input_t){0};
    s_ui_shape.instance_buf = (nt_buffer_t){0};
    /* Queued commands reference the discarded GPU objects; never flush them. */
    s_ui_shape.instance_count = 0;
    s_ui_shape.cmd_count = 0;
    s_ui_shape.current_mat = (nt_material_t){0};
    s_ui_shape.current_program = NT_PROGRAM_INVALID;
    s_ui_shape.warned_program_not_ready = false;
}

nt_result_t nt_ui_shape_renderer_init(uint32_t max_instances) {
    NT_ASSERT(!s_ui_shape.initialized);
    NT_ASSERT(max_instances > 0U);
    memset(&s_ui_shape, 0, sizeof(s_ui_shape));
    s_ui_shape.max_instances = max_instances;
    s_ui_shape.staging = (nt_ui_shape_instance_t *)calloc(max_instances, sizeof(nt_ui_shape_instance_t));
    if (s_ui_shape.staging == NULL || create_gpu_resources() != NT_OK) {
        free(s_ui_shape.staging);
        memset(&s_ui_shape, 0, sizeof(s_ui_shape));
        NT_LOG_ERROR("failed to initialize UI shape renderer");
        return NT_ERR_INIT_FAILED;
    }
    s_ui_shape.initialized = true;
    return NT_OK;
}

void nt_ui_shape_renderer_shutdown(void) {
    if (!s_ui_shape.initialized) {
        return;
    }
    destroy_gpu_resources();
    free(s_ui_shape.staging);
    memset(&s_ui_shape, 0, sizeof(s_ui_shape));
}

nt_result_t nt_ui_shape_renderer_restore_gpu(void) {
    if (!s_ui_shape.initialized) {
        return NT_OK;
    }
    destroy_gpu_resources();
    return create_gpu_resources();
}
// #endregion

// #region commands
static nt_pipeline_t find_or_create_pipeline(const nt_material_info_t *mat_info) {
    if (!nt_gfx_program_ready(mat_info->program)) {
        nt_renderer_warn_program_not_ready(&s_ui_shape.warned_program_not_ready, mat_info);
        return (nt_pipeline_t){0};
    }
    const nt_pipeline_desc_t desc = nt_renderer_material_pipeline_desc(mat_info, "ui_shape_pipeline");
    const nt_gfx_pipeline_key_t key = nt_gfx_pipeline_key(&desc);
    const nt_pipeline_t cached = nt_renderer_pipeline_cache_find(s_ui_shape.pipelines, s_ui_shape.pipeline_count, &key);
    if (cached.id != 0) {
        return cached;
    }
    return nt_renderer_pipeline_cache_insert(s_ui_shape.pipelines, &s_ui_shape.pipeline_count, NT_UI_SHAPE_RENDERER_MAX_PIPELINES, &key, &desc, &s_ui_shape.warned_program_not_ready);
}

/* Empty commands are popped so they never reach the GPU. */
static void close_current_cmd(void) {
    if (s_ui_shape.cmd_count == 0) {
        return;
    }
    nt_ui_shape_draw_cmd_t *c = &s_ui_shape.cmds[s_ui_shape.cmd_count - 1];
    c->instance_count = s_ui_shape.instance_count - c->first_instance;
    if (c->instance_count == 0) {
        s_ui_shape.cmd_count--;
    }
}

static void open_cmd(nt_pipeline_t pipeline, nt_material_t mat) {
    if (s_ui_shape.cmd_count >= NT_UI_SHAPE_RENDERER_MAX_DRAW_CMDS) {
        nt_ui_shape_renderer_flush();
    }
    s_ui_shape.cmds[s_ui_shape.cmd_count++] = (nt_ui_shape_draw_cmd_t){.pipeline = pipeline, .material = mat, .first_instance = s_ui_shape.instance_count};
}

void nt_ui_shape_renderer_set_material(nt_material_t mat) {
    NT_ASSERT(s_ui_shape.initialized && "nt_ui_shape_renderer_init before drawing shapes");
    NT_ASSERT(s_ui_shape.instance_buf.id != 0 && "retry failed GPU restore before submitting shapes");
    /* Validate before the same-handle early return: a stale handle must assert. */
    const nt_material_info_t *mat_info = nt_material_get_info(mat);
    NT_ASSERT(mat_info != NULL && mat_info->program.id != 0 && "nt_ui_shape_renderer_set_material: material has no program");
    if (mat.id == s_ui_shape.current_mat.id && mat_info->program.id == s_ui_shape.current_program.id && s_ui_shape.cmd_count > 0) {
        return;
    }
    close_current_cmd();
    /* An invalid pipeline still opens a command; flush drops it. */
    open_cmd(find_or_create_pipeline(mat_info), mat);
    s_ui_shape.current_mat = mat;
    s_ui_shape.current_program = mat_info->program;
}
// #endregion

// #region emit + flush
void nt_ui_shape_renderer_emit(const nt_ui_shape_instance_t *instance) {
    NT_ASSERT(instance != NULL);
    NT_ASSERT(s_ui_shape.cmd_count > 0 && "nt_ui_shape_renderer_emit: call nt_ui_shape_renderer_set_material first");
    if (s_ui_shape.instance_count == s_ui_shape.max_instances) {
        const nt_ui_shape_draw_cmd_t snapshot = s_ui_shape.cmds[s_ui_shape.cmd_count - 1];
        const nt_program_t program = s_ui_shape.current_program;
        nt_ui_shape_renderer_flush();
        open_cmd(snapshot.pipeline, snapshot.material);
        s_ui_shape.current_mat = snapshot.material;
        s_ui_shape.current_program = program;
    }
    s_ui_shape.staging[s_ui_shape.instance_count++] = *instance;
#ifdef NT_TEST_ACCESS
    if (s_ui_shape.test_emit_count < 256U) {
        s_ui_shape.test_emitted[s_ui_shape.test_emit_count] = *instance;
    }
    s_ui_shape.test_emit_count++;
#endif
}

void nt_ui_shape_renderer_flush(void) {
    close_current_cmd();
    if (s_ui_shape.instance_count == 0) {
        s_ui_shape.cmd_count = 0;
        s_ui_shape.current_mat = (nt_material_t){0};
        s_ui_shape.current_program = NT_PROGRAM_INVALID;
        return;
    }
    /* Orphan with data, like sprite and text: a partial rewrite of a buffer in flight stalls on mobile. */
    nt_gfx_orphan_buffer(s_ui_shape.instance_buf, s_ui_shape.staging, s_ui_shape.instance_count * (uint32_t)sizeof(nt_ui_shape_instance_t));
    nt_renderer_bound_t bound = {0};
    for (uint32_t ci = 0; ci < s_ui_shape.cmd_count; ci++) {
        const nt_ui_shape_draw_cmd_t *c = &s_ui_shape.cmds[ci];
        if (!nt_gfx_pipeline_valid(c->pipeline) || !nt_gfx_vertex_input_valid(s_ui_shape.vertex_input)) {
            continue;
        }
        const nt_material_info_t *mi = nt_material_get_info(c->material);
        if (mi == NULL) {
            continue;
        }
        nt_renderer_bind_pipeline(&bound, c->pipeline);
        nt_renderer_bind_vertex_input(&bound, s_ui_shape.vertex_input);
        nt_gfx_bind_instance_buffer(s_ui_shape.instance_buf, c->first_instance * (uint32_t)sizeof(nt_ui_shape_instance_t));
        const nt_renderer_material_view_t view = nt_renderer_material_view(mi);
        nt_renderer_apply_material_uniforms(&bound, c->material.id, &view);
        nt_renderer_apply_texture_slots(&view);
        nt_gfx_draw_instanced(0, NT_UI_SHAPE_VERTICES_PER_INSTANCE, c->instance_count);
#ifdef NT_TEST_ACCESS
        s_ui_shape.test_draw_count++;
#endif
    }
    s_ui_shape.instance_count = 0;
    s_ui_shape.cmd_count = 0;
    s_ui_shape.current_mat = (nt_material_t){0};
    s_ui_shape.current_program = NT_PROGRAM_INVALID;
}
// #endregion

// #region test accessors
#ifdef NT_TEST_ACCESS
void nt_ui_shape_renderer_test_reset(void) {
    s_ui_shape.test_emit_count = 0;
    s_ui_shape.test_draw_count = 0;
}
uint32_t nt_ui_shape_renderer_test_emit_count(void) { return s_ui_shape.test_emit_count; }
const nt_ui_shape_instance_t *nt_ui_shape_renderer_test_emitted(uint32_t index) {
    NT_ASSERT(index < s_ui_shape.test_emit_count && index < 256U);
    return &s_ui_shape.test_emitted[index];
}
uint32_t nt_ui_shape_renderer_test_draw_count(void) { return s_ui_shape.test_draw_count; }
#endif
// #endregion
