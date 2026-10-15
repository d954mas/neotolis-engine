#include "renderers/nt_shape_renderer.h"

#include "core/nt_assert.h"
#include "core/nt_builtins.h"
#include "graphics/nt_gfx.h"
#include "log/nt_log.h"
#include "math/nt_math.h"

#include <stddef.h>
#include <string.h>

_Static_assert(NT_GFX_MAX_INSTANCE_ATTRS >= 5, "shape instance layout needs 5 attributes: raise NT_GFX_MAX_INSTANCE_ATTRS");

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

/* ---- Embedded shader source ---- */

/* The Draw block of all four shape programs: one std140 layout, so their binds of one view dedup. */
#define NT_SHAPE_DRAW_GLSL "layout(std140) uniform Draw { mat4 u_vp; vec4 u_eye; vec4 u_pixel_scale; };\n"

/* Fill and wire templates share one transform. xyz is the unit template; w tags a capsule hemisphere
 * (+1 top, -1 bottom) and is 0 for every other shape: a tagged point scales by the radius on all axes
 * and moves by half the body. */
#define NT_SHAPE_POINT_GLSL                                                                                                                                                                            \
    "vec3 shape_point(vec4 p, vec3 center, vec3 scale, vec4 rot) {\n"                                                                                                                                  \
    "    vec3 v = p.xyz * mix(scale, scale.xxx, abs(p.w));\n"                                                                                                                                          \
    "    v.y += p.w * scale.y;\n"                                                                                                                                                                      \
    "    vec3 t = 2.0 * cross(rot.xyz, v);\n"                                                                                                                                                          \
    "    return center + v + rot.w * t + cross(rot.xyz, t);\n"                                                                                                                                         \
    "}\n"

static const char *s_shape_vs_src = "precision highp float;\n"
                                    "layout(location = 0) in vec3 a_position;\n"
                                    "layout(location = 2) in vec4 a_color;\n" NT_SHAPE_DRAW_GLSL "out mediump vec4 v_color;\n"
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
    NT_SHAPE_DRAW_GLSL                                                                                                                                                                                 \
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
    "vec4 pixel_position(vec2 corner, vec3 before, vec3 p, vec3 after, float width) {\n"                                                                                                               \
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
    "    cp.xy+=offset.xy*(0.5*width)*u_pixel_scale.xy*cp.w;\n"                                                                                                                                        \
    "    return cp;\n"                                                                                                                                                                                 \
    "}\n"                                                                                                                                                                                              \
    "vec4 stroke_position(vec2 corner, vec3 before, vec3 p, vec3 after, float width) {\n"                                                                                                              \
    "    if (u_pixel_scale.x>0.0) return pixel_position(corner,before,p,after,width);\n"                                                                                                               \
    "    vec3 view = to_eye(p);\n"                                                                                                                                                                     \
    "    vec3 offset = join_offset(side(p-before,view),side(after-p,view),corner.y);\n"                                                                                                                \
    "    return u_vp*vec4(biased(p+offset*(0.5*width)),1);\n"                                                                                                                                          \
    "}\n"

static const char *s_line_vs_src =
    "precision highp float;\n"
    "layout(location = 0) in vec2 a_corner;\n"
    "layout(location = 1) in vec4 i_prev;\n"
    "layout(location = 2) in vec3 i_a;\n"
    "layout(location = 3) in vec3 i_b;\n"
    "layout(location = 4) in vec3 i_next;\n"
    "layout(location = 5) in vec4 i_color;\n"
    "out mediump vec4 v_color;\n" NT_STROKE_GLSL "void main(){bool s=a_corner.x<0.5;gl_Position=stroke_position(a_corner,s?i_prev.xyz:i_a,s?i_a:i_b,s?i_b:i_next,i_prev.w);v_color=i_color;}\n";

/* Template vertices already store their corner's (before, p, after) triple. */
static const char *s_wire_vs_src = "precision highp float;\n"
                                   "layout(location=0) in vec2 a_corner;\n"
                                   "layout(location=1) in vec4 a_before;\n"
                                   "layout(location=2) in vec4 a_p;\n"
                                   "layout(location=3) in vec4 a_after;\n"
                                   "layout(location=5) in vec3 i_center;\n"
                                   "layout(location=6) in vec4 i_scale;\n"
                                   "layout(location=7) in vec4 i_rot;\n"
                                   "layout(location=8) in vec4 i_color;\n"
                                   "out mediump vec4 v_color;\n" NT_SHAPE_POINT_GLSL "vec3 wire_point(vec4 p) { return shape_point(p, i_center, i_scale.xyz, i_rot); }\n" NT_STROKE_GLSL
                                   "void main(){gl_Position=stroke_position(a_corner,wire_point(a_before),wire_point(a_p),wire_point(a_after),i_scale.w);v_color=i_color;}\n";

/* Stroke corners: vertices 0-4 sit at the segment start (4 = joint center), 5-6 at its end.
 * The first 6 indices are the body quad; the last 6 are the start bevel. */
static const uint16_t s_stroke_indices[12] = {2, 3, 6, 2, 6, 5, 4, 0, 2, 4, 1, 3};

/* ---- Instance data ---- */

/* Lines and strokes put the width after their first point: one vec4 attribute for both. */
typedef struct {
    float prev[3];
    float width;
    float a[3];
    float b[3];
    float next[3];
    uint32_t color;
} nt_shape_stroke_instance_t;

_Static_assert(sizeof(nt_shape_stroke_instance_t) == 56, "stroke instance size");

typedef struct {
    float a[3];
    float width;
    float b[3];
    uint32_t color;
} nt_shape_line_instance_t;

_Static_assert(sizeof(nt_shape_line_instance_t) == 32, "line instance size");

typedef struct {
    float center[3];
    float scale[3];
    float rot[4];
    uint32_t color;
} nt_shape_instance_t;

_Static_assert(sizeof(nt_shape_instance_t) == 44, "shape instance size");

/* A shape instance plus the width, which rides in the scale attribute's fourth lane. */
typedef struct {
    float center[3];
    float scale[3];
    float width;
    float rot[4];
    uint32_t color;
} nt_shape_wire_instance_t;

_Static_assert(sizeof(nt_shape_wire_instance_t) == 48, "wire instance size");

/* Template types, filled and wire alike */
enum {
    NT_SHAPE_RECT = 0,
    NT_SHAPE_CUBE,
    NT_SHAPE_CIRCLE,
    NT_SHAPE_SPHERE,
    NT_SHAPE_CYLINDER,
    NT_SHAPE_CAPSULE,
    NT_SHAPE_TYPE_COUNT,
};

/* Capsule meridian: a full ring plus the equator angle repeated on each straight side. */
#define NT_WIRE_CAP_POINTS (NT_SHAPE_SEGMENTS + 2)

/* points = (before, p, after) of this corner; w tags the capsule half. */
typedef struct {
    float points[3][4];
    float corner[2];
} nt_wire_vertex_t;

/* One template's index range in a shared template buffer; indices are absolute in that buffer. */
typedef struct {
    uint32_t first_index;
    uint32_t num_indices;
    uint32_t num_vertices;
} nt_shape_range_t;

/* Filled templates share one buffer; every vertex is vec4 so the capsule's hemisphere tag fits. */
#define NT_FILL_TEMPLATE_VERTICES (4 + 8 + NT_SEG_CIRCLE_NV + NT_SEG_SPHERE_NV + NT_SEG_CYL_NV + NT_SEG_CAP_NV)
#define NT_FILL_TEMPLATE_INDICES (6 + 36 + NT_SEG_CIRCLE_NI + NT_SEG_SPHERE_NI + NT_SEG_CYL_NI + NT_SEG_CAP_NI)
/* Rect 4 edges, cube 12, circle 1 ring, sphere 3, cylinder 2 rings + 4 struts, capsule 2 rings + 2 meridians. */
#define NT_WIRE_TEMPLATE_SEGMENTS (4 + 12 + (8 * NT_SHAPE_SEGMENTS) + 4 + (2 * NT_WIRE_CAP_POINTS))
/* WebGL 2 always restarts primitives at index 65535. */
_Static_assert(NT_SHAPE_RENDERER_MAX_VERTICES >= 3, "the triangle batch holds at least one triangle");
_Static_assert(NT_FILL_TEMPLATE_VERTICES < 65535 && NT_WIRE_TEMPLATE_SEGMENTS * 7 < 65535, "template indices must stay below the restart index");

typedef struct {
    float pos[3];   /* world-space position */
    uint32_t color; /* RGBA8 0xAABBGGRR, read normalized */
} nt_shape_vertex_t;

/* ---- Filled shape vertex shader ---- */

static const char *s_fill_vs_src = "precision highp float;\n"
                                   "layout(location = 0) in vec4 a_pos_tag;\n"
                                   "layout(location = 1) in vec3 i_center;\n"
                                   "layout(location = 2) in vec3 i_scale;\n"
                                   "layout(location = 3) in vec4 i_rot;\n"
                                   "layout(location = 4) in vec4 i_color;\n" NT_SHAPE_DRAW_GLSL NT_SHAPE_POINT_GLSL "out mediump vec4 v_color;\n"
                                   "void main() {\n"
                                   "    v_color = i_color;\n"
                                   "    gl_Position = u_vp * vec4(shape_point(a_pos_tag, i_center, i_scale, i_rot), 1.0);\n"
                                   "}\n";

/* ---- Module state ---- */

/* Mirrors NT_SHAPE_DRAW_GLSL (std140). */
typedef struct {
    float vp[16];
    float eye[4];         /* derived from vp; w = 0 for an orthographic view direction */
    float pixel_scale[4]; /* .xy = 2 / viewport for pixel widths; 0 = world-space widths */
} nt_shape_draw_block_t;

_Static_assert(offsetof(nt_shape_draw_block_t, eye) == 64 && offsetof(nt_shape_draw_block_t, pixel_scale) == 80 && sizeof(nt_shape_draw_block_t) == 96,
               "nt_shape_draw_block_t mirrors the std140 Draw block of the shape shaders");

static struct {
    /* GPU objects: made by create_gpu, released and zeroed by destroy_gpu. Pipelines index by depth_enabled. */
    struct {
        nt_shader_t fs;
        nt_shader_t fill_vs;
        nt_shader_t batch_vs;
        nt_shader_t wire_vs;
        nt_shader_t line_vs;
        nt_program_t fill_prog;
        nt_program_t batch_prog;
        nt_program_t wire_prog;
        nt_program_t line_prog;
        nt_pipeline_t fill_pip[2];
        nt_pipeline_t batch_pip[2];
        nt_pipeline_t wire_pip[2];
        nt_pipeline_t line_pip[2];
        nt_buffer_t fill_vbo;
        nt_buffer_t fill_ibo;
        nt_buffer_t wire_vbo;
        nt_buffer_t wire_ibo;
        nt_buffer_t line_vbo;
        nt_buffer_t line_ibo;
        /* Instanced kinds per frame vertex stream, each over its stream's buffer; made on first selection. */
        nt_vertex_input_t fill_vi[NT_GFX_MAX_VERTEX_STREAMS];
        nt_vertex_input_t wire_vi[NT_GFX_MAX_VERTEX_STREAMS];
        nt_vertex_input_t line_vi[NT_GFX_MAX_VERTEX_STREAMS];
        nt_vertex_input_t stroke_vi[NT_GFX_MAX_VERTEX_STREAMS];
        nt_vertex_input_t batch_vi; /* over the frame vertex buffer, which a restore replaces */
    } gpu;
    /* CPU-only gfx records: registered at init, they survive a restore. */
    nt_instance_layout_t fill_layout;
    nt_instance_layout_t wire_layout;
    nt_instance_layout_t line_layout;
    nt_instance_layout_t stroke_layout;
    nt_shape_range_t fill_ranges[NT_SHAPE_TYPE_COUNT];
    nt_shape_range_t wire_ranges[NT_SHAPE_TYPE_COUNT];

    /* CPU staging per kind and template type: a flush copies each into frame storage in kind order. */
    nt_shape_instance_t fills[NT_SHAPE_TYPE_COUNT][NT_SHAPE_RENDERER_MAX_FILLS];
    uint32_t fill_counts[NT_SHAPE_TYPE_COUNT];
    nt_shape_vertex_t vertices[NT_SHAPE_RENDERER_MAX_VERTICES]; /* triangles, three per triangle */
    uint32_t vertex_count;
    nt_shape_wire_instance_t wires[NT_SHAPE_TYPE_COUNT][NT_SHAPE_RENDERER_MAX_WIRES];
    uint32_t wire_counts[NT_SHAPE_TYPE_COUNT];
    nt_shape_stroke_instance_t strokes[NT_SHAPE_RENDERER_MAX_POLYLINE_SEGMENTS];
    uint32_t stroke_count;
    /* Templates are built in create_gpu, which runs with the line queue empty: the scratch stays
     * off the small WASM stack and costs nothing while MAX_LINES * 32 B covers it. */
    union {
        nt_shape_line_instance_t lines[NT_SHAPE_RENDERER_MAX_LINES];
        struct {
            float fill_vertices[NT_FILL_TEMPLATE_VERTICES][4];
            uint16_t fill_indices[NT_FILL_TEMPLATE_INDICES];
            nt_wire_vertex_t wire_vertices[NT_WIRE_TEMPLATE_SEGMENTS * 7];
            uint16_t wire_indices[NT_WIRE_TEMPLATE_SEGMENTS * 12];
        } build;
    } line_staging;
    uint32_t line_count;

    /* Settings: the Draw block every flush binds, then the CPU-only ones. */
    nt_shape_draw_block_t draw;
    bool depth_enabled;
    uint8_t stream; /* frame vertex stream of instanced kinds */
    bool initialized;

    /* Sin/Cos lookup table (fixed NT_SHAPE_SEGMENTS) */
    float sin_lut[NT_SHAPE_SEGMENTS + 1];
    float cos_lut[NT_SHAPE_SEGMENTS + 1];
} s_shape;

/* ---- Layouts and pipelines ---- */

static const nt_vertex_layout_t k_batch_layout = {
    .attr_count = 2,
    .stride = (uint16_t)sizeof(nt_shape_vertex_t),
    .attrs =
        {
            {.location = NT_LOC_POSITION, .type = NT_VERTEX_FLOAT, .count = 3, .offset = 0},
            {.location = NT_LOC_COLOR, .type = NT_VERTEX_UINT8, .count = 4, .normalized = true, .offset = 12},
        },
};

static const nt_vertex_layout_t k_fill_template_layout = {
    .attr_count = 1,
    .stride = (uint16_t)(4 * sizeof(float)),
    .attrs = {{.location = 0, .type = NT_VERTEX_FLOAT, .count = 4, .offset = 0}},
};

static const nt_vertex_layout_t k_shape_instance_layout = {
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

/* The wire program reads its instance after its template's corner triple. */
static const nt_vertex_layout_t k_wire_instance_layout = {
    .attr_count = 4,
    .stride = (uint16_t)sizeof(nt_shape_wire_instance_t),
    .attrs =
        {
            {.location = 5, .type = NT_VERTEX_FLOAT, .count = 3, .offset = 0},
            {.location = 6, .type = NT_VERTEX_FLOAT, .count = 4, .offset = 12},
            {.location = 7, .type = NT_VERTEX_FLOAT, .count = 4, .offset = 28},
            {.location = 8, .type = NT_VERTEX_UINT8, .count = 4, .normalized = true, .offset = 44},
        },
};

static const nt_vertex_layout_t k_wire_vertex_layout = {
    .stride = sizeof(nt_wire_vertex_t),
    .attr_count = 4,
    .attrs =
        {
            {.location = 0, .type = NT_VERTEX_FLOAT, .count = 2, .offset = 48},
            {.location = 1, .type = NT_VERTEX_FLOAT, .count = 4, .offset = 0},
            {.location = 2, .type = NT_VERTEX_FLOAT, .count = 4, .offset = 16},
            {.location = 3, .type = NT_VERTEX_FLOAT, .count = 4, .offset = 32},
        },
};

static const nt_vertex_layout_t k_line_template_layout = {
    .attr_count = 1,
    .stride = (uint16_t)(2 * sizeof(float)),
    .attrs = {{.location = 0, .type = NT_VERTEX_FLOAT, .count = 2, .offset = 0}},
};

static const nt_vertex_layout_t k_stroke_instance_layout = {
    .attr_count = 5,
    .stride = sizeof(nt_shape_stroke_instance_t),
    .attrs =
        {
            {.location = 1, .type = NT_VERTEX_FLOAT, .count = 4, .offset = 0},
            {.location = 2, .type = NT_VERTEX_FLOAT, .count = 3, .offset = 16},
            {.location = 3, .type = NT_VERTEX_FLOAT, .count = 3, .offset = 28},
            {.location = 4, .type = NT_VERTEX_FLOAT, .count = 3, .offset = 40},
            {.location = 5, .type = NT_VERTEX_UINT8, .count = 4, .normalized = true, .offset = 52},
        },
};

/* An independent edge needs no stored neighbors: prev aliases a and next aliases b. */
static const nt_vertex_layout_t k_line_instance_layout = {
    .attr_count = 5,
    .stride = sizeof(nt_shape_line_instance_t),
    .attrs =
        {
            {.location = 1, .type = NT_VERTEX_FLOAT, .count = 4, .offset = 0},
            {.location = 2, .type = NT_VERTEX_FLOAT, .count = 3, .offset = 0},
            {.location = 3, .type = NT_VERTEX_FLOAT, .count = 3, .offset = 16},
            {.location = 4, .type = NT_VERTEX_FLOAT, .count = 3, .offset = 16},
            {.location = 5, .type = NT_VERTEX_UINT8, .count = 4, .normalized = true, .offset = 28},
        },
};

/* Filled geometry gets a depth bias so strokes over it win the depth test. */
static nt_pipeline_t make_fill_pipeline(nt_program_t program, bool depth, const char *label) {
    return nt_gfx_make_pipeline(&(nt_pipeline_desc_t){
        .program = program,
        .depth_test = depth,
        .depth_write = depth,
        .depth_func = NT_DEPTH_LEQUAL,
        .cull_mode = 0,
        .polygon_offset = depth,
        .polygon_offset_factor = depth ? 1.0F : 0.0F,
        .polygon_offset_units = depth ? 1.0F : 0.0F,
        .label = label,
    });
}

static nt_pipeline_t make_stroke_pipeline(nt_program_t program, bool depth, const char *label) {
    return nt_gfx_make_pipeline(&(nt_pipeline_desc_t){
        .program = program,
        .depth_test = depth,
        .depth_write = depth,
        .depth_func = NT_DEPTH_LEQUAL,
        .cull_mode = 0,
        .label = label,
    });
}

/* ---- Push instance helper ---- */

static const float k_identity_rot[4] = {0.0F, 0.0F, 0.0F, 1.0F};

static void push_instance(int type, const float center[3], const float scale[3], const float *rot, uint32_t color) {
    if (s_shape.fill_counts[type] == NT_SHAPE_RENDERER_MAX_FILLS) {
        nt_shape_renderer_flush();
    }
    nt_shape_instance_t *inst = &s_shape.fills[type][s_shape.fill_counts[type]++];
    memcpy(inst->center, center, sizeof(inst->center));
    memcpy(inst->scale, scale, sizeof(inst->scale));
    memcpy(inst->rot, rot ? rot : k_identity_rot, sizeof(inst->rot));
    inst->color = color;
}

static void assert_width(float width) { NT_ASSERT(isfinite(width) && width > 0.0F); }

static void push_wire_instance(int type, const float center[3], const float scale[3], const float *rot, float width, uint32_t color) {
    assert_width(width);
    if (s_shape.wire_counts[type] == NT_SHAPE_RENDERER_MAX_WIRES) {
        nt_shape_renderer_flush();
    }
    nt_shape_wire_instance_t *inst = &s_shape.wires[type][s_shape.wire_counts[type]++];
    memcpy(inst->center, center, sizeof(inst->center));
    memcpy(inst->scale, scale, sizeof(inst->scale));
    inst->width = width;
    memcpy(inst->rot, rot ? rot : k_identity_rot, sizeof(inst->rot));
    inst->color = color;
}

static void build_trig_lut(void) {
    float inv = 2.0F * NT_PI / (float)NT_SHAPE_SEGMENTS;
    for (int i = 0; i <= NT_SHAPE_SEGMENTS; i++) {
        float theta = inv * (float)i;
        s_shape.sin_lut[i] = sinf(theta);
        s_shape.cos_lut[i] = cosf(theta);
    }
}

// #region templates
/* Unit cube ±0.5: the filled cube's vertices and the cube wire's edge ends. */
static const float k_cube_corners[8][3] = {
    {-0.5F, -0.5F, -0.5F}, {0.5F, -0.5F, -0.5F}, {0.5F, 0.5F, -0.5F}, {-0.5F, 0.5F, -0.5F}, {-0.5F, -0.5F, 0.5F}, {0.5F, -0.5F, 0.5F}, {0.5F, 0.5F, 0.5F}, {-0.5F, 0.5F, 0.5F},
};

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

/* Appends a path's segments to the shared wire template; segment s owns vertices s*7... A segment with
 * no previous neighbour takes only the body indices: its start bevel would be degenerate.
 * Init-only and called from every template: one copy, not one per call site. */
static NT_NOINLINE void wire_template_path(uint32_t *segments, uint32_t *index_count, const float (*points)[4], uint32_t count, bool closed) {
    nt_wire_vertex_t *vertices = s_shape.line_staging.build.wire_vertices;
    uint16_t *indices = s_shape.line_staging.build.wire_indices;
    uint32_t edges = closed ? count : count - 1;
    for (uint32_t i = 0; i < edges; i++) {
        NT_ASSERT(*segments < NT_WIRE_TEMPLATE_SEGMENTS);
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
        const uint32_t n = (closed || i > 0) ? 12 : 6;
        for (uint32_t j = 0; j < n; j++) {
            indices[(*index_count)++] = (uint16_t)(base + s_stroke_indices[j]);
        }
        (*segments)++;
    }
}

static void wire_ring_points(float points[NT_WIRE_CAP_POINTS][4], int plane, float y, float tag) {
    for (int i = 0; i < NT_SHAPE_SEGMENTS; i++) {
        memset(points[i], 0, sizeof(points[i]));
        points[i][plane == 2 ? 1 : 0] = s_shape.cos_lut[i];
        points[i][plane == 1 ? 1 : 2] = s_shape.sin_lut[i];
        points[i][1] += y;
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

/* Unit rings of the round wire templates: plane (0 XZ, 1 XY, 2 YZ), height and capsule tag. */
static const struct {
    uint8_t type;
    uint8_t plane;
    float y;
    float tag;
} k_wire_rings[] = {
    {NT_SHAPE_CIRCLE, 0, 0.0F, 0.0F},   {NT_SHAPE_SPHERE, 0, 0.0F, 0.0F},    {NT_SHAPE_SPHERE, 1, 0.0F, 0.0F},  {NT_SHAPE_SPHERE, 2, 0.0F, 0.0F},
    {NT_SHAPE_CYLINDER, 0, 0.5F, 0.0F}, {NT_SHAPE_CYLINDER, 0, -0.5F, 0.0F}, {NT_SHAPE_CAPSULE, 0, 0.0F, 1.0F}, {NT_SHAPE_CAPSULE, 0, 0.0F, -1.0F},
};

/* Each wire template sits on its filled template's unit shape, so both take the same instance scale. */
static void build_wire_templates(uint32_t *segments, uint32_t *index_count) {
    for (int type = 0; type < NT_SHAPE_TYPE_COUNT; type++) {
        const uint32_t first = *segments;
        const uint32_t first_index = *index_count;
        float points[NT_WIRE_CAP_POINTS][4];
        for (size_t r = 0; r < sizeof(k_wire_rings) / sizeof(k_wire_rings[0]); r++) {
            if (k_wire_rings[r].type == type) {
                wire_ring_points(points, k_wire_rings[r].plane, k_wire_rings[r].y, k_wire_rings[r].tag);
                wire_template_path(segments, index_count, (const float(*)[4])points, NT_SHAPE_SEGMENTS, true);
            }
        }
        if (type == NT_SHAPE_RECT) {
            static const float corners[4][4] = {{-0.5F, -0.5F, 0, 0}, {0.5F, -0.5F, 0, 0}, {0.5F, 0.5F, 0, 0}, {-0.5F, 0.5F, 0, 0}};
            wire_template_path(segments, index_count, corners, 4, true);
        } else if (type == NT_SHAPE_CUBE) {
            /* 12 independent edges: bottom face, top face, verticals. */
            static const uint8_t edges[12][2] = {{0, 1}, {1, 5}, {5, 4}, {4, 0}, {3, 2}, {2, 6}, {6, 7}, {7, 3}, {0, 3}, {1, 2}, {5, 6}, {4, 7}};
            for (int i = 0; i < 12; i++) {
                const float *a = k_cube_corners[edges[i][0]];
                const float *b = k_cube_corners[edges[i][1]];
                const float ends[2][4] = {{a[0], a[1], a[2], 0}, {b[0], b[1], b[2], 0}};
                wire_template_path(segments, index_count, ends, 2, false);
            }
        } else if (type == NT_SHAPE_CYLINDER) {
            for (int i = 0; i < 4; i++) {
                int k = i * (NT_SHAPE_SEGMENTS / 4);
                const float ends[2][4] = {{s_shape.cos_lut[k], 0.5F, s_shape.sin_lut[k], 0}, {s_shape.cos_lut[k], -0.5F, s_shape.sin_lut[k], 0}};
                wire_template_path(segments, index_count, ends, 2, false);
            }
        } else if (type == NT_SHAPE_CAPSULE) {
            for (int plane = 0; plane < 2; plane++) {
                wire_capsule_profile(points, plane);
                wire_template_path(segments, index_count, (const float(*)[4])points, NT_WIRE_CAP_POINTS, true);
            }
        }
        s_shape.wire_ranges[type] = (nt_shape_range_t){.first_index = first_index, .num_indices = *index_count - first_index, .num_vertices = (*segments - first) * 7};
    }
}

/* Appends one filled template (components 3 or 4 per vertex; w = 0 when 3) with its indices rebased. */
static void append_fill(int type, const float *verts, uint32_t nv, uint32_t components, const uint16_t *idx, uint32_t ni, uint32_t *vertex_count, uint32_t *index_count) {
    float(*dst)[4] = s_shape.line_staging.build.fill_vertices;
    uint16_t *dst_idx = s_shape.line_staging.build.fill_indices;
    NT_ASSERT(*vertex_count + nv <= NT_FILL_TEMPLATE_VERTICES && *index_count + ni <= NT_FILL_TEMPLATE_INDICES);
    for (uint32_t i = 0; i < nv; i++) {
        const float *v = &verts[(size_t)i * components];
        dst[*vertex_count + i][0] = v[0];
        dst[*vertex_count + i][1] = v[1];
        dst[*vertex_count + i][2] = v[2];
        dst[*vertex_count + i][3] = components == 4 ? v[3] : 0.0F;
    }
    for (uint32_t i = 0; i < ni; i++) {
        dst_idx[*index_count + i] = (uint16_t)(*vertex_count + idx[i]);
    }
    s_shape.fill_ranges[type] = (nt_shape_range_t){.first_index = *index_count, .num_indices = ni, .num_vertices = nv};
    *vertex_count += nv;
    *index_count += ni;
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void build_fill_templates(uint32_t *nv_total, uint32_t *ni_total) {
    /* Rect: unit quad in XY plane */
    {
        static const float v[] = {-0.5F, -0.5F, 0.0F, 0.5F, -0.5F, 0.0F, 0.5F, 0.5F, 0.0F, -0.5F, 0.5F, 0.0F};
        static const uint16_t i[] = {0, 1, 2, 0, 2, 3};
        append_fill(NT_SHAPE_RECT, v, 4, 3, i, 6, nv_total, ni_total);
    }

    /* Cube: unit cube ±0.5 */
    {
        static const uint16_t i[] = {0, 1, 2, 0, 2, 3, 5, 4, 7, 5, 7, 6, 4, 0, 3, 4, 3, 7, 1, 5, 6, 1, 6, 2, 3, 2, 6, 3, 6, 7, 4, 5, 1, 4, 1, 0};
        append_fill(NT_SHAPE_CUBE, &k_cube_corners[0][0], 8, 3, i, 36, nv_total, ni_total);
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
        append_fill(NT_SHAPE_CIRCLE, v, NT_SEG_CIRCLE_NV, 3, idx, NT_SEG_CIRCLE_NI, nv_total, ni_total);
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
        append_fill(NT_SHAPE_SPHERE, v, nv, 3, idx, ni, nv_total, ni_total);
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
        append_fill(NT_SHAPE_CYLINDER, v, nv, 3, idx, ni, nv_total, ni_total);
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
        append_fill(NT_SHAPE_CAPSULE, v, nv, 4, idx, ni, nv_total, ni_total);
    }
}
// #endregion

/* Neighbors let adjacent segments construct the same endpoint cross-section. */
static void emit_stroke(const float prev[3], const float a[3], const float b[3], const float next[3], float width, uint32_t color) {
    if (s_shape.stroke_count >= NT_SHAPE_RENDERER_MAX_POLYLINE_SEGMENTS) {
        nt_shape_renderer_flush();
    }
    nt_shape_stroke_instance_t *inst = &s_shape.strokes[s_shape.stroke_count++];
    memcpy(inst->prev, prev, sizeof(inst->prev));
    inst->width = width;
    memcpy(inst->next, next, sizeof(inst->next));
    memcpy(inst->a, a, sizeof(inst->a));
    memcpy(inst->b, b, sizeof(inst->b));
    inst->color = color;
}

void nt_shape_renderer_line(const float a[3], const float b[3], float width, uint32_t color) {
    assert_width(width);
    if (s_shape.line_count >= NT_SHAPE_RENDERER_MAX_LINES) {
        nt_shape_renderer_flush();
    }
    nt_shape_line_instance_t *inst = &s_shape.line_staging.lines[s_shape.line_count++];
    memcpy(inst->a, a, sizeof(inst->a));
    inst->width = width;
    memcpy(inst->b, b, sizeof(inst->b));
    inst->color = color;
}

/* ---- Lifecycle ---- */

static nt_buffer_t make_static_buffer(nt_buffer_type_t type, const void *data, uint32_t size, const char *label) {
    return nt_gfx_make_buffer(
        &(nt_buffer_desc_t){.type = type, .usage = NT_USAGE_IMMUTABLE, .data = data, .size = size, .index_type = type == NT_BUFFER_INDEX ? NT_INDEX_UINT16 : NT_INDEX_NONE, .label = label});
}

/* Straight line: a loss met on the way latches in gfx, every later create returns 0, and the next restore makes all again. */
/* The instanced kinds drawn from one sized stream, made when the stream is selected: one set per
 * stream keeps each VAO's instance offsets stable, and an unused stream takes no vertex-input
 * slots. Only missing ones are made: a reselected stream reuses its set, and after a loss all are made again. */
static void make_stream_vertex_inputs(uint32_t stream) {
    const uint32_t k = stream - NT_GFX_FRAME_VERTEX;
    if (!nt_gfx_vertex_input_valid(s_shape.gpu.fill_vi[k])) {
        s_shape.gpu.fill_vi[k] = nt_gfx_make_vertex_input(
            &(nt_vertex_input_desc_t){.layout = k_fill_template_layout, .vertex_buffer = s_shape.gpu.fill_vbo, .index_buffer = s_shape.gpu.fill_ibo, .label = "shape_fill_vi"});
    }
    if (!nt_gfx_vertex_input_valid(s_shape.gpu.wire_vi[k])) {
        s_shape.gpu.wire_vi[k] =
            nt_gfx_make_vertex_input(&(nt_vertex_input_desc_t){.layout = k_wire_vertex_layout, .vertex_buffer = s_shape.gpu.wire_vbo, .index_buffer = s_shape.gpu.wire_ibo, .label = "shape_wire_vi"});
    }
    if (!nt_gfx_vertex_input_valid(s_shape.gpu.line_vi[k])) {
        s_shape.gpu.line_vi[k] = nt_gfx_make_vertex_input(
            &(nt_vertex_input_desc_t){.layout = k_line_template_layout, .vertex_buffer = s_shape.gpu.line_vbo, .index_buffer = s_shape.gpu.line_ibo, .label = "shape_line_vi"});
    }
    /* Same buffers as line_vi, but its own VAO: one shared by two instance layouts would switch
     * layouts on every flush. */
    if (!nt_gfx_vertex_input_valid(s_shape.gpu.stroke_vi[k])) {
        s_shape.gpu.stroke_vi[k] = nt_gfx_make_vertex_input(
            &(nt_vertex_input_desc_t){.layout = k_line_template_layout, .vertex_buffer = s_shape.gpu.line_vbo, .index_buffer = s_shape.gpu.line_ibo, .label = "shape_stroke_vi"});
    }
}

static void create_gpu(void) {
    s_shape.gpu.fs = nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_FRAGMENT, .source = s_shape_fs_src, .label = "shape_fs"});
    s_shape.gpu.fill_vs = nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_VERTEX, .source = s_fill_vs_src, .label = "shape_fill_vs"});
    s_shape.gpu.batch_vs = nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_VERTEX, .source = s_shape_vs_src, .label = "shape_batch_vs"});
    s_shape.gpu.wire_vs = nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_VERTEX, .source = s_wire_vs_src, .label = "shape_wire_vs"});
    s_shape.gpu.line_vs = nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_VERTEX, .source = s_line_vs_src, .label = "shape_line_vs"});
    s_shape.gpu.fill_prog = nt_gfx_make_program(s_shape.gpu.fill_vs, s_shape.gpu.fs);
    s_shape.gpu.batch_prog = nt_gfx_make_program(s_shape.gpu.batch_vs, s_shape.gpu.fs);
    s_shape.gpu.wire_prog = nt_gfx_make_program(s_shape.gpu.wire_vs, s_shape.gpu.fs);
    s_shape.gpu.line_prog = nt_gfx_make_program(s_shape.gpu.line_vs, s_shape.gpu.fs);
    for (int depth = 0; depth < 2; depth++) {
        s_shape.gpu.fill_pip[depth] = make_fill_pipeline(s_shape.gpu.fill_prog, depth != 0, "shape_fill_pipeline");
        s_shape.gpu.batch_pip[depth] = make_fill_pipeline(s_shape.gpu.batch_prog, depth != 0, "shape_batch_pipeline");
        s_shape.gpu.wire_pip[depth] = make_stroke_pipeline(s_shape.gpu.wire_prog, depth != 0, "shape_wire_pipeline");
        s_shape.gpu.line_pip[depth] = make_stroke_pipeline(s_shape.gpu.line_prog, depth != 0, "shape_line_pipeline");
    }

    uint32_t fill_vertices = 0;
    uint32_t fill_indices = 0;
    build_fill_templates(&fill_vertices, &fill_indices);
    s_shape.gpu.fill_vbo = make_static_buffer(NT_BUFFER_VERTEX, s_shape.line_staging.build.fill_vertices, fill_vertices * 4U * (uint32_t)sizeof(float), "shape_fill_templates");
    s_shape.gpu.fill_ibo = make_static_buffer(NT_BUFFER_INDEX, s_shape.line_staging.build.fill_indices, fill_indices * (uint32_t)sizeof(uint16_t), "shape_fill_templates");
    uint32_t wire_segments = 0;
    uint32_t wire_indices = 0;
    build_wire_templates(&wire_segments, &wire_indices);
    s_shape.gpu.wire_vbo = make_static_buffer(NT_BUFFER_VERTEX, s_shape.line_staging.build.wire_vertices, wire_segments * 7U * (uint32_t)sizeof(nt_wire_vertex_t), "shape_wire_templates");
    s_shape.gpu.wire_ibo = make_static_buffer(NT_BUFFER_INDEX, s_shape.line_staging.build.wire_indices, wire_indices * (uint32_t)sizeof(uint16_t), "shape_wire_templates");
    static const float line_template_verts[] = {0, 0, 0, 1, 0, 2, 0, 3, 0, 4, 1, 0, 1, 1};
    s_shape.gpu.line_vbo = make_static_buffer(NT_BUFFER_VERTEX, line_template_verts, sizeof(line_template_verts), "shape_line_template");
    s_shape.gpu.line_ibo = make_static_buffer(NT_BUFFER_INDEX, s_stroke_indices, sizeof(s_stroke_indices), "shape_line_template");

    s_shape.gpu.batch_vi = nt_gfx_make_vertex_input(&(nt_vertex_input_desc_t){.layout = k_batch_layout, .vertex_buffer = nt_gfx_frame_buffer(NT_GFX_FRAME_VERTEX), .label = "shape_batch_vi"});
    make_stream_vertex_inputs(s_shape.stream);
}

/* A buffer takes its vertex inputs and a program its pipelines along: the instanced vertex inputs
 * die with their template buffers; the batch vertex input has only a borrowed one. */
static void destroy_gpu(void) {
    nt_gfx_destroy_vertex_input(s_shape.gpu.batch_vi);
    nt_gfx_destroy_buffer(s_shape.gpu.line_ibo);
    nt_gfx_destroy_buffer(s_shape.gpu.line_vbo);
    nt_gfx_destroy_buffer(s_shape.gpu.wire_ibo);
    nt_gfx_destroy_buffer(s_shape.gpu.wire_vbo);
    nt_gfx_destroy_buffer(s_shape.gpu.fill_ibo);
    nt_gfx_destroy_buffer(s_shape.gpu.fill_vbo);
    nt_gfx_destroy_program(s_shape.gpu.line_prog);
    nt_gfx_destroy_program(s_shape.gpu.wire_prog);
    nt_gfx_destroy_program(s_shape.gpu.batch_prog);
    nt_gfx_destroy_program(s_shape.gpu.fill_prog);
    nt_gfx_destroy_shader(s_shape.gpu.line_vs);
    nt_gfx_destroy_shader(s_shape.gpu.wire_vs);
    nt_gfx_destroy_shader(s_shape.gpu.batch_vs);
    nt_gfx_destroy_shader(s_shape.gpu.fill_vs);
    nt_gfx_destroy_shader(s_shape.gpu.fs);
    memset(&s_shape.gpu, 0, sizeof(s_shape.gpu));
}

static void drop_queues(void) {
    memset(s_shape.fill_counts, 0, sizeof(s_shape.fill_counts));
    memset(s_shape.wire_counts, 0, sizeof(s_shape.wire_counts));
    s_shape.vertex_count = 0;
    s_shape.stroke_count = 0;
    s_shape.line_count = 0;
}

void nt_shape_renderer_init(void) {
    /* Every flush draws from frame storage, and triangle batches always from the general stream. */
    NT_ASSERT(g_nt_gfx_frame_storage[NT_GFX_FRAME_VERTEX].capacity > 0 && "shape renderer: set nt_gfx_desc_t.frame_capacity[NT_GFX_FRAME_VERTEX]");
    memset(&s_shape, 0, sizeof(s_shape));
    build_trig_lut();
    s_shape.depth_enabled = true;
    s_shape.stream = NT_GFX_FRAME_VERTEX;
    s_shape.fill_layout = nt_gfx_make_instance_layout(&k_shape_instance_layout);
    s_shape.wire_layout = nt_gfx_make_instance_layout(&k_wire_instance_layout);
    s_shape.line_layout = nt_gfx_make_instance_layout(&k_line_instance_layout);
    s_shape.stroke_layout = nt_gfx_make_instance_layout(&k_stroke_instance_layout);
    create_gpu();
    s_shape.initialized = true;
}

void nt_shape_renderer_shutdown(void) {
    if (!s_shape.initialized) {
        return;
    }
    destroy_gpu();
    memset(&s_shape, 0, sizeof(s_shape));
}

/* Settings stay; queued shapes name objects the loss freed, and the template build reuses the line queue. */
void nt_shape_renderer_restore_gpu(void) {
    if (!s_shape.initialized) {
        return;
    }
    drop_queues();
    destroy_gpu();
    create_gpu();
}

/* Copies one kind's staging into frame storage and draws it instanced from there. */
static void draw_instances(nt_pipeline_t pipeline, nt_vertex_input_t vi, nt_instance_layout_t layout, const void *data, uint32_t bytes, nt_shape_range_t range, uint32_t count) {
    uint32_t offset = 0;
    memcpy(nt_gfx_frame_alloc(s_shape.stream, bytes, 4, &offset), data, bytes);
    nt_gfx_bind_pipeline(pipeline);
    nt_gfx_bind_vertex_input_instanced(vi, layout, nt_gfx_frame_buffer(s_shape.stream), offset);
    /* Unchanged settings end CACHE in gfx: no copy, and the batches around the bind still merge. */
    nt_gfx_bind_uniform_block(NT_GFX_SLOT_DRAW, &s_shape.draw, sizeof(s_shape.draw));
    nt_gfx_draw_indexed_instanced(range.first_index, range.num_indices, range.num_vertices, count);
}

void nt_shape_renderer_flush(void) {
    NT_ASSERT(s_shape.initialized && "nt_shape_renderer_flush: module is not initialized");
    /* Each program finishes linking on its own frame; until all do, queued shapes are dropped. */
    if (!nt_gfx_program_ready(s_shape.gpu.fill_prog) || !nt_gfx_program_ready(s_shape.gpu.batch_prog) || !nt_gfx_program_ready(s_shape.gpu.wire_prog) || !nt_gfx_program_ready(s_shape.gpu.line_prog)) {
        drop_queues();
        return;
    }
    const int d = s_shape.depth_enabled ? 1 : 0;
    const uint32_t k = s_shape.stream - NT_GFX_FRAME_VERTEX;
    for (int type = 0; type < NT_SHAPE_TYPE_COUNT; type++) {
        const uint32_t count = s_shape.fill_counts[type];
        if (count > 0) {
            draw_instances(s_shape.gpu.fill_pip[d], s_shape.gpu.fill_vi[k], s_shape.fill_layout, s_shape.fills[type], count * (uint32_t)sizeof(nt_shape_instance_t), s_shape.fill_ranges[type], count);
            s_shape.fill_counts[type] = 0;
        }
    }
    if (s_shape.vertex_count > 0) {
        const uint32_t bytes = s_shape.vertex_count * (uint32_t)sizeof(nt_shape_vertex_t);
        uint32_t offset = 0;
        memcpy(nt_gfx_frame_alloc(NT_GFX_FRAME_VERTEX, bytes, (uint32_t)sizeof(nt_shape_vertex_t), &offset), s_shape.vertices, bytes);
        nt_gfx_bind_pipeline(s_shape.gpu.batch_pip[d]);
        nt_gfx_bind_vertex_input(s_shape.gpu.batch_vi);
        nt_gfx_bind_uniform_block(NT_GFX_SLOT_DRAW, &s_shape.draw, sizeof(s_shape.draw));
        nt_gfx_draw(offset / (uint32_t)sizeof(nt_shape_vertex_t), s_shape.vertex_count);
        s_shape.vertex_count = 0;
    }
    /* Strokes last: within one flush, outlines stay on top of filled shapes. */
    for (int type = 0; type < NT_SHAPE_TYPE_COUNT; type++) {
        const uint32_t count = s_shape.wire_counts[type];
        if (count > 0) {
            draw_instances(s_shape.gpu.wire_pip[d], s_shape.gpu.wire_vi[k], s_shape.wire_layout, s_shape.wires[type], count * (uint32_t)sizeof(nt_shape_wire_instance_t), s_shape.wire_ranges[type],
                           count);
            s_shape.wire_counts[type] = 0;
        }
    }
    if (s_shape.stroke_count > 0) {
        draw_instances(s_shape.gpu.line_pip[d], s_shape.gpu.stroke_vi[k], s_shape.stroke_layout, s_shape.strokes, s_shape.stroke_count * (uint32_t)sizeof(nt_shape_stroke_instance_t),
                       (nt_shape_range_t){0, 12, 7}, s_shape.stroke_count);
        s_shape.stroke_count = 0;
    }
    if (s_shape.line_count > 0) {
        /* Independent lines skip the join triangles of the shared template. */
        draw_instances(s_shape.gpu.line_pip[d], s_shape.gpu.line_vi[k], s_shape.line_layout, s_shape.line_staging.lines, s_shape.line_count * (uint32_t)sizeof(nt_shape_line_instance_t),
                       (nt_shape_range_t){0, 6, 7}, s_shape.line_count);
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
    if (memcmp(s_shape.draw.vp, vp, sizeof(float) * 16) == 0) { // NOLINT — intentional bitwise dirty-check
        return;
    }
    nt_shape_renderer_flush();
    memcpy(s_shape.draw.vp, vp, sizeof(float) * 16);
    eye_from_vp(vp, s_shape.draw.eye);
}

void nt_shape_renderer_set_width_units_world(void) {
    if (s_shape.draw.pixel_scale[0] == 0.0F) {
        return;
    }
    nt_shape_renderer_flush();
    memset(s_shape.draw.pixel_scale, 0, sizeof(s_shape.draw.pixel_scale));
}

void nt_shape_renderer_set_width_units_pixels(uint32_t viewport_width, uint32_t viewport_height) {
    NT_ASSERT(viewport_width > 0 && viewport_height > 0);
    float x = 2.0F / (float)viewport_width;
    float y = 2.0F / (float)viewport_height;
    if (x == s_shape.draw.pixel_scale[0] && y == s_shape.draw.pixel_scale[1]) {
        return;
    }
    nt_shape_renderer_flush();
    s_shape.draw.pixel_scale[0] = x;
    s_shape.draw.pixel_scale[1] = y;
}

void nt_shape_renderer_set_depth(bool enabled) {
    if (enabled == s_shape.depth_enabled) {
        return;
    }
    nt_shape_renderer_flush();
    s_shape.depth_enabled = enabled;
}

void nt_shape_renderer_set_stream(uint32_t stream) {
    NT_ASSERT(stream >= NT_GFX_FRAME_VERTEX && stream < NT_GFX_FRAME_STREAM_COUNT && g_nt_gfx_frame_storage[stream].capacity > 0 && "set_stream: not a sized frame vertex stream");
    if ((uint8_t)stream == s_shape.stream) {
        return;
    }
    nt_shape_renderer_flush();
    s_shape.stream = (uint8_t)stream;
    make_stream_vertex_inputs(stream);
}

/* ---- Line ---- */

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

void nt_shape_renderer_polyline(const float (*points)[3], uint32_t count, bool closed, float width, uint32_t color) {
    NT_ASSERT(points != NULL || count == 0);
    assert_finite_points(points, count);
    assert_width(width);
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
        emit_stroke(points[prev], points[a], points[b], points[after], width, color);
        prev = a;
        a = b;
        b = next;
    }
    if (closed) {
        emit_stroke(points[prev], points[a], points[0], points[first_next], width, color);
    }
}

/* ---- Rectangle ---- */

void nt_shape_renderer_rect(const float pos[3], const float size[2], const float rot[4], uint32_t color) {
    float scale[3] = {size[0], size[1], 1.0F};
    push_instance(NT_SHAPE_RECT, pos, scale, rot, color);
}

void nt_shape_renderer_rect_wire(const float pos[3], const float size[2], const float rot[4], float width, uint32_t color) {
    float scale[3] = {size[0], size[1], 1.0F};
    push_wire_instance(NT_SHAPE_RECT, pos, scale, rot, width, color);
}

/* ---- Triangle ---- */

void nt_shape_renderer_triangle(const float a[3], const float b[3], const float c[3], uint32_t color) {
    if (s_shape.vertex_count + 3 > NT_SHAPE_RENDERER_MAX_VERTICES) {
        nt_shape_renderer_flush();
    }
    nt_shape_vertex_t *v = &s_shape.vertices[s_shape.vertex_count];
    v[0] = (nt_shape_vertex_t){{a[0], a[1], a[2]}, color};
    v[1] = (nt_shape_vertex_t){{b[0], b[1], b[2]}, color};
    v[2] = (nt_shape_vertex_t){{c[0], c[1], c[2]}, color};
    s_shape.vertex_count += 3;
}

void nt_shape_renderer_triangle_wire(const float a[3], const float b[3], const float c[3], float width, uint32_t color) {
    const float corners[3][3] = {{a[0], a[1], a[2]}, {b[0], b[1], b[2]}, {c[0], c[1], c[2]}};
    nt_shape_renderer_polyline(corners, 3, true, width, color);
}

/* ---- Circle ---- */

void nt_shape_renderer_circle(const float center[3], float radius, const float rot[4], uint32_t color) {
    float scale[3] = {radius, 1.0F, radius};
    push_instance(NT_SHAPE_CIRCLE, center, scale, rot, color);
}

void nt_shape_renderer_circle_wire(const float center[3], float radius, const float rot[4], float width, uint32_t color) {
    float scale[3] = {radius, 1.0F, radius};
    push_wire_instance(NT_SHAPE_CIRCLE, center, scale, rot, width, color);
}

/* ---- Cube ---- */

void nt_shape_renderer_cube(const float center[3], const float size[3], const float rot[4], uint32_t color) { push_instance(NT_SHAPE_CUBE, center, size, rot, color); }

void nt_shape_renderer_cube_wire(const float center[3], const float size[3], const float rot[4], float width, uint32_t color) { push_wire_instance(NT_SHAPE_CUBE, center, size, rot, width, color); }

/* ---- Sphere ---- */

void nt_shape_renderer_sphere(const float center[3], float radius, const float rot[4], uint32_t color) {
    float scale[3] = {radius, radius, radius};
    push_instance(NT_SHAPE_SPHERE, center, scale, rot, color);
}

void nt_shape_renderer_sphere_wire(const float center[3], float radius, const float rot[4], float width, uint32_t color) {
    float scale[3] = {radius, radius, radius};
    push_wire_instance(NT_SHAPE_SPHERE, center, scale, rot, width, color);
}

/* ---- Cylinder ---- */

void nt_shape_renderer_cylinder(const float center[3], float radius, float height, const float rot[4], uint32_t color) {
    float scale[3] = {radius, height, radius};
    push_instance(NT_SHAPE_CYLINDER, center, scale, rot, color);
}

void nt_shape_renderer_cylinder_wire(const float center[3], float radius, float height, const float rot[4], float width, uint32_t color) {
    float scale[3] = {radius, height, radius};
    push_wire_instance(NT_SHAPE_CYLINDER, center, scale, rot, width, color);
}

/* ---- Capsule ---- */

void nt_shape_renderer_capsule(const float center[3], float radius, float height, const float rot[4], uint32_t color) {
    float scale[3] = {radius, fmaxf(0.0F, (height - 2.0F * radius) * 0.5F), radius};
    push_instance(NT_SHAPE_CAPSULE, center, scale, rot, color);
}

void nt_shape_renderer_capsule_wire(const float center[3], float radius, float height, const float rot[4], float width, uint32_t color) {
    float body_half = fmaxf(0.0F, (height - 2.0F * radius) * 0.5F);
    float scale[3] = {radius, body_half > 0.0F ? body_half : radius, radius};
    push_wire_instance(body_half > 0.0F ? NT_SHAPE_CAPSULE : NT_SHAPE_SPHERE, center, scale, rot, width, color);
}

// #region test_access
#ifdef NT_TEST_ACCESS
uint32_t nt_shape_renderer_test_instance_count(int type) { return s_shape.fill_counts[type]; }
uint32_t nt_shape_renderer_test_vertex_count(void) { return s_shape.vertex_count; }
uint32_t nt_shape_renderer_test_stroke_count(void) {
    uint32_t count = s_shape.line_count + s_shape.stroke_count;
    for (int type = 0; type < NT_SHAPE_TYPE_COUNT; type++) {
        count += s_shape.wire_counts[type];
    }
    return count;
}
const float *nt_shape_renderer_test_vp(void) { return s_shape.draw.vp; }
const float *nt_shape_renderer_test_eye(void) { return s_shape.draw.eye; }
bool nt_shape_renderer_test_depth_enabled(void) { return s_shape.depth_enabled; }
bool nt_shape_renderer_test_initialized(void) { return s_shape.initialized; }
#endif
// #endregion
