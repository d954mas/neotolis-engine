#ifndef NT_GFX_H
#define NT_GFX_H

#include "core/nt_assert.h"
#include "core/nt_types.h"
#include "hash/nt_hash.h"
#include "nt_mesh_format.h"
#include "nt_texture_format.h"

#include <stddef.h>
#include <string.h>

/* ---- Index buffer type constants ---- */

#define NT_INDEX_NONE 0
#define NT_INDEX_UINT16 1
#define NT_INDEX_UINT32 2

/* ---- Handle types (typed opaque handles backed by pool) ---- */

typedef struct {
    uint32_t id;
} nt_shader_t;

/* Linked stage pair owned by the caller; destroy with nt_gfx_destroy_program.
 * Linking never deduplicates. Pipelines and materials borrow the handle.
 * Linkage is immutable; context recovery
 * requires a newly linked program. */
typedef struct {
    uint32_t id;
} nt_program_t;

typedef struct {
    uint32_t id;
} nt_pipeline_t;

typedef struct {
    uint32_t id;
} nt_buffer_t;

typedef struct {
    uint32_t id;
} nt_texture_t;

typedef struct {
    uint32_t id;
} nt_render_target_t;

typedef struct {
    uint32_t id;
} nt_mesh_t;

/* Owned vertex-input object (GL: a VAO): vertex layout + optional instance
 * layout baked against a VBO [+ IBO]. The static half (vertex attrs + index
 * buffer) is immutable after creation; the instance attribute pointers are
 * re-specified into the bound object by each nt_gfx_bind_instance_buffer. */
typedef struct {
    uint32_t id;
} nt_vertex_input_t;

#define NT_RENDER_TARGET_INVALID ((nt_render_target_t){0})
#define NT_MESH_INVALID ((nt_mesh_t){0})
#define NT_PROGRAM_INVALID ((nt_program_t){0})
#define NT_VERTEX_INPUT_INVALID ((nt_vertex_input_t){0})

/* Sampler object — texture-side filter/wrap state decoupled from the texture
 * itself. One texture can be sampled with different filters in different
 * materials by binding different samplers to the same texture unit. */
typedef struct {
    uint32_t id;
} nt_sampler_t;

#define NT_SAMPLER_INVALID ((nt_sampler_t){0})
/* Same value, read as "no override": nt_gfx_bind_texture then uses the texture's asset-baked default. */
#define NT_SAMPLER_DEFAULT NT_SAMPLER_INVALID

/* ---- Global UBO block registry (compile-time limit) ---- */

#define NT_GFX_MAX_GLOBAL_BLOCKS 8

/* Samplers are deduplicated by their (filter/wrap/compare) descriptor; most apps
 * use 3-10 unique configs. 128 is headroom, not coverage — all 324 combinations
 * are constructible. Costs ~4 KB of BSS, not binary size; the linear scan
 * iterates sampler_count, not capacity, so capacity is free for the hot path. */
#define NT_GFX_MAX_SAMPLERS 128

typedef struct {
    const char *name; /* borrowed unchanged until nt_gfx_shutdown */
    uint32_t binding_slot;
    bool active;
} nt_global_block_t;

/* ---- Mesh info (side table for VBO+IBO pairs from mesh activator) ---- */

typedef struct {
    nt_buffer_t vbo;
    nt_buffer_t ibo;
    uint32_t vertex_count;
    uint32_t index_count;
    uint8_t stream_count;
    uint8_t index_type;                        /* 0=none, 1=uint16, 2=uint32 */
    NtStreamDesc streams[NT_MESH_MAX_STREAMS]; /* copied from pack data at activation */
    uint16_t stride;                           /* total vertex size in bytes */
} nt_gfx_mesh_info_t;

/* ---- Enums ---- */

typedef enum {
    NT_SHADER_VERTEX = 0,
    NT_SHADER_FRAGMENT,
} nt_shader_type_t;

typedef enum {
    NT_BUFFER_VERTEX = 0,
    NT_BUFFER_INDEX,
    NT_BUFFER_UNIFORM,
} nt_buffer_type_t;

typedef enum {
    NT_USAGE_IMMUTABLE = 0, /* GL: STATIC_DRAW */
    NT_USAGE_DYNAMIC,       /* GL: DYNAMIC_DRAW */
    NT_USAGE_STREAM,        /* GL: STREAM_DRAW */
} nt_buffer_usage_t;

/* Vertex attribute component type. With count (1-4) and normalized this spans
 * the vertexAttribPointer space over float/half/byte/short types -- no enum of
 * allowed combinations. (No int32 or 2_10_10_2 packed types.) */
typedef enum {
    NT_VERTEX_FLOAT = 0, /* GL_FLOAT, 4 bytes */
    NT_VERTEX_HALF,      /* GL_HALF_FLOAT, 2 bytes */
    NT_VERTEX_UINT8,     /* GL_UNSIGNED_BYTE, 1 byte */
    NT_VERTEX_INT8,      /* GL_BYTE, 1 byte */
    NT_VERTEX_UINT16,    /* GL_UNSIGNED_SHORT, 2 bytes */
    NT_VERTEX_INT16,     /* GL_SHORT, 2 bytes */
} nt_vertex_type_t;

/* Byte size of one component of a vertex attribute type (0 for invalid values) */
static inline uint16_t nt_vertex_type_size(nt_vertex_type_t type) {
    static const uint16_t sizes[] = {4, 2, 1, 1, 2, 2};
    return ((uint32_t)type < 6) ? sizes[type] : 0;
}

typedef enum {
    NT_ATTR_POSITION = 0,
    NT_ATTR_NORMAL = 1,
    NT_ATTR_COLOR = 2,
    NT_ATTR_TEXCOORD0 = 3,
} nt_attr_location_t;

typedef uint8_t nt_blend_factor_t;
enum {
    NT_BLEND_ZERO = 0,
    NT_BLEND_ONE,
    NT_BLEND_SRC_COLOR,
    NT_BLEND_ONE_MINUS_SRC_COLOR,
    NT_BLEND_DST_COLOR,
    NT_BLEND_ONE_MINUS_DST_COLOR,
    NT_BLEND_SRC_ALPHA,
    NT_BLEND_ONE_MINUS_SRC_ALPHA,
    NT_BLEND_DST_ALPHA,
    NT_BLEND_ONE_MINUS_DST_ALPHA,
    NT_BLEND_CONSTANT_COLOR,
    NT_BLEND_ONE_MINUS_CONSTANT_COLOR,
    NT_BLEND_CONSTANT_ALPHA,
    NT_BLEND_ONE_MINUS_CONSTANT_ALPHA,
    NT_BLEND_SRC_ALPHA_SATURATE,
};

typedef uint8_t nt_blend_op_t;
enum {
    NT_BLEND_OP_ADD = 0,
    NT_BLEND_OP_SUBTRACT,
    NT_BLEND_OP_REVERSE_SUBTRACT,
    NT_BLEND_OP_MIN,
    NT_BLEND_OP_MAX,
};

typedef struct {
    float constant_color[4];
    nt_blend_factor_t src_rgb;
    nt_blend_factor_t dst_rgb;
    nt_blend_factor_t src_alpha;
    nt_blend_factor_t dst_alpha;
    nt_blend_op_t op_rgb;
    nt_blend_op_t op_alpha;
    bool enabled;
    uint8_t _reserved;
} nt_blend_state_t;

_Static_assert(sizeof(nt_blend_state_t) == 24, "nt_blend_state_t layout changed");

/* Presets return ordinary structs; callers may override any field. */
static inline nt_blend_state_t nt_blend_opaque(void) { return (nt_blend_state_t){0}; }

static inline nt_blend_state_t nt_blend_alpha(void) {
    return (nt_blend_state_t){
        .src_rgb = NT_BLEND_SRC_ALPHA,
        .dst_rgb = NT_BLEND_ONE_MINUS_SRC_ALPHA,
        .src_alpha = NT_BLEND_ONE,
        .dst_alpha = NT_BLEND_ONE_MINUS_SRC_ALPHA,
        .op_rgb = NT_BLEND_OP_ADD,
        .op_alpha = NT_BLEND_OP_ADD,
        .enabled = true,
    };
}

static inline nt_blend_state_t nt_blend_alpha_premultiplied(void) {
    nt_blend_state_t blend = nt_blend_alpha();
    blend.src_rgb = NT_BLEND_ONE;
    return blend;
}

static inline nt_blend_state_t nt_blend_additive(void) {
    return (nt_blend_state_t){
        .src_rgb = NT_BLEND_SRC_ALPHA,
        .dst_rgb = NT_BLEND_ONE,
        .src_alpha = NT_BLEND_ZERO,
        .dst_alpha = NT_BLEND_ONE,
        .op_rgb = NT_BLEND_OP_ADD,
        .op_alpha = NT_BLEND_OP_ADD,
        .enabled = true,
    };
}

static inline nt_blend_state_t nt_blend_additive_premultiplied(void) {
    nt_blend_state_t blend = nt_blend_additive();
    blend.src_rgb = NT_BLEND_ONE;
    return blend;
}

static inline nt_blend_state_t nt_blend_subtractive(void) {
    nt_blend_state_t blend = nt_blend_additive();
    blend.op_rgb = NT_BLEND_OP_REVERSE_SUBTRACT;
    return blend;
}

static inline nt_blend_state_t nt_blend_subtractive_premultiplied(void) {
    nt_blend_state_t blend = nt_blend_additive_premultiplied();
    blend.op_rgb = NT_BLEND_OP_REVERSE_SUBTRACT;
    return blend;
}

static inline nt_blend_state_t nt_blend_multiply(void) {
    return (nt_blend_state_t){
        .src_rgb = NT_BLEND_DST_COLOR,
        .dst_rgb = NT_BLEND_ZERO,
        .src_alpha = NT_BLEND_ZERO,
        .dst_alpha = NT_BLEND_ONE,
        .op_rgb = NT_BLEND_OP_ADD,
        .op_alpha = NT_BLEND_OP_ADD,
        .enabled = true,
    };
}

typedef enum {
    NT_DEPTH_LESS = 0,
    NT_DEPTH_LEQUAL,
    NT_DEPTH_ALWAYS,
} nt_depth_func_t;

typedef enum {
    NT_FILTER_NEAREST = 0,
    NT_FILTER_LINEAR,
    NT_FILTER_NEAREST_MIPMAP_NEAREST,
    NT_FILTER_LINEAR_MIPMAP_NEAREST,
    NT_FILTER_NEAREST_MIPMAP_LINEAR,
    NT_FILTER_LINEAR_MIPMAP_LINEAR,
} nt_texture_filter_t;

typedef enum {
    NT_WRAP_CLAMP_TO_EDGE = 0,
    NT_WRAP_REPEAT,
    NT_WRAP_MIRRORED_REPEAT,
} nt_texture_wrap_t;

/* Depth comparison on sampler objects. NONE is zero so a zero-filled descriptor
 * is a plain sampler; enabling comparison names its function, since LEQUAL and
 * LESS differ exactly on a receiver at its own stored depth. */
typedef enum {
    NT_COMPARE_NONE = 0,
    NT_COMPARE_LEQUAL,
    NT_COMPARE_LESS,
} nt_compare_func_t;

typedef enum {
    NT_RT_DEPTH_NONE = 0,
    NT_RT_DEPTH_BUFFER,
    NT_RT_DEPTH_TEXTURE,
} nt_render_target_depth_t;

/* ---- Vertex layout ---- */

#define NT_GFX_MAX_VERTEX_ATTRS 16
/* Instance layouts are capped tighter: the backend keeps a per-vertex-input
 * copy for per-draw re-pointing, and max_vertex_inputs slots exist. */
#define NT_GFX_MAX_INSTANCE_ATTRS 8
#define NT_GFX_MAX_TEXTURE_SLOTS 8

typedef struct {
    uint8_t location;
    nt_vertex_type_t type;
    uint8_t count;   /* components per attribute, 1-4 */
    bool normalized; /* integer types only: map to [0,1] / [-1,1] */
    uint16_t offset;
} nt_vertex_attr_t;

typedef struct {
    nt_vertex_attr_t attrs[NT_GFX_MAX_VERTEX_ATTRS];
    uint8_t attr_count;
    uint16_t stride;
} nt_vertex_layout_t;

/* ---- Descriptor structs ---- */

typedef struct {
    uint16_t max_shaders;   /* default: 32 */
    uint16_t max_programs;  /* default: 16 */
    uint16_t max_pipelines; /* default: 16 */
    uint16_t max_buffers;   /* default: 128 */
    uint16_t max_textures;  /* default: 64 */
    uint16_t max_meshes;    /* default: 128 */
    /* default: 560 = max_meshes(128) x mesh renderer max_mesh_layouts(4)
     * worst case + 48 for renderer-owned vertex inputs (shape ~14, text,
     * blur, ~32 sprite custom layouts). Scale it together with max_meshes;
     * raise it near the sprite custom-layout hardcap (64). */
    uint16_t max_vertex_inputs;
    uint16_t max_render_targets; /* default: 16 */
    bool depth;                  /* request depth buffer (default: true) */
    bool stencil;                /* request stencil buffer (default: false) */
    bool antialias;              /* MSAA (default: false) */
    bool alpha;                  /* transparent canvas/window (default: false) */
    bool premultiplied_alpha;    /* web only: canvas-to-page blending (default: true, ignored when alpha=false) */
} nt_gfx_desc_t;

typedef struct {
    nt_shader_type_t type;
    const char *source;
    const char *label;
} nt_shader_desc_t;

/* Program + fixed render state only; vertex input is a separate owned object
 * (nt_vertex_input_desc_t) bound independently of the pipeline. */
typedef struct {
    /* Borrowed; destroying the program also destroys this pipeline. */
    nt_program_t program;
    bool depth_test;
    bool depth_write;
    nt_depth_func_t depth_func;
    uint8_t cull_mode; /* 0=none, 1=back, 2=front (matches nt_cull_mode_t) */
    nt_blend_state_t blend;
    bool polygon_offset;         /* enable GL_POLYGON_OFFSET_FILL */
    float polygon_offset_factor; /* glPolygonOffset factor (typically 1.0) */
    float polygon_offset_units;  /* glPolygonOffset units (typically 1.0) */
    const char *label;           /* keep last: nt_gfx_pipeline_key packs everything before it */
} nt_pipeline_desc_t;

/* ---- Pipeline identity ----
 * Exact identity of a desc, label excluded: equal keys <=> identical baked state.
 * `bits` packs every enum/bool lane; the float payloads ride along as bit patterns. */
typedef struct {
    uint64_t bits;
    uint32_t float_bits[6]; /* blend.constant_color, polygon_offset_factor, polygon_offset_units */
} nt_gfx_pipeline_key_t;

/* Lane widths. The packer asserts each input fits: an out-of-range value must not
 * truncate onto a valid neighbour and skip make_pipeline's validation on a cache hit. */
_Static_assert(NT_BLEND_SRC_ALPHA_SATURATE < 16, "blend factor lane is 4 bits");
_Static_assert(NT_BLEND_OP_MAX < 8, "blend op lane is 3 bits");
_Static_assert(NT_DEPTH_ALWAYS < 4, "depth func lane is 2 bits");
/* Partial tripwire for the packer: catches a desc field that moves a later offset or the
 * size. A field that fits an existing padding hole moves nothing -- review by hand. */
_Static_assert(offsetof(nt_pipeline_desc_t, depth_func) == 8 && offsetof(nt_pipeline_desc_t, cull_mode) == 12 && offsetof(nt_pipeline_desc_t, blend) == 16 &&
                   offsetof(nt_pipeline_desc_t, polygon_offset) == 40 && offsetof(nt_pipeline_desc_t, polygon_offset_factor) == 44 && offsetof(nt_pipeline_desc_t, polygon_offset_units) == 48 &&
                   offsetof(nt_pipeline_desc_t, label) == (sizeof(void *) == 8 ? 56 : 52) && sizeof(nt_pipeline_desc_t) == (sizeof(void *) == 8 ? 64 : 56),
               "nt_pipeline_desc_t changed -- update nt_gfx_pipeline_key");

/* desc / a / b are required and borrowed for the call; NULL asserts. */
nt_gfx_pipeline_key_t nt_gfx_pipeline_key(const nt_pipeline_desc_t *desc);

/* `bits` first: a miss costs one word, the floats are read only on a match. */
static inline bool nt_gfx_pipeline_key_equal(const nt_gfx_pipeline_key_t *a, const nt_gfx_pipeline_key_t *b) {
    NT_ASSERT(a != NULL && b != NULL);
    return a->bits == b->bits && memcmp(a->float_bits, b->float_bits, sizeof(a->float_bits)) == 0;
}

typedef struct {
    nt_vertex_layout_t layout;          /* per-vertex attrs, divisor 0; attr_count 0 = attribute-less (gl_VertexID) */
    nt_vertex_layout_t instance_layout; /* optional per-instance attrs, divisor 1; pointers set per draw by nt_gfx_bind_instance_buffer */
    nt_buffer_t vertex_buffer;          /* NT_BUFFER_VERTEX; required iff layout.attr_count > 0 */
    nt_buffer_t index_buffer;           /* optional ({0} = non-indexed); NT_BUFFER_INDEX with index_type != NT_INDEX_NONE */
    const char *label;                  /* optional debug name; borrowed for the call */
} nt_vertex_input_desc_t;

typedef struct {
    nt_buffer_type_t type;
    nt_buffer_usage_t usage;
    const void *data;
    uint32_t size;
    uint8_t index_type; /* INDEX buffers: NT_INDEX_NONE/NT_INDEX_UINT16/NT_INDEX_UINT32 */
    const char *label;
} nt_buffer_desc_t;

typedef struct {
    uint16_t width;
    uint16_t height;
    const void *data;               /* raw pixel data; DEPTH* requires NULL */
    nt_texture_format_t format;     /* required */
    nt_texture_filter_t min_filter; /* default: NEAREST; RG16UI/DEPTH* require NEAREST */
    nt_texture_filter_t mag_filter; /* default: NEAREST; RG16UI/DEPTH* require NEAREST */
    nt_texture_wrap_t wrap_u;       /* default: NT_WRAP_CLAMP_TO_EDGE */
    nt_texture_wrap_t wrap_v;       /* default: NT_WRAP_CLAMP_TO_EDGE */
    bool gen_mipmaps;               /* DEPTH* requires false */
    const char *label;
} nt_texture_desc_t;

typedef struct {
    nt_texture_filter_t min_filter; /* default: NT_FILTER_LINEAR */
    nt_texture_filter_t mag_filter; /* default: NT_FILTER_LINEAR (NEAREST or LINEAR only) */
    nt_texture_wrap_t wrap_u;       /* default: NT_WRAP_CLAMP_TO_EDGE */
    nt_texture_wrap_t wrap_v;       /* default: NT_WRAP_CLAMP_TO_EDGE */
    /* Comparison lives on the sampler, not the texture: one depth target reads
     * through a comparison sampler for the shadow lookup and through a plain
     * sampler for a raw-depth view. Non-NONE requires DEPTH* storage. */
    nt_compare_func_t compare_func;
    const char *label; /* debug name; static storage */
} nt_sampler_desc_t;

typedef struct {
    uint16_t width;
    uint16_t height;
    nt_texture_format_t color_format;
    nt_texture_filter_t color_min_filter;
    nt_texture_filter_t color_mag_filter;
    nt_texture_wrap_t color_wrap_u;
    nt_texture_wrap_t color_wrap_v;
    nt_render_target_depth_t depth_storage;
    nt_texture_format_t depth_format;             /* INVALID for NONE; DEPTH* otherwise */
    nt_texture_filter_t depth_texture_min_filter; /* TEXTURE only; NEAREST — comparison is sampler state */
    nt_texture_filter_t depth_texture_mag_filter; /* TEXTURE only; NEAREST — comparison is sampler state */
    nt_texture_wrap_t depth_texture_wrap_u;       /* TEXTURE only */
    nt_texture_wrap_t depth_texture_wrap_v;       /* TEXTURE only */
    const char *label;                            /* debug name; static storage */
} nt_render_target_desc_t;

typedef struct {
    nt_render_target_t target; /* zero selects the default framebuffer */
    float clear_color[4];
    /* Applied regardless of the previous pipeline's depth_write state.
     * Typically 1.0f; zero-init gives 0.0 which fails all depth tests. */
    float clear_depth;
} nt_pass_desc_t;

/* ---- Frame statistics ---- */

typedef struct {
    uint32_t draw_calls;           /* all GPU draw calls */
    uint32_t draw_calls_instanced; /* of those, instanced */
    uint32_t vertices;
    uint32_t indices;
    uint32_t instances; /* total objects drawn via instanced calls */
} nt_gfx_frame_stats_t;

/* ---- GPU format capabilities ---- */

typedef struct {
    bool has_astc;                /* ASTC 4x4 LDR (WEBGL_compressed_texture_astc / KHR_texture_compression_astc_ldr) */
    bool has_bc7;                 /* BC7 / BPTC (EXT_texture_compression_bptc / ARB_texture_compression_bptc) */
    bool has_etc2;                /* ETC2 + EAC (WEBGL_compressed_texture_etc / core GL 4.3+) */
    bool has_float_render_target; /* RGBA16F as a colour attachment (EXT_color_buffer_float / core GL 3.0+) */
    uint32_t max_texture_size;    /* GL_MAX_TEXTURE_SIZE, queried at init */
} nt_gfx_gpu_caps_t;

/* ---- Global state ---- */

typedef struct {
    nt_gfx_frame_stats_t frame_stats;
    nt_gfx_gpu_caps_t gpu_caps;
    bool context_lost;
    bool context_restored;
    bool initialized;
} nt_gfx_t;

extern nt_gfx_t g_nt_gfx;

/* ---- Defaults ---- */

static inline nt_gfx_desc_t nt_gfx_desc_defaults(void) {
    return (nt_gfx_desc_t){
        .max_shaders = 32,
        .max_programs = 16,
        .max_pipelines = 16,
        .max_buffers = 128,
        .max_textures = 64,
        .max_meshes = 128,
        .max_vertex_inputs = 560,
        .max_render_targets = 16,
        .depth = true,
        .premultiplied_alpha = true,
    };
}

/* ---- Global UBO block registration ---- */

/* Registers the block binding for existing and future programs.
 * name is required and borrowed unchanged until nt_gfx_shutdown; gfx never frees it. */
void nt_gfx_register_global_block(const char *name, uint32_t binding_slot);
void nt_gfx_get_global_blocks(const nt_global_block_t **blocks, uint32_t *count);

/* ---- Lifecycle ---- */

/* nt_gfx_stub has no resources: creates return INVALID, queries are empty and
 * commands are inert. Headless callers omit renderer initialization and draws. */
void nt_gfx_init(const nt_gfx_desc_t *desc);
void nt_gfx_shutdown(void);

/* GPU format capabilities (valid after nt_gfx_init) */
const nt_gfx_gpu_caps_t *nt_gfx_gpu_caps(void);

/* ---- Frame / Pass ---- */

void nt_gfx_begin_frame(void);
void nt_gfx_end_frame(void);
void nt_gfx_begin_pass(const nt_pass_desc_t *desc);
void nt_gfx_end_pass(void);

/* ---- Resource creation ---- */

nt_shader_t nt_gfx_make_shader(const nt_shader_desc_t *desc);
/* Links valid stages. Link errors, >16 non-sampler uniforms and >NT_GFX_MAX_TEXTURE_SLOTS samplers assert.
 * Returns invalid while the context is lost / begin_frame has not finished recovery, and for a live stage
 * whose GPU object a loss discarded -- recreate the stages and relink. Only a stale stage handle asserts. */
nt_program_t nt_gfx_make_program(nt_shader_t vs, nt_shader_t fs);
/* Creation preserves the currently bound pipeline. */
nt_pipeline_t nt_gfx_make_pipeline(const nt_pipeline_desc_t *desc);
/* Caller owns the result; destroy it with nt_gfx_destroy_vertex_input. The VI
 * borrows its buffers; creation borrows desc/label and preserves the bound VI.
 * INVALID means context loss or backend allocation failure; caller errors assert. */
nt_vertex_input_t nt_gfx_make_vertex_input(const nt_vertex_input_desc_t *desc);
nt_buffer_t nt_gfx_make_buffer(const nt_buffer_desc_t *desc);
nt_texture_t nt_gfx_make_texture(const nt_texture_desc_t *desc);
nt_sampler_t nt_gfx_make_sampler(const nt_sampler_desc_t *desc);
/* The descriptor is copied. Attachment textures belong to the target and
 * remain valid until nt_gfx_destroy_render_target(). */
nt_render_target_t nt_gfx_make_render_target(const nt_render_target_desc_t *desc);

/* ---- Resource destruction ---- */

/* Already linked programs remain usable after their stages are destroyed. */
void nt_gfx_destroy_shader(nt_shader_t shd);
/* Destroys the program and its pipelines; materials retain the now-unready handle.
 * Renderer caches drop dead entries on insertion/reset. INVALID is a no-op; stale nonzero handles assert.
 * Clear
 * the caller's handle to NT_PROGRAM_INVALID after destruction. */
void nt_gfx_destroy_program(nt_program_t prog);
/* Invalid and stale handles are no-ops: program destruction also destroys
 * pipelines, and a context loss frees every pipeline slot. */
void nt_gfx_destroy_pipeline(nt_pipeline_t pip);
/* Invalid and stale handles are no-ops because buffer destruction also
 * destroys dependent vertex inputs (see nt_gfx_destroy_buffer). */
void nt_gfx_destroy_vertex_input(nt_vertex_input_t vi);
/* Destroys VIs that borrow this vertex/index buffer. Destroying a captured
 * instance buffer instead makes every draw assert until it is re-pointed;
 * GL retains the old storage until that re-point or the VI's destruction. */
void nt_gfx_destroy_buffer(nt_buffer_t buf);
void nt_gfx_destroy_texture(nt_texture_t tex);
void nt_gfx_destroy_render_target(nt_render_target_t rt);
/* Samplers have no destroy: nt_gfx_make_sampler dedupes against an internal
 * cache (NT_GFX_MAX_SAMPLERS), and all cached samplers are released by
 * nt_gfx_shutdown. The shared lifetime is intentional — multiple materials
 * and textures reference the same sampler handle. */

/* Resize preserves logical target and attachment handles; pixels become undefined. */
bool nt_gfx_resize_render_target(nt_render_target_t rt, uint16_t width, uint16_t height);
nt_texture_t nt_gfx_render_target_color(nt_render_target_t rt);
/* Returns invalid unless the target was created with NT_RT_DEPTH_TEXTURE. */
nt_texture_t nt_gfx_render_target_depth(nt_render_target_t rt);
/* False after a context restore that could not recreate the target (a runtime GPU
 * failure, same class as creation returning invalid). No automatic retry: the owner
 * destroys and recreates it, or falls back. */
bool nt_gfx_render_target_ready(nt_render_target_t rt);
bool nt_gfx_texture_ready(nt_texture_t tex);
/* Reports a live stage backend. Readiness lost to context loss never returns for that handle;
 * re-read the new handle from its resource after reactivation. */
bool nt_gfx_shader_ready(nt_shader_t shd);
/* Reports a live pool slot, independent of GPU readiness. */
bool nt_gfx_program_valid(nt_program_t prog);
/* Reports a live pipeline slot; false after pipeline or program destruction,
 * or a context loss (loss frees every pipeline slot -- pipelines are baked
 * objects, like vertex inputs). Renderer caches check this on lookup. */
bool nt_gfx_pipeline_valid(nt_pipeline_t pip);
/* Reports a live vertex-input slot; false after direct destruction, the
 * destroy_buffer cascade, or a context loss (loss frees every vertex-input
 * slot -- they are baked objects with no re-fill path). Renderer caches
 * check this on lookup and self-heal. */
bool nt_gfx_vertex_input_valid(nt_vertex_input_t vi);
/* Reports a live program backend, required by nt_gfx_make_pipeline.
 * Readiness lost to context loss never returns for that handle. */
bool nt_gfx_program_ready(nt_program_t prog);
/* Sampler uniforms are program state: units are fixed at link, nobody writes
 * them. Returns the unit the named sampler reads from, or -1 when the program
 * has no active sampler of that name. Program must be ready (asserted). */
int nt_gfx_program_sampler_unit(nt_program_t prog, nt_hash32_t name);
/* Every unit the program samples, as 1<<unit bits -- the interface a material
 * must cover. Program must be ready (asserted). */
uint32_t nt_gfx_program_sampler_mask(nt_program_t prog);
/* The program the pipeline borrows; INVALID for an invalid or stale pipeline. */
nt_program_t nt_gfx_pipeline_program(nt_pipeline_t pip);
/* Writes logical dimensions. Outputs are required; invalid handles write zero and return false. */
bool nt_gfx_texture_size(nt_texture_t tex, uint16_t *out_width, uint16_t *out_height);
/* Returns INVALID for invalid or stale handles. */
nt_texture_format_t nt_gfx_texture_format(nt_texture_t tex);

/* ---- Draw state ---- Pipeline, vertex input, instance pointers and uniforms are
 * pass-scoped: set them inside a pass (asserted); nt_gfx_begin_pass discards them.
 * Texture, sampler and uniform-buffer binds are context state. */

void nt_gfx_bind_pipeline(nt_pipeline_t pip);
/* One backend bind selects the whole vertex-input state (layout + buffers +
 * index binding) for the following draws. Orthogonal to pipeline binding --
 * either may change without re-binding the other. Every draw requires a bound
 * vertex input (asserted); attribute-less draws bind an empty one. */
void nt_gfx_bind_vertex_input(nt_vertex_input_t vi);
/* Binds the texture on unit `slot` with the sampler it is read through; NT_SAMPLER_DEFAULT selects
 * the texture's asset-baked default. A unit never holds a texture without its sampler. Format/filter
 * compatibility is asserted (rules: docs/spec/core/api-contracts.md, "Texture descriptors"). */
void nt_gfx_bind_texture(nt_texture_t tex, nt_sampler_t sampler, uint32_t slot);

/* ---- Scissor and viewport ----
 *
 * GL bottom-left convention. Callers thinking in top-left coordinates must
 * y-flip against framebuffer height; the wrapper does not. State persists
 * across frames — caller manages enable/disable explicitly. */
void nt_gfx_set_scissor(int x, int y, int w, int h);
void nt_gfx_set_scissor_enabled(bool enabled);
/* Returns caller-owned state; resets to false after context restore. */
bool nt_gfx_scissor_enabled(void);
void nt_gfx_set_viewport(int x, int y, int w, int h);

/* Returns NT_SAMPLER_INVALID if texture has no asset-baked default. */
nt_sampler_t nt_gfx_get_texture_default_sampler(nt_texture_t tex);

/* ---- Uniforms ---- The hash is the identity, as for tags and resources. Hash once
 * at init with nt_hash32_str, or inline where the cost does not matter. The
 * value lands on the bound pipeline's program (asserted). */

void nt_gfx_set_uniform_mat4(nt_hash32_t name, const float *matrix);
void nt_gfx_set_uniform_vec4(nt_hash32_t name, const float *vec);
void nt_gfx_set_uniform_float(nt_hash32_t name, float val);
void nt_gfx_set_uniform_int(nt_hash32_t name, int val);

/* ---- Draw calls ---- */

void nt_gfx_draw(uint32_t first_vertex, uint32_t num_vertices);
void nt_gfx_draw_instanced(uint32_t first_vertex, uint32_t num_vertices, uint32_t instance_count);
void nt_gfx_draw_indexed(uint32_t first_index, uint32_t num_indices, uint32_t num_vertices);
void nt_gfx_draw_indexed_instanced(uint32_t first_index, uint32_t num_indices, uint32_t num_vertices, uint32_t instance_count);

/* Convenience getter for g_nt_gfx.frame_stats.draw_calls. Reset by
 * nt_gfx_begin_frame, incremented by every public draw function. Read
 * by nt_debug_overlay; equivalent to reading frame_stats.draw_calls directly. */
uint32_t nt_gfx_get_frame_draw_calls(void);

/* Reads an (x,y,w,h) sub-rect of the bound default framebuffer into caller-owned `out`:
 * rgba8 (row pitch w*4), TOP-LEFT origin (GL's bottom-left read is y-flipped once here),
 * straight alpha. Returns false on w<=0 || h<=0 or w*h*4 > out_cap (no write past the cap). */
bool nt_gfx_read_pixels(int x, int y, int w, int h, uint8_t *out, uint32_t out_cap);

/* ---- Instance buffer ---- */

/* Re-specifies instance attrib pointers at byte_offset into the bound vertex
 * input, which must declare a nonempty instance_layout; both asserted. The
 * offset must be 4-byte aligned (WebGL2 rejects unaligned attrib offsets);
 * asserted. Re-bind per draw to re-point. */
void nt_gfx_bind_instance_buffer(nt_buffer_t buf, uint32_t byte_offset);
void nt_gfx_set_vertex_attrib_default(uint8_t location, float x, float y, float z, float w);

/* ---- Uniform buffer ---- */

void nt_gfx_bind_uniform_buffer(nt_buffer_t buf, uint32_t slot);

/* update_buffer = glBufferSubData at byte offset; offset + size must fit the
 * buffer, data must point to size bytes (NULL only with size 0). Disjoint
 * offsets keep in-flight data untouched. orphan_buffer = glBufferData. */
void nt_gfx_update_buffer(nt_buffer_t buf, uint32_t offset, const void *data, uint32_t size);
void nt_gfx_orphan_buffer(nt_buffer_t buf, const void *data, uint32_t size);

/* Named GPU TIME_ELAPSED segments. Pairs must be sequential (no nesting —
 * GL can only have one TIME_ELAPSED query active at a time). Game opens
 * the segments it wants to time; nt_debug_overlay polls "frame" by convention.
 *
 * Pass a stable string literal — the backend hashes for internal slot
 * lookup AND emits glPushDebugGroup so the name shows up in RenderDoc /
 * gDEBugger / Apitrace as a debug group around the timing query
 * (KHR_debug; no-op on WebGL2 where the extension is absent). */
void nt_gfx_begin_segment(const char *name);
void nt_gfx_end_segment(void);
bool nt_gfx_poll_segment_time_ns(const char *name, uint64_t *out_ns);

/* Toggle GPU time-elapsed queries. Default = enabled. Disable for
 * RenderDoc / Spector captures or mobile drivers that stall on it. */
void nt_gfx_set_gpu_timing_enabled(bool enabled);
bool nt_gfx_is_gpu_timing_supported(void);

/* ---- Texture update (non-mipmapped, non-depth textures only, level 0) ---- */

void nt_gfx_update_texture(nt_texture_t tex, uint16_t x, uint16_t y, uint16_t w, uint16_t h, const void *data);

/* ---- Asset activators (called by nt_resource via callback registration) ---- */

uint32_t nt_gfx_activate_texture(const uint8_t *data, uint32_t size);
uint32_t nt_gfx_activate_mesh(const uint8_t *data, uint32_t size);
uint32_t nt_gfx_activate_shader(const uint8_t *data, uint32_t size);
void nt_gfx_deactivate_texture(uint32_t handle);
void nt_gfx_deactivate_mesh(uint32_t handle);
void nt_gfx_deactivate_shader(uint32_t handle);

/* ---- Mesh info query ---- */

const nt_gfx_mesh_info_t *nt_gfx_get_mesh_info(nt_mesh_t mesh);
/* Mesh pool capacity from nt_gfx_desc_t (valid after nt_gfx_init) -- sizes
 * renderer-side per-mesh tables; mesh pool slots index [1..max]. */
uint16_t nt_gfx_max_meshes(void);

// #region test_access
#ifdef NT_TEST_ACCESS
typedef struct {
    nt_pipeline_t pipeline;
    nt_program_t program;
    uint32_t first_vertex;
    uint32_t num_vertices;
    uint32_t first_index;
    uint32_t num_indices;
    uint32_t instance_count;
} nt_gfx_test_draw_t;

void nt_gfx_test_draw_trace_reset(bool enabled);
uint32_t nt_gfx_test_draw_trace_count(void);
nt_gfx_test_draw_t nt_gfx_test_draw_trace_at(uint32_t index);
bool nt_gfx_test_draw_trace_overflowed(void);

/* Read back the cached scissor rect [x, y, w, h] from the last
 * nt_gfx_set_scissor call. Out-param must be a 4-element int array. */
void nt_gfx_test_scissor_rect(int out[4]);
/* Read back the cached viewport rect [x, y, w, h] from the last
 * nt_gfx_set_viewport call. Out-param must be a 4-element int array. */
void nt_gfx_test_viewport_rect(int out[4]);
/* Hash (nt_hash32) of the GPU-form bytes the last nt_gfx_activate_mesh
 * uploaded -- pins that wire decode actually ran at the upload boundary.
 * Index hash is 0 for a non-indexed mesh. */
uint32_t nt_gfx_test_last_mesh_vertex_hash(void);
uint32_t nt_gfx_test_last_mesh_index_hash(void);
#endif
// #endregion

#endif /* NT_GFX_H */
