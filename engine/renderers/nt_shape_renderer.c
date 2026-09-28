#include "renderers/nt_shape_renderer.h"

#include "core/nt_assert.h"
#include "graphics/nt_gfx.h"
#include "log/nt_log.h"
#include "math/nt_math.h"

#include <string.h>

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

static const char *s_shape_vs_src = "precision mediump float;\n"
                                    "layout(location = 0) in vec3 a_position;\n"
                                    "layout(location = 2) in vec4 a_color;\n"
                                    "uniform mat4 u_vp;\n"
                                    "out vec4 v_color;\n"
                                    "void main() {\n"
                                    "    v_color = a_color;\n"
                                    "    gl_Position = u_vp * vec4(a_position, 1.0);\n"
                                    "}\n";

static const char *s_shape_fs_src = "precision mediump float;\n"
                                    "in vec4 v_color;\n"
                                    "out vec4 frag_color;\n"
                                    "void main() {\n"
                                    "    frag_color = v_color;\n"
                                    "}\n";

/* ---- Instanced line vertex shader ---- */

#define NT_STROKE_GLSL                                                                                                                                                                                 \
    "uniform mat4 u_vp;\n"                                                                                                                                                                             \
    "uniform vec4 u_eye;\n"                                                                                                                                                                            \
    "uniform float u_line_width;\n"                                                                                                                                                                    \
    "uniform vec4 u_pixel_scale;\n"                                                                                                                                                                    \
    "vec3 side(vec3 edge, vec3 view) {\n"                                                                                                                                                              \
    "    vec3 n = cross(edge, view);\n"                                                                                                                                                                \
    "    if (dot(n,n) < 1e-12) n = cross(edge, vec3(0,1,0));\n"                                                                                                                                        \
    "    if (dot(n,n) < 1e-12) n = cross(edge, vec3(1,0,0));\n"                                                                                                                                        \
    "    return n * inversesqrt(max(dot(n,n), 1e-12));\n"                                                                                                                                              \
    "}\n"                                                                                                                                                                                              \
    "vec3 join_offset(vec3 n0, vec3 n1, float role) {\n"                                                                                                                                               \
    "    if (dot(n0,n0) < 0.5) n0 = n1;\n"                                                                                                                                                             \
    "    if (dot(n1,n1) < 0.5) n1 = n0;\n"                                                                                                                                                             \
    "    vec3 m = n0+n1;\n"                                                                                                                                                                            \
    "    m *= inversesqrt(max(dot(m,m),1e-12));\n"                                                                                                                                                     \
    "    float d = dot(m,n1);\n"                                                                                                                                                                       \
    "    vec3 offset = role < 2.0 ? n0 : n1;\n"                                                                                                                                                        \
    "    if (d >= 0.25) offset = m / d;\n"                                                                                                                                                             \
    "    offset *= mod(role,2.0) < 0.5 ? -1.0 : 1.0;\n"                                                                                                                                                \
    "    if (role > 3.5) offset = vec3(0);\n"                                                                                                                                                          \
    "    return offset;\n"                                                                                                                                                                             \
    "}\n"                                                                                                                                                                                              \
    "vec3 to_eye(vec3 p) {\n"                                                                                                                                                                          \
    "    return u_eye.xyz-p*u_eye.w;\n"                                                                                                                                                                \
    "}\n"                                                                                                                                                                                              \
    "vec3 biased(vec3 p) {\n"                                                                                                                                                                          \
    "    vec3 view = to_eye(p);\n"                                                                                                                                                                     \
    "    float dist = length(view);\n"                                                                                                                                                                 \
    "    return dist > 1e-6 ? p+view*(0.0005/dist) : p;\n"                                                                                                                                             \
    "}\n"                                                                                                                                                                                              \
    "vec4 near_clip(vec4 outside, vec4 inside) {\n"                                                                                                                                                    \
    "    float a = outside.z+outside.w;\n"                                                                                                                                                             \
    "    float b = inside.z+inside.w;\n"                                                                                                                                                               \
    "    return mix(outside,inside,clamp(-a/(b-a),0.0,1.0));\n"                                                                                                                                        \
    "}\n"                                                                                                                                                                                              \
    "vec3 pixel_side(vec2 edge) {\n"                                                                                                                                                                   \
    "    return vec3(-edge.y,edge.x,0)*inversesqrt(max(dot(edge,edge),1e-12));\n"                                                                                                                      \
    "}\n"                                                                                                                                                                                              \
    "vec4 pixel_position(vec2 corner, vec3 before, vec3 p, vec3 after) {\n"                                                                                                                            \
    "    vec4 cb=u_vp*vec4(biased(before),1), cp=u_vp*vec4(biased(p),1), ca=u_vp*vec4(biased(after),1);\n"                                                                                             \
    "    bool start=corner.x<0.5;\n"                                                                                                                                                                   \
    "    vec4 other=start?ca:cb, outer=start?cb:ca;\n"                                                                                                                                                 \
    "    bool hide_p=cp.z+cp.w<0.0, hide_other=other.z+other.w<0.0;\n"                                                                                                                                 \
    "    if (hide_p && hide_other) return vec4(0,0,2,1);\n"                                                                                                                                            \
    "    if (hide_p) {cp=near_clip(cp,other);outer=cp;}\n"                                                                                                                                             \
    "    else if (outer.z+outer.w<0.0) outer=near_clip(outer,cp);\n"                                                                                                                                   \
    "    if (hide_other) other=near_clip(other,cp);\n"                                                                                                                                                 \
    "    cb=start?outer:other; ca=start?other:outer;\n"                                                                                                                                                \
    "    vec2 pos=cp.xy/max(cp.w,1e-6);\n"                                                                                                                                                             \
    "    vec2 incoming=(pos-cb.xy/max(cb.w,1e-6))/u_pixel_scale.xy;\n"                                                                                                                                 \
    "    vec2 outgoing=(ca.xy/max(ca.w,1e-6)-pos)/u_pixel_scale.xy;\n"                                                                                                                                 \
    "    vec3 offset=join_offset(pixel_side(incoming),pixel_side(outgoing),corner.y);\n"                                                                                                               \
    "    cp.xy+=offset.xy*(0.5*u_line_width)*u_pixel_scale.xy*cp.w;\n"                                                                                                                                 \
    "    return cp;\n"                                                                                                                                                                                 \
    "}\n"                                                                                                                                                                                              \
    "vec4 stroke_position(vec2 corner, vec3 before, vec3 p, vec3 after) {\n"                                                                                                                           \
    "    if (u_pixel_scale.x>0.0) return pixel_position(corner,before,p,after);\n"                                                                                                                     \
    "    vec3 view = to_eye(p);\n"                                                                                                                                                                     \
    "    vec3 offset = join_offset(side(p-before,view),side(after-p,view),corner.y);\n"                                                                                                                \
    "    return u_vp*vec4(biased(p+offset*(0.5*u_line_width)),1);\n"                                                                                                                                   \
    "}\n"

static const char *s_line_vs_src =
    "precision highp float;\n"
    "layout(location = 0) in vec2 a_corner;\n"
    "layout(location = 1) in vec3 i_prev;\n"
    "layout(location = 2) in vec3 i_a;\n"
    "layout(location = 3) in vec3 i_b;\n"
    "layout(location = 4) in vec3 i_next;\n"
    "layout(location = 5) in vec4 i_color;\n"
    "out mediump vec4 v_color;\n" NT_STROKE_GLSL "void main(){bool s=a_corner.x<0.5;gl_Position=stroke_position(a_corner,s?i_prev:i_a,s?i_a:i_b,s?i_b:i_next);v_color=i_color;}\n";

/* Template vertices already store their corner's (before, p, after) triple. */
static const char *s_wire_vs_src = "precision highp float;\n"
                                   "layout(location=0) in vec2 a_corner;\n"
                                   "layout(location=1) in vec4 a_before;\n"
                                   "layout(location=2) in vec4 a_p;\n"
                                   "layout(location=3) in vec4 a_after;\n"
                                   "layout(location=5) in vec3 i_center;\n"
                                   "layout(location=6) in vec3 i_scale;\n"
                                   "layout(location=7) in vec4 i_rot;\n"
                                   "layout(location=8) in vec4 i_color;\n"
                                   "out mediump vec4 v_color;\n"
                                   "vec3 wire_point(vec4 p) {\n"
                                   "    vec3 v=p.xyz*i_scale.x;\n"
                                   "    v.y+=p.w*i_scale.y;\n"
                                   "    vec3 t=2.0*cross(i_rot.xyz,v);\n"
                                   "    return i_center+v+i_rot.w*t+cross(i_rot.xyz,t);\n"
                                   "}\n" NT_STROKE_GLSL "void main(){gl_Position=stroke_position(a_corner,wire_point(a_before),wire_point(a_p),wire_point(a_after));v_color=i_color;}\n";

/* Stroke corners: vertices 0-4 sit at the segment start (4 = joint center), 5-6 at its end.
 * The first 6 indices are the body quad; the last 6 are the start bevel. */
static const uint16_t s_stroke_indices[12] = {2, 3, 6, 2, 6, 5, 4, 0, 2, 4, 1, 3};

/* ---- Instance data ---- */

typedef struct {
    float prev[3];
    float a[3];
    float b[3];
    float next[3];
    uint8_t color[4];
} nt_shape_stroke_instance_t;

_Static_assert(sizeof(nt_shape_stroke_instance_t) == 52, "stroke instance size");

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

enum { NT_WIRE_CIRCLE, NT_WIRE_SPHERE, NT_WIRE_CYLINDER, NT_WIRE_CAPSULE, NT_WIRE_COUNT };
#define NT_WIRE_MAX_INSTANCES ((NT_SHAPE_RENDERER_MAX_INSTANCES + NT_WIRE_COUNT - 1) / NT_WIRE_COUNT)
/* Capsule meridian: a full ring plus the equator angle repeated on each straight side. */
#define NT_WIRE_CAP_POINTS (NT_SHAPE_SEGMENTS + 2)
#define NT_WIRE_MAX_SEGMENTS ((2 * NT_SHAPE_SEGMENTS) + (2 * NT_WIRE_CAP_POINTS))

/* points = (before, p, after) of this corner; w tags the capsule/cylinder half. */
typedef struct {
    float points[3][4];
    float corner[2];
} nt_wire_vertex_t;

/* Per-type template mesh */
typedef struct {
    nt_buffer_t vbo;
    nt_buffer_t ibo;
    uint32_t num_vertices;
    uint32_t num_indices;
} nt_shape_template_t;

/* ---- Instanced shape vertex shader ---- */

static const char *s_inst_vs_src = "precision mediump float;\n"
                                   "layout(location = 0) in vec3 a_position;\n"
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

static const char *s_cap_inst_vs_src = "precision mediump float;\n"
                                       "layout(location = 0) in vec4 a_pos_tag;\n"
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

    /* CPU batch for non-instanced shapes (triangle, mesh) */
    nt_shader_t batch_vs;
    nt_program_t batch_prog;
    nt_pipeline_t batch_pip_depth;
    nt_pipeline_t batch_pip_overlay;
    nt_buffer_t batch_vbo;
    nt_buffer_t batch_ibo;
    nt_vertex_input_t batch_vi;
    nt_shape_renderer_vertex_t vertices[NT_SHAPE_RENDERER_MAX_VERTICES];
    nt_shape_index_t indices[NT_SHAPE_RENDERER_MAX_INDICES];
    uint32_t vertex_count;
    uint32_t index_count;

    /* Instanced shapes (rect, cube, circle, sphere, cylinder) */
    nt_shader_t inst_vs;
    nt_program_t inst_prog;
    nt_pipeline_t inst_pip_depth;
    nt_pipeline_t inst_pip_overlay;
    nt_buffer_t inst_buf;      /* shared GPU instance buffer, reused per type */
    uint32_t inst_ring_cursor; /* next free byte; disjoint writes avoid driver copies of in-flight data */
    nt_shape_template_t templates[NT_SHAPE_TYPE_COUNT];
    /* One vertex input per template (depth/overlay pipeline pairs share it);
     * instance pointers are re-specified into it by each flush. */
    nt_vertex_input_t template_vi[NT_SHAPE_TYPE_COUNT];
    nt_shape_instance_t inst_data[NT_SHAPE_TYPE_COUNT][NT_SHAPE_RENDERER_MAX_INSTANCES];
    uint32_t inst_counts[NT_SHAPE_TYPE_COUNT];

    /* Capsule instancing (separate pipeline: vec4 template + hemisphere-tagged shader) */
    nt_shader_t cap_inst_vs;
    nt_program_t cap_inst_prog;
    nt_pipeline_t cap_inst_pip_depth;
    nt_pipeline_t cap_inst_pip_overlay;

    /* Instanced lines */
    nt_shader_t line_vs;
    nt_program_t line_prog;
    nt_pipeline_t line_pip_depth;
    nt_pipeline_t line_pip_overlay;
    nt_buffer_t line_template_vbo;
    nt_buffer_t line_template_ibo;
    nt_buffer_t line_instance_buf;
    nt_vertex_input_t line_vi;
    uint32_t line_ring_cursor;
    uint32_t line_count;
    nt_buffer_t stroke_instance_buf;
    nt_vertex_input_t stroke_vi;
    uint32_t stroke_ring_cursor;
    uint32_t stroke_count;

    nt_shader_t wire_vs;
    nt_program_t wire_prog;
    nt_pipeline_t wire_pip_depth;
    nt_pipeline_t wire_pip_overlay;
    nt_shape_template_t wire_templates[NT_WIRE_COUNT];
    nt_vertex_input_t wire_vi[NT_WIRE_COUNT];
    uint32_t wire_counts[NT_WIRE_COUNT];
    nt_shape_instance_t wire_data[NT_WIRE_COUNT][NT_WIRE_MAX_INSTANCES];
    nt_shape_stroke_instance_t strokes[NT_SHAPE_RENDERER_MAX_POLYLINE_SEGMENTS];
    /* Wire templates are built during init, while the line queue is empty: the scratch
     * stays off the small WASM stack and costs nothing while MAX_LINES * 28 B covers it. */
    union {
        nt_shape_line_instance_t lines[NT_SHAPE_RENDERER_MAX_LINES];
        nt_wire_vertex_t wire_build[NT_WIRE_MAX_SEGMENTS * 7];
    } line_staging;

    /* Settings */
    float vp[16];
    float eye[4]; /* derived from vp; w = 0 for an orthographic view direction */
    float line_width;
    float pixel_scale[4];
    bool depth_enabled;
    bool initialized;

    /* Sin/Cos lookup table (fixed NT_SHAPE_SEGMENTS) */
    float sin_lut[NT_SHAPE_SEGMENTS + 1];
    float cos_lut[NT_SHAPE_SEGMENTS + 1];
    /* A restore whose re-init failed -- a second context loss landing mid-recovery
     * -- must still be retried by the next one, or the module stays dark for the
     * session. restore_gpu sets this after init(), so the memset does not eat it. */
    bool restore_pending;
} s_shape;

/* ---- Helpers ---- */

/* Matching depth/overlay layouts share one vertex input per program. */
static nt_vertex_layout_t batch_vertex_layout(void) {
    return (nt_vertex_layout_t){
        .attr_count = 2,
        .stride = (uint16_t)sizeof(nt_shape_renderer_vertex_t),
        .attrs =
            {
                {.location = NT_ATTR_POSITION, .type = NT_VERTEX_FLOAT, .count = 3, .offset = 0},
                {.location = NT_ATTR_COLOR, .type = NT_VERTEX_UINT8, .count = 4, .normalized = true, .offset = 12},
            },
    };
}

static nt_vertex_layout_t inst_template_layout(void) {
    return (nt_vertex_layout_t){
        .attr_count = 1,
        .stride = (uint16_t)(3 * sizeof(float)),
        .attrs = {{.location = 0, .type = NT_VERTEX_FLOAT, .count = 3, .offset = 0}},
    };
}

static nt_vertex_layout_t cap_template_layout(void) {
    return (nt_vertex_layout_t){
        .attr_count = 1,
        .stride = (uint16_t)(4 * sizeof(float)), /* vec4 template */
        .attrs = {{.location = 0, .type = NT_VERTEX_FLOAT, .count = 4, .offset = 0}},
    };
}

static nt_vertex_layout_t shape_instance_layout(void) {
    return (nt_vertex_layout_t){
        .attr_count = 4,
        .stride = (uint16_t)sizeof(nt_shape_instance_t),
        .attrs =
            {
                {.location = 1, .type = NT_VERTEX_FLOAT, .count = 3, .offset = 0},
                {.location = 2, .type = NT_VERTEX_FLOAT, .count = 3, .offset = 12},
                {.location = 3, .type = NT_VERTEX_FLOAT, .count = 4, .offset = 24},
                {.location = 4, .type = NT_VERTEX_UINT8, .count = 4, .normalized = true, .offset = 40},
            },
    };
}

static nt_vertex_layout_t line_template_layout(void) {
    return (nt_vertex_layout_t){
        .attr_count = 1,
        .stride = (uint16_t)(2 * sizeof(float)),
        .attrs = {{.location = 0, .type = NT_VERTEX_FLOAT, .count = 2, .offset = 0}},
    };
}

static nt_vertex_layout_t stroke_instance_layout(void) {
    return (nt_vertex_layout_t){
        .attr_count = 5,
        .stride = sizeof(nt_shape_stroke_instance_t),
        .attrs =
            {
                {.location = 1, .type = NT_VERTEX_FLOAT, .count = 3, .offset = 0},
                {.location = 2, .type = NT_VERTEX_FLOAT, .count = 3, .offset = 12},
                {.location = 3, .type = NT_VERTEX_FLOAT, .count = 3, .offset = 24},
                {.location = 4, .type = NT_VERTEX_FLOAT, .count = 3, .offset = 36},
                {.location = 5, .type = NT_VERTEX_UINT8, .count = 4, .normalized = true, .offset = 48},
            },
    };
}

static nt_vertex_layout_t line_instance_layout(void) {
    nt_vertex_layout_t layout = stroke_instance_layout();
    layout.stride = sizeof(nt_shape_line_instance_t);
    /* Alias endpoint attributes: an independent edge needs no stored neighbors. */
    layout.attrs[1].offset = 0;
    layout.attrs[2].offset = 12;
    layout.attrs[3].offset = 12;
    layout.attrs[4].offset = 24;
    return layout;
}

static nt_pipeline_t make_batch_pipeline(bool depth, bool poly_offset) {
    nt_pipeline_desc_t desc = {
        .program = s_shape.batch_prog,
        .depth_test = depth,
        .depth_write = depth,
        .depth_func = NT_DEPTH_LEQUAL,
        .cull_mode = 0,
        .polygon_offset = poly_offset,
        .polygon_offset_factor = poly_offset ? 1.0F : 0.0F,
        .polygon_offset_units = poly_offset ? 1.0F : 0.0F,
        .label = "shape_pipeline",
    };
    return nt_gfx_make_pipeline(&desc);
}

static nt_pipeline_t make_stroke_pipeline(nt_program_t program, bool depth, const char *label) {
    nt_pipeline_desc_t desc = {
        .program = program,
        .depth_test = depth,
        .depth_write = depth,
        .depth_func = NT_DEPTH_LEQUAL,
        .cull_mode = 0,
        .label = label,
    };
    return nt_gfx_make_pipeline(&desc);
}

static nt_pipeline_t make_inst_pipeline(bool depth) {
    nt_pipeline_desc_t desc = {
        .program = s_shape.inst_prog,
        .depth_test = depth,
        .depth_write = depth,
        .depth_func = NT_DEPTH_LEQUAL,
        .cull_mode = 0,
        .polygon_offset = depth,
        .polygon_offset_factor = depth ? 1.0F : 0.0F,
        .polygon_offset_units = depth ? 1.0F : 0.0F,
        .label = "shape_inst_pipeline",
    };
    return nt_gfx_make_pipeline(&desc);
}

static nt_pipeline_t make_cap_inst_pipeline(bool depth) {
    nt_pipeline_desc_t desc = {
        .program = s_shape.cap_inst_prog,
        .depth_test = depth,
        .depth_write = depth,
        .depth_func = NT_DEPTH_LEQUAL,
        .cull_mode = 0,
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
    t.ibo = nt_gfx_make_buffer(
        &(nt_buffer_desc_t){.type = NT_BUFFER_INDEX, .usage = NT_USAGE_IMMUTABLE, .data = idx, .size = ni * (uint32_t)sizeof(uint16_t), .index_type = NT_INDEX_UINT16, .label = label});
    return t;
}

static nt_shape_template_t make_template(const float *verts, uint32_t nv, const uint16_t *idx, uint32_t ni, const char *label) { return make_template_ex(verts, nv, 3, idx, ni, label); }

static void wire_segment_vertices(nt_wire_vertex_t *vertices, const float prev[4], const float a[4], const float b[4], const float next[4]) {
    for (uint32_t v = 0; v < 7; v++) {
        nt_wire_vertex_t *dst = &vertices[v];
        bool start = v < 5;
        memcpy(dst->points[0], start ? prev : a, sizeof(dst->points[0]));
        memcpy(dst->points[1], start ? a : b, sizeof(dst->points[1]));
        memcpy(dst->points[2], start ? b : next, sizeof(dst->points[2]));
        dst->corner[0] = start ? 0.0F : 1.0F;
        dst->corner[1] = (float)(v < 5 ? v : v - 5);
    }
}

static void wire_template_path(nt_wire_vertex_t *vertices, uint16_t *indices, uint32_t *segments, const float (*points)[4], uint32_t count, bool closed) {
    uint32_t edges = closed ? count : count - 1;
    for (uint32_t i = 0; i < edges; i++) {
        NT_ASSERT(*segments < NT_WIRE_MAX_SEGMENTS);
        uint32_t base = *segments * 7;
        uint32_t prev = i > 0 ? i - 1 : 0;
        if (closed && i == 0) {
            prev = count - 1;
        }
        uint32_t end = (i + 1) % count;
        uint32_t next = (i + 2) % count;
        if (!closed && end + 1 >= count) {
            next = end;
        }
        wire_segment_vertices(vertices + base, points[prev], points[i], points[end], points[next]);
        for (uint32_t j = 0; j < 12; j++) {
            indices[(*segments * 12) + j] = (uint16_t)(base + s_stroke_indices[j]);
        }
        (*segments)++;
    }
}

static void wire_ring_points(float points[NT_WIRE_CAP_POINTS][4], int plane, float tag) {
    for (int i = 0; i < NT_SHAPE_SEGMENTS; i++) {
        memset(points[i], 0, sizeof(points[i]));
        points[i][plane == 2 ? 1 : 0] = s_shape.cos_lut[i];
        points[i][plane == 1 ? 1 : 2] = s_shape.sin_lut[i];
        points[i][3] = tag;
    }
}

/* The equator angle repeats once per hemisphere, joined by the straight side. */
static void wire_capsule_profile(float points[NT_WIRE_CAP_POINTS][4], int plane) {
    const int half = NT_WIRE_CAP_POINTS / 2;
    for (int i = 0; i < NT_WIRE_CAP_POINTS; i++) {
        int row = i <= half ? i : NT_WIRE_CAP_POINTS - i;
        bool top = row <= NT_SEG_CAP_HALF;
        int k = top ? row : row - 1;
        memset(points[i], 0, sizeof(points[i]));
        points[i][plane == 0 ? 0 : 2] = s_shape.sin_lut[k] * (i <= half ? 1.0F : -1.0F);
        points[i][1] = s_shape.cos_lut[k];
        points[i][3] = top ? 1.0F : -1.0F;
    }
}

static void build_wire_template(int type) {
    nt_wire_vertex_t *vertices = s_shape.line_staging.wire_build;
    uint16_t indices[NT_WIRE_MAX_SEGMENTS * 12];
    float points[NT_WIRE_CAP_POINTS][4];
    uint32_t segments = 0;
    static const int ring_counts[NT_WIRE_COUNT] = {1, 3, 2, 2};
    for (int ring = 0; ring < ring_counts[type]; ring++) {
        float tag = 0.0F;
        if (type >= NT_WIRE_CYLINDER) {
            tag = ring == 0 ? 1.0F : -1.0F;
        }
        wire_ring_points(points, type == NT_WIRE_SPHERE ? ring : 0, tag);
        wire_template_path(vertices, indices, &segments, (const float(*)[4])points, NT_SHAPE_SEGMENTS, true);
    }
    if (type == NT_WIRE_CYLINDER) {
        for (int i = 0; i < 4; i++) {
            int k = i * (NT_SHAPE_SEGMENTS / 4);
            float ends[2][4] = {{s_shape.cos_lut[k], 0, s_shape.sin_lut[k], 1}, {s_shape.cos_lut[k], 0, s_shape.sin_lut[k], -1}};
            wire_template_path(vertices, indices, &segments, (const float(*)[4])ends, 2, false);
        }
    }
    if (type == NT_WIRE_CAPSULE) {
        for (int plane = 0; plane < 2; plane++) {
            wire_capsule_profile(points, plane);
            wire_template_path(vertices, indices, &segments, (const float(*)[4])points, NT_WIRE_CAP_POINTS, true);
        }
    }
    s_shape.wire_templates[type] = make_template_ex((const float *)vertices, segments * 7, sizeof(nt_wire_vertex_t) / sizeof(float), indices, segments * 12, "shape_wire_template");
}

static nt_vertex_layout_t wire_vertex_layout(void) {
    return (nt_vertex_layout_t){.stride = sizeof(nt_wire_vertex_t),
                                .attr_count = 4,
                                .attrs = {
                                    {.location = 0, .type = NT_VERTEX_FLOAT, .count = 2, .offset = 48},
                                    {.location = 1, .type = NT_VERTEX_FLOAT, .count = 4, .offset = 0},
                                    {.location = 2, .type = NT_VERTEX_FLOAT, .count = 4, .offset = 16},
                                    {.location = 3, .type = NT_VERTEX_FLOAT, .count = 4, .offset = 32},
                                }};
}

static void push_wire_instance(int type, const float center[3], float radius, float half_height, const float *rot, const float color[4]) {
    if (s_shape.wire_counts[type] == NT_WIRE_MAX_INSTANCES) {
        nt_shape_renderer_flush();
    }
    nt_shape_instance_t *inst = &s_shape.wire_data[type][s_shape.wire_counts[type]++];
    memcpy(inst->center, center, sizeof(inst->center));
    inst->scale[0] = radius;
    inst->scale[1] = half_height;
    inst->scale[2] = 0.0F;
    if (rot) {
        memcpy(inst->rot, rot, sizeof(inst->rot));
    } else {
        memset(inst->rot, 0, sizeof(inst->rot));
        inst->rot[3] = 1.0F;
    }
    pack_color(inst->color, color);
}

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

/* Neighbors let adjacent segments construct the same endpoint cross-section. */
static void emit_stroke(const float prev[3], const float a[3], const float b[3], const float next[3], const uint8_t color[4]) {
    if (s_shape.stroke_count >= NT_SHAPE_RENDERER_MAX_POLYLINE_SEGMENTS) {
        nt_shape_renderer_flush();
    }
    nt_shape_stroke_instance_t *inst = &s_shape.strokes[s_shape.stroke_count++];
    memcpy(inst->prev, prev, sizeof(inst->prev));
    memcpy(inst->next, next, sizeof(inst->next));
    memcpy(inst->a, a, sizeof(inst->a));
    memcpy(inst->b, b, sizeof(inst->b));
    memcpy(inst->color, color, sizeof(inst->color));
}

static void emit_line(const float a[3], const float b[3], const uint8_t color[4]) {
    if (s_shape.line_count >= NT_SHAPE_RENDERER_MAX_LINES) {
        nt_shape_renderer_flush();
    }
    nt_shape_line_instance_t *inst = &s_shape.line_staging.lines[s_shape.line_count++];
    memcpy(inst->a, a, sizeof(inst->a));
    memcpy(inst->b, b, sizeof(inst->b));
    memcpy(inst->color, color, sizeof(inst->color));
}

static bool build_wire_vertex_inputs(void) {
    nt_vertex_layout_t wire_instance_layout = shape_instance_layout();
    for (uint32_t i = 0; i < wire_instance_layout.attr_count; i++) {
        wire_instance_layout.attrs[i].location += 4;
    }
    for (int type = 0; type < NT_WIRE_COUNT; type++) {
        build_wire_template(type);
        nt_shape_template_t *tpl = &s_shape.wire_templates[type];
        if (!tpl->vbo.id || !tpl->ibo.id) {
            return false;
        }
        s_shape.wire_vi[type] = nt_gfx_make_vertex_input(
            &(nt_vertex_input_desc_t){.layout = wire_vertex_layout(), .instance_layout = wire_instance_layout, .vertex_buffer = tpl->vbo, .index_buffer = tpl->ibo, .label = "shape_wire_vi"});
        if (!s_shape.wire_vi[type].id) {
            return false;
        }
    }
    return true;
}

/* ---- Lifecycle ---- */

/* Fixed uniform names: hashed once, the draw path sets them every frame. */
static nt_hash32_t s_u_vp;
static nt_hash32_t s_u_eye;
static nt_hash32_t s_u_line_width;
static nt_hash32_t s_u_pixel_scale;

void nt_shape_renderer_init(void) {
    memset(&s_shape, 0, sizeof(s_shape));
    s_u_vp = nt_hash32_str("u_vp");
    s_u_eye = nt_hash32_str("u_eye");
    s_u_line_width = nt_hash32_str("u_line_width");
    s_u_pixel_scale = nt_hash32_str("u_pixel_scale");
    /* Set before anything is created: the failure paths below route cleanup
     * through shutdown(), which no-ops while this is false and would strand
     * every shader and program allocated so far. shutdown() re-clears it. */
    s_shape.initialized = true;

    /* Shaders */
    s_shape.fs = nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_FRAGMENT, .source = s_shape_fs_src, .label = "shape_fs"});
    s_shape.batch_vs = nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_VERTEX, .source = s_shape_vs_src, .label = "shape_batch_vs"});
    s_shape.inst_vs = nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_VERTEX, .source = s_inst_vs_src, .label = "shape_inst_vs"});
    s_shape.cap_inst_vs = nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_VERTEX, .source = s_cap_inst_vs_src, .label = "shape_cap_inst_vs"});
    s_shape.line_vs = nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_VERTEX, .source = s_line_vs_src, .label = "shape_line_vs"});
    s_shape.wire_vs = nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_VERTEX, .source = s_wire_vs_src, .label = "shape_wire_vs"});

    if (!s_shape.fs.id || !s_shape.batch_vs.id || !s_shape.inst_vs.id || !s_shape.cap_inst_vs.id || !s_shape.line_vs.id || !s_shape.wire_vs.id) {
        NT_LOG_ERROR("init failed -- shader creation error");
        nt_shape_renderer_shutdown();
        return;
    }

    /* Programs -- one per vertex shader, shared by the depth and overlay pipelines */
    s_shape.batch_prog = nt_gfx_make_program(s_shape.batch_vs, s_shape.fs);
    s_shape.inst_prog = nt_gfx_make_program(s_shape.inst_vs, s_shape.fs);
    s_shape.cap_inst_prog = nt_gfx_make_program(s_shape.cap_inst_vs, s_shape.fs);
    s_shape.line_prog = nt_gfx_make_program(s_shape.line_vs, s_shape.fs);
    s_shape.wire_prog = nt_gfx_make_program(s_shape.wire_vs, s_shape.fs);
    if (!nt_gfx_program_ready(s_shape.batch_prog) || !nt_gfx_program_ready(s_shape.inst_prog) || !nt_gfx_program_ready(s_shape.cap_inst_prog) || !nt_gfx_program_ready(s_shape.line_prog) ||
        !nt_gfx_program_ready(s_shape.wire_prog)) {
        NT_LOG_ERROR("init failed -- program link error");
        nt_shape_renderer_shutdown();
        return;
    }

    /* Pipelines */
    s_shape.batch_pip_depth = make_batch_pipeline(true, true);
    s_shape.batch_pip_overlay = make_batch_pipeline(false, false);
    s_shape.inst_pip_depth = make_inst_pipeline(true);
    s_shape.inst_pip_overlay = make_inst_pipeline(false);
    s_shape.cap_inst_pip_depth = make_cap_inst_pipeline(true);
    s_shape.cap_inst_pip_overlay = make_cap_inst_pipeline(false);
    s_shape.line_pip_depth = make_stroke_pipeline(s_shape.line_prog, true, "shape_line_pipeline");
    s_shape.line_pip_overlay = make_stroke_pipeline(s_shape.line_prog, false, "shape_line_pipeline");
    s_shape.wire_pip_depth = make_stroke_pipeline(s_shape.wire_prog, true, "shape_wire_pipeline");
    s_shape.wire_pip_overlay = make_stroke_pipeline(s_shape.wire_prog, false, "shape_wire_pipeline");

    /* CPU batch buffers (triangle, mesh) */
    s_shape.batch_vbo = nt_gfx_make_buffer(
        &(nt_buffer_desc_t){.type = NT_BUFFER_VERTEX, .usage = NT_USAGE_STREAM, .size = NT_SHAPE_RENDERER_MAX_VERTICES * (uint32_t)sizeof(nt_shape_renderer_vertex_t), .label = "shape_batch_vbo"});
    s_shape.batch_ibo = nt_gfx_make_buffer(&(nt_buffer_desc_t){
        .type = NT_BUFFER_INDEX, .usage = NT_USAGE_STREAM, .size = NT_SHAPE_RENDERER_MAX_INDICES * (uint32_t)sizeof(nt_shape_index_t), .index_type = NT_SHAPE_INDEX_TYPE, .label = "shape_batch_ibo"});

    /* Instanced shape buffer (shared across types, reused per draw) */
    s_shape.inst_buf = nt_gfx_make_buffer(
        &(nt_buffer_desc_t){.type = NT_BUFFER_VERTEX, .usage = NT_USAGE_STREAM, .size = NT_SHAPE_RENDERER_MAX_INSTANCES * (uint32_t)sizeof(nt_shape_instance_t), .label = "shape_inst_buf"});

    /* Build template meshes (requires trig LUT) */
    build_trig_lut();
    build_templates();

    /* Instanced line buffers */
    static const float line_template_verts[] = {0, 0, 0, 1, 0, 2, 0, 3, 0, 4, 1, 0, 1, 1};
    s_shape.line_template_vbo =
        nt_gfx_make_buffer(&(nt_buffer_desc_t){.type = NT_BUFFER_VERTEX, .usage = NT_USAGE_IMMUTABLE, .data = line_template_verts, .size = sizeof(line_template_verts), .label = "shape_line_quad"});
    s_shape.line_template_ibo = nt_gfx_make_buffer(&(nt_buffer_desc_t){
        .type = NT_BUFFER_INDEX, .usage = NT_USAGE_IMMUTABLE, .data = s_stroke_indices, .size = sizeof(s_stroke_indices), .index_type = NT_INDEX_UINT16, .label = "shape_line_idx"});
    s_shape.line_instance_buf = nt_gfx_make_buffer(
        &(nt_buffer_desc_t){.type = NT_BUFFER_VERTEX, .usage = NT_USAGE_STREAM, .size = NT_SHAPE_RENDERER_MAX_LINES * (uint32_t)sizeof(nt_shape_line_instance_t), .label = "shape_line_inst"});
    s_shape.stroke_instance_buf = nt_gfx_make_buffer(&(nt_buffer_desc_t){
        .type = NT_BUFFER_VERTEX, .usage = NT_USAGE_STREAM, .size = NT_SHAPE_RENDERER_MAX_POLYLINE_SEGMENTS * (uint32_t)sizeof(nt_shape_stroke_instance_t), .label = "shape_stroke_inst"});

    /* Verify all buffers/pipelines were created successfully (the vertex
     * inputs below trap on invalid buffer handles instead of skipping). */
    bool template_bufs_ok = true;
    for (int t = 0; t < NT_SHAPE_TYPE_COUNT; t++) {
        template_bufs_ok = template_bufs_ok && s_shape.templates[t].vbo.id != 0 && s_shape.templates[t].ibo.id != 0;
    }
    if (!template_bufs_ok || !s_shape.batch_pip_depth.id || !s_shape.batch_pip_overlay.id || !s_shape.inst_pip_depth.id || !s_shape.inst_pip_overlay.id || !s_shape.cap_inst_pip_depth.id ||
        !s_shape.cap_inst_pip_overlay.id || !s_shape.line_pip_depth.id || !s_shape.line_pip_overlay.id || !s_shape.wire_pip_depth.id || !s_shape.wire_pip_overlay.id || !s_shape.batch_vbo.id ||
        !s_shape.batch_ibo.id || !s_shape.inst_buf.id || !s_shape.line_template_vbo.id || !s_shape.line_template_ibo.id || !s_shape.line_instance_buf.id || !s_shape.stroke_instance_buf.id) {
        NT_LOG_ERROR("init failed -- resource creation error");
        nt_shape_renderer_shutdown();
        return;
    }

    /* Vertex inputs: one per template + batch + line; each depth/overlay
     * pipeline pair shares one. */
    s_shape.batch_vi =
        nt_gfx_make_vertex_input(&(nt_vertex_input_desc_t){.layout = batch_vertex_layout(), .vertex_buffer = s_shape.batch_vbo, .index_buffer = s_shape.batch_ibo, .label = "shape_batch_vi"});
    for (int t = 0; t < NT_SHAPE_TYPE_COUNT; t++) {
        s_shape.template_vi[t] = nt_gfx_make_vertex_input(&(nt_vertex_input_desc_t){
            .layout = (t == NT_SHAPE_CAPSULE) ? cap_template_layout() : inst_template_layout(),
            .instance_layout = shape_instance_layout(),
            .vertex_buffer = s_shape.templates[t].vbo,
            .index_buffer = s_shape.templates[t].ibo,
            .label = "shape_template_vi",
        });
    }
    s_shape.line_vi = nt_gfx_make_vertex_input(&(nt_vertex_input_desc_t){
        .layout = line_template_layout(),
        .instance_layout = line_instance_layout(),
        .vertex_buffer = s_shape.line_template_vbo,
        .index_buffer = s_shape.line_template_ibo,
        .label = "shape_line_vi",
    });
    s_shape.stroke_vi = nt_gfx_make_vertex_input(&(nt_vertex_input_desc_t){
        .layout = line_template_layout(),
        .instance_layout = stroke_instance_layout(),
        .vertex_buffer = s_shape.line_template_vbo,
        .index_buffer = s_shape.line_template_ibo,
        .label = "shape_stroke_vi",
    });
    bool template_vis_ok = true;
    for (int t = 0; t < NT_SHAPE_TYPE_COUNT; t++) {
        template_vis_ok = template_vis_ok && s_shape.template_vi[t].id != 0;
    }
    if (!s_shape.batch_vi.id || !template_vis_ok || !s_shape.line_vi.id || !s_shape.stroke_vi.id) {
        NT_LOG_ERROR("init failed -- vertex input creation error");
        nt_shape_renderer_shutdown();
        return;
    }

    if (!build_wire_vertex_inputs()) {
        NT_LOG_ERROR("init failed -- wire template creation error");
        nt_shape_renderer_shutdown();
        return;
    }

    s_shape.line_width = 0.02F;
    s_shape.depth_enabled = true;
}

void nt_shape_renderer_shutdown(void) {
    /* Before the early return: an explicit shutdown ends the module even if the
     * last restore left it pending, so a later restore must not resurrect it.
     * restore_gpu re-arms this after its own init. */
    s_shape.restore_pending = false;
    if (!s_shape.initialized) {
        return;
    }
    for (int type = 0; type < NT_WIRE_COUNT; type++) {
        nt_gfx_destroy_vertex_input(s_shape.wire_vi[type]);
        nt_gfx_destroy_buffer(s_shape.wire_templates[type].vbo);
        nt_gfx_destroy_buffer(s_shape.wire_templates[type].ibo);
    }
    nt_gfx_destroy_program(s_shape.wire_prog);
    nt_gfx_destroy_shader(s_shape.wire_vs);
    nt_gfx_destroy_vertex_input(s_shape.stroke_vi);
    nt_gfx_destroy_buffer(s_shape.stroke_instance_buf);
    nt_gfx_destroy_vertex_input(s_shape.line_vi);
    for (int t = NT_SHAPE_TYPE_COUNT - 1; t >= 0; t--) {
        nt_gfx_destroy_vertex_input(s_shape.template_vi[t]);
    }
    nt_gfx_destroy_vertex_input(s_shape.batch_vi);
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
    nt_gfx_destroy_program(s_shape.line_prog);
    nt_gfx_destroy_program(s_shape.cap_inst_prog);
    nt_gfx_destroy_program(s_shape.inst_prog);
    nt_gfx_destroy_program(s_shape.batch_prog);
    nt_gfx_destroy_shader(s_shape.line_vs);
    nt_gfx_destroy_shader(s_shape.cap_inst_vs);
    nt_gfx_destroy_shader(s_shape.inst_vs);
    nt_gfx_destroy_shader(s_shape.batch_vs);
    nt_gfx_destroy_shader(s_shape.fs);
    memset(&s_shape, 0, sizeof(s_shape));
}

void nt_shape_renderer_restore_gpu(void) {
    /* The contract is "every ACTIVE renderer": without this, restoring an
     * unused shape renderer would silently init it and consume
     * resource pools the game sized for itself. */
    if (!s_shape.initialized && !s_shape.restore_pending) {
        return;
    }
    /* Save CPU-side state that survives context loss */
    float saved_vp[16];
    float saved_eye[4];
    memcpy(saved_eye, s_shape.eye, sizeof(saved_eye));
    float saved_line_width = s_shape.line_width;
    float saved_pixel_scale[4];
    memcpy(saved_pixel_scale, s_shape.pixel_scale, sizeof(saved_pixel_scale));
    bool saved_depth = s_shape.depth_enabled;
    memcpy(saved_vp, s_shape.vp, sizeof(saved_vp));

    /* Shutdown destroys GPU handles (no-ops for zero backends after context loss)
       and clears all state including CPU-side arrays. */
    nt_shape_renderer_shutdown();

    /* Full re-init: recreates shaders, pipelines, buffers, template meshes, trig LUT */
    nt_shape_renderer_init();

    /* Restore saved settings */
    memcpy(s_shape.vp, saved_vp, sizeof(s_shape.vp));
    memcpy(s_shape.eye, saved_eye, sizeof(s_shape.eye));
    s_shape.line_width = saved_line_width;
    memcpy(s_shape.pixel_scale, saved_pixel_scale, sizeof(saved_pixel_scale));
    s_shape.depth_enabled = saved_depth;
    if (!s_shape.initialized) {
        s_shape.restore_pending = true; /* after init()'s memset, so it survives */
        return;
    }
    s_shape.restore_pending = false;
}

/* Disjoint ring ranges avoid driver copies of in-flight data; after a wrap the driver copies. */
static uint32_t ring_upload(nt_buffer_t buffer, uint32_t *cursor, uint32_t capacity, const void *data, uint32_t bytes) {
    if (*cursor + bytes > capacity) {
        *cursor = 0;
    }
    uint32_t base = *cursor;
    *cursor += bytes;
    nt_gfx_update_buffer(buffer, base, data, bytes);
    return base;
}

static void draw_strokes(nt_pipeline_t pipeline, nt_vertex_input_t vi, nt_buffer_t buffer, uint32_t base, uint32_t num_indices, uint32_t num_vertices, uint32_t count) {
    nt_gfx_bind_pipeline(pipeline);
    nt_gfx_bind_vertex_input(vi);
    nt_gfx_bind_instance_buffer(buffer, base);
    nt_gfx_set_uniform_mat4(s_u_vp, s_shape.vp);
    nt_gfx_set_uniform_vec4(s_u_eye, s_shape.eye);
    nt_gfx_set_uniform_float(s_u_line_width, s_shape.line_width);
    nt_gfx_set_uniform_vec4(s_u_pixel_scale, s_shape.pixel_scale);
    nt_gfx_draw_indexed_instanced(0, num_indices, num_vertices, count);
}

void nt_shape_renderer_flush(void) {
    /* A skipped flush must free CPU staging for the next emit. */
    if (!s_shape.initialized) {
        memset(s_shape.inst_counts, 0, sizeof(s_shape.inst_counts));
        memset(s_shape.wire_counts, 0, sizeof(s_shape.wire_counts));
        s_shape.vertex_count = 0;
        s_shape.index_count = 0;
        s_shape.line_count = 0;
        s_shape.stroke_count = 0;
        return;
    }
    const bool depth = s_shape.depth_enabled;
    const uint32_t inst_capacity = NT_SHAPE_RENDERER_MAX_INSTANCES * (uint32_t)sizeof(nt_shape_instance_t);

    /* Flush instanced shapes (rect, cube, circle, sphere, cylinder, capsule) */
    for (int t = 0; t < NT_SHAPE_TYPE_COUNT; t++) {
        uint32_t cnt = s_shape.inst_counts[t];
        if (cnt == 0) {
            continue;
        }
        uint32_t inst_base = ring_upload(s_shape.inst_buf, &s_shape.inst_ring_cursor, inst_capacity, s_shape.inst_data[t], cnt * (uint32_t)sizeof(nt_shape_instance_t));
        if (t == NT_SHAPE_CAPSULE) {
            nt_gfx_bind_pipeline(depth ? s_shape.cap_inst_pip_depth : s_shape.cap_inst_pip_overlay);
        } else {
            nt_gfx_bind_pipeline(depth ? s_shape.inst_pip_depth : s_shape.inst_pip_overlay);
        }
        nt_gfx_bind_vertex_input(s_shape.template_vi[t]);
        nt_gfx_bind_instance_buffer(s_shape.inst_buf, inst_base);
        nt_gfx_set_uniform_mat4(s_u_vp, s_shape.vp);

        nt_gfx_draw_indexed_instanced(0, s_shape.templates[t].num_indices, s_shape.templates[t].num_vertices, cnt);
        s_shape.inst_counts[t] = 0;
    }

    /* Flush CPU-batched shapes (triangle, mesh). Batch vbo/ibo stay at offset 0:
     * the non-instanced draw path has no read-side offset plumbing, so the
     * in-flight rewrite (driver copy) is accepted here. */
    if (s_shape.index_count > 0) {
        nt_gfx_update_buffer(s_shape.batch_vbo, 0, s_shape.vertices, s_shape.vertex_count * (uint32_t)sizeof(nt_shape_renderer_vertex_t));
        nt_gfx_update_buffer(s_shape.batch_ibo, 0, s_shape.indices, s_shape.index_count * (uint32_t)sizeof(nt_shape_index_t));

        nt_gfx_bind_pipeline(depth ? s_shape.batch_pip_depth : s_shape.batch_pip_overlay);
        nt_gfx_bind_vertex_input(s_shape.batch_vi);
        nt_gfx_set_uniform_mat4(s_u_vp, s_shape.vp);

        nt_gfx_draw_indexed(0, s_shape.index_count, s_shape.vertex_count);

        s_shape.vertex_count = 0;
        s_shape.index_count = 0;
    }

    /* Strokes last: within one flush, outlines stay on top of filled shapes. */
    for (int type = 0; type < NT_WIRE_COUNT; type++) {
        uint32_t count = s_shape.wire_counts[type];
        if (count == 0) {
            continue;
        }
        uint32_t base = ring_upload(s_shape.inst_buf, &s_shape.inst_ring_cursor, inst_capacity, s_shape.wire_data[type], count * (uint32_t)sizeof(nt_shape_instance_t));
        const nt_shape_template_t *tpl = &s_shape.wire_templates[type];
        draw_strokes(depth ? s_shape.wire_pip_depth : s_shape.wire_pip_overlay, s_shape.wire_vi[type], s_shape.inst_buf, base, tpl->num_indices, tpl->num_vertices, count);
        s_shape.wire_counts[type] = 0;
    }
    nt_pipeline_t line_pip = depth ? s_shape.line_pip_depth : s_shape.line_pip_overlay;
    if (s_shape.stroke_count > 0) {
        uint32_t base =
            ring_upload(s_shape.stroke_instance_buf, &s_shape.stroke_ring_cursor, sizeof(s_shape.strokes), s_shape.strokes, s_shape.stroke_count * (uint32_t)sizeof(nt_shape_stroke_instance_t));
        draw_strokes(line_pip, s_shape.stroke_vi, s_shape.stroke_instance_buf, base, 12, 7, s_shape.stroke_count);
        s_shape.stroke_count = 0;
    }
    if (s_shape.line_count > 0) {
        uint32_t base = ring_upload(s_shape.line_instance_buf, &s_shape.line_ring_cursor, sizeof(s_shape.line_staging.lines), s_shape.line_staging.lines,
                                    s_shape.line_count * (uint32_t)sizeof(nt_shape_line_instance_t));
        /* Independent lines skip the join triangles of the shared template. */
        draw_strokes(line_pip, s_shape.line_vi, s_shape.line_instance_buf, base, 6, 7, s_shape.line_count);
        s_shape.line_count = 0;
    }
}

/* ---- State setters ---- */

static float det3(const float a[3], const float b[3], const float c[3]) {
    return (a[0] * ((b[1] * c[2]) - (b[2] * c[1]))) - (a[1] * ((b[0] * c[2]) - (b[2] * c[0]))) + (a[2] * ((b[0] * c[1]) - (b[1] * c[0])));
}

/* Strokes face the projection center: the homogeneous point VP maps to clip x = y = w = 0.
 * An orthographic VP has no such point; its w = 0 result is the direction toward the viewer. */
static void eye_from_vp(const float vp[16], float eye[4]) {
    /* Column-major: clip row r is (vp[r], vp[4 + r], vp[8 + r], vp[12 + r]). */
    const float rows[3][4] = {{vp[0], vp[4], vp[8], vp[12]}, {vp[1], vp[5], vp[9], vp[13]}, {vp[3], vp[7], vp[11], vp[15]}};
    for (int col = 0; col < 4; col++) {
        float minor[3][3];
        for (int r = 0; r < 3; r++) {
            for (int c = 0, k = 0; c < 4; c++) {
                if (c != col) {
                    minor[r][k++] = rows[r][c];
                }
            }
        }
        float det = det3(minor[0], minor[1], minor[2]);
        eye[col] = (col % 2 == 0) ? det : -det;
    }
    if (eye[3] != 0.0F) {
        float inv_w = 1.0F / eye[3];
        eye[0] *= inv_w;
        eye[1] *= inv_w;
        eye[2] *= inv_w;
        eye[3] = 1.0F;
    } else if ((vp[2] * eye[0]) + (vp[6] * eye[1]) + (vp[10] * eye[2]) > 0.0F) {
        /* Clip z decreases toward the viewer. */
        eye[0] = -eye[0];
        eye[1] = -eye[1];
        eye[2] = -eye[2];
    }
}

void nt_shape_renderer_set_vp(const float vp[16]) {
    if (memcmp(s_shape.vp, vp, sizeof(float) * 16) == 0) { // NOLINT — intentional bitwise dirty-check
        return;
    }
    nt_shape_renderer_flush();
    memcpy(s_shape.vp, vp, sizeof(float) * 16);
    eye_from_vp(vp, s_shape.eye);
}

void nt_shape_renderer_set_line_width(float width) {
    NT_ASSERT(isfinite(width) && width > 0.0F);
    if (width == s_shape.line_width && s_shape.pixel_scale[0] == 0.0F) {
        return;
    }
    nt_shape_renderer_flush();
    s_shape.line_width = width;
    memset(s_shape.pixel_scale, 0, sizeof(s_shape.pixel_scale));
}

void nt_shape_renderer_set_line_width_pixels(float width, uint32_t viewport_width, uint32_t viewport_height) {
    NT_ASSERT(isfinite(width) && width > 0.0F);
    NT_ASSERT(viewport_width > 0 && viewport_height > 0);
    float x = 2.0F / (float)viewport_width;
    float y = 2.0F / (float)viewport_height;
    if (width == s_shape.line_width && x == s_shape.pixel_scale[0] && y == s_shape.pixel_scale[1]) {
        return;
    }
    nt_shape_renderer_flush();
    s_shape.line_width = width;
    s_shape.pixel_scale[0] = x;
    s_shape.pixel_scale[1] = y;
}

void nt_shape_renderer_set_depth(bool enabled) {
    if (enabled == s_shape.depth_enabled) {
        return;
    }
    nt_shape_renderer_flush();
    s_shape.depth_enabled = enabled;
}

/* ---- Line ---- */

void nt_shape_renderer_line(const float a[3], const float b[3], const float color[4]) {
    uint8_t packed[4];
    pack_color(packed, color);
    emit_line(a, b, packed);
}

static bool same_point(const float a[3], const float b[3]) { return a[0] == b[0] && a[1] == b[1] && a[2] == b[2]; }

static uint32_t next_distinct(const float (*points)[3], uint32_t count, uint32_t current) {
    uint32_t next = current + 1;
    while (next < count && same_point(points[current], points[next])) {
        next++;
    }
    return next;
}

static void assert_finite_points(const float (*points)[3], uint32_t count) {
    for (uint32_t i = 0; i < count; i++) {
        NT_ASSERT(isfinite(points[i][0]) && isfinite(points[i][1]) && isfinite(points[i][2]));
    }
}

void nt_shape_renderer_polyline(const float (*points)[3], uint32_t count, bool closed, const float color[4]) {
    NT_ASSERT(points != NULL || count == 0);
    assert_finite_points(points, count);
    if (count < 2) {
        return;
    }
    while (closed && count > 1 && same_point(points[0], points[count - 1])) {
        count--;
    }
    uint32_t first_next = next_distinct(points, count, 0);
    if (first_next == count) {
        return;
    }
    uint8_t packed[4];
    pack_color(packed, color);
    uint32_t last = count - 1;
    /* Two distinct endpoints are one stroke, including a requested closed path. */
    closed = closed && next_distinct(points, count, first_next) < count;
    uint32_t prev = closed ? last : 0;
    uint32_t a = 0;
    uint32_t b = first_next;
    while (b < count) {
        uint32_t next = next_distinct(points, count, b);
        uint32_t after = next;
        if (next == count) {
            after = closed ? 0 : b;
        }
        emit_stroke(points[prev], points[a], points[b], points[after], packed);
        prev = a;
        a = b;
        b = next;
    }
    if (closed) {
        emit_stroke(points[prev], points[a], points[0], points[first_next], packed);
    }
}

/* ---- Rectangle ---- */

void nt_shape_renderer_rect(const float pos[3], const float size[2], const float color[4]) {
    float scale[3] = {size[0], size[1], 1.0F};
    push_instance(NT_SHAPE_RECT, pos, scale, NULL, color);
}

void nt_shape_renderer_rect_wire(const float pos[3], const float size[2], const float color[4]) {
    float hx = size[0] * 0.5F;
    float hy = size[1] * 0.5F;

    const float corners[4][3] = {
        {pos[0] - hx, pos[1] - hy, pos[2]},
        {pos[0] + hx, pos[1] - hy, pos[2]},
        {pos[0] + hx, pos[1] + hy, pos[2]},
        {pos[0] - hx, pos[1] + hy, pos[2]},
    };
    nt_shape_renderer_polyline(corners, 4, true, color);
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

    nt_shape_renderer_polyline((const float(*)[3])corners, 4, true, color);
}

/* ---- Triangle ---- */

void nt_shape_renderer_triangle(const float a[3], const float b[3], const float c[3], const float color[4]) {
    if (s_shape.vertex_count + 3 > NT_SHAPE_RENDERER_MAX_VERTICES || s_shape.index_count + 3 > NT_SHAPE_RENDERER_MAX_INDICES) {
        nt_shape_renderer_flush();
    }

    nt_shape_index_t base = (nt_shape_index_t)s_shape.vertex_count;
    nt_shape_renderer_vertex_t *v = &s_shape.vertices[s_shape.vertex_count];

    set_vertex(&v[0], a, color);
    set_vertex(&v[1], b, color);
    set_vertex(&v[2], c, color);

    nt_shape_index_t *idx = &s_shape.indices[s_shape.index_count];
    idx[0] = base;
    idx[1] = (nt_shape_index_t)(base + 1);
    idx[2] = (nt_shape_index_t)(base + 2);

    s_shape.vertex_count += 3;
    s_shape.index_count += 3;
}

void nt_shape_renderer_triangle_wire(const float a[3], const float b[3], const float c[3], const float color[4]) {
    const float corners[3][3] = {{a[0], a[1], a[2]}, {b[0], b[1], b[2]}, {c[0], c[1], c[2]}};
    nt_shape_renderer_polyline(corners, 3, true, color);
}

/* ---- Circle ---- */

void nt_shape_renderer_circle(const float center[3], float radius, const float color[4]) {
    float scale[3] = {radius, 1.0F, radius};
    push_instance(NT_SHAPE_CIRCLE, center, scale, NULL, color);
}

void nt_shape_renderer_circle_wire(const float center[3], float radius, const float color[4]) { push_wire_instance(NT_WIRE_CIRCLE, center, radius, 0.0F, NULL, color); }

void nt_shape_renderer_circle_rot(const float center[3], float radius, const float rot[4], const float color[4]) {
    float scale[3] = {radius, 1.0F, radius};
    push_instance(NT_SHAPE_CIRCLE, center, scale, rot, color);
}

void nt_shape_renderer_circle_wire_rot(const float center[3], float radius, const float rot[4], const float color[4]) { push_wire_instance(NT_WIRE_CIRCLE, center, radius, 0.0F, rot, color); }

/* ---- Cube ---- */

void nt_shape_renderer_cube(const float center[3], const float size[3], const float color[4]) { push_instance(NT_SHAPE_CUBE, center, size, NULL, color); }

void nt_shape_renderer_cube_wire(const float center[3], const float size[3], const float color[4]) {
    float hx = size[0] * 0.5F;
    float hy = size[1] * 0.5F;
    float hz = size[2] * 0.5F;
    uint8_t packed[4];
    pack_color(packed, color);

    float c[8][3] = {
        {center[0] - hx, center[1] - hy, center[2] - hz}, {center[0] + hx, center[1] - hy, center[2] - hz}, {center[0] + hx, center[1] + hy, center[2] - hz},
        {center[0] - hx, center[1] + hy, center[2] - hz}, {center[0] - hx, center[1] - hy, center[2] + hz}, {center[0] + hx, center[1] - hy, center[2] + hz},
        {center[0] + hx, center[1] + hy, center[2] + hz}, {center[0] - hx, center[1] + hy, center[2] + hz},
    };

    /* 12 edges */
    /* Bottom face */
    emit_line(c[0], c[1], packed);
    emit_line(c[1], c[5], packed);
    emit_line(c[5], c[4], packed);
    emit_line(c[4], c[0], packed);
    /* Top face */
    emit_line(c[3], c[2], packed);
    emit_line(c[2], c[6], packed);
    emit_line(c[6], c[7], packed);
    emit_line(c[7], c[3], packed);
    /* Vertical edges */
    emit_line(c[0], c[3], packed);
    emit_line(c[1], c[2], packed);
    emit_line(c[5], c[6], packed);
    emit_line(c[4], c[7], packed);
}

void nt_shape_renderer_cube_rot(const float center[3], const float size[3], const float rot[4], const float color[4]) { push_instance(NT_SHAPE_CUBE, center, size, rot, color); }

void nt_shape_renderer_cube_wire_rot(const float center[3], const float size[3], const float rot[4], const float color[4]) {
    float hx = size[0] * 0.5F;
    float hy = size[1] * 0.5F;
    float hz = size[2] * 0.5F;
    uint8_t packed[4];
    pack_color(packed, color);

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

    emit_line(c[0], c[1], packed);
    emit_line(c[1], c[5], packed);
    emit_line(c[5], c[4], packed);
    emit_line(c[4], c[0], packed);
    emit_line(c[3], c[2], packed);
    emit_line(c[2], c[6], packed);
    emit_line(c[6], c[7], packed);
    emit_line(c[7], c[3], packed);
    emit_line(c[0], c[3], packed);
    emit_line(c[1], c[2], packed);
    emit_line(c[5], c[6], packed);
    emit_line(c[4], c[7], packed);
}

/* ---- Sphere ---- */

void nt_shape_renderer_sphere(const float center[3], float radius, const float color[4]) {
    float scale[3] = {radius, radius, radius};
    push_instance(NT_SHAPE_SPHERE, center, scale, NULL, color);
}

void nt_shape_renderer_sphere_wire(const float center[3], float radius, const float color[4]) { push_wire_instance(NT_WIRE_SPHERE, center, radius, 0.0F, NULL, color); }

void nt_shape_renderer_sphere_rot(const float center[3], float radius, const float rot[4], const float color[4]) {
    float scale[3] = {radius, radius, radius};
    push_instance(NT_SHAPE_SPHERE, center, scale, rot, color);
}

void nt_shape_renderer_sphere_wire_rot(const float center[3], float radius, const float rot[4], const float color[4]) { push_wire_instance(NT_WIRE_SPHERE, center, radius, 0.0F, rot, color); }

/* ---- Cylinder ---- */

void nt_shape_renderer_cylinder(const float center[3], float radius, float height, const float color[4]) {
    float scale[3] = {radius, height, radius};
    push_instance(NT_SHAPE_CYLINDER, center, scale, NULL, color);
}

void nt_shape_renderer_cylinder_wire(const float center[3], float radius, float height, const float color[4]) { push_wire_instance(NT_WIRE_CYLINDER, center, radius, height * 0.5F, NULL, color); }

void nt_shape_renderer_cylinder_rot(const float center[3], float radius, float height, const float rot[4], const float color[4]) {
    float scale[3] = {radius, height, radius};
    push_instance(NT_SHAPE_CYLINDER, center, scale, rot, color);
}

void nt_shape_renderer_cylinder_wire_rot(const float center[3], float radius, float height, const float rot[4], const float color[4]) {
    push_wire_instance(NT_WIRE_CYLINDER, center, radius, height * 0.5F, rot, color);
}

/* ---- Capsule ---- */

void nt_shape_renderer_capsule(const float center[3], float radius, float height, const float color[4]) {
    float body_half = (height - 2.0F * radius) * 0.5F;
    if (body_half < 0.0F) {
        body_half = 0.0F;
    }
    float scale[3] = {radius, body_half, 0.0F};
    push_instance(NT_SHAPE_CAPSULE, center, scale, NULL, color);
}

void nt_shape_renderer_capsule_wire(const float center[3], float radius, float height, const float color[4]) {
    float body_half = fmaxf(0.0F, (height - 2.0F * radius) * 0.5F);
    push_wire_instance(body_half > 0.0F ? NT_WIRE_CAPSULE : NT_WIRE_SPHERE, center, radius, body_half, NULL, color);
}

void nt_shape_renderer_capsule_rot(const float center[3], float radius, float height, const float rot[4], const float color[4]) {
    float body_half = (height - 2.0F * radius) * 0.5F;
    if (body_half < 0.0F) {
        body_half = 0.0F;
    }
    float scale[3] = {radius, body_half, 0.0F};
    push_instance(NT_SHAPE_CAPSULE, center, scale, rot, color);
}

void nt_shape_renderer_capsule_wire_rot(const float center[3], float radius, float height, const float rot[4], const float color[4]) {
    float body_half = fmaxf(0.0F, (height - 2.0F * radius) * 0.5F);
    push_wire_instance(body_half > 0.0F ? NT_WIRE_CAPSULE : NT_WIRE_SPHERE, center, radius, body_half, rot, color);
}

/* ---- Mesh ---- */

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void nt_shape_renderer_mesh(const float *positions, uint32_t num_vertices, const nt_shape_index_t *indices, uint32_t num_indices, const float color[4]) {
    if (s_shape.vertex_count + num_vertices > NT_SHAPE_RENDERER_MAX_VERTICES || s_shape.index_count + num_indices > NT_SHAPE_RENDERER_MAX_INDICES) {
        nt_shape_renderer_flush();
    }
    if (num_vertices > NT_SHAPE_RENDERER_MAX_VERTICES || num_indices > NT_SHAPE_RENDERER_MAX_INDICES) {
        NT_ASSERT(0 && "mesh exceeds batch limits");
        NT_LOG_ERROR("mesh too large for batch, dropped");
        return;
    }

    /* Validate indices are within bounds */
    for (uint32_t i = 0; i < num_indices; i++) {
        if (indices[i] >= num_vertices) {
            NT_ASSERT(0 && "mesh index out of bounds");
            NT_LOG_ERROR("mesh index out of bounds, dropped");
            return;
        }
    }

    nt_shape_index_t base = (nt_shape_index_t)s_shape.vertex_count;

    /* Copy positions into vertex buffer with color */
    for (uint32_t i = 0; i < num_vertices; i++) {
        const float *pos = &positions[(size_t)i * 3];
        set_vertex(&s_shape.vertices[s_shape.vertex_count], pos, color);
        s_shape.vertex_count++;
    }

    /* Copy indices with base offset */
    for (uint32_t i = 0; i < num_indices; i++) {
        s_shape.indices[s_shape.index_count++] = (nt_shape_index_t)(base + indices[i]);
    }
}

void nt_shape_renderer_mesh_wire(const float *positions, uint32_t num_vertices, const nt_shape_index_t *indices, uint32_t num_indices, const float color[4]) {
    uint8_t packed[4];
    pack_color(packed, color);
    /* For each triangle (3 consecutive indices), emit 3 wireframe edges */
    for (uint32_t i = 0; (i + 2) < num_indices; i += 3) {
        if (indices[i] >= num_vertices || indices[i + 1] >= num_vertices || indices[i + 2] >= num_vertices) {
            NT_ASSERT(0 && "mesh_wire index out of bounds");
            NT_LOG_ERROR("mesh_wire index out of bounds, skipped triangle");
            continue;
        }
        const float *a = &positions[(ptrdiff_t)indices[i] * 3];
        const float *b = &positions[(ptrdiff_t)indices[i + 1] * 3];
        const float *c = &positions[(ptrdiff_t)indices[i + 2] * 3];
        emit_line(a, b, packed);
        emit_line(b, c, packed);
        emit_line(c, a, packed);
    }
}

/* ---- Test accessors (always compiled; header guards visibility) ---- */

uint32_t nt_shape_renderer_test_instance_count(int type) { return s_shape.inst_counts[type]; }

uint32_t nt_shape_renderer_test_instance_capacity(void) { return NT_SHAPE_RENDERER_MAX_INSTANCES; }
uint32_t nt_shape_renderer_test_vertex_count(void) { return s_shape.vertex_count; }
uint32_t nt_shape_renderer_test_index_count(void) { return s_shape.index_count; }
uint32_t nt_shape_renderer_test_stroke_count(void) {
    uint32_t count = s_shape.line_count + s_shape.stroke_count;
    for (int type = 0; type < NT_WIRE_COUNT; type++) {
        count += s_shape.wire_counts[type];
    }
    return count;
}
const float *nt_shape_renderer_test_vp(void) { return s_shape.vp; }
const float *nt_shape_renderer_test_eye(void) { return s_shape.eye; }
float nt_shape_renderer_test_line_width(void) { return s_shape.line_width; }
bool nt_shape_renderer_test_depth_enabled(void) { return s_shape.depth_enabled; }
bool nt_shape_renderer_test_initialized(void) { return s_shape.initialized; }
