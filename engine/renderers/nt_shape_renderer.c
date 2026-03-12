#include "renderers/nt_shape_renderer.h"

#include "core/nt_assert.h"
#include "graphics/nt_gfx.h"
#include "math/nt_math.h"

#include <string.h>

_Static_assert(NT_SHAPE_RENDERER_MAX_VERTICES <= 65535, "uint16 index limit");

/* Convert quaternion to 3x3 rotation matrix (column-major). */
static void quat_to_mat3(const float q[4], float m[3][3]) { glm_quat_mat3((float *)q, (vec3 *)m); }

/* Multiply mat3 by vec3: out = M * v (column-major). */
static void mat3_mulv(const float m[3][3], const float v[3], float out[3]) { glm_mat3_mulv((vec3 *)m, (float *)v, out); }

/* ---- Embedded shader source ---- */

static const char *s_shape_vs_src = "layout(location = 0) in vec3 a_position;\n"
                                    "layout(location = 2) in vec4 a_color;\n"
                                    "uniform mat4 u_vp;\n"
                                    "out vec4 v_color;\n"
                                    "void main() {\n"
                                    "    v_color = a_color;\n"
                                    "    gl_Position = u_vp * vec4(a_position, 1.0);\n"
                                    "}\n";

static const char *s_shape_fs_src = "in vec4 v_color;\n"
                                    "out vec4 frag_color;\n"
                                    "void main() {\n"
                                    "    frag_color = v_color;\n"
                                    "}\n";

/* ---- Module state ---- */

static struct {
    /* GPU resources */
    nt_shader_t vs;
    nt_shader_t fs;
    nt_pipeline_t pip_depth;         /* depth=on,  blend=off */
    nt_pipeline_t pip_depth_blend;   /* depth=on,  blend=on  */
    nt_pipeline_t pip_overlay;       /* depth=off, blend=off */
    nt_pipeline_t pip_overlay_blend; /* depth=off, blend=on  */
    nt_pipeline_t pip_active;
    nt_buffer_t vbo;
    nt_buffer_t ibo;

    /* CPU-side batch buffers */
    nt_shape_renderer_vertex_t vertices[NT_SHAPE_RENDERER_MAX_VERTICES];
    uint16_t indices[NT_SHAPE_RENDERER_MAX_INDICES];
    uint32_t vertex_count;
    uint32_t index_count;

    /* Settings */
    float vp[16];
    float cam_pos[3];
    float line_width;
    int segments;
    bool depth_enabled;
    bool blend_enabled;
    bool initialized;

    /* Sin/Cos lookup table for segment-based shapes */
    float sin_lut[NT_SHAPE_RENDERER_MAX_SEGMENTS + 1];
    float cos_lut[NT_SHAPE_RENDERER_MAX_SEGMENTS + 1];
    int lut_segs;
} s_shape;

/* ---- Helpers ---- */

static nt_pipeline_t get_active_pipeline(void) {
    if (s_shape.depth_enabled) {
        return s_shape.blend_enabled ? s_shape.pip_depth_blend : s_shape.pip_depth;
    }
    return s_shape.blend_enabled ? s_shape.pip_overlay_blend : s_shape.pip_overlay;
}

static nt_pipeline_t make_shape_pipeline(bool depth, bool blend, bool poly_offset) {
    nt_pipeline_desc_t desc = {
        .vertex_shader = s_shape.vs,
        .fragment_shader = s_shape.fs,
        .layout =
            {
                .attr_count = 2,
                .stride = (uint16_t)sizeof(nt_shape_renderer_vertex_t),
                .attrs =
                    {
                        {.location = NT_ATTR_POSITION, .format = NT_FORMAT_FLOAT3, .offset = 0},
                        {.location = NT_ATTR_COLOR, .format = NT_FORMAT_FLOAT4, .offset = 12},
                    },
            },
        .depth_test = depth,
        .depth_write = depth,
        .depth_func = NT_DEPTH_LEQUAL,
        .cull_face = false,
        .blend = blend,
        .blend_src = NT_BLEND_SRC_ALPHA,
        .blend_dst = NT_BLEND_ONE_MINUS_SRC_ALPHA,
        .polygon_offset = poly_offset,
        .polygon_offset_factor = poly_offset ? 1.0F : 0.0F,
        .polygon_offset_units = poly_offset ? 1.0F : 0.0F,
        .label = "shape_pipeline",
    };
    return nt_gfx_make_pipeline(&desc);
}

static void set_vertex(nt_shape_renderer_vertex_t *v, const float pos[3], const float color[4]) {
    v->pos[0] = pos[0];
    v->pos[1] = pos[1];
    v->pos[2] = pos[2];
    v->color[0] = color[0];
    v->color[1] = color[1];
    v->color[2] = color[2];
    v->color[3] = color[3];
}

/* Rebuild the sin/cos lookup table when segment count changes. */
static void rebuild_trig_lut(int segs) {
    if (s_shape.lut_segs == segs) {
        return;
    }
    float inv = 2.0F * NT_PI / (float)segs;
    for (int i = 0; i <= segs; i++) {
        float theta = inv * (float)i;
        s_shape.sin_lut[i] = sinf(theta);
        s_shape.cos_lut[i] = cosf(theta);
    }
    s_shape.lut_segs = segs;
}

/* Emit a billboard quad wireframe edge between two endpoints.
   Both endpoints get the same color. */
static void emit_wire_edge(const float a[3], const float b[3], const float color[4]) {
    if (s_shape.vertex_count + 4 > NT_SHAPE_RENDERER_MAX_VERTICES || s_shape.index_count + 6 > NT_SHAPE_RENDERER_MAX_INDICES) {
        nt_shape_renderer_flush();
    }

    float edge[3];
    glm_vec3_sub((float *)b, (float *)a, edge);

    /* Midpoint to camera */
    float mid[3] = {(a[0] + b[0]) * 0.5F, (a[1] + b[1]) * 0.5F, (a[2] + b[2]) * 0.5F};
    float to_cam[3];
    glm_vec3_sub(s_shape.cam_pos, mid, to_cam);

    /* side = normalize(cross(edge, to_cam)) * half_width */
    float side[3];
    glm_vec3_cross(edge, to_cam, side);

    float len_sq = glm_vec3_norm2(side);
    if (len_sq < 1e-12F) {
        /* Degenerate: edge parallel to camera. Fallback to cross(edge, up) */
        float up[3] = {0.0F, 1.0F, 0.0F};
        glm_vec3_cross(edge, up, side);
        len_sq = glm_vec3_norm2(side);
        if (len_sq < 1e-12F) {
            /* Still degenerate: edge parallel to Y. Use right. */
            float right[3] = {1.0F, 0.0F, 0.0F};
            glm_vec3_cross(edge, right, side);
            len_sq = glm_vec3_norm2(side);
        }
    }
    float inv_len = 1.0F / sqrtf(len_sq);
    side[0] *= inv_len;
    side[1] *= inv_len;
    side[2] *= inv_len;

    float hw = s_shape.line_width * 0.5F;
    glm_vec3_scale(side, hw, side);

    uint16_t base = (uint16_t)s_shape.vertex_count;
    nt_shape_renderer_vertex_t *v = &s_shape.vertices[s_shape.vertex_count];

    float p0[3] = {a[0] - side[0], a[1] - side[1], a[2] - side[2]};
    float p1[3] = {a[0] + side[0], a[1] + side[1], a[2] + side[2]};
    float p2[3] = {b[0] + side[0], b[1] + side[1], b[2] + side[2]};
    float p3[3] = {b[0] - side[0], b[1] - side[1], b[2] - side[2]};

    set_vertex(&v[0], p0, color);
    set_vertex(&v[1], p1, color);
    set_vertex(&v[2], p2, color);
    set_vertex(&v[3], p3, color);

    uint16_t *idx = &s_shape.indices[s_shape.index_count];
    idx[0] = base;
    idx[1] = (uint16_t)(base + 1);
    idx[2] = (uint16_t)(base + 2);
    idx[3] = base;
    idx[4] = (uint16_t)(base + 2);
    idx[5] = (uint16_t)(base + 3);

    s_shape.vertex_count += 4;
    s_shape.index_count += 6;
}

/* Emit a billboard quad wireframe edge with per-vertex color. */
static void emit_wire_edge_col(const float a[3], const float b[3], const float color_a[4], const float color_b[4]) {
    if (s_shape.vertex_count + 4 > NT_SHAPE_RENDERER_MAX_VERTICES || s_shape.index_count + 6 > NT_SHAPE_RENDERER_MAX_INDICES) {
        nt_shape_renderer_flush();
    }

    float edge[3];
    glm_vec3_sub((float *)b, (float *)a, edge);

    float mid[3] = {(a[0] + b[0]) * 0.5F, (a[1] + b[1]) * 0.5F, (a[2] + b[2]) * 0.5F};
    float to_cam[3];
    glm_vec3_sub(s_shape.cam_pos, mid, to_cam);

    float side[3];
    glm_vec3_cross(edge, to_cam, side);

    float len_sq = glm_vec3_norm2(side);
    if (len_sq < 1e-12F) {
        float up[3] = {0.0F, 1.0F, 0.0F};
        glm_vec3_cross(edge, up, side);
        len_sq = glm_vec3_norm2(side);
        if (len_sq < 1e-12F) {
            float right[3] = {1.0F, 0.0F, 0.0F};
            glm_vec3_cross(edge, right, side);
            len_sq = glm_vec3_norm2(side);
        }
    }
    float inv_len = 1.0F / sqrtf(len_sq);
    side[0] *= inv_len;
    side[1] *= inv_len;
    side[2] *= inv_len;

    float hw = s_shape.line_width * 0.5F;
    glm_vec3_scale(side, hw, side);

    uint16_t base = (uint16_t)s_shape.vertex_count;
    nt_shape_renderer_vertex_t *v = &s_shape.vertices[s_shape.vertex_count];

    float p0[3] = {a[0] - side[0], a[1] - side[1], a[2] - side[2]};
    float p1[3] = {a[0] + side[0], a[1] + side[1], a[2] + side[2]};
    float p2[3] = {b[0] + side[0], b[1] + side[1], b[2] + side[2]};
    float p3[3] = {b[0] - side[0], b[1] - side[1], b[2] - side[2]};

    set_vertex(&v[0], p0, color_a);
    set_vertex(&v[1], p1, color_a);
    set_vertex(&v[2], p2, color_b);
    set_vertex(&v[3], p3, color_b);

    uint16_t *idx = &s_shape.indices[s_shape.index_count];
    idx[0] = base;
    idx[1] = (uint16_t)(base + 1);
    idx[2] = (uint16_t)(base + 2);
    idx[3] = base;
    idx[4] = (uint16_t)(base + 2);
    idx[5] = (uint16_t)(base + 3);

    s_shape.vertex_count += 4;
    s_shape.index_count += 6;
}

/* ---- Lifecycle ---- */

void nt_shape_renderer_init(void) {
    memset(&s_shape, 0, sizeof(s_shape));

    s_shape.vs = nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_VERTEX, .source = s_shape_vs_src, .label = "shape_vs"});
    s_shape.fs = nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_FRAGMENT, .source = s_shape_fs_src, .label = "shape_fs"});

    /* 4 pipelines: depth x blend matrix.
       Fill pipelines get polygon_offset to prevent z-fighting. */
    s_shape.pip_depth = make_shape_pipeline(true, false, true);
    s_shape.pip_depth_blend = make_shape_pipeline(true, true, true);
    s_shape.pip_overlay = make_shape_pipeline(false, false, false);
    s_shape.pip_overlay_blend = make_shape_pipeline(false, true, false);

    s_shape.vbo = nt_gfx_make_buffer(
        &(nt_buffer_desc_t){.type = NT_BUFFER_VERTEX, .usage = NT_USAGE_STREAM, .size = NT_SHAPE_RENDERER_MAX_VERTICES * (uint32_t)sizeof(nt_shape_renderer_vertex_t), .label = "shape_vbo"});
    s_shape.ibo = nt_gfx_make_buffer(&(nt_buffer_desc_t){.type = NT_BUFFER_INDEX, .usage = NT_USAGE_STREAM, .size = NT_SHAPE_RENDERER_MAX_INDICES * (uint32_t)sizeof(uint16_t), .label = "shape_ibo"});

    s_shape.line_width = 0.02F;
    s_shape.segments = 32;
    rebuild_trig_lut(s_shape.segments);
    s_shape.depth_enabled = true;
    s_shape.blend_enabled = false;
    s_shape.pip_active = s_shape.pip_depth;
    s_shape.initialized = true;
}

void nt_shape_renderer_shutdown(void) {
    if (!s_shape.initialized) {
        return;
    }
    nt_gfx_destroy_buffer(s_shape.ibo);
    nt_gfx_destroy_buffer(s_shape.vbo);
    nt_gfx_destroy_pipeline(s_shape.pip_overlay_blend);
    nt_gfx_destroy_pipeline(s_shape.pip_overlay);
    nt_gfx_destroy_pipeline(s_shape.pip_depth_blend);
    nt_gfx_destroy_pipeline(s_shape.pip_depth);
    nt_gfx_destroy_shader(s_shape.fs);
    nt_gfx_destroy_shader(s_shape.vs);
    memset(&s_shape, 0, sizeof(s_shape));
}

void nt_shape_renderer_flush(void) {
    if (s_shape.index_count == 0) {
        return;
    }

    nt_gfx_update_buffer(s_shape.vbo, s_shape.vertices, s_shape.vertex_count * (uint32_t)sizeof(nt_shape_renderer_vertex_t));
    nt_gfx_update_buffer(s_shape.ibo, s_shape.indices, s_shape.index_count * (uint32_t)sizeof(uint16_t));

    nt_gfx_bind_pipeline(s_shape.pip_active);
    nt_gfx_bind_vertex_buffer(s_shape.vbo);
    nt_gfx_bind_index_buffer(s_shape.ibo);
    nt_gfx_set_uniform_mat4("u_vp", s_shape.vp);

    nt_gfx_draw_indexed(0, s_shape.index_count);

    s_shape.vertex_count = 0;
    s_shape.index_count = 0;
}

/* ---- State setters ---- */

void nt_shape_renderer_set_vp(const float vp[16]) {
    memcpy(s_shape.vp, vp, sizeof(float) * 16);

    /* Extract camera position: inverse(VP) column 3 */
    float inv_vp[16];
    glm_mat4_inv((vec4 *)s_shape.vp, (vec4 *)inv_vp);
    s_shape.cam_pos[0] = inv_vp[12]; /* col3.x */
    s_shape.cam_pos[1] = inv_vp[13]; /* col3.y */
    s_shape.cam_pos[2] = inv_vp[14]; /* col3.z */
}

void nt_shape_renderer_set_line_width(float width) { s_shape.line_width = width; }

void nt_shape_renderer_set_depth(bool enabled) {
    if (enabled == s_shape.depth_enabled) {
        return;
    }
    if (s_shape.index_count > 0) {
        nt_shape_renderer_flush();
    }
    s_shape.depth_enabled = enabled;
    s_shape.pip_active = get_active_pipeline();
}

void nt_shape_renderer_set_blend(bool enabled) {
    if (enabled == s_shape.blend_enabled) {
        return;
    }
    if (s_shape.index_count > 0) {
        nt_shape_renderer_flush();
    }
    s_shape.blend_enabled = enabled;
    s_shape.pip_active = get_active_pipeline();
}

void nt_shape_renderer_set_segments(int segments) {
    int s = (segments <= 0) ? 32 : segments;
    if (s > NT_SHAPE_RENDERER_MAX_SEGMENTS) {
        s = NT_SHAPE_RENDERER_MAX_SEGMENTS;
    }
    s_shape.segments = s;
    rebuild_trig_lut(s);
}

/* ---- Line ---- */

void nt_shape_renderer_line(const float a[3], const float b[3], const float color[4]) { emit_wire_edge(a, b, color); }

void nt_shape_renderer_line_col(const float a[3], const float b[3], const float color_a[4], const float color_b[4]) { emit_wire_edge_col(a, b, color_a, color_b); }

/* ---- Rectangle ---- */

void nt_shape_renderer_rect(const float pos[3], const float size[2], const float color[4]) {
    if (s_shape.vertex_count + 4 > NT_SHAPE_RENDERER_MAX_VERTICES || s_shape.index_count + 6 > NT_SHAPE_RENDERER_MAX_INDICES) {
        nt_shape_renderer_flush();
    }

    uint16_t base = (uint16_t)s_shape.vertex_count;
    float hx = size[0] * 0.5F;
    float hy = size[1] * 0.5F;

    nt_shape_renderer_vertex_t *v = &s_shape.vertices[s_shape.vertex_count];
    float p0[3] = {pos[0] - hx, pos[1] - hy, pos[2]};
    float p1[3] = {pos[0] + hx, pos[1] - hy, pos[2]};
    float p2[3] = {pos[0] + hx, pos[1] + hy, pos[2]};
    float p3[3] = {pos[0] - hx, pos[1] + hy, pos[2]};

    set_vertex(&v[0], p0, color);
    set_vertex(&v[1], p1, color);
    set_vertex(&v[2], p2, color);
    set_vertex(&v[3], p3, color);

    uint16_t *idx = &s_shape.indices[s_shape.index_count];
    idx[0] = base;
    idx[1] = (uint16_t)(base + 1);
    idx[2] = (uint16_t)(base + 2);
    idx[3] = base;
    idx[4] = (uint16_t)(base + 2);
    idx[5] = (uint16_t)(base + 3);

    s_shape.vertex_count += 4;
    s_shape.index_count += 6;
}

void nt_shape_renderer_rect_wire(const float pos[3], const float size[2], const float color[4]) {
    float hx = size[0] * 0.5F;
    float hy = size[1] * 0.5F;

    float c0[3] = {pos[0] - hx, pos[1] - hy, pos[2]};
    float c1[3] = {pos[0] + hx, pos[1] - hy, pos[2]};
    float c2[3] = {pos[0] + hx, pos[1] + hy, pos[2]};
    float c3[3] = {pos[0] - hx, pos[1] + hy, pos[2]};

    emit_wire_edge(c0, c1, color);
    emit_wire_edge(c1, c2, color);
    emit_wire_edge(c2, c3, color);
    emit_wire_edge(c3, c0, color);
}

void nt_shape_renderer_rect_rot(const float pos[3], const float size[2], const float rot[4], const float color[4]) {
    if (s_shape.vertex_count + 4 > NT_SHAPE_RENDERER_MAX_VERTICES || s_shape.index_count + 6 > NT_SHAPE_RENDERER_MAX_INDICES) {
        nt_shape_renderer_flush();
    }

    float hx = size[0] * 0.5F;
    float hy = size[1] * 0.5F;

    float offsets[4][3] = {
        {-hx, -hy, 0.0F},
        {+hx, -hy, 0.0F},
        {+hx, +hy, 0.0F},
        {-hx, +hy, 0.0F},
    };

    float rm[3][3];
    quat_to_mat3(rot, rm);

    uint16_t base = (uint16_t)s_shape.vertex_count;
    nt_shape_renderer_vertex_t *v = &s_shape.vertices[s_shape.vertex_count];

    for (int i = 0; i < 4; i++) {
        float rotated[3];
        mat3_mulv(rm, offsets[i], rotated);
        float p[3] = {pos[0] + rotated[0], pos[1] + rotated[1], pos[2] + rotated[2]};
        set_vertex(&v[i], p, color);
    }

    uint16_t *idx = &s_shape.indices[s_shape.index_count];
    idx[0] = base;
    idx[1] = (uint16_t)(base + 1);
    idx[2] = (uint16_t)(base + 2);
    idx[3] = base;
    idx[4] = (uint16_t)(base + 2);
    idx[5] = (uint16_t)(base + 3);

    s_shape.vertex_count += 4;
    s_shape.index_count += 6;
}

void nt_shape_renderer_rect_wire_rot(const float pos[3], const float size[2], const float rot[4], const float color[4]) {
    float hx = size[0] * 0.5F;
    float hy = size[1] * 0.5F;

    float offsets[4][3] = {
        {-hx, -hy, 0.0F},
        {+hx, -hy, 0.0F},
        {+hx, +hy, 0.0F},
        {-hx, +hy, 0.0F},
    };

    float rm[3][3];
    quat_to_mat3(rot, rm);

    float corners[4][3];
    for (int i = 0; i < 4; i++) {
        float rotated[3];
        mat3_mulv(rm, offsets[i], rotated);
        corners[i][0] = pos[0] + rotated[0];
        corners[i][1] = pos[1] + rotated[1];
        corners[i][2] = pos[2] + rotated[2];
    }

    emit_wire_edge(corners[0], corners[1], color);
    emit_wire_edge(corners[1], corners[2], color);
    emit_wire_edge(corners[2], corners[3], color);
    emit_wire_edge(corners[3], corners[0], color);
}

/* ---- Triangle ---- */

void nt_shape_renderer_triangle(const float a[3], const float b[3], const float c[3], const float color[4]) {
    if (s_shape.vertex_count + 3 > NT_SHAPE_RENDERER_MAX_VERTICES || s_shape.index_count + 3 > NT_SHAPE_RENDERER_MAX_INDICES) {
        nt_shape_renderer_flush();
    }

    uint16_t base = (uint16_t)s_shape.vertex_count;
    nt_shape_renderer_vertex_t *v = &s_shape.vertices[s_shape.vertex_count];

    set_vertex(&v[0], a, color);
    set_vertex(&v[1], b, color);
    set_vertex(&v[2], c, color);

    uint16_t *idx = &s_shape.indices[s_shape.index_count];
    idx[0] = base;
    idx[1] = (uint16_t)(base + 1);
    idx[2] = (uint16_t)(base + 2);

    s_shape.vertex_count += 3;
    s_shape.index_count += 3;
}

void nt_shape_renderer_triangle_wire(const float a[3], const float b[3], const float c[3], const float color[4]) {
    emit_wire_edge(a, b, color);
    emit_wire_edge(b, c, color);
    emit_wire_edge(c, a, color);
}

void nt_shape_renderer_triangle_col(const float a[3], const float b[3], const float c[3], const float color_a[4], const float color_b[4], const float color_c[4]) {
    if (s_shape.vertex_count + 3 > NT_SHAPE_RENDERER_MAX_VERTICES || s_shape.index_count + 3 > NT_SHAPE_RENDERER_MAX_INDICES) {
        nt_shape_renderer_flush();
    }

    uint16_t base = (uint16_t)s_shape.vertex_count;
    nt_shape_renderer_vertex_t *v = &s_shape.vertices[s_shape.vertex_count];

    set_vertex(&v[0], a, color_a);
    set_vertex(&v[1], b, color_b);
    set_vertex(&v[2], c, color_c);

    uint16_t *idx = &s_shape.indices[s_shape.index_count];
    idx[0] = base;
    idx[1] = (uint16_t)(base + 1);
    idx[2] = (uint16_t)(base + 2);

    s_shape.vertex_count += 3;
    s_shape.index_count += 3;
}

void nt_shape_renderer_triangle_wire_col(const float a[3], const float b[3], const float c[3], const float color_a[4], const float color_b[4], const float color_c[4]) {
    emit_wire_edge_col(a, b, color_a, color_b);
    emit_wire_edge_col(b, c, color_b, color_c); // NOLINT(readability-suspicious-call-argument)
    emit_wire_edge_col(c, a, color_c, color_a); // NOLINT(readability-suspicious-call-argument)
}

/* ---- Circle ---- */

void nt_shape_renderer_circle(const float center[3], float radius, const float color[4]) {
    int segs = s_shape.segments;
    uint32_t vert_count = (uint32_t)(segs + 1); /* center + ring */
    uint32_t idx_count = (uint32_t)(segs * 3);

    if (s_shape.vertex_count + vert_count > NT_SHAPE_RENDERER_MAX_VERTICES || s_shape.index_count + idx_count > NT_SHAPE_RENDERER_MAX_INDICES) {
        nt_shape_renderer_flush();
    }

    uint16_t base = (uint16_t)s_shape.vertex_count;
    nt_shape_renderer_vertex_t *v = &s_shape.vertices[s_shape.vertex_count];

    /* Center vertex */
    set_vertex(&v[0], center, color);

    /* Ring vertices in XZ plane */
    for (int i = 0; i < segs; i++) {
        float p[3] = {center[0] + (radius * s_shape.cos_lut[i]), center[1], center[2] + (radius * s_shape.sin_lut[i])};
        set_vertex(&v[1 + i], p, color);
    }

    /* Triangle fan indices */
    uint16_t *idx = &s_shape.indices[s_shape.index_count];
    int idx_off = 0;
    for (int i = 0; i < segs; i++) {
        int next = ((i + 1) % segs);
        idx[idx_off++] = base;                        /* center */
        idx[idx_off++] = (uint16_t)(base + 1 + i);    /* current ring */
        idx[idx_off++] = (uint16_t)(base + 1 + next); /* next ring */
    }

    s_shape.vertex_count += vert_count;
    s_shape.index_count += idx_count;
}

void nt_shape_renderer_circle_wire(const float center[3], float radius, const float color[4]) {
    int segs = s_shape.segments;

    for (int i = 0; i < segs; i++) {
        int next = (i + 1) % segs;
        float a[3] = {center[0] + (radius * s_shape.cos_lut[i]), center[1], center[2] + (radius * s_shape.sin_lut[i])};
        float b[3] = {center[0] + (radius * s_shape.cos_lut[next]), center[1], center[2] + (radius * s_shape.sin_lut[next])};
        emit_wire_edge(a, b, color);
    }
}

void nt_shape_renderer_circle_rot(const float center[3], float radius, const float rot[4], const float color[4]) {
    int segs = s_shape.segments;
    uint32_t vert_count = (uint32_t)(segs + 1);
    uint32_t idx_count = (uint32_t)(segs * 3);

    if (s_shape.vertex_count + vert_count > NT_SHAPE_RENDERER_MAX_VERTICES || s_shape.index_count + idx_count > NT_SHAPE_RENDERER_MAX_INDICES) {
        nt_shape_renderer_flush();
    }

    uint16_t base = (uint16_t)s_shape.vertex_count;
    nt_shape_renderer_vertex_t *v = &s_shape.vertices[s_shape.vertex_count];

    set_vertex(&v[0], center, color);

    float rm[3][3];
    quat_to_mat3(rot, rm);

    for (int i = 0; i < segs; i++) {
        float offset[3] = {radius * s_shape.cos_lut[i], 0.0F, radius * s_shape.sin_lut[i]};
        float rotated[3];
        mat3_mulv(rm, offset, rotated);
        float p[3] = {center[0] + rotated[0], center[1] + rotated[1], center[2] + rotated[2]};
        set_vertex(&v[1 + i], p, color);
    }

    uint16_t *idx = &s_shape.indices[s_shape.index_count];
    int idx_off = 0;
    for (int i = 0; i < segs; i++) {
        int next = ((i + 1) % segs);
        idx[idx_off++] = base;
        idx[idx_off++] = (uint16_t)(base + 1 + i);
        idx[idx_off++] = (uint16_t)(base + 1 + next);
    }

    s_shape.vertex_count += vert_count;
    s_shape.index_count += idx_count;
}

void nt_shape_renderer_circle_wire_rot(const float center[3], float radius, const float rot[4], const float color[4]) {
    int segs = s_shape.segments;
    float rm[3][3];
    quat_to_mat3(rot, rm);

    for (int i = 0; i < segs; i++) {
        int next = (i + 1) % segs;
        float off_a[3] = {radius * s_shape.cos_lut[i], 0.0F, radius * s_shape.sin_lut[i]};
        float off_b[3] = {radius * s_shape.cos_lut[next], 0.0F, radius * s_shape.sin_lut[next]};
        float rot_a[3];
        float rot_b[3];
        mat3_mulv(rm, off_a, rot_a);
        mat3_mulv(rm, off_b, rot_b);
        float a[3] = {center[0] + rot_a[0], center[1] + rot_a[1], center[2] + rot_a[2]};
        float b[3] = {center[0] + rot_b[0], center[1] + rot_b[1], center[2] + rot_b[2]};
        emit_wire_edge(a, b, color);
    }
}

/* ---- Cube ---- */

void nt_shape_renderer_cube(const float center[3], const float size[3], const float color[4]) {
    if (s_shape.vertex_count + 8 > NT_SHAPE_RENDERER_MAX_VERTICES || s_shape.index_count + 36 > NT_SHAPE_RENDERER_MAX_INDICES) {
        nt_shape_renderer_flush();
    }

    float hx = size[0] * 0.5F;
    float hy = size[1] * 0.5F;
    float hz = size[2] * 0.5F;

    uint16_t base = (uint16_t)s_shape.vertex_count;
    nt_shape_renderer_vertex_t *v = &s_shape.vertices[s_shape.vertex_count];

    /* 8 corner vertices */
    float corners[8][3] = {
        {center[0] - hx, center[1] - hy, center[2] - hz}, /* 0: ---  */
        {center[0] + hx, center[1] - hy, center[2] - hz}, /* 1: +--  */
        {center[0] + hx, center[1] + hy, center[2] - hz}, /* 2: ++-  */
        {center[0] - hx, center[1] + hy, center[2] - hz}, /* 3: -+-  */
        {center[0] - hx, center[1] - hy, center[2] + hz}, /* 4: --+  */
        {center[0] + hx, center[1] - hy, center[2] + hz}, /* 5: +-+  */
        {center[0] + hx, center[1] + hy, center[2] + hz}, /* 6: +++  */
        {center[0] - hx, center[1] + hy, center[2] + hz}, /* 7: -++  */
    };
    for (int i = 0; i < 8; i++) {
        set_vertex(&v[i], corners[i], color);
    }

    /* 36 indices: 6 faces x 2 triangles x 3 */
    static const uint16_t face_idx[36] = {
        0, 1, 2, 0, 2, 3, /* front  (-Z) */
        5, 4, 7, 5, 7, 6, /* back   (+Z) */
        4, 0, 3, 4, 3, 7, /* left   (-X) */
        1, 5, 6, 1, 6, 2, /* right  (+X) */
        3, 2, 6, 3, 6, 7, /* top    (+Y) */
        4, 5, 1, 4, 1, 0, /* bottom (-Y) */
    };

    uint16_t *idx = &s_shape.indices[s_shape.index_count];
    for (int i = 0; i < 36; i++) {
        idx[i] = (uint16_t)(base + face_idx[i]);
    }

    s_shape.vertex_count += 8;
    s_shape.index_count += 36;
}

void nt_shape_renderer_cube_wire(const float center[3], const float size[3], const float color[4]) {
    float hx = size[0] * 0.5F;
    float hy = size[1] * 0.5F;
    float hz = size[2] * 0.5F;

    float c[8][3] = {
        {center[0] - hx, center[1] - hy, center[2] - hz}, {center[0] + hx, center[1] - hy, center[2] - hz}, {center[0] + hx, center[1] + hy, center[2] - hz},
        {center[0] - hx, center[1] + hy, center[2] - hz}, {center[0] - hx, center[1] - hy, center[2] + hz}, {center[0] + hx, center[1] - hy, center[2] + hz},
        {center[0] + hx, center[1] + hy, center[2] + hz}, {center[0] - hx, center[1] + hy, center[2] + hz},
    };

    /* 12 edges */
    /* Bottom face */
    emit_wire_edge(c[0], c[1], color);
    emit_wire_edge(c[1], c[5], color);
    emit_wire_edge(c[5], c[4], color);
    emit_wire_edge(c[4], c[0], color);
    /* Top face */
    emit_wire_edge(c[3], c[2], color);
    emit_wire_edge(c[2], c[6], color);
    emit_wire_edge(c[6], c[7], color);
    emit_wire_edge(c[7], c[3], color);
    /* Vertical edges */
    emit_wire_edge(c[0], c[3], color);
    emit_wire_edge(c[1], c[2], color);
    emit_wire_edge(c[5], c[6], color);
    emit_wire_edge(c[4], c[7], color);
}

void nt_shape_renderer_cube_rot(const float center[3], const float size[3], const float rot[4], const float color[4]) {
    if (s_shape.vertex_count + 8 > NT_SHAPE_RENDERER_MAX_VERTICES || s_shape.index_count + 36 > NT_SHAPE_RENDERER_MAX_INDICES) {
        nt_shape_renderer_flush();
    }

    float hx = size[0] * 0.5F;
    float hy = size[1] * 0.5F;
    float hz = size[2] * 0.5F;

    float offsets[8][3] = {
        {-hx, -hy, -hz}, {+hx, -hy, -hz}, {+hx, +hy, -hz}, {-hx, +hy, -hz}, {-hx, -hy, +hz}, {+hx, -hy, +hz}, {+hx, +hy, +hz}, {-hx, +hy, +hz},
    };

    float rm[3][3];
    quat_to_mat3(rot, rm);

    uint16_t base = (uint16_t)s_shape.vertex_count;
    nt_shape_renderer_vertex_t *v = &s_shape.vertices[s_shape.vertex_count];

    for (int i = 0; i < 8; i++) {
        float rotated[3];
        mat3_mulv(rm, offsets[i], rotated);
        float p[3] = {center[0] + rotated[0], center[1] + rotated[1], center[2] + rotated[2]};
        set_vertex(&v[i], p, color);
    }

    static const uint16_t face_idx[36] = {
        0, 1, 2, 0, 2, 3, 5, 4, 7, 5, 7, 6, 4, 0, 3, 4, 3, 7, 1, 5, 6, 1, 6, 2, 3, 2, 6, 3, 6, 7, 4, 5, 1, 4, 1, 0,
    };

    uint16_t *idx = &s_shape.indices[s_shape.index_count];
    for (int i = 0; i < 36; i++) {
        idx[i] = (uint16_t)(base + face_idx[i]);
    }

    s_shape.vertex_count += 8;
    s_shape.index_count += 36;
}

void nt_shape_renderer_cube_wire_rot(const float center[3], const float size[3], const float rot[4], const float color[4]) {
    float hx = size[0] * 0.5F;
    float hy = size[1] * 0.5F;
    float hz = size[2] * 0.5F;

    float offsets[8][3] = {
        {-hx, -hy, -hz}, {+hx, -hy, -hz}, {+hx, +hy, -hz}, {-hx, +hy, -hz}, {-hx, -hy, +hz}, {+hx, -hy, +hz}, {+hx, +hy, +hz}, {-hx, +hy, +hz},
    };

    float rm[3][3];
    quat_to_mat3(rot, rm);

    float c[8][3];
    for (int i = 0; i < 8; i++) {
        float rotated[3];
        mat3_mulv(rm, offsets[i], rotated);
        c[i][0] = center[0] + rotated[0];
        c[i][1] = center[1] + rotated[1];
        c[i][2] = center[2] + rotated[2];
    }

    emit_wire_edge(c[0], c[1], color);
    emit_wire_edge(c[1], c[5], color);
    emit_wire_edge(c[5], c[4], color);
    emit_wire_edge(c[4], c[0], color);
    emit_wire_edge(c[3], c[2], color);
    emit_wire_edge(c[2], c[6], color);
    emit_wire_edge(c[6], c[7], color);
    emit_wire_edge(c[7], c[3], color);
    emit_wire_edge(c[0], c[3], color);
    emit_wire_edge(c[1], c[2], color);
    emit_wire_edge(c[5], c[6], color);
    emit_wire_edge(c[4], c[7], color);
}

/* ---- Sphere ---- */

void nt_shape_renderer_sphere(const float center[3], float radius, const float color[4]) {
    int segs = s_shape.segments;
    int rings = segs / 2;
    uint32_t vert_count = (uint32_t)((rings + 1) * (segs + 1));
    uint32_t idx_count = (uint32_t)(rings * segs * 6);

    if (s_shape.vertex_count + vert_count > NT_SHAPE_RENDERER_MAX_VERTICES || s_shape.index_count + idx_count > NT_SHAPE_RENDERER_MAX_INDICES) {
        nt_shape_renderer_flush();
    }

    uint16_t base = (uint16_t)s_shape.vertex_count;

    /* Precompute phi sin/cos per ring (stack-local) */
    float sin_phi[(NT_SHAPE_RENDERER_MAX_SEGMENTS / 2) + 1];
    float cos_phi[(NT_SHAPE_RENDERER_MAX_SEGMENTS / 2) + 1];
    for (int ring = 0; ring <= rings; ring++) {
        float phi = NT_PI * (float)ring / (float)rings;
        sin_phi[ring] = sinf(phi);
        cos_phi[ring] = cosf(phi);
    }

    /* Generate vertices */
    for (int ring = 0; ring <= rings; ring++) {
        float sp = sin_phi[ring];
        float cp = cos_phi[ring];
        for (int seg = 0; seg <= segs; seg++) {
            float st = s_shape.sin_lut[seg];
            float ct = s_shape.cos_lut[seg];
            float p[3] = {center[0] + (radius * sp * ct), center[1] + (radius * cp), center[2] + (radius * sp * st)};
            set_vertex(&s_shape.vertices[s_shape.vertex_count], p, color);
            s_shape.vertex_count++;
        }
    }

    /* Generate indices: 2 triangles per quad */
    for (int ring = 0; ring < rings; ring++) {
        for (int seg = 0; seg < segs; seg++) {
            uint16_t a = (uint16_t)(base + (ring * (segs + 1)) + seg);
            uint16_t b = (uint16_t)(a + 1);
            uint16_t c = (uint16_t)(a + (segs + 1));
            uint16_t d = (uint16_t)(c + 1);
            s_shape.indices[s_shape.index_count++] = a;
            s_shape.indices[s_shape.index_count++] = c;
            s_shape.indices[s_shape.index_count++] = b;
            s_shape.indices[s_shape.index_count++] = b;
            s_shape.indices[s_shape.index_count++] = c;
            s_shape.indices[s_shape.index_count++] = d;
        }
    }
}

void nt_shape_renderer_sphere_wire(const float center[3], float radius, const float color[4]) {
    int segs = s_shape.segments;

    /* Draw 3 great circles (XZ, XY, YZ planes) for clean wireframe contour.
       Each circle is a full loop of 'segs' edges using angle 0..2PI. */

    /* Equator (XZ plane): y=0, circle in XZ */
    for (int i = 0; i < segs; i++) {
        int next = (i + 1) % segs;
        float a[3] = {center[0] + (radius * s_shape.cos_lut[i]), center[1], center[2] + (radius * s_shape.sin_lut[i])};
        float b[3] = {center[0] + (radius * s_shape.cos_lut[next]), center[1], center[2] + (radius * s_shape.sin_lut[next])};
        emit_wire_edge(a, b, color);
    }

    /* Great circle in XY plane: z=0, circle in XY */
    for (int i = 0; i < segs; i++) {
        int next = (i + 1) % segs;
        float a[3] = {center[0] + (radius * s_shape.cos_lut[i]), center[1] + (radius * s_shape.sin_lut[i]), center[2]};
        float b[3] = {center[0] + (radius * s_shape.cos_lut[next]), center[1] + (radius * s_shape.sin_lut[next]), center[2]};
        emit_wire_edge(a, b, color);
    }

    /* Great circle in YZ plane: x=0, circle in YZ */
    for (int i = 0; i < segs; i++) {
        int next = (i + 1) % segs;
        float a[3] = {center[0], center[1] + (radius * s_shape.cos_lut[i]), center[2] + (radius * s_shape.sin_lut[i])};
        float b[3] = {center[0], center[1] + (radius * s_shape.cos_lut[next]), center[2] + (radius * s_shape.sin_lut[next])};
        emit_wire_edge(a, b, color);
    }
}

/* ---- Cylinder ---- */

/* Internal: emit cylinder geometry with optional quaternion rotation.
   Center is the midpoint of the cylinder. Height along Y axis. */
static void emit_cylinder_fill(const float center[3], float radius, float height, const float *rot, const float color[4]) {
    int segs = s_shape.segments;
    uint32_t vert_count = (uint32_t)((2 * (segs + 1)) + 2);
    uint32_t idx_count = (uint32_t)(12 * segs);

    if (s_shape.vertex_count + vert_count > NT_SHAPE_RENDERER_MAX_VERTICES || s_shape.index_count + idx_count > NT_SHAPE_RENDERER_MAX_INDICES) {
        nt_shape_renderer_flush();
    }

    uint16_t base = (uint16_t)s_shape.vertex_count;
    float hy = height * 0.5F;
    bool has_rot = (rot != NULL);
    float rm[3][3];
    if (has_rot) {
        quat_to_mat3(rot, rm);
    }

    /* Vertex layout: [top_center, bottom_center, top_ring[0..segs], bottom_ring[0..segs]] */
    /* Index 0: top center, 1: bottom center, 2..segs+2: top ring, segs+3..2*segs+3: bottom ring */

    /* Top center */
    float tc_off[3] = {0, hy, 0};
    if (has_rot) {
        float tmp[3];
        mat3_mulv(rm, tc_off, tmp);
        tc_off[0] = tmp[0];
        tc_off[1] = tmp[1];
        tc_off[2] = tmp[2];
    }
    float tc[3] = {center[0] + tc_off[0], center[1] + tc_off[1], center[2] + tc_off[2]};
    set_vertex(&s_shape.vertices[s_shape.vertex_count++], tc, color);

    /* Bottom center */
    float bc_off[3] = {0, -hy, 0};
    if (has_rot) {
        float tmp[3];
        mat3_mulv(rm, bc_off, tmp);
        bc_off[0] = tmp[0];
        bc_off[1] = tmp[1];
        bc_off[2] = tmp[2];
    }
    float bc[3] = {center[0] + bc_off[0], center[1] + bc_off[1], center[2] + bc_off[2]};
    set_vertex(&s_shape.vertices[s_shape.vertex_count++], bc, color);

    /* Top ring (segs+1 verts, last duplicates first for seam) */
    for (int i = 0; i <= segs; i++) {
        float off[3] = {radius * s_shape.cos_lut[i], hy, radius * s_shape.sin_lut[i]};
        if (has_rot) {
            float tmp[3];
            mat3_mulv(rm, off, tmp);
            off[0] = tmp[0];
            off[1] = tmp[1];
            off[2] = tmp[2];
        }
        float p[3] = {center[0] + off[0], center[1] + off[1], center[2] + off[2]};
        set_vertex(&s_shape.vertices[s_shape.vertex_count++], p, color);
    }

    /* Bottom ring (segs+1 verts) */
    for (int i = 0; i <= segs; i++) {
        float off[3] = {radius * s_shape.cos_lut[i], -hy, radius * s_shape.sin_lut[i]};
        if (has_rot) {
            float tmp[3];
            mat3_mulv(rm, off, tmp);
            off[0] = tmp[0];
            off[1] = tmp[1];
            off[2] = tmp[2];
        }
        float p[3] = {center[0] + off[0], center[1] + off[1], center[2] + off[2]};
        set_vertex(&s_shape.vertices[s_shape.vertex_count++], p, color);
    }

    /* Indices */
    uint16_t top_center = base;
    uint16_t bot_center = (uint16_t)(base + 1);
    uint16_t top_ring_start = (uint16_t)(base + 2);
    uint16_t bot_ring_start = (uint16_t)(base + 2 + segs + 1);

    /* Top cap fan */
    for (int i = 0; i < segs; i++) {
        s_shape.indices[s_shape.index_count++] = top_center;
        s_shape.indices[s_shape.index_count++] = (uint16_t)(top_ring_start + i);
        s_shape.indices[s_shape.index_count++] = (uint16_t)(top_ring_start + i + 1);
    }

    /* Bottom cap fan (reversed winding) */
    for (int i = 0; i < segs; i++) {
        s_shape.indices[s_shape.index_count++] = bot_center;
        s_shape.indices[s_shape.index_count++] = (uint16_t)(bot_ring_start + i + 1);
        s_shape.indices[s_shape.index_count++] = (uint16_t)(bot_ring_start + i);
    }

    /* Tube quads */
    for (int i = 0; i < segs; i++) {
        uint16_t t0 = (uint16_t)(top_ring_start + i);
        uint16_t t1 = (uint16_t)(top_ring_start + i + 1);
        uint16_t b0 = (uint16_t)(bot_ring_start + i);
        uint16_t b1 = (uint16_t)(bot_ring_start + i + 1);
        s_shape.indices[s_shape.index_count++] = t0;
        s_shape.indices[s_shape.index_count++] = b0;
        s_shape.indices[s_shape.index_count++] = t1;
        s_shape.indices[s_shape.index_count++] = t1;
        s_shape.indices[s_shape.index_count++] = b0;
        s_shape.indices[s_shape.index_count++] = b1;
    }
}

static void emit_cylinder_wire(const float center[3], float radius, float height, const float *rot, const float color[4]) {
    int segs = s_shape.segments;
    float hy = height * 0.5F;
    bool has_rot = (rot != NULL);
    float rm[3][3];
    if (has_rot) {
        quat_to_mat3(rot, rm);
    }

    /* Top and bottom circles */
    for (int i = 0; i < segs; i++) {
        int next = (i + 1) % segs;

        /* Top circle edge */
        float top_a_off[3] = {radius * s_shape.cos_lut[i], hy, radius * s_shape.sin_lut[i]};
        float top_b_off[3] = {radius * s_shape.cos_lut[next], hy, radius * s_shape.sin_lut[next]};
        if (has_rot) {
            float tmp[3];
            mat3_mulv(rm, top_a_off, tmp);
            top_a_off[0] = tmp[0];
            top_a_off[1] = tmp[1];
            top_a_off[2] = tmp[2];
            mat3_mulv(rm, top_b_off, tmp);
            top_b_off[0] = tmp[0];
            top_b_off[1] = tmp[1];
            top_b_off[2] = tmp[2];
        }
        float ta[3] = {center[0] + top_a_off[0], center[1] + top_a_off[1], center[2] + top_a_off[2]};
        float tb[3] = {center[0] + top_b_off[0], center[1] + top_b_off[1], center[2] + top_b_off[2]};
        emit_wire_edge(ta, tb, color);

        /* Bottom circle edge */
        float bot_a_off[3] = {radius * s_shape.cos_lut[i], -hy, radius * s_shape.sin_lut[i]};
        float bot_b_off[3] = {radius * s_shape.cos_lut[next], -hy, radius * s_shape.sin_lut[next]};
        if (has_rot) {
            float tmp[3];
            mat3_mulv(rm, bot_a_off, tmp);
            bot_a_off[0] = tmp[0];
            bot_a_off[1] = tmp[1];
            bot_a_off[2] = tmp[2];
            mat3_mulv(rm, bot_b_off, tmp);
            bot_b_off[0] = tmp[0];
            bot_b_off[1] = tmp[1];
            bot_b_off[2] = tmp[2];
        }
        float ba[3] = {center[0] + bot_a_off[0], center[1] + bot_a_off[1], center[2] + bot_a_off[2]};
        float bb[3] = {center[0] + bot_b_off[0], center[1] + bot_b_off[1], center[2] + bot_b_off[2]};
        emit_wire_edge(ba, bb, color);
    }

    /* 4 vertical struts at 0, 90, 180, 270 degrees */
    for (int i = 0; i < 4; i++) {
        float theta = NT_PI * 0.5F * (float)i;
        float ct = cosf(theta);
        float st = sinf(theta);
        float top_off[3] = {radius * ct, hy, radius * st};
        float bot_off[3] = {radius * ct, -hy, radius * st};
        if (has_rot) {
            float tmp[3];
            mat3_mulv(rm, top_off, tmp);
            top_off[0] = tmp[0];
            top_off[1] = tmp[1];
            top_off[2] = tmp[2];
            mat3_mulv(rm, bot_off, tmp);
            bot_off[0] = tmp[0];
            bot_off[1] = tmp[1];
            bot_off[2] = tmp[2];
        }
        float ta[3] = {center[0] + top_off[0], center[1] + top_off[1], center[2] + top_off[2]};
        float bv[3] = {center[0] + bot_off[0], center[1] + bot_off[1], center[2] + bot_off[2]};
        emit_wire_edge(ta, bv, color);
    }
}

void nt_shape_renderer_cylinder(const float center[3], float radius, float height, const float color[4]) { emit_cylinder_fill(center, radius, height, NULL, color); }

void nt_shape_renderer_cylinder_wire(const float center[3], float radius, float height, const float color[4]) { emit_cylinder_wire(center, radius, height, NULL, color); }

void nt_shape_renderer_cylinder_rot(const float center[3], float radius, float height, const float rot[4], const float color[4]) { emit_cylinder_fill(center, radius, height, rot, color); }

void nt_shape_renderer_cylinder_wire_rot(const float center[3], float radius, float height, const float rot[4], const float color[4]) { emit_cylinder_wire(center, radius, height, rot, color); }

/* ---- Capsule ---- */

/* Capsule: two hemisphere caps + cylindrical tube body.
   Center is the midpoint. Height is total height including caps.
   Body height = max(0, height - 2*radius). Hemisphere radius = radius.
   Tessellation: half_rings = segments/4 per hemisphere, tube = 1 ring section.
   Total ring sections = half_rings + 1 + half_rings = segments/2 + 1.
   Verts: (total_sections + 1) * (segs + 1). */

static void emit_capsule_fill(const float center[3], float radius, float height, const float *rot, const float color[4]) {
    int segs = s_shape.segments;
    int half_rings = segs / 4;
    int total_sections = (2 * half_rings) + 1; /* top hemi + tube + bottom hemi */
    uint32_t vert_count = (uint32_t)((total_sections + 1) * (segs + 1));
    uint32_t idx_count = (uint32_t)(total_sections * segs * 6);

    if (s_shape.vertex_count + vert_count > NT_SHAPE_RENDERER_MAX_VERTICES || s_shape.index_count + idx_count > NT_SHAPE_RENDERER_MAX_INDICES) {
        nt_shape_renderer_flush();
    }

    uint16_t base = (uint16_t)s_shape.vertex_count;
    float body_half = (height - (2.0F * radius)) * 0.5F;
    if (body_half < 0.0F) {
        body_half = 0.0F;
    }
    bool has_rot = (rot != NULL);
    float rm[3][3];
    if (has_rot) {
        quat_to_mat3(rot, rm);
    }

    /* Generate vertices row by row from top pole to bottom pole.
       Row indices: 0..half_rings = top hemisphere, half_rings+1 = body bottom,
       half_rings+1..total_sections = bottom hemisphere */
    for (int row = 0; row <= total_sections; row++) {
        float y_offset;
        float ring_radius;

        if (row <= half_rings) {
            /* Top hemisphere: row 0 = pole, row half_rings = equator */
            float phi = (NT_PI * 0.5F) * (float)(half_rings - row) / (float)half_rings; /* PI/2 -> 0 */
            y_offset = body_half + (radius * sinf(phi));
            ring_radius = radius * cosf(phi);
        } else if (row == half_rings + 1) {
            /* Body bottom edge (tube bottom) */
            y_offset = -body_half;
            ring_radius = radius;
        } else {
            /* Bottom hemisphere: row half_rings+2..total_sections */
            int bot_row = row - half_rings - 1;                              /* 1..half_rings */
            float phi = (NT_PI * 0.5F) * (float)bot_row / (float)half_rings; /* 0 -> PI/2 */
            y_offset = -body_half - (radius * sinf(phi));
            ring_radius = radius * cosf(phi);
        }

        for (int seg = 0; seg <= segs; seg++) {
            float off[3] = {ring_radius * s_shape.cos_lut[seg], y_offset, ring_radius * s_shape.sin_lut[seg]};
            if (has_rot) {
                float tmp[3];
                mat3_mulv(rm, off, tmp);
                off[0] = tmp[0];
                off[1] = tmp[1];
                off[2] = tmp[2];
            }
            float p[3] = {center[0] + off[0], center[1] + off[1], center[2] + off[2]};
            set_vertex(&s_shape.vertices[s_shape.vertex_count], p, color);
            s_shape.vertex_count++;
        }
    }

    /* Generate indices: 2 triangles per quad between adjacent rows */
    for (int row = 0; row < total_sections; row++) {
        for (int seg = 0; seg < segs; seg++) {
            uint16_t a = (uint16_t)(base + (row * (segs + 1)) + seg);
            uint16_t b = (uint16_t)(a + 1);
            uint16_t c = (uint16_t)(a + (segs + 1));
            uint16_t d = (uint16_t)(c + 1);
            s_shape.indices[s_shape.index_count++] = a;
            s_shape.indices[s_shape.index_count++] = c;
            s_shape.indices[s_shape.index_count++] = b;
            s_shape.indices[s_shape.index_count++] = b;
            s_shape.indices[s_shape.index_count++] = c;
            s_shape.indices[s_shape.index_count++] = d;
        }
    }
}

/* Compute y-offset and ring radius for a given capsule row.
   Extracts the branching logic from capsule wireframe to reduce complexity. */
static void capsule_row_params(int row, int half_rings, float body_half, float radius, float *out_y, float *out_r) {
    if (row <= half_rings) {
        float phi = (NT_PI * 0.5F) * (float)(half_rings - row) / (float)half_rings;
        *out_y = body_half + (radius * sinf(phi));
        *out_r = radius * cosf(phi);
    } else if (row == half_rings + 1) {
        *out_y = -body_half;
        *out_r = radius;
    } else {
        int br = row - half_rings - 1;
        float phi = (NT_PI * 0.5F) * (float)br / (float)half_rings;
        *out_y = -body_half - (radius * sinf(phi));
        *out_r = radius * cosf(phi);
    }
}

/* Optionally rotate an offset vector, then add center to produce final position. */
static void apply_rot_and_offset(const float center[3], float off[3], bool has_rot, const float rm[3][3], float out[3]) {
    if (has_rot) {
        float tmp[3];
        mat3_mulv(rm, off, tmp);
        off[0] = tmp[0];
        off[1] = tmp[1];
        off[2] = tmp[2];
    }
    out[0] = center[0] + off[0];
    out[1] = center[1] + off[1];
    out[2] = center[2] + off[2];
}

static void emit_capsule_wire(const float center[3], float radius, float height, const float *rot, const float color[4]) {
    int segs = s_shape.segments;
    int half_rings = segs / 4;
    int total_sections = (2 * half_rings) + 1;

    float body_half = (height - (2.0F * radius)) * 0.5F;
    if (body_half < 0.0F) {
        body_half = 0.0F;
    }
    bool has_rot = (rot != NULL);
    float rm[3][3];
    if (has_rot) {
        quat_to_mat3(rot, rm);
    }

    /* 4 meridian profile lines at 0, 90, 180, 270 degrees */
    for (int m = 0; m < 4; m++) {
        float theta = NT_PI * 0.5F * (float)m;
        float ct = cosf(theta);
        float st = sinf(theta);

        for (int row = 0; row < total_sections; row++) {
            float y0;
            float r0;
            float y1;
            float r1;
            capsule_row_params(row, half_rings, body_half, radius, &y0, &r0);
            capsule_row_params(row + 1, half_rings, body_half, radius, &y1, &r1);

            float off_a[3] = {r0 * ct, y0, r0 * st};
            float off_b[3] = {r1 * ct, y1, r1 * st};
            float a[3];
            float b[3];
            apply_rot_and_offset(center, off_a, has_rot, rm, a);
            apply_rot_and_offset(center, off_b, has_rot, rm, b);
            emit_wire_edge(a, b, color);
        }
    }

    /* 2 latitude rings: top equator (row half_rings) and bottom equator (row half_rings+1) */
    for (int r = 0; r < 2; r++) {
        int row = (r == 0) ? half_rings : half_rings + 1;
        float y_off;
        float rr;
        capsule_row_params(row, half_rings, body_half, radius, &y_off, &rr);

        for (int seg = 0; seg < segs; seg++) {
            int next = (seg + 1) % segs;
            float off_a[3] = {rr * s_shape.cos_lut[seg], y_off, rr * s_shape.sin_lut[seg]};
            float off_b[3] = {rr * s_shape.cos_lut[next], y_off, rr * s_shape.sin_lut[next]};
            float a[3];
            float b[3];
            apply_rot_and_offset(center, off_a, has_rot, rm, a);
            apply_rot_and_offset(center, off_b, has_rot, rm, b);
            emit_wire_edge(a, b, color);
        }
    }
}

void nt_shape_renderer_capsule(const float center[3], float radius, float height, const float color[4]) { emit_capsule_fill(center, radius, height, NULL, color); }

void nt_shape_renderer_capsule_wire(const float center[3], float radius, float height, const float color[4]) { emit_capsule_wire(center, radius, height, NULL, color); }

void nt_shape_renderer_capsule_rot(const float center[3], float radius, float height, const float rot[4], const float color[4]) { emit_capsule_fill(center, radius, height, rot, color); }

void nt_shape_renderer_capsule_wire_rot(const float center[3], float radius, float height, const float rot[4], const float color[4]) { emit_capsule_wire(center, radius, height, rot, color); }

/* ---- Mesh ---- */

void nt_shape_renderer_mesh(const float *positions, const uint16_t *indices, uint32_t num_indices, const float color[4]) {
    /* Determine how many unique vertices we need.
       Find max index to know vertex count. */
    uint16_t max_idx = 0;
    for (uint32_t i = 0; i < num_indices; i++) {
        if (indices[i] > max_idx) {
            max_idx = indices[i];
        }
    }
    uint32_t num_verts = (uint32_t)(max_idx + 1);

    if (s_shape.vertex_count + num_verts > NT_SHAPE_RENDERER_MAX_VERTICES || s_shape.index_count + num_indices > NT_SHAPE_RENDERER_MAX_INDICES) {
        nt_shape_renderer_flush();
    }

    uint16_t base = (uint16_t)s_shape.vertex_count;

    /* Copy positions into vertex buffer with color */
    for (uint32_t i = 0; i < num_verts; i++) {
        const float *pos = &positions[(size_t)i * 3];
        set_vertex(&s_shape.vertices[s_shape.vertex_count], pos, color);
        s_shape.vertex_count++;
    }

    /* Copy indices with base offset */
    for (uint32_t i = 0; i < num_indices; i++) {
        s_shape.indices[s_shape.index_count++] = (uint16_t)(base + indices[i]);
    }
}

void nt_shape_renderer_mesh_wire(const float *positions, const uint16_t *indices, uint32_t num_indices, const float color[4]) {
    /* For each triangle (3 consecutive indices), emit 3 wireframe edges */
    for (uint32_t i = 0; (i + 2) < num_indices; i += 3) {
        const float *a = &positions[(ptrdiff_t)indices[i] * 3];
        const float *b = &positions[(ptrdiff_t)indices[i + 1] * 3];
        const float *c = &positions[(ptrdiff_t)indices[i + 2] * 3];
        emit_wire_edge(a, b, color);
        emit_wire_edge(b, c, color);
        emit_wire_edge(c, a, color);
    }
}

/* ---- Test accessors (always compiled; header guards visibility) ---- */

uint32_t nt_shape_renderer_test_vertex_count(void) { return s_shape.vertex_count; }
uint32_t nt_shape_renderer_test_index_count(void) { return s_shape.index_count; }
const float *nt_shape_renderer_test_cam_pos(void) { return s_shape.cam_pos; }
float nt_shape_renderer_test_line_width(void) { return s_shape.line_width; }
int nt_shape_renderer_test_segments(void) { return s_shape.segments; }
bool nt_shape_renderer_test_initialized(void) { return s_shape.initialized; }
