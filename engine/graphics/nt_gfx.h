#ifndef NT_GFX_H
#define NT_GFX_H

#include "core/nt_types.h"

/* ---- Handle types (typed opaque handles backed by pool) ---- */

typedef struct {
    uint32_t id;
} nt_shader_t;

typedef struct {
    uint32_t id;
} nt_pipeline_t;

typedef struct {
    uint32_t id;
} nt_buffer_t;

/* ---- Enums ---- */

typedef enum {
    NT_SHADER_VERTEX = 0,
    NT_SHADER_FRAGMENT,
} nt_shader_type_t;

typedef enum {
    NT_BUFFER_VERTEX = 0,
    NT_BUFFER_INDEX,
} nt_buffer_type_t;

typedef enum {
    NT_USAGE_IMMUTABLE = 0, /* GL: STATIC_DRAW */
    NT_USAGE_DYNAMIC,       /* GL: DYNAMIC_DRAW */
    NT_USAGE_STREAM,        /* GL: STREAM_DRAW */
} nt_buffer_usage_t;

typedef enum {
    NT_FORMAT_FLOAT = 0,
    NT_FORMAT_FLOAT2,
    NT_FORMAT_FLOAT3,
    NT_FORMAT_FLOAT4,
    NT_FORMAT_UBYTE4N, /* normalized ubyte4 */
} nt_vertex_format_t;

typedef enum {
    NT_ATTR_POSITION = 0,
    NT_ATTR_NORMAL = 1,
    NT_ATTR_COLOR = 2,
    NT_ATTR_TEXCOORD0 = 3,
} nt_attr_location_t;

typedef enum {
    NT_BLEND_ZERO = 0,
    NT_BLEND_ONE,
    NT_BLEND_SRC_ALPHA,
    NT_BLEND_ONE_MINUS_SRC_ALPHA,
} nt_blend_factor_t;

typedef enum {
    NT_DEPTH_LESS = 0,
    NT_DEPTH_LEQUAL,
    NT_DEPTH_ALWAYS,
} nt_depth_func_t;

/* ---- Vertex layout ---- */

#define NT_GFX_MAX_VERTEX_ATTRS 16

typedef struct {
    uint8_t location;
    nt_vertex_format_t format;
    uint16_t offset;
} nt_vertex_attr_t;

typedef struct {
    nt_vertex_attr_t attrs[NT_GFX_MAX_VERTEX_ATTRS];
    uint8_t attr_count;
    uint16_t stride;
} nt_vertex_layout_t;

/* ---- Descriptor structs ---- */

typedef struct {
    uint32_t max_shaders;     /* default: 32 */
    uint32_t max_pipelines;   /* default: 16 */
    uint32_t max_buffers;     /* default: 128 */
    bool depth;               /* request depth buffer (default: true) */
    bool stencil;             /* request stencil buffer (default: false) */
    bool antialias;           /* MSAA (default: false) */
    bool alpha;               /* transparent canvas/window (default: false) */
    bool premultiplied_alpha; /* web only: canvas-to-page blending (default: true, ignored when alpha=false) */
} nt_gfx_desc_t;

typedef struct {
    nt_shader_type_t type;
    const char *source;
    const char *label;
} nt_shader_desc_t;

typedef struct {
    nt_shader_t vertex_shader;
    nt_shader_t fragment_shader;
    nt_vertex_layout_t layout;
    bool depth_test;
    bool depth_write;
    nt_depth_func_t depth_func;
    bool cull_face;
    bool blend;
    nt_blend_factor_t blend_src;
    nt_blend_factor_t blend_dst;
    bool polygon_offset;                /* enable GL_POLYGON_OFFSET_FILL */
    float polygon_offset_factor;        /* glPolygonOffset factor (typically 1.0) */
    float polygon_offset_units;         /* glPolygonOffset units (typically 1.0) */
    nt_vertex_layout_t instance_layout; /* per-instance vertex attributes (optional, divisor=1) */
    const char *label;
} nt_pipeline_desc_t;

typedef struct {
    nt_buffer_type_t type;
    nt_buffer_usage_t usage;
    const void *data;
    uint32_t size;
    const char *label;
} nt_buffer_desc_t;

typedef struct {
    float clear_color[4];
    float clear_depth; /* typically 1.0f; zero-init gives 0.0 which fails all depth tests */
} nt_pass_desc_t;

/* ---- Frame statistics ---- */

typedef struct {
    uint32_t draw_calls;
    uint32_t vertices;
    uint32_t indices;
    uint32_t instances; /* total objects drawn via instanced calls */
} nt_gfx_frame_stats_t;

/* ---- Global state ---- */

typedef struct {
    nt_gfx_frame_stats_t frame_stats;
    bool context_lost;
    bool context_restored;
    bool initialized;
} nt_gfx_t;

extern nt_gfx_t g_nt_gfx;

/* ---- Defaults ---- */

static inline nt_gfx_desc_t nt_gfx_desc_defaults(void) {
    return (nt_gfx_desc_t){
        .max_shaders = 32,
        .max_pipelines = 16,
        .max_buffers = 128,
        .depth = true,
        .premultiplied_alpha = true,
    };
}

/* ---- Lifecycle ---- */

void nt_gfx_init(const nt_gfx_desc_t *desc);
void nt_gfx_shutdown(void);

/* ---- Frame / Pass ---- */

void nt_gfx_begin_frame(void);
void nt_gfx_end_frame(void);
void nt_gfx_begin_pass(const nt_pass_desc_t *desc);
void nt_gfx_end_pass(void);

/* ---- Resource creation ---- */

nt_shader_t nt_gfx_make_shader(const nt_shader_desc_t *desc);
nt_pipeline_t nt_gfx_make_pipeline(const nt_pipeline_desc_t *desc);
nt_buffer_t nt_gfx_make_buffer(const nt_buffer_desc_t *desc);

/* ---- Resource destruction ---- */

void nt_gfx_destroy_shader(nt_shader_t shd);
void nt_gfx_destroy_pipeline(nt_pipeline_t pip);
void nt_gfx_destroy_buffer(nt_buffer_t buf);

/* ---- Draw state ---- */

void nt_gfx_bind_pipeline(nt_pipeline_t pip);
void nt_gfx_bind_vertex_buffer(nt_buffer_t buf);
void nt_gfx_bind_index_buffer(nt_buffer_t buf);

/* ---- Uniforms ---- */

void nt_gfx_set_uniform_mat4(const char *name, const float *matrix);
void nt_gfx_set_uniform_vec4(const char *name, const float *vec);
void nt_gfx_set_uniform_float(const char *name, float val);
void nt_gfx_set_uniform_int(const char *name, int val);

/* ---- Draw calls ---- */

void nt_gfx_draw(uint32_t first_vertex, uint32_t num_vertices);
void nt_gfx_draw_indexed(uint32_t first_index, uint32_t num_indices);
void nt_gfx_draw_indexed_instanced(uint32_t first_index, uint32_t num_indices, uint32_t instance_count);

/* ---- Instance buffer ---- */

void nt_gfx_bind_instance_buffer(nt_buffer_t buf);

/* ---- Buffer update ---- */

void nt_gfx_update_buffer(nt_buffer_t buf, const void *data, uint32_t size);

#endif /* NT_GFX_H */
