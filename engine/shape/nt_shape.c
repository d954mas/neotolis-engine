#include "shape/nt_shape.h"

#include "core/nt_assert.h"
#include "graphics/nt_gfx.h"

#include <string.h>

/* Use compiler builtin for sqrtf/fabsf to avoid Windows UCRT DLL import issues
   with clang + ASan. __builtin_sqrtf maps to the CPU instruction directly. */
#define nt_sqrtf(x) __builtin_sqrtf(x)
#define nt_fabsf(x) __builtin_fabsf(x)

_Static_assert(NT_SHAPE_MAX_VERTICES <= 65535, "uint16 index limit");

/* ---- Inline vector/matrix math (avoids cglm header-only issues on Windows/clang) ---- */

static void v3_sub(const float a[3], const float b[3], float out[3]) {
    out[0] = a[0] - b[0];
    out[1] = a[1] - b[1];
    out[2] = a[2] - b[2];
}

static void v3_cross(const float a[3], const float b[3], float out[3]) {
    out[0] = a[1] * b[2] - a[2] * b[1];
    out[1] = a[2] * b[0] - a[0] * b[2];
    out[2] = a[0] * b[1] - a[1] * b[0];
}

static float v3_norm2(const float v[3]) { return (v[0] * v[0]) + (v[1] * v[1]) + (v[2] * v[2]); }

static void v3_normalize(float v[3]) {
    float len2 = v3_norm2(v);
    if (len2 > 1e-12F) {
        float inv = 1.0F / nt_sqrtf(len2);
        v[0] *= inv;
        v[1] *= inv;
        v[2] *= inv;
    }
}

static void v3_scale(float v[3], float s) {
    v[0] *= s;
    v[1] *= s;
    v[2] *= s;
}

/* 4x4 matrix inversion (column-major).
   Extracts camera world position from VP matrix inverse column 3. */
static void m4_inv(const float m[16], float out[16]) {
    /* Cofactor expansion for general 4x4 inverse */
    float a00 = m[0];
    float a01 = m[1];
    float a02 = m[2];
    float a03 = m[3];
    float a10 = m[4];
    float a11 = m[5];
    float a12 = m[6];
    float a13 = m[7];
    float a20 = m[8];
    float a21 = m[9];
    float a22 = m[10];
    float a23 = m[11];
    float a30 = m[12];
    float a31 = m[13];
    float a32 = m[14];
    float a33 = m[15];

    float b00 = (a00 * a11) - (a01 * a10);
    float b01 = (a00 * a12) - (a02 * a10);
    float b02 = (a00 * a13) - (a03 * a10);
    float b03 = (a01 * a12) - (a02 * a11);
    float b04 = (a01 * a13) - (a03 * a11);
    float b05 = (a02 * a13) - (a03 * a12);
    float b06 = (a20 * a31) - (a21 * a30);
    float b07 = (a20 * a32) - (a22 * a30);
    float b08 = (a20 * a33) - (a23 * a30);
    float b09 = (a21 * a32) - (a22 * a31);
    float b10 = (a21 * a33) - (a23 * a31);
    float b11 = (a22 * a33) - (a23 * a32);

    float det = (b00 * b11) - (b01 * b10) + (b02 * b09) + (b03 * b08) - (b04 * b07) + (b05 * b06);
    if (nt_fabsf(det) < 1e-12F) {
        memset(out, 0, sizeof(float) * 16);
        return;
    }
    float inv_det = 1.0F / det;

    out[0] = (a11 * b11 - a12 * b10 + a13 * b09) * inv_det;
    out[1] = (a02 * b10 - a01 * b11 - a03 * b09) * inv_det;
    out[2] = (a31 * b05 - a32 * b04 + a33 * b03) * inv_det;
    out[3] = (a22 * b04 - a21 * b05 - a23 * b03) * inv_det;
    out[4] = (a12 * b08 - a10 * b11 - a13 * b07) * inv_det;
    out[5] = (a00 * b11 - a02 * b08 + a03 * b07) * inv_det;
    out[6] = (a32 * b02 - a30 * b05 - a33 * b01) * inv_det;
    out[7] = (a20 * b05 - a22 * b02 + a23 * b01) * inv_det;
    out[8] = (a10 * b10 - a11 * b08 + a13 * b06) * inv_det;
    out[9] = (a01 * b08 - a00 * b10 - a03 * b06) * inv_det;
    out[10] = (a30 * b04 - a31 * b02 + a33 * b00) * inv_det;
    out[11] = (a21 * b02 - a20 * b04 - a23 * b00) * inv_det;
    out[12] = (a11 * b07 - a10 * b09 - a12 * b06) * inv_det;
    out[13] = (a00 * b09 - a01 * b07 + a02 * b06) * inv_det;
    out[14] = (a31 * b01 - a30 * b03 - a32 * b00) * inv_det;
    out[15] = (a20 * b03 - a21 * b01 + a22 * b00) * inv_det;
}

/* Rotate vec3 by quaternion: q * v * q_conjugate.
   Quaternion layout: [x, y, z, w]. */
static void quat_rotatev(const float q[4], const float v[3], float out[3]) {
    float qx = q[0];
    float qy = q[1];
    float qz = q[2];
    float qw = q[3];
    /* t = 2 * cross(q.xyz, v) */
    float tx = 2.0F * (qy * v[2] - qz * v[1]);
    float ty = 2.0F * (qz * v[0] - qx * v[2]);
    float tz = 2.0F * (qx * v[1] - qy * v[0]);
    /* result = v + w*t + cross(q.xyz, t) */
    out[0] = v[0] + qw * tx + (qy * tz - qz * ty);
    out[1] = v[1] + qw * ty + (qz * tx - qx * tz);
    out[2] = v[2] + qw * tz + (qx * ty - qy * tx);
}

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
    nt_shape_vertex_t vertices[NT_SHAPE_MAX_VERTICES];
    uint16_t indices[NT_SHAPE_MAX_INDICES];
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
                .stride = (uint16_t)sizeof(nt_shape_vertex_t),
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

static void set_vertex(nt_shape_vertex_t *v, const float pos[3], const float color[4]) {
    v->pos[0] = pos[0];
    v->pos[1] = pos[1];
    v->pos[2] = pos[2];
    v->color[0] = color[0];
    v->color[1] = color[1];
    v->color[2] = color[2];
    v->color[3] = color[3];
}

/* Emit a billboard quad wireframe edge between two endpoints.
   Both endpoints get the same color. */
static void emit_wire_edge(const float a[3], const float b[3], const float color[4]) {
    if (s_shape.vertex_count + 4 > NT_SHAPE_MAX_VERTICES || s_shape.index_count + 6 > NT_SHAPE_MAX_INDICES) {
        nt_shape_flush();
    }

    float edge[3];
    v3_sub(b, a, edge);

    /* Midpoint to camera */
    float mid[3] = {(a[0] + b[0]) * 0.5F, (a[1] + b[1]) * 0.5F, (a[2] + b[2]) * 0.5F};
    float to_cam[3];
    v3_sub(s_shape.cam_pos, mid, to_cam);

    /* side = normalize(cross(edge, to_cam)) * half_width */
    float side[3];
    v3_cross(edge, to_cam, side);

    float len_sq = v3_norm2(side);
    if (len_sq < 1e-12F) {
        /* Degenerate: edge parallel to camera. Fallback to cross(edge, up) */
        float up[3] = {0.0F, 1.0F, 0.0F};
        v3_cross(edge, up, side);
        len_sq = v3_norm2(side);
        if (len_sq < 1e-12F) {
            /* Still degenerate: edge parallel to Y. Use right. */
            float right[3] = {1.0F, 0.0F, 0.0F};
            v3_cross(edge, right, side);
        }
    }
    v3_normalize(side);

    float hw = s_shape.line_width * 0.5F;
    v3_scale(side, hw);

    uint16_t base = (uint16_t)s_shape.vertex_count;
    nt_shape_vertex_t *v = &s_shape.vertices[s_shape.vertex_count];

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
    if (s_shape.vertex_count + 4 > NT_SHAPE_MAX_VERTICES || s_shape.index_count + 6 > NT_SHAPE_MAX_INDICES) {
        nt_shape_flush();
    }

    float edge[3];
    v3_sub(b, a, edge);

    float mid[3] = {(a[0] + b[0]) * 0.5F, (a[1] + b[1]) * 0.5F, (a[2] + b[2]) * 0.5F};
    float to_cam[3];
    v3_sub(s_shape.cam_pos, mid, to_cam);

    float side[3];
    v3_cross(edge, to_cam, side);

    float len_sq = v3_norm2(side);
    if (len_sq < 1e-12F) {
        float up[3] = {0.0F, 1.0F, 0.0F};
        v3_cross(edge, up, side);
        len_sq = v3_norm2(side);
        if (len_sq < 1e-12F) {
            float right[3] = {1.0F, 0.0F, 0.0F};
            v3_cross(edge, right, side);
        }
    }
    v3_normalize(side);

    float hw = s_shape.line_width * 0.5F;
    v3_scale(side, hw);

    uint16_t base = (uint16_t)s_shape.vertex_count;
    nt_shape_vertex_t *v = &s_shape.vertices[s_shape.vertex_count];

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

void nt_shape_init(void) {
    memset(&s_shape, 0, sizeof(s_shape));

    s_shape.vs = nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_VERTEX, .source = s_shape_vs_src, .label = "shape_vs"});
    s_shape.fs = nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_FRAGMENT, .source = s_shape_fs_src, .label = "shape_fs"});

    /* 4 pipelines: depth x blend matrix.
       Fill pipelines get polygon_offset to prevent z-fighting. */
    s_shape.pip_depth = make_shape_pipeline(true, false, true);
    s_shape.pip_depth_blend = make_shape_pipeline(true, true, true);
    s_shape.pip_overlay = make_shape_pipeline(false, false, false);
    s_shape.pip_overlay_blend = make_shape_pipeline(false, true, false);

    s_shape.vbo =
        nt_gfx_make_buffer(&(nt_buffer_desc_t){.type = NT_BUFFER_VERTEX, .usage = NT_USAGE_STREAM, .size = NT_SHAPE_MAX_VERTICES * (uint32_t)sizeof(nt_shape_vertex_t), .label = "shape_vbo"});
    s_shape.ibo = nt_gfx_make_buffer(&(nt_buffer_desc_t){.type = NT_BUFFER_INDEX, .usage = NT_USAGE_STREAM, .size = NT_SHAPE_MAX_INDICES * (uint32_t)sizeof(uint16_t), .label = "shape_ibo"});

    s_shape.line_width = 0.02F;
    s_shape.segments = 32;
    s_shape.depth_enabled = true;
    s_shape.blend_enabled = false;
    s_shape.pip_active = s_shape.pip_depth;
    s_shape.initialized = true;
}

void nt_shape_shutdown(void) {
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

void nt_shape_flush(void) {
    if (s_shape.index_count == 0) {
        return;
    }

    nt_gfx_update_buffer(s_shape.vbo, s_shape.vertices, s_shape.vertex_count * (uint32_t)sizeof(nt_shape_vertex_t));
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

void nt_shape_set_vp(const float vp[16]) {
    memcpy(s_shape.vp, vp, sizeof(float) * 16);

    /* Extract camera position: inverse(VP) column 3 */
    float inv_vp[16];
    m4_inv(s_shape.vp, inv_vp);
    s_shape.cam_pos[0] = inv_vp[12]; /* col3.x */
    s_shape.cam_pos[1] = inv_vp[13]; /* col3.y */
    s_shape.cam_pos[2] = inv_vp[14]; /* col3.z */
}

void nt_shape_set_line_width(float width) { s_shape.line_width = width; }

void nt_shape_set_depth(bool enabled) {
    if (enabled == s_shape.depth_enabled) {
        return;
    }
    if (s_shape.index_count > 0) {
        nt_shape_flush();
    }
    s_shape.depth_enabled = enabled;
    s_shape.pip_active = get_active_pipeline();
}

void nt_shape_set_blend(bool enabled) {
    if (enabled == s_shape.blend_enabled) {
        return;
    }
    if (s_shape.index_count > 0) {
        nt_shape_flush();
    }
    s_shape.blend_enabled = enabled;
    s_shape.pip_active = get_active_pipeline();
}

void nt_shape_set_segments(int segments) { s_shape.segments = (segments <= 0) ? 32 : segments; }

/* ---- Line ---- */

void nt_shape_line(const float a[3], const float b[3], const float color[4]) { emit_wire_edge(a, b, color); }

void nt_shape_line_col(const float a[3], const float b[3], const float color_a[4], const float color_b[4]) { emit_wire_edge_col(a, b, color_a, color_b); }

/* ---- Rectangle ---- */

void nt_shape_rect(const float pos[3], const float size[2], const float color[4]) {
    if (s_shape.vertex_count + 4 > NT_SHAPE_MAX_VERTICES || s_shape.index_count + 6 > NT_SHAPE_MAX_INDICES) {
        nt_shape_flush();
    }

    uint16_t base = (uint16_t)s_shape.vertex_count;
    float hx = size[0] * 0.5F;
    float hy = size[1] * 0.5F;

    nt_shape_vertex_t *v = &s_shape.vertices[s_shape.vertex_count];
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

void nt_shape_rect_wire(const float pos[3], const float size[2], const float color[4]) {
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

void nt_shape_rect_rot(const float pos[3], const float size[2], const float rot[4], const float color[4]) {
    if (s_shape.vertex_count + 4 > NT_SHAPE_MAX_VERTICES || s_shape.index_count + 6 > NT_SHAPE_MAX_INDICES) {
        nt_shape_flush();
    }

    float hx = size[0] * 0.5F;
    float hy = size[1] * 0.5F;

    float offsets[4][3] = {
        {-hx, -hy, 0.0F},
        {+hx, -hy, 0.0F},
        {+hx, +hy, 0.0F},
        {-hx, +hy, 0.0F},
    };

    uint16_t base = (uint16_t)s_shape.vertex_count;
    nt_shape_vertex_t *v = &s_shape.vertices[s_shape.vertex_count];

    for (int i = 0; i < 4; i++) {
        float rotated[3];
        quat_rotatev(rot, offsets[i], rotated);
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

void nt_shape_rect_wire_rot(const float pos[3], const float size[2], const float rot[4], const float color[4]) {
    float hx = size[0] * 0.5F;
    float hy = size[1] * 0.5F;

    float offsets[4][3] = {
        {-hx, -hy, 0.0F},
        {+hx, -hy, 0.0F},
        {+hx, +hy, 0.0F},
        {-hx, +hy, 0.0F},
    };

    float corners[4][3];
    for (int i = 0; i < 4; i++) {
        float rotated[3];
        quat_rotatev(rot, offsets[i], rotated);
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

void nt_shape_triangle(const float a[3], const float b[3], const float c[3], const float color[4]) {
    if (s_shape.vertex_count + 3 > NT_SHAPE_MAX_VERTICES || s_shape.index_count + 3 > NT_SHAPE_MAX_INDICES) {
        nt_shape_flush();
    }

    uint16_t base = (uint16_t)s_shape.vertex_count;
    nt_shape_vertex_t *v = &s_shape.vertices[s_shape.vertex_count];

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

void nt_shape_triangle_wire(const float a[3], const float b[3], const float c[3], const float color[4]) {
    emit_wire_edge(a, b, color);
    emit_wire_edge(b, c, color);
    emit_wire_edge(c, a, color);
}

void nt_shape_triangle_col(const float a[3], const float b[3], const float c[3], const float color_a[4], const float color_b[4], const float color_c[4]) {
    if (s_shape.vertex_count + 3 > NT_SHAPE_MAX_VERTICES || s_shape.index_count + 3 > NT_SHAPE_MAX_INDICES) {
        nt_shape_flush();
    }

    uint16_t base = (uint16_t)s_shape.vertex_count;
    nt_shape_vertex_t *v = &s_shape.vertices[s_shape.vertex_count];

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

void nt_shape_triangle_wire_col(const float a[3], const float b[3], const float c[3], const float color_a[4], const float color_b[4], const float color_c[4]) {
    emit_wire_edge_col(a, b, color_a, color_b);
    emit_wire_edge_col(b, c, color_b, color_c); // NOLINT(readability-suspicious-call-argument)
    emit_wire_edge_col(c, a, color_c, color_a); // NOLINT(readability-suspicious-call-argument)
}

/* ---- Stubs for plan 02 shapes ---- */

void nt_shape_circle(const float center[3], float radius, const float color[4]) {
    (void)center;
    (void)radius;
    (void)color;
}

void nt_shape_circle_wire(const float center[3], float radius, const float color[4]) {
    (void)center;
    (void)radius;
    (void)color;
}

void nt_shape_cube(const float center[3], const float size[3], const float color[4]) {
    (void)center;
    (void)size;
    (void)color;
}

void nt_shape_cube_wire(const float center[3], const float size[3], const float color[4]) {
    (void)center;
    (void)size;
    (void)color;
}

void nt_shape_sphere(const float center[3], float radius, const float color[4]) {
    (void)center;
    (void)radius;
    (void)color;
}

void nt_shape_sphere_wire(const float center[3], float radius, const float color[4]) {
    (void)center;
    (void)radius;
    (void)color;
}

void nt_shape_cylinder(const float base[3], float radius, float height, const float color[4]) {
    (void)base;
    (void)radius;
    (void)height;
    (void)color;
}

void nt_shape_cylinder_wire(const float base[3], float radius, float height, const float color[4]) {
    (void)base;
    (void)radius;
    (void)height;
    (void)color;
}

void nt_shape_capsule(const float base[3], float radius, float height, const float color[4]) {
    (void)base;
    (void)radius;
    (void)height;
    (void)color;
}

void nt_shape_capsule_wire(const float base[3], float radius, float height, const float color[4]) {
    (void)base;
    (void)radius;
    (void)height;
    (void)color;
}

void nt_shape_mesh(const float *positions, const uint16_t *indices, uint32_t num_indices, const float color[4]) {
    (void)positions;
    (void)indices;
    (void)num_indices;
    (void)color;
}

void nt_shape_mesh_wire(const float *positions, const uint16_t *indices, uint32_t num_indices, const float color[4]) {
    (void)positions;
    (void)indices;
    (void)num_indices;
    (void)color;
}

/* ---- Test accessors (always compiled; header guards visibility) ---- */

uint32_t nt_shape_test_vertex_count(void) { return s_shape.vertex_count; }
uint32_t nt_shape_test_index_count(void) { return s_shape.index_count; }
const float *nt_shape_test_cam_pos(void) { return s_shape.cam_pos; }
float nt_shape_test_line_width(void) { return s_shape.line_width; }
int nt_shape_test_segments(void) { return s_shape.segments; }
bool nt_shape_test_initialized(void) { return s_shape.initialized; }
