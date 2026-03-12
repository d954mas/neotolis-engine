#include "renderers/nt_shape_renderer.h"

#include "core/nt_assert.h"
#include "graphics/nt_gfx.h"
#include "log/nt_log.h"
#include "math/nt_math.h"

#include <string.h>

_Static_assert(NT_SHAPE_RENDERER_MAX_VERTICES <= 65535, "uint16 index limit");

#define NT_SHAPE_SEGMENTS 16

/* Derived template sizes (compile-time, used for stack arrays) */
#define NT_SEG_CIRCLE_NV (NT_SHAPE_SEGMENTS + 1)
#define NT_SEG_CIRCLE_NI (NT_SHAPE_SEGMENTS * 3)
#define NT_SEG_SPHERE_RINGS (NT_SHAPE_SEGMENTS / 2)
#define NT_SEG_SPHERE_NV ((NT_SEG_SPHERE_RINGS + 1) * (NT_SHAPE_SEGMENTS + 1))
#define NT_SEG_SPHERE_NI (NT_SEG_SPHERE_RINGS * NT_SHAPE_SEGMENTS * 6)
#define NT_SEG_CYL_NV ((2 * (NT_SHAPE_SEGMENTS + 1)) + 2)
#define NT_SEG_CYL_NI (12 * NT_SHAPE_SEGMENTS)
#define NT_SEG_CAP_HALF (NT_SHAPE_SEGMENTS / 4)
#define NT_SEG_CAP_SECTIONS ((2 * NT_SEG_CAP_HALF) + 1)
#define NT_SEG_CAP_NV ((NT_SEG_CAP_SECTIONS + 1) * (NT_SHAPE_SEGMENTS + 1))
#define NT_SEG_CAP_NI (NT_SEG_CAP_SECTIONS * NT_SHAPE_SEGMENTS * 6)

/* Thin wrappers over cglm — cast float[] to cglm typedefs (versor=float[4],
   mat3=float[3][3], vec3=float[3]).  Casts only strip const; types are
   layout-identical.  No SIMD path for mat3/quat in cglm. */
static void quat_to_mat3(const float q[4], float m[3][3]) { glm_quat_mat3((float *)q, (vec3 *)m); }
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

/* ---- Instanced line vertex shader ---- */

static const char *s_line_vs_src = "layout(location = 0) in vec2 a_corner;\n"
                                   "layout(location = 1) in vec3 i_a;\n"
                                   "layout(location = 2) in vec3 i_b;\n"
                                   "layout(location = 3) in vec4 i_color;\n"
                                   "uniform mat4 u_vp;\n"
                                   "uniform vec4 u_cam_pos;\n"
                                   "uniform float u_line_width;\n"
                                   "out vec4 v_color;\n"
                                   "void main() {\n"
                                   "    vec3 pos = mix(i_a, i_b, a_corner.y);\n"
                                   "    v_color = i_color;\n"
                                   "    vec3 edge = i_b - i_a;\n"
                                   "    vec3 to_cam = u_cam_pos.xyz - pos;\n"
                                   "    vec3 side = cross(edge, to_cam);\n"
                                   "    float len_sq = dot(side, side);\n"
                                   "    if (len_sq < 1e-12) {\n"
                                   "        side = cross(edge, vec3(0.0, 1.0, 0.0));\n"
                                   "        len_sq = dot(side, side);\n"
                                   "    }\n"
                                   "    if (len_sq < 1e-12) {\n"
                                   "        side = cross(edge, vec3(1.0, 0.0, 0.0));\n"
                                   "        len_sq = dot(side, side);\n"
                                   "    }\n"
                                   "    side *= inversesqrt(max(len_sq, 1e-12));\n"
                                   "    pos += side * a_corner.x * u_line_width * 0.5;\n"
                                   "    float dist = length(to_cam);\n"
                                   "    if (dist > 1e-6) {\n"
                                   "        pos += to_cam * (0.0005 / dist);\n"
                                   "    }\n"
                                   "    gl_Position = u_vp * vec4(pos, 1.0);\n"
                                   "}\n";

/* ---- Instance data ---- */

typedef struct {
    float a[3];
    float b[3];
    uint8_t color[4];
} nt_shape_line_instance_t;

_Static_assert(sizeof(nt_shape_line_instance_t) == 28, "line instance size");

typedef struct {
    float center[3];
    float scale[3];
    float rot[4];
    uint8_t color[4];
} nt_shape_instance_t;

_Static_assert(sizeof(nt_shape_instance_t) == 44, "shape instance size");

/* Shape types that use instanced rendering */
enum {
    NT_SHAPE_RECT = 0,
    NT_SHAPE_CUBE,
    NT_SHAPE_CIRCLE,
    NT_SHAPE_SPHERE,
    NT_SHAPE_CYLINDER,
    NT_SHAPE_CAPSULE,
    NT_SHAPE_TYPE_COUNT,
};

#ifndef NT_SHAPE_RENDERER_MAX_INSTANCES
#define NT_SHAPE_RENDERER_MAX_INSTANCES 2048
#endif

/* Per-type template mesh */
typedef struct {
    nt_buffer_t vbo;
    nt_buffer_t ibo;
    uint32_t num_vertices;
    uint32_t num_indices;
} nt_shape_template_t;

/* ---- Instanced shape vertex shader ---- */

static const char *s_inst_vs_src = "layout(location = 0) in vec3 a_position;\n"
                                   "layout(location = 1) in vec3 i_center;\n"
                                   "layout(location = 2) in vec3 i_scale;\n"
                                   "layout(location = 3) in vec4 i_rot;\n"
                                   "layout(location = 4) in vec4 i_color;\n"
                                   "uniform mat4 u_vp;\n"
                                   "out vec4 v_color;\n"
                                   "void main() {\n"
                                   "    vec3 s = a_position * i_scale;\n"
                                   "    vec3 t = 2.0 * cross(i_rot.xyz, s);\n"
                                   "    vec3 r = s + i_rot.w * t + cross(i_rot.xyz, t);\n"
                                   "    v_color = i_color;\n"
                                   "    gl_Position = u_vp * vec4(i_center + r, 1.0);\n"
                                   "}\n";

/* ---- Capsule instanced vertex shader (vec4 template: xyz=unit sphere, w=hemisphere sign) ---- */

static const char *s_cap_inst_vs_src = "layout(location = 0) in vec4 a_pos_tag;\n"
                                       "layout(location = 1) in vec3 i_center;\n"
                                       "layout(location = 2) in vec3 i_scale;\n"
                                       "layout(location = 3) in vec4 i_rot;\n"
                                       "layout(location = 4) in vec4 i_color;\n"
                                       "uniform mat4 u_vp;\n"
                                       "out vec4 v_color;\n"
                                       "void main() {\n"
                                       "    float radius = i_scale.x;\n"
                                       "    float body_half = i_scale.y;\n"
                                       "    vec3 p = a_pos_tag.xyz * radius;\n"
                                       "    p.y += a_pos_tag.w * body_half;\n"
                                       "    vec3 t = 2.0 * cross(i_rot.xyz, p);\n"
                                       "    vec3 r = p + i_rot.w * t + cross(i_rot.xyz, t);\n"
                                       "    v_color = i_color;\n"
                                       "    gl_Position = u_vp * vec4(i_center + r, 1.0);\n"
                                       "}\n";

/* ---- Module state ---- */

static struct {
    /* Shared fragment shader */
    nt_shader_t fs;

    /* CPU batch for non-instanced shapes (triangle, capsule, mesh) */
    nt_shader_t batch_vs;
    nt_pipeline_t batch_pip_depth;
    nt_pipeline_t batch_pip_overlay;
    nt_pipeline_t batch_pip_active;
    nt_buffer_t batch_vbo;
    nt_buffer_t batch_ibo;
    nt_shape_renderer_vertex_t vertices[NT_SHAPE_RENDERER_MAX_VERTICES];
    uint16_t indices[NT_SHAPE_RENDERER_MAX_INDICES];
    uint32_t vertex_count;
    uint32_t index_count;

    /* Instanced shapes (rect, cube, circle, sphere, cylinder) */
    nt_shader_t inst_vs;
    nt_pipeline_t inst_pip_depth;
    nt_pipeline_t inst_pip_overlay;
    nt_pipeline_t inst_pip_active;
    nt_buffer_t inst_buf; /* shared GPU instance buffer, reused per type */
    nt_shape_template_t templates[NT_SHAPE_TYPE_COUNT];
    nt_shape_instance_t inst_data[NT_SHAPE_TYPE_COUNT][NT_SHAPE_RENDERER_MAX_INSTANCES];
    uint32_t inst_counts[NT_SHAPE_TYPE_COUNT];

    /* Capsule instancing (separate pipeline: vec4 template + hemisphere-tagged shader) */
    nt_shader_t cap_inst_vs;
    nt_pipeline_t cap_inst_pip_depth;
    nt_pipeline_t cap_inst_pip_overlay;
    nt_pipeline_t cap_inst_pip_active;

    /* Instanced lines */
    nt_shader_t line_vs;
    nt_pipeline_t line_pip_depth;
    nt_pipeline_t line_pip_overlay;
    nt_pipeline_t line_pip_active;
    nt_buffer_t line_template_vbo;
    nt_buffer_t line_template_ibo;
    nt_buffer_t line_instance_buf;
    nt_shape_line_instance_t lines[NT_SHAPE_RENDERER_MAX_LINES];
    uint32_t line_count;

    /* Settings */
    float vp[16];
    float cam_pos[3];
    float line_width;
    bool depth_enabled;
    bool initialized;

    /* Sin/Cos lookup table (fixed NT_SHAPE_SEGMENTS) */
    float sin_lut[NT_SHAPE_SEGMENTS + 1];
    float cos_lut[NT_SHAPE_SEGMENTS + 1];
} s_shape;

/* ---- Helpers ---- */

static nt_pipeline_t get_active_batch_pipeline(void) { return s_shape.depth_enabled ? s_shape.batch_pip_depth : s_shape.batch_pip_overlay; }

static nt_pipeline_t get_active_inst_pipeline(void) { return s_shape.depth_enabled ? s_shape.inst_pip_depth : s_shape.inst_pip_overlay; }

static nt_pipeline_t get_active_cap_inst_pipeline(void) { return s_shape.depth_enabled ? s_shape.cap_inst_pip_depth : s_shape.cap_inst_pip_overlay; }

static nt_pipeline_t get_active_line_pipeline(void) { return s_shape.depth_enabled ? s_shape.line_pip_depth : s_shape.line_pip_overlay; }

static nt_pipeline_t make_batch_pipeline(bool depth, bool poly_offset) {
    nt_pipeline_desc_t desc = {
        .vertex_shader = s_shape.batch_vs,
        .fragment_shader = s_shape.fs,
        .layout =
            {
                .attr_count = 2,
                .stride = (uint16_t)sizeof(nt_shape_renderer_vertex_t),
                .attrs =
                    {
                        {.location = NT_ATTR_POSITION, .format = NT_FORMAT_FLOAT3, .offset = 0},
                        {.location = NT_ATTR_COLOR, .format = NT_FORMAT_UBYTE4N, .offset = 12},
                    },
            },
        .depth_test = depth,
        .depth_write = depth,
        .depth_func = NT_DEPTH_LEQUAL,
        .cull_face = false,
        .polygon_offset = poly_offset,
        .polygon_offset_factor = poly_offset ? 1.0F : 0.0F,
        .polygon_offset_units = poly_offset ? 1.0F : 0.0F,
        .label = "shape_pipeline",
    };
    return nt_gfx_make_pipeline(&desc);
}

static nt_pipeline_t make_line_pipeline(bool depth) {
    nt_pipeline_desc_t desc = {
        .vertex_shader = s_shape.line_vs,
        .fragment_shader = s_shape.fs,
        .layout =
            {
                .attr_count = 1,
                .stride = (uint16_t)(2 * sizeof(float)),
                .attrs =
                    {
                        {.location = 0, .format = NT_FORMAT_FLOAT2, .offset = 0},
                    },
            },
        .instance_layout =
            {
                .attr_count = 3,
                .stride = (uint16_t)sizeof(nt_shape_line_instance_t),
                .attrs =
                    {
                        {.location = 1, .format = NT_FORMAT_FLOAT3, .offset = 0},
                        {.location = 2, .format = NT_FORMAT_FLOAT3, .offset = 12},
                        {.location = 3, .format = NT_FORMAT_UBYTE4N, .offset = 24},
                    },
            },
        .depth_test = depth,
        .depth_write = depth,
        .depth_func = NT_DEPTH_LEQUAL,
        .cull_face = false,
        .label = "shape_line_pipeline",
    };
    return nt_gfx_make_pipeline(&desc);
}

static nt_pipeline_t make_inst_pipeline(bool depth) {
    nt_pipeline_desc_t desc = {
        .vertex_shader = s_shape.inst_vs,
        .fragment_shader = s_shape.fs,
        .layout =
            {
                .attr_count = 1,
                .stride = (uint16_t)(3 * sizeof(float)),
                .attrs =
                    {
                        {.location = 0, .format = NT_FORMAT_FLOAT3, .offset = 0},
                    },
            },
        .instance_layout =
            {
                .attr_count = 4,
                .stride = (uint16_t)sizeof(nt_shape_instance_t),
                .attrs =
                    {
                        {.location = 1, .format = NT_FORMAT_FLOAT3, .offset = 0},
                        {.location = 2, .format = NT_FORMAT_FLOAT3, .offset = 12},
                        {.location = 3, .format = NT_FORMAT_FLOAT4, .offset = 24},
                        {.location = 4, .format = NT_FORMAT_UBYTE4N, .offset = 40},
                    },
            },
        .depth_test = depth,
        .depth_write = depth,
        .depth_func = NT_DEPTH_LEQUAL,
        .cull_face = false,
        .polygon_offset = depth,
        .polygon_offset_factor = depth ? 1.0F : 0.0F,
        .polygon_offset_units = depth ? 1.0F : 0.0F,
        .label = "shape_inst_pipeline",
    };
    return nt_gfx_make_pipeline(&desc);
}

static nt_pipeline_t make_cap_inst_pipeline(bool depth) {
    nt_pipeline_desc_t desc = {
        .vertex_shader = s_shape.cap_inst_vs,
        .fragment_shader = s_shape.fs,
        .layout =
            {
                .attr_count = 1,
                .stride = (uint16_t)(4 * sizeof(float)), /* vec4 template */
                .attrs =
                    {
                        {.location = 0, .format = NT_FORMAT_FLOAT4, .offset = 0},
                    },
            },
        .instance_layout =
            {
                .attr_count = 4,
                .stride = (uint16_t)sizeof(nt_shape_instance_t),
                .attrs =
                    {
                        {.location = 1, .format = NT_FORMAT_FLOAT3, .offset = 0},
                        {.location = 2, .format = NT_FORMAT_FLOAT3, .offset = 12},
                        {.location = 3, .format = NT_FORMAT_FLOAT4, .offset = 24},
                        {.location = 4, .format = NT_FORMAT_UBYTE4N, .offset = 40},
                    },
            },
        .depth_test = depth,
        .depth_write = depth,
        .depth_func = NT_DEPTH_LEQUAL,
        .cull_face = false,
        .polygon_offset = depth,
        .polygon_offset_factor = depth ? 1.0F : 0.0F,
        .polygon_offset_units = depth ? 1.0F : 0.0F,
        .label = "shape_cap_inst_pipeline",
    };
    return nt_gfx_make_pipeline(&desc);
}

/* ---- Color packing: float [0,1] → uint8 [0,255] ---- */

static inline uint8_t float_to_u8(float v) {
    if (v <= 0.0F) {
        return 0;
    }
    if (v >= 1.0F) {
        return 255;
    }
    return (uint8_t)((v * 255.0F) + 0.5F);
}

static void pack_color(uint8_t out[4], const float f[4]) {
    out[0] = float_to_u8(f[0]);
    out[1] = float_to_u8(f[1]);
    out[2] = float_to_u8(f[2]);
    out[3] = float_to_u8(f[3]);
}

/* ---- Push instance helper ---- */

static void push_instance(int type, const float center[3], const float scale[3], const float *rot, const float color[4]) {
    if (s_shape.inst_counts[type] >= NT_SHAPE_RENDERER_MAX_INSTANCES) {
        nt_shape_renderer_flush();
    }
    nt_shape_instance_t *inst = &s_shape.inst_data[type][s_shape.inst_counts[type]];
    memcpy(inst->center, center, sizeof(float) * 3);
    memcpy(inst->scale, scale, sizeof(float) * 3);
    if (rot) {
        memcpy(inst->rot, rot, sizeof(float) * 4);
    } else {
        inst->rot[0] = 0.0F;
        inst->rot[1] = 0.0F;
        inst->rot[2] = 0.0F;
        inst->rot[3] = 1.0F;
    }
    pack_color(inst->color, color);
    s_shape.inst_counts[type]++;
}

static void set_vertex(nt_shape_renderer_vertex_t *v, const float pos[3], const float color[4]) {
    v->pos[0] = pos[0];
    v->pos[1] = pos[1];
    v->pos[2] = pos[2];
    pack_color(v->color, color);
}

static void build_trig_lut(void) {
    float inv = 2.0F * NT_PI / (float)NT_SHAPE_SEGMENTS;
    for (int i = 0; i <= NT_SHAPE_SEGMENTS; i++) {
        float theta = inv * (float)i;
        s_shape.sin_lut[i] = sinf(theta);
        s_shape.cos_lut[i] = cosf(theta);
    }
}

/* ---- Template mesh generation (unit scale, positions only) ---- */

static nt_shape_template_t make_template_ex(const float *verts, uint32_t nv, uint32_t components, const uint16_t *idx, uint32_t ni, const char *label) {
    nt_shape_template_t t;
    t.num_vertices = nv;
    t.num_indices = ni;
    t.vbo = nt_gfx_make_buffer(&(nt_buffer_desc_t){.type = NT_BUFFER_VERTEX, .usage = NT_USAGE_IMMUTABLE, .data = verts, .size = nv * components * (uint32_t)sizeof(float), .label = label});
    t.ibo = nt_gfx_make_buffer(&(nt_buffer_desc_t){.type = NT_BUFFER_INDEX, .usage = NT_USAGE_IMMUTABLE, .data = idx, .size = ni * (uint32_t)sizeof(uint16_t), .label = label});
    return t;
}

static nt_shape_template_t make_template(const float *verts, uint32_t nv, const uint16_t *idx, uint32_t ni, const char *label) { return make_template_ex(verts, nv, 3, idx, ni, label); }

static void build_templates(void) {
    /* Rect: unit quad in XY plane */
    {
        static const float v[] = {-0.5F, -0.5F, 0.0F, 0.5F, -0.5F, 0.0F, 0.5F, 0.5F, 0.0F, -0.5F, 0.5F, 0.0F};
        static const uint16_t i[] = {0, 1, 2, 0, 2, 3};
        s_shape.templates[NT_SHAPE_RECT] = make_template(v, 4, i, 6, "tpl_rect");
    }

    /* Cube: unit cube ±0.5 */
    {
        /* clang-format off */
        static const float v[] = {
            -0.5F, -0.5F, -0.5F, 0.5F, -0.5F, -0.5F, 0.5F, 0.5F, -0.5F, -0.5F, 0.5F, -0.5F,
            -0.5F, -0.5F, 0.5F,  0.5F, -0.5F, 0.5F,  0.5F, 0.5F, 0.5F,  -0.5F, 0.5F, 0.5F,
        };
        /* clang-format on */
        static const uint16_t i[] = {0, 1, 2, 0, 2, 3, 5, 4, 7, 5, 7, 6, 4, 0, 3, 4, 3, 7, 1, 5, 6, 1, 6, 2, 3, 2, 6, 3, 6, 7, 4, 5, 1, 4, 1, 0};
        s_shape.templates[NT_SHAPE_CUBE] = make_template(v, 8, i, 36, "tpl_cube");
    }

    /* Circle: unit circle in XZ plane (center + 32 ring) */
    {
        float v[NT_SEG_CIRCLE_NV * 3];
        uint16_t idx[NT_SEG_CIRCLE_NI];
        v[0] = 0.0F;
        v[1] = 0.0F;
        v[2] = 0.0F;
        for (int j = 0; j < NT_SHAPE_SEGMENTS; j++) {
            v[((1 + j) * 3) + 0] = s_shape.cos_lut[j];
            v[((1 + j) * 3) + 1] = 0.0F;
            v[((1 + j) * 3) + 2] = s_shape.sin_lut[j];
        }
        for (int j = 0; j < NT_SHAPE_SEGMENTS; j++) {
            int next = (j + 1) % NT_SHAPE_SEGMENTS;
            idx[(j * 3) + 0] = 0;
            idx[(j * 3) + 1] = (uint16_t)(1 + j);
            idx[(j * 3) + 2] = (uint16_t)(1 + next);
        }
        s_shape.templates[NT_SHAPE_CIRCLE] = make_template(v, NT_SEG_CIRCLE_NV, idx, NT_SEG_CIRCLE_NI, "tpl_circle");
    }

    /* Sphere: unit sphere */
    {
        int segs = NT_SHAPE_SEGMENTS;
        int rings = NT_SEG_SPHERE_RINGS;
        uint32_t nv = NT_SEG_SPHERE_NV;
        uint32_t ni = NT_SEG_SPHERE_NI;
        float v[NT_SEG_SPHERE_NV * 3];
        uint16_t idx[NT_SEG_SPHERE_NI];

        float sin_phi[NT_SEG_SPHERE_RINGS + 1];
        float cos_phi[NT_SEG_SPHERE_RINGS + 1];
        for (int r = 0; r <= rings; r++) {
            float phi = NT_PI * (float)r / (float)rings;
            sin_phi[r] = sinf(phi);
            cos_phi[r] = cosf(phi);
        }
        int vi = 0;
        for (int r = 0; r <= rings; r++) {
            for (int s2 = 0; s2 <= segs; s2++) {
                v[vi++] = sin_phi[r] * s_shape.cos_lut[s2];
                v[vi++] = cos_phi[r];
                v[vi++] = sin_phi[r] * s_shape.sin_lut[s2];
            }
        }
        int ii = 0;
        for (int r = 0; r < rings; r++) {
            for (int s2 = 0; s2 < segs; s2++) {
                uint16_t a = (uint16_t)((r * (segs + 1)) + s2);
                uint16_t b = (uint16_t)(a + 1);
                uint16_t c = (uint16_t)(a + segs + 1);
                uint16_t d = (uint16_t)(c + 1);
                idx[ii++] = a;
                idx[ii++] = c;
                idx[ii++] = b;
                idx[ii++] = b;
                idx[ii++] = c;
                idx[ii++] = d;
            }
        }
        s_shape.templates[NT_SHAPE_SPHERE] = make_template(v, nv, idx, ni, "tpl_sphere");
    }

    /* Cylinder: unit cylinder R=1 H=1, center at origin */
    {
        int segs = NT_SHAPE_SEGMENTS;
        uint32_t nv = NT_SEG_CYL_NV;
        uint32_t ni = NT_SEG_CYL_NI;
        float v[NT_SEG_CYL_NV * 3];
        uint16_t idx[NT_SEG_CYL_NI];

        int vi = 0;
        /* 0: top center */
        v[vi++] = 0.0F;
        v[vi++] = 0.5F;
        v[vi++] = 0.0F;
        /* 1: bottom center */
        v[vi++] = 0.0F;
        v[vi++] = -0.5F;
        v[vi++] = 0.0F;
        /* 2..segs+2: top ring */
        for (int j = 0; j <= segs; j++) {
            v[vi++] = s_shape.cos_lut[j];
            v[vi++] = 0.5F;
            v[vi++] = s_shape.sin_lut[j];
        }
        /* segs+3..2*segs+3: bottom ring */
        for (int j = 0; j <= segs; j++) {
            v[vi++] = s_shape.cos_lut[j];
            v[vi++] = -0.5F;
            v[vi++] = s_shape.sin_lut[j];
        }

        int ii = 0;
        uint16_t top_c = 0;
        uint16_t bot_c = 1;
        uint16_t top_ring = 2;
        uint16_t bot_ring = (uint16_t)(2 + segs + 1);
        /* Top cap */
        for (int j = 0; j < segs; j++) {
            idx[ii++] = top_c;
            idx[ii++] = (uint16_t)(top_ring + j);
            idx[ii++] = (uint16_t)(top_ring + j + 1);
        }
        /* Bottom cap */
        for (int j = 0; j < segs; j++) {
            idx[ii++] = bot_c;
            idx[ii++] = (uint16_t)(bot_ring + j + 1);
            idx[ii++] = (uint16_t)(bot_ring + j);
        }
        /* Tube */
        for (int j = 0; j < segs; j++) {
            uint16_t t0 = (uint16_t)(top_ring + j);
            uint16_t t1 = (uint16_t)(top_ring + j + 1);
            uint16_t b0 = (uint16_t)(bot_ring + j);
            uint16_t b1 = (uint16_t)(bot_ring + j + 1);
            idx[ii++] = t0;
            idx[ii++] = b0;
            idx[ii++] = t1;
            idx[ii++] = t1;
            idx[ii++] = b0;
            idx[ii++] = b1;
        }
        s_shape.templates[NT_SHAPE_CYLINDER] = make_template(v, nv, idx, ni, "tpl_cylinder");
    }

    /* Capsule: unit sphere with hemisphere-tagged vec4 vertices (w=+1 top, w=-1 bottom).
       Template is radius=1, body_half=0. Shader shifts hemispheres via p.y += w * body_half. */
    {
        int segs = NT_SHAPE_SEGMENTS;
        int half_rings = NT_SEG_CAP_HALF;
        int total_sections = NT_SEG_CAP_SECTIONS;
        uint32_t nv = NT_SEG_CAP_NV;
        uint32_t ni = NT_SEG_CAP_NI;
        float v[NT_SEG_CAP_NV * 4]; /* vec4 per vertex */
        uint16_t idx[NT_SEG_CAP_NI];

        int vi = 0;
        for (int row = 0; row <= total_sections; row++) {
            float tag;
            float phi;
            float sp;
            float cp;

            if (row <= half_rings) {
                tag = 1.0F;
                phi = NT_PI * 0.5F * (float)(half_rings - row) / (float)half_rings;
                sp = sinf(phi);
                cp = cosf(phi);
            } else {
                tag = -1.0F;
                int bot_row = row - half_rings - 1;
                phi = NT_PI * 0.5F * (float)bot_row / (float)half_rings;
                sp = -sinf(phi);
                cp = cosf(phi);
            }

            for (int seg = 0; seg <= segs; seg++) {
                v[vi++] = cp * s_shape.cos_lut[seg]; /* x */
                v[vi++] = sp;                        /* y */
                v[vi++] = cp * s_shape.sin_lut[seg]; /* z */
                v[vi++] = tag;                       /* w: hemisphere sign */
            }
        }

        int ii = 0;
        for (int row = 0; row < total_sections; row++) {
            for (int seg = 0; seg < segs; seg++) {
                uint16_t a = (uint16_t)((row * (segs + 1)) + seg);
                uint16_t b = (uint16_t)(a + 1);
                uint16_t c = (uint16_t)(a + segs + 1);
                uint16_t d = (uint16_t)(c + 1);
                idx[ii++] = a;
                idx[ii++] = c;
                idx[ii++] = b;
                idx[ii++] = b;
                idx[ii++] = c;
                idx[ii++] = d;
            }
        }
        s_shape.templates[NT_SHAPE_CAPSULE] = make_template_ex(v, nv, 4, idx, ni, "tpl_capsule");
    }
}

/* Emit a line instance (billboard quad computed on GPU). */
static void emit_wire_edge(const float a[3], const float b[3], const float color[4]) {
    if (s_shape.line_count >= NT_SHAPE_RENDERER_MAX_LINES) {
        nt_shape_renderer_flush();
    }
    nt_shape_line_instance_t *inst = &s_shape.lines[s_shape.line_count];
    memcpy(inst->a, a, sizeof(float) * 3);
    memcpy(inst->b, b, sizeof(float) * 3);
    pack_color(inst->color, color);
    s_shape.line_count++;
}

/* ---- Lifecycle ---- */

void nt_shape_renderer_init(void) {
    memset(&s_shape, 0, sizeof(s_shape));

    /* Shaders */
    s_shape.fs = nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_FRAGMENT, .source = s_shape_fs_src, .label = "shape_fs"});
    s_shape.batch_vs = nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_VERTEX, .source = s_shape_vs_src, .label = "shape_batch_vs"});
    s_shape.inst_vs = nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_VERTEX, .source = s_inst_vs_src, .label = "shape_inst_vs"});
    s_shape.cap_inst_vs = nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_VERTEX, .source = s_cap_inst_vs_src, .label = "shape_cap_inst_vs"});
    s_shape.line_vs = nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_VERTEX, .source = s_line_vs_src, .label = "shape_line_vs"});

    /* Pipelines */
    s_shape.batch_pip_depth = make_batch_pipeline(true, true);
    s_shape.batch_pip_overlay = make_batch_pipeline(false, false);
    s_shape.inst_pip_depth = make_inst_pipeline(true);
    s_shape.inst_pip_overlay = make_inst_pipeline(false);
    s_shape.cap_inst_pip_depth = make_cap_inst_pipeline(true);
    s_shape.cap_inst_pip_overlay = make_cap_inst_pipeline(false);
    s_shape.line_pip_depth = make_line_pipeline(true);
    s_shape.line_pip_overlay = make_line_pipeline(false);

    /* CPU batch buffers (triangle, mesh) */
    s_shape.batch_vbo = nt_gfx_make_buffer(
        &(nt_buffer_desc_t){.type = NT_BUFFER_VERTEX, .usage = NT_USAGE_STREAM, .size = NT_SHAPE_RENDERER_MAX_VERTICES * (uint32_t)sizeof(nt_shape_renderer_vertex_t), .label = "shape_batch_vbo"});
    s_shape.batch_ibo =
        nt_gfx_make_buffer(&(nt_buffer_desc_t){.type = NT_BUFFER_INDEX, .usage = NT_USAGE_STREAM, .size = NT_SHAPE_RENDERER_MAX_INDICES * (uint32_t)sizeof(uint16_t), .label = "shape_batch_ibo"});

    /* Instanced shape buffer (shared across types, reused per draw) */
    s_shape.inst_buf = nt_gfx_make_buffer(
        &(nt_buffer_desc_t){.type = NT_BUFFER_VERTEX, .usage = NT_USAGE_STREAM, .size = NT_SHAPE_RENDERER_MAX_INSTANCES * (uint32_t)sizeof(nt_shape_instance_t), .label = "shape_inst_buf"});

    /* Build template meshes (requires trig LUT) */
    build_trig_lut();
    build_templates();

    /* Instanced line buffers */
    static const float line_template_verts[] = {-1.0F, 0.0F, 1.0F, 0.0F, 1.0F, 1.0F, -1.0F, 1.0F};
    s_shape.line_template_vbo =
        nt_gfx_make_buffer(&(nt_buffer_desc_t){.type = NT_BUFFER_VERTEX, .usage = NT_USAGE_IMMUTABLE, .data = line_template_verts, .size = sizeof(line_template_verts), .label = "shape_line_quad"});
    static const uint16_t line_template_indices[] = {0, 1, 2, 0, 2, 3};
    s_shape.line_template_ibo =
        nt_gfx_make_buffer(&(nt_buffer_desc_t){.type = NT_BUFFER_INDEX, .usage = NT_USAGE_IMMUTABLE, .data = line_template_indices, .size = sizeof(line_template_indices), .label = "shape_line_idx"});
    s_shape.line_instance_buf = nt_gfx_make_buffer(
        &(nt_buffer_desc_t){.type = NT_BUFFER_VERTEX, .usage = NT_USAGE_STREAM, .size = NT_SHAPE_RENDERER_MAX_LINES * (uint32_t)sizeof(nt_shape_line_instance_t), .label = "shape_line_inst"});

    /* Verify all resources were created successfully */
    if (!s_shape.fs.id || !s_shape.batch_vs.id || !s_shape.inst_vs.id || !s_shape.cap_inst_vs.id || !s_shape.line_vs.id || !s_shape.batch_pip_depth.id || !s_shape.batch_pip_overlay.id ||
        !s_shape.inst_pip_depth.id || !s_shape.inst_pip_overlay.id || !s_shape.cap_inst_pip_depth.id || !s_shape.cap_inst_pip_overlay.id || !s_shape.line_pip_depth.id ||
        !s_shape.line_pip_overlay.id || !s_shape.batch_vbo.id || !s_shape.batch_ibo.id || !s_shape.inst_buf.id || !s_shape.line_template_vbo.id || !s_shape.line_template_ibo.id ||
        !s_shape.line_instance_buf.id) {
        nt_log_error("shape_renderer: init failed — resource creation error");
        nt_shape_renderer_shutdown();
        return;
    }

    s_shape.line_width = 0.02F;
    s_shape.depth_enabled = true;
    s_shape.batch_pip_active = s_shape.batch_pip_depth;
    s_shape.inst_pip_active = s_shape.inst_pip_depth;
    s_shape.cap_inst_pip_active = s_shape.cap_inst_pip_depth;
    s_shape.line_pip_active = s_shape.line_pip_depth;
    s_shape.initialized = true;
}

void nt_shape_renderer_shutdown(void) {
    if (!s_shape.initialized) {
        return;
    }
    nt_gfx_destroy_buffer(s_shape.line_instance_buf);
    nt_gfx_destroy_buffer(s_shape.line_template_ibo);
    nt_gfx_destroy_buffer(s_shape.line_template_vbo);
    nt_gfx_destroy_buffer(s_shape.inst_buf);
    for (int t = NT_SHAPE_TYPE_COUNT - 1; t >= 0; t--) {
        nt_gfx_destroy_buffer(s_shape.templates[t].ibo);
        nt_gfx_destroy_buffer(s_shape.templates[t].vbo);
    }
    nt_gfx_destroy_buffer(s_shape.batch_ibo);
    nt_gfx_destroy_buffer(s_shape.batch_vbo);
    nt_gfx_destroy_pipeline(s_shape.line_pip_overlay);
    nt_gfx_destroy_pipeline(s_shape.line_pip_depth);
    nt_gfx_destroy_pipeline(s_shape.cap_inst_pip_overlay);
    nt_gfx_destroy_pipeline(s_shape.cap_inst_pip_depth);
    nt_gfx_destroy_pipeline(s_shape.inst_pip_overlay);
    nt_gfx_destroy_pipeline(s_shape.inst_pip_depth);
    nt_gfx_destroy_pipeline(s_shape.batch_pip_overlay);
    nt_gfx_destroy_pipeline(s_shape.batch_pip_depth);
    nt_gfx_destroy_shader(s_shape.line_vs);
    nt_gfx_destroy_shader(s_shape.cap_inst_vs);
    nt_gfx_destroy_shader(s_shape.inst_vs);
    nt_gfx_destroy_shader(s_shape.batch_vs);
    nt_gfx_destroy_shader(s_shape.fs);
    memset(&s_shape, 0, sizeof(s_shape));
}

void nt_shape_renderer_flush(void) {
    /* Flush instanced shapes (rect, cube, circle, sphere, cylinder, capsule) */
    for (int t = 0; t < NT_SHAPE_TYPE_COUNT; t++) {
        uint32_t cnt = s_shape.inst_counts[t];
        if (cnt == 0) {
            continue;
        }
        nt_gfx_update_buffer(s_shape.inst_buf, s_shape.inst_data[t], cnt * (uint32_t)sizeof(nt_shape_instance_t));
        nt_gfx_bind_pipeline(t == NT_SHAPE_CAPSULE ? s_shape.cap_inst_pip_active : s_shape.inst_pip_active);
        nt_gfx_bind_vertex_buffer(s_shape.templates[t].vbo);
        nt_gfx_bind_index_buffer(s_shape.templates[t].ibo);
        nt_gfx_bind_instance_buffer(s_shape.inst_buf);
        nt_gfx_set_uniform_mat4("u_vp", s_shape.vp);

        nt_gfx_draw_indexed_instanced(0, s_shape.templates[t].num_indices, s_shape.templates[t].num_vertices, cnt);
        s_shape.inst_counts[t] = 0;
    }

    /* Flush CPU-batched shapes (triangle, mesh) */
    if (s_shape.index_count > 0) {
        nt_gfx_update_buffer(s_shape.batch_vbo, s_shape.vertices, s_shape.vertex_count * (uint32_t)sizeof(nt_shape_renderer_vertex_t));
        nt_gfx_update_buffer(s_shape.batch_ibo, s_shape.indices, s_shape.index_count * (uint32_t)sizeof(uint16_t));

        nt_gfx_bind_pipeline(s_shape.batch_pip_active);
        nt_gfx_bind_vertex_buffer(s_shape.batch_vbo);
        nt_gfx_bind_index_buffer(s_shape.batch_ibo);
        nt_gfx_set_uniform_mat4("u_vp", s_shape.vp);

        nt_gfx_draw_indexed(0, s_shape.index_count, s_shape.vertex_count);

        s_shape.vertex_count = 0;
        s_shape.index_count = 0;
    }

    /* Flush instanced lines */
    if (s_shape.line_count > 0) {
        nt_gfx_update_buffer(s_shape.line_instance_buf, s_shape.lines, s_shape.line_count * (uint32_t)sizeof(nt_shape_line_instance_t));

        nt_gfx_bind_pipeline(s_shape.line_pip_active);
        nt_gfx_bind_vertex_buffer(s_shape.line_template_vbo);
        nt_gfx_bind_index_buffer(s_shape.line_template_ibo);
        nt_gfx_bind_instance_buffer(s_shape.line_instance_buf);
        nt_gfx_set_uniform_mat4("u_vp", s_shape.vp);
        float cp4[4] = {s_shape.cam_pos[0], s_shape.cam_pos[1], s_shape.cam_pos[2], 0.0F};
        nt_gfx_set_uniform_vec4("u_cam_pos", cp4);
        nt_gfx_set_uniform_float("u_line_width", s_shape.line_width);

        nt_gfx_draw_indexed_instanced(0, 6, 4, s_shape.line_count);

        s_shape.line_count = 0;
    }
}

/* ---- State setters ---- */

void nt_shape_renderer_set_vp(const float vp[16]) {
    if (memcmp(s_shape.vp, vp, sizeof(float) * 16) == 0) { // NOLINT — intentional bitwise dirty-check
        return;
    }
    nt_shape_renderer_flush();
    memcpy(s_shape.vp, vp, sizeof(float) * 16);
}

void nt_shape_renderer_set_cam_pos(const float pos[3]) {
    if (s_shape.cam_pos[0] == pos[0] && s_shape.cam_pos[1] == pos[1] && s_shape.cam_pos[2] == pos[2]) {
        return;
    }
    nt_shape_renderer_flush();
    s_shape.cam_pos[0] = pos[0];
    s_shape.cam_pos[1] = pos[1];
    s_shape.cam_pos[2] = pos[2];
}

void nt_shape_renderer_set_line_width(float width) {
    if (width == s_shape.line_width) {
        return;
    }
    if (s_shape.line_count > 0) {
        nt_shape_renderer_flush();
    }
    s_shape.line_width = width;
}

void nt_shape_renderer_set_depth(bool enabled) {
    if (enabled == s_shape.depth_enabled) {
        return;
    }
    nt_shape_renderer_flush();
    s_shape.depth_enabled = enabled;
    s_shape.batch_pip_active = get_active_batch_pipeline();
    s_shape.inst_pip_active = get_active_inst_pipeline();
    s_shape.cap_inst_pip_active = get_active_cap_inst_pipeline();
    s_shape.line_pip_active = get_active_line_pipeline();
}

/* ---- Line ---- */

void nt_shape_renderer_line(const float a[3], const float b[3], const float color[4]) { emit_wire_edge(a, b, color); }

/* ---- Rectangle ---- */

void nt_shape_renderer_rect(const float pos[3], const float size[2], const float color[4]) {
    float scale[3] = {size[0], size[1], 1.0F};
    push_instance(NT_SHAPE_RECT, pos, scale, NULL, color);
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
    float scale[3] = {size[0], size[1], 1.0F};
    push_instance(NT_SHAPE_RECT, pos, scale, rot, color);
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

/* ---- Circle ---- */

void nt_shape_renderer_circle(const float center[3], float radius, const float color[4]) {
    float scale[3] = {radius, 1.0F, radius};
    push_instance(NT_SHAPE_CIRCLE, center, scale, NULL, color);
}

void nt_shape_renderer_circle_wire(const float center[3], float radius, const float color[4]) {
    int segs = NT_SHAPE_SEGMENTS;

    for (int i = 0; i < segs; i++) {
        int next = (i + 1) % segs;
        float a[3] = {center[0] + (radius * s_shape.cos_lut[i]), center[1], center[2] + (radius * s_shape.sin_lut[i])};
        float b[3] = {center[0] + (radius * s_shape.cos_lut[next]), center[1], center[2] + (radius * s_shape.sin_lut[next])};
        emit_wire_edge(a, b, color);
    }
}

void nt_shape_renderer_circle_rot(const float center[3], float radius, const float rot[4], const float color[4]) {
    float scale[3] = {radius, 1.0F, radius};
    push_instance(NT_SHAPE_CIRCLE, center, scale, rot, color);
}

void nt_shape_renderer_circle_wire_rot(const float center[3], float radius, const float rot[4], const float color[4]) {
    int segs = NT_SHAPE_SEGMENTS;
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

void nt_shape_renderer_cube(const float center[3], const float size[3], const float color[4]) { push_instance(NT_SHAPE_CUBE, center, size, NULL, color); }

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

void nt_shape_renderer_cube_rot(const float center[3], const float size[3], const float rot[4], const float color[4]) { push_instance(NT_SHAPE_CUBE, center, size, rot, color); }

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
    float scale[3] = {radius, radius, radius};
    push_instance(NT_SHAPE_SPHERE, center, scale, NULL, color);
}

void nt_shape_renderer_sphere_wire(const float center[3], float radius, const float color[4]) {
    int segs = NT_SHAPE_SEGMENTS;

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

void nt_shape_renderer_sphere_rot(const float center[3], float radius, const float rot[4], const float color[4]) {
    float scale[3] = {radius, radius, radius};
    push_instance(NT_SHAPE_SPHERE, center, scale, rot, color);
}

void nt_shape_renderer_sphere_wire_rot(const float center[3], float radius, const float rot[4], const float color[4]) {
    int segs = NT_SHAPE_SEGMENTS;
    float rm[3][3];
    quat_to_mat3(rot, rm);

    /* 3 great circles, same as sphere_wire but with rotation applied */
    for (int i = 0; i < segs; i++) {
        int next = (i + 1) % segs;
        float c_i = s_shape.cos_lut[i];
        float s_i = s_shape.sin_lut[i];
        float c_n = s_shape.cos_lut[next];
        float s_n = s_shape.sin_lut[next];

        /* XZ plane (equator) */
        float xz_a[3] = {radius * c_i, 0.0F, radius * s_i};
        float xz_b[3] = {radius * c_n, 0.0F, radius * s_n};
        /* XY plane */
        float xy_a[3] = {radius * c_i, radius * s_i, 0.0F};
        float xy_b[3] = {radius * c_n, radius * s_n, 0.0F};
        /* YZ plane */
        float yz_a[3] = {0.0F, radius * c_i, radius * s_i};
        float yz_b[3] = {0.0F, radius * c_n, radius * s_n};

        float ra[3];
        float rb[3];

        mat3_mulv(rm, xz_a, ra);
        mat3_mulv(rm, xz_b, rb);
        float a1[3] = {center[0] + ra[0], center[1] + ra[1], center[2] + ra[2]};
        float b1[3] = {center[0] + rb[0], center[1] + rb[1], center[2] + rb[2]};
        emit_wire_edge(a1, b1, color);

        mat3_mulv(rm, xy_a, ra);
        mat3_mulv(rm, xy_b, rb);
        float a2[3] = {center[0] + ra[0], center[1] + ra[1], center[2] + ra[2]};
        float b2[3] = {center[0] + rb[0], center[1] + rb[1], center[2] + rb[2]};
        emit_wire_edge(a2, b2, color);

        mat3_mulv(rm, yz_a, ra);
        mat3_mulv(rm, yz_b, rb);
        float a3[3] = {center[0] + ra[0], center[1] + ra[1], center[2] + ra[2]};
        float b3[3] = {center[0] + rb[0], center[1] + rb[1], center[2] + rb[2]};
        emit_wire_edge(a3, b3, color);
    }
}

/* ---- Cylinder ---- */

static void emit_cylinder_wire(const float center[3], float radius, float height, const float *rot, const float color[4]) {
    int segs = NT_SHAPE_SEGMENTS;
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
    int quarter = NT_SHAPE_SEGMENTS / 4;
    for (int i = 0; i < 4; i++) {
        int si = i * quarter;
        float ct = s_shape.cos_lut[si];
        float st = s_shape.sin_lut[si];
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

void nt_shape_renderer_cylinder(const float center[3], float radius, float height, const float color[4]) {
    float scale[3] = {radius, height, radius};
    push_instance(NT_SHAPE_CYLINDER, center, scale, NULL, color);
}

void nt_shape_renderer_cylinder_wire(const float center[3], float radius, float height, const float color[4]) { emit_cylinder_wire(center, radius, height, NULL, color); }

void nt_shape_renderer_cylinder_rot(const float center[3], float radius, float height, const float rot[4], const float color[4]) {
    float scale[3] = {radius, height, radius};
    push_instance(NT_SHAPE_CYLINDER, center, scale, rot, color);
}

void nt_shape_renderer_cylinder_wire_rot(const float center[3], float radius, float height, const float rot[4], const float color[4]) { emit_cylinder_wire(center, radius, height, rot, color); }

/* ---- Capsule ---- */

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
    int segs = NT_SHAPE_SEGMENTS;
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
    int quarter = segs / 4;
    for (int m = 0; m < 4; m++) {
        int si = m * quarter;
        float ct = s_shape.cos_lut[si];
        float st = s_shape.sin_lut[si];

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

void nt_shape_renderer_capsule(const float center[3], float radius, float height, const float color[4]) {
    float body_half = (height - 2.0F * radius) * 0.5F;
    if (body_half < 0.0F) {
        body_half = 0.0F;
    }
    float scale[3] = {radius, body_half, 0.0F};
    push_instance(NT_SHAPE_CAPSULE, center, scale, NULL, color);
}

void nt_shape_renderer_capsule_wire(const float center[3], float radius, float height, const float color[4]) { emit_capsule_wire(center, radius, height, NULL, color); }

void nt_shape_renderer_capsule_rot(const float center[3], float radius, float height, const float rot[4], const float color[4]) {
    float body_half = (height - 2.0F * radius) * 0.5F;
    if (body_half < 0.0F) {
        body_half = 0.0F;
    }
    float scale[3] = {radius, body_half, 0.0F};
    push_instance(NT_SHAPE_CAPSULE, center, scale, rot, color);
}

void nt_shape_renderer_capsule_wire_rot(const float center[3], float radius, float height, const float rot[4], const float color[4]) { emit_capsule_wire(center, radius, height, rot, color); }

/* ---- Mesh ---- */

void nt_shape_renderer_mesh(const float *positions, uint32_t num_vertices, const uint16_t *indices, uint32_t num_indices, const float color[4]) {
    if (s_shape.vertex_count + num_vertices > NT_SHAPE_RENDERER_MAX_VERTICES || s_shape.index_count + num_indices > NT_SHAPE_RENDERER_MAX_INDICES) {
        nt_shape_renderer_flush();
    }
    if (num_vertices > NT_SHAPE_RENDERER_MAX_VERTICES || num_indices > NT_SHAPE_RENDERER_MAX_INDICES) {
        NT_ASSERT(0 && "mesh exceeds batch limits");
        nt_log_error("mesh too large for batch, dropped");
        return;
    }

    /* Validate indices are within bounds */
    for (uint32_t i = 0; i < num_indices; i++) {
        if (indices[i] >= num_vertices) {
            NT_ASSERT(0 && "mesh index out of bounds");
            nt_log_error("mesh index out of bounds, dropped");
            return;
        }
    }

    uint16_t base = (uint16_t)s_shape.vertex_count;

    /* Copy positions into vertex buffer with color */
    for (uint32_t i = 0; i < num_vertices; i++) {
        const float *pos = &positions[(size_t)i * 3];
        set_vertex(&s_shape.vertices[s_shape.vertex_count], pos, color);
        s_shape.vertex_count++;
    }

    /* Copy indices with base offset */
    for (uint32_t i = 0; i < num_indices; i++) {
        s_shape.indices[s_shape.index_count++] = (uint16_t)(base + indices[i]);
    }
}

void nt_shape_renderer_mesh_wire(const float *positions, uint32_t num_vertices, const uint16_t *indices, uint32_t num_indices, const float color[4]) {
    /* For each triangle (3 consecutive indices), emit 3 wireframe edges */
    for (uint32_t i = 0; (i + 2) < num_indices; i += 3) {
        if (indices[i] >= num_vertices || indices[i + 1] >= num_vertices || indices[i + 2] >= num_vertices) {
            NT_ASSERT(0 && "mesh_wire index out of bounds");
            nt_log_error("mesh_wire index out of bounds, skipped triangle");
            continue;
        }
        const float *a = &positions[(ptrdiff_t)indices[i] * 3];
        const float *b = &positions[(ptrdiff_t)indices[i + 1] * 3];
        const float *c = &positions[(ptrdiff_t)indices[i + 2] * 3];
        emit_wire_edge(a, b, color);
        emit_wire_edge(b, c, color);
        emit_wire_edge(c, a, color);
    }
}

/* ---- Test accessors (always compiled; header guards visibility) ---- */

uint32_t nt_shape_renderer_test_instance_count(int type) { return s_shape.inst_counts[type]; }
uint32_t nt_shape_renderer_test_vertex_count(void) { return s_shape.vertex_count; }
uint32_t nt_shape_renderer_test_index_count(void) { return s_shape.index_count; }
uint32_t nt_shape_renderer_test_line_count(void) { return s_shape.line_count; }
const float *nt_shape_renderer_test_cam_pos(void) { return s_shape.cam_pos; }
float nt_shape_renderer_test_line_width(void) { return s_shape.line_width; }
bool nt_shape_renderer_test_initialized(void) { return s_shape.initialized; }
