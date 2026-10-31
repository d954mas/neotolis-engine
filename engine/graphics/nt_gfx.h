#ifndef NT_GFX_H
#define NT_GFX_H

#ifndef NT_GFX_GPU_TIMING_ENABLED
#error "NT_GFX_GPU_TIMING_ENABLED must be defined by the nt_gfx_interface target"
#endif

#ifndef NT_GFX_CAPTURE_ENABLED
#error "NT_GFX_CAPTURE_ENABLED must be defined by the nt_gfx_interface target"
#endif

#ifndef NT_GFX_MAX_VERTEX_STREAMS
#error "NT_GFX_MAX_VERTEX_STREAMS must be defined by the nt_gfx_interface target"
#endif
#if NT_GFX_MAX_VERTEX_STREAMS < 1 || NT_GFX_MAX_VERTEX_STREAMS > 32
#error "NT_GFX_MAX_VERTEX_STREAMS must be 1-32"
#endif

#include "core/nt_assert.h"
#include "core/nt_platform.h"
#include "core/nt_types.h"
#include "hash/nt_hash.h"
#include "nt_mesh_format.h"
#include "nt_texture_format.h"
#include "nt_vertex_locations.h"

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

/* Owned vertex-input object (GL: one VAO): the per-vertex layout baked against a VBO [+ IBO].
 * Instances come at bind time (nt_gfx_bind_vertex_input_instanced); the VAO keeps the
 * instance pointers of its last bind, so a vertex input bound with one tuple re-points nothing. */
typedef struct {
    uint32_t id;
} nt_vertex_input_t;

/* Registered per-instance layout (divisor 1): CPU data only, no GPU object, no destroy;
 * it survives context loss and lives until nt_gfx_shutdown. */
typedef struct {
    uint32_t id;
} nt_instance_layout_t;

#define NT_RENDER_TARGET_INVALID ((nt_render_target_t){0})
#define NT_MESH_INVALID ((nt_mesh_t){0})
#define NT_PROGRAM_INVALID ((nt_program_t){0})
#define NT_VERTEX_INPUT_INVALID ((nt_vertex_input_t){0})
#define NT_INSTANCE_LAYOUT_INVALID ((nt_instance_layout_t){0})

/* Sampler object — texture-side filter/wrap state decoupled from the texture
 * itself. One texture can be sampled with different filters in different
 * materials by binding different samplers to the same texture unit. */
typedef struct {
    uint32_t id;
} nt_sampler_t;

#define NT_SAMPLER_INVALID ((nt_sampler_t){0})
/* Same value, read as "no override": texture binding uses the texture's asset-baked default. */
#define NT_SAMPLER_DEFAULT NT_SAMPLER_INVALID

typedef struct {
    nt_hash32_t name;
    nt_texture_t texture;
    nt_sampler_t sampler;
} nt_gfx_texture_binding_t;

/* Samplers are deduplicated by their (filter/wrap/compare) descriptor; most apps
 * use 3-10 unique configs. 128 is headroom, not coverage — all 324 combinations
 * are constructible. Costs ~4 KB of BSS, not binary size; the linear scan
 * iterates sampler_count, not capacity, so capacity is free for the hot path. */
#define NT_GFX_MAX_SAMPLERS 128

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
#if !defined(NT_PLATFORM_WEB)
    NT_BUFFER_READBACK, /* native: pixels the GPU copies for the CPU (nt_gfx_read_pixels_to_buffer); no data, usage ignored */
#endif
} nt_buffer_type_t; /* uniform data has no public buffer: see nt_gfx_bind_uniform_block */

typedef enum {
    NT_USAGE_IMMUTABLE = 0, /* GL: STATIC_DRAW */
    NT_USAGE_DYNAMIC,       /* GL: DYNAMIC_DRAW */
    NT_USAGE_STREAM,        /* GL: STREAM_DRAW */
} nt_buffer_usage_t;

/* Frame storage streams: per-frame data allocated with nt_gfx_frame_alloc. Vertex streams
 * come last, so a stream past the range fails the stream < NT_GFX_FRAME_STREAM_COUNT assert.
 * The game names its vertex streams with its own enum starting at NT_GFX_FRAME_VERTEX; stream
 * parameters are uint32_t because those streams have no enumerator here. */
enum {
    NT_GFX_FRAME_INDEX,   /* uint32_t indices, absolute into the general vertex stream */
    NT_GFX_FRAME_UNIFORM, /* view blocks and other per-frame uniform data */
    NT_GFX_FRAME_VERTEX,  /* general vertex stream (sprites, text); vertex stream k is NT_GFX_FRAME_VERTEX + k */
    NT_GFX_FRAME_STREAM_COUNT = NT_GFX_FRAME_VERTEX + NT_GFX_MAX_VERTEX_STREAMS,
};

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

/* Every GL/WebGPU comparison, with literal meaning; values are pipeline-key lane bits. */
typedef enum {
    NT_DEPTH_LESS = 0,
    NT_DEPTH_LEQUAL,
    NT_DEPTH_ALWAYS,
    NT_DEPTH_GEQUAL,
    NT_DEPTH_GREATER,
    NT_DEPTH_EQUAL,
    NT_DEPTH_NOTEQUAL,
    NT_DEPTH_NEVER,
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
 * is a plain sampler; enabling comparison names one of the eight GL/WebGPU
 * functions, since e.g. LEQUAL and LESS differ exactly on a receiver at its own
 * stored depth and reversed depth compares the other way. */
typedef enum {
    NT_COMPARE_NONE = 0,
    NT_COMPARE_LEQUAL,
    NT_COMPARE_LESS,
    NT_COMPARE_GEQUAL,
    NT_COMPARE_GREATER,
    NT_COMPARE_EQUAL,
    NT_COMPARE_NOTEQUAL,
    NT_COMPARE_ALWAYS,
    NT_COMPARE_NEVER,
} nt_compare_func_t;

/* ---- Vertex layout ---- */

#define NT_GFX_MAX_VERTEX_ATTRS 16
/* Registered instance layouts: renderers register a few each at init, and an equal layout
 * returns the existing handle, so re-init never grows the table. */
#define NT_GFX_MAX_INSTANCE_LAYOUTS 32 /* a slot fits a byte: the GL VAO mirror and the bind command store it so */
#define NT_GFX_MAX_TEXTURE_SLOTS 8
/* Uniform block slots, logical and ordered by update frequency. A program's block binds to the slot
 * of its name at link: the engine reserves the first three names, the game lists its own. */
#define NT_GFX_SLOT_GLOBALS 0  /* "Globals": frame and view data, filled and bound by the game */
#define NT_GFX_SLOT_MATERIAL 1 /* "Material": the material's params, bound by nt_material_apply */
#define NT_GFX_SLOT_DRAW 2     /* "Draw": per-draw data of a renderer or game shader */
#define NT_GFX_SLOT_GAME 3     /* nt_gfx_desc_t.game_blocks[i] binds to NT_GFX_SLOT_GAME + i */
/* Every slot is frame data at a dynamic offset: all fit WebGPU's 8 dynamic uniform bindings. */
#define NT_GFX_MAX_GAME_BLOCKS 5
#define NT_GFX_MAX_UNIFORM_BUFFER_SLOTS (NT_GFX_SLOT_GAME + NT_GFX_MAX_GAME_BLOCKS)

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
    uint16_t max_buffers;   /* default: 128; each enabled frame storage stream takes one */
    uint16_t max_textures;  /* default: 64 */
    uint16_t max_meshes;    /* default: 128 */
    /* default: 560, sized for ~4 vertex inputs per mesh plus 48 others. A mesh takes one per frame
     * vertex stream it is drawn from, plus one if drawn from any other buffer (nt_gfx_mesh_vertex_input).
     * The others: 4 per stream shapes draw from, the sprite layouts and the game's own. */
    uint16_t max_vertex_inputs;
    uint16_t max_render_targets; /* default: 16 */
    uint32_t capture_capacity;   /* event records of one frame, default: 0, below 2^24; init allocates two halves */
    uint32_t stream_capacity;    /* draw-phase command bytes of one frame, default: 32 KiB; allocated once at init */
    /* Frame storage bytes per frame by stream (NT_GFX_FRAME_*), default: 0 (disabled) except the
     * uniform stream, which every pass's Globals block uses: 16 KiB;
     * each enabled stream is a CPU staging copy plus a GPU buffer, allocated once at init. */
    uint32_t frame_capacity[NT_GFX_FRAME_STREAM_COUNT];
    /* The game's uniform block names, slot NT_GFX_SLOT_GAME + index; the list ends at the first
     * NULL (default: none). Borrowed unchanged until nt_gfx_shutdown. */
    const char *game_blocks[NT_GFX_MAX_GAME_BLOCKS];
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

/* Program + fixed render state only; vertex input is a separate owned object
 * (nt_vertex_input_desc_t) bound independently of the pipeline. A transient argument, never
 * stored densely, so its padding costs nothing. */
// NOLINTNEXTLINE(clang-analyzer-optin.performance.Padding)
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
    const char *label;           /* not identity, and gfx stores none */
} nt_pipeline_desc_t;

typedef struct {
    nt_vertex_layout_t layout; /* per-vertex attrs, divisor 0; attr_count 0 = attribute-less (gl_VertexID) */
    nt_buffer_t vertex_buffer; /* NT_BUFFER_VERTEX; required iff layout.attr_count > 0 */
    nt_buffer_t index_buffer;  /* optional ({0} = non-indexed); NT_BUFFER_INDEX with index_type != NT_INDEX_NONE */
    const char *label;         /* optional debug name; borrowed for the call */
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
    bool gen_mipmaps;               /* DEPTH*, SRGB8 and RGB9E5 require false; RGBA32F requires float filtering + rendering caps */
    /* 0 or 1 = base level only; N = data holds levels 0..N-1 back to back with
     * no padding (KTX/DDS layout). Any count from 1 to the full chain is legal. */
    uint8_t level_count;
    const char *label;
} nt_texture_desc_t;

typedef struct {
    nt_texture_filter_t min_filter; /* default: NT_FILTER_NEAREST */
    nt_texture_filter_t mag_filter; /* default: NT_FILTER_NEAREST (NEAREST or LINEAR only) */
    nt_texture_wrap_t wrap_u;       /* default: NT_WRAP_CLAMP_TO_EDGE */
    nt_texture_wrap_t wrap_v;       /* default: NT_WRAP_CLAMP_TO_EDGE */
    /* Comparison lives on the sampler, not the texture: one depth target reads
     * through a comparison sampler for the shadow lookup and through a plain
     * sampler for a raw-depth view. Non-NONE requires DEPTH* storage. */
    nt_compare_func_t compare_func;
    const char *label; /* debug name; static storage */
} nt_sampler_desc_t;

/* The target borrows live single-level textures of one size; an invalid handle
 * means the attachment is absent, and at least one must be present. */
typedef struct {
    nt_texture_t color; /* INVALID, RGBA8 or RGBA16F */
    nt_texture_t depth; /* INVALID or DEPTH* */
    const char *label;  /* debug name; static storage */
} nt_render_target_desc_t;

typedef struct {
    nt_render_target_t target; /* zero selects the default framebuffer */
    float clear_color[4];
    /* Applied regardless of the previous pipeline's depth_write state.
     * Typically 1.0f; zero-init gives 0.0 which fails all depth tests. */
    float clear_depth;
    /* false clears the full attachment regardless of scissor; true keeps its contents */
    bool load_color, load_depth;
    /* Contents are undefined after end_pass; texture handles stay valid.
     * discard_depth also discards stencil; discard_color requires a render target. */
    bool discard_color, discard_depth;
} nt_pass_desc_t;

/* Select attachments independently; unselected values are ignored. */
typedef struct {
    float clear_color[4];
    float clear_depth;
    bool color, depth;
} nt_clear_desc_t;

// #region frame counters and observation
/* Public operations (BEGIN/END pairs) and the record-only STATE marker. */
typedef enum {
    NT_GFX_OP_PASS,
    NT_GFX_OP_END_PASS,
    NT_GFX_OP_CREATE,
    NT_GFX_OP_DESTROY,
    NT_GFX_OP_PIPELINE,
    NT_GFX_OP_VERTEX_INPUT,
    NT_GFX_OP_TEXTURE_SET,
    NT_GFX_OP_TEXTURE,
    NT_GFX_OP_SAMPLER,
    NT_GFX_OP_VIEWPORT,
    NT_GFX_OP_SCISSOR,
    NT_GFX_OP_SCISSOR_ENABLE,
    NT_GFX_OP_UBO,
    NT_GFX_OP_BUFFER_UPLOAD,
    NT_GFX_OP_TEXTURE_UPLOAD,
    NT_GFX_OP_ATTRIBUTE,
    NT_GFX_OP_DRAW,
    NT_GFX_OP_DRAW_INSTANCED,
    NT_GFX_OP_DRAW_INDEXED,
    NT_GFX_OP_DRAW_INDEXED_INSTANCED,
    NT_GFX_OP_CONTEXT,
    NT_GFX_OP_STATE,
    NT_GFX_OP_READ_PIXELS,
    NT_GFX_OP_SEGMENT_BEGIN,
    NT_GFX_OP_SEGMENT_END,
    NT_GFX_OP_SEGMENT_POLL,
    NT_GFX_OP_GPU_TIMING,
    NT_GFX_OP_TIMER_DISJOINT,
    NT_GFX_OP_CLEAR,
#if !defined(NT_PLATFORM_WEB)
    NT_GFX_OP_BUFFER_READ,
    NT_GFX_OP_BUFFER_WAIT,
#endif
    NT_GFX_OP_COUNT
} nt_gfx_operation_t;

/* Every function the GL backend issues, queries included; the single source of
 * nt_gfx_gl_call_t, NT_GFX_GL_COUNT and the call names. The web context issues
 * glGetString as getParameter and getExtension directly from JS. */
#define NT_GFX_GL_CALLS(X)                                                                                                                                                                             \
    X(glGetString)                                                                                                                                                                                     \
    X(getExtension)                                                                                                                                                                                    \
    X(glActiveTexture)                                                                                                                                                                                 \
    X(glAttachShader)                                                                                                                                                                                  \
    X(glBeginQuery)                                                                                                                                                                                    \
    X(glBindBuffer)                                                                                                                                                                                    \
    X(glBindBufferRange)                                                                                                                                                                               \
    X(glBindFramebuffer)                                                                                                                                                                               \
    X(glBindSampler)                                                                                                                                                                                   \
    X(glBindTexture)                                                                                                                                                                                   \
    X(glBindVertexArray)                                                                                                                                                                               \
    X(glBlendColor)                                                                                                                                                                                    \
    X(glBlendEquationSeparate)                                                                                                                                                                         \
    X(glBlendFuncSeparate)                                                                                                                                                                             \
    X(glBufferData)                                                                                                                                                                                    \
    X(glBufferSubData)                                                                                                                                                                                 \
    X(glCheckFramebufferStatus)                                                                                                                                                                        \
    X(glClear)                                                                                                                                                                                         \
    X(glClearColor)                                                                                                                                                                                    \
    X(glClearDepth)                                                                                                                                                                                    \
    X(glClearDepthf)                                                                                                                                                                                   \
    X(glCompileShader)                                                                                                                                                                                 \
    X(glCompressedTexImage2D)                                                                                                                                                                          \
    X(glCreateProgram)                                                                                                                                                                                 \
    X(glCreateShader)                                                                                                                                                                                  \
    X(glCullFace)                                                                                                                                                                                      \
    X(glDebugMessageCallback)                                                                                                                                                                          \
    X(glDebugMessageControl)                                                                                                                                                                           \
    X(glDeleteBuffers)                                                                                                                                                                                 \
    X(glDeleteFramebuffers)                                                                                                                                                                            \
    X(glDeleteProgram)                                                                                                                                                                                 \
    X(glDeleteQueries)                                                                                                                                                                                 \
    X(glDeleteSamplers)                                                                                                                                                                                \
    X(glDeleteShader)                                                                                                                                                                                  \
    X(glDeleteTextures)                                                                                                                                                                                \
    X(glDeleteVertexArrays)                                                                                                                                                                            \
    X(glDepthFunc)                                                                                                                                                                                     \
    X(glDepthMask)                                                                                                                                                                                     \
    X(glDisable)                                                                                                                                                                                       \
    X(glDisableVertexAttribArray)                                                                                                                                                                      \
    X(glDrawArrays)                                                                                                                                                                                    \
    X(glDrawArraysInstanced)                                                                                                                                                                           \
    X(glDrawBuffers)                                                                                                                                                                                   \
    X(glDrawElements)                                                                                                                                                                                  \
    X(glDrawElementsInstanced)                                                                                                                                                                         \
    X(glEnable)                                                                                                                                                                                        \
    X(glEnableVertexAttribArray)                                                                                                                                                                       \
    X(glEndQuery)                                                                                                                                                                                      \
    X(glFramebufferTexture2D)                                                                                                                                                                          \
    X(glGenBuffers)                                                                                                                                                                                    \
    X(glGenFramebuffers)                                                                                                                                                                               \
    X(glGenQueries)                                                                                                                                                                                    \
    X(glGenSamplers)                                                                                                                                                                                   \
    X(glGenTextures)                                                                                                                                                                                   \
    X(glGenVertexArrays)                                                                                                                                                                               \
    X(glGenerateMipmap)                                                                                                                                                                                \
    X(glGetActiveUniform)                                                                                                                                                                              \
    X(glGetActiveUniformBlockName)                                                                                                                                                                     \
    X(glGetActiveUniformBlockiv)                                                                                                                                                                       \
    X(glGetActiveUniformsiv)                                                                                                                                                                           \
    X(glGetError)                                                                                                                                                                                      \
    X(glGetIntegerv)                                                                                                                                                                                   \
    X(glGetProgramInfoLog)                                                                                                                                                                             \
    X(glGetProgramiv)                                                                                                                                                                                  \
    X(glGetQueryObjectui64v)                                                                                                                                                                           \
    X(glGetQueryObjectuiv)                                                                                                                                                                             \
    X(glGetShaderInfoLog)                                                                                                                                                                              \
    X(glGetShaderiv)                                                                                                                                                                                   \
    X(glGetUniformLocation)                                                                                                                                                                            \
    X(glInvalidateFramebuffer)                                                                                                                                                                         \
    X(glLinkProgram)                                                                                                                                                                                   \
    X(glPixelStorei)                                                                                                                                                                                   \
    X(glPolygonOffset)                                                                                                                                                                                 \
    X(glPopDebugGroup)                                                                                                                                                                                 \
    X(glPushDebugGroup)                                                                                                                                                                                \
    X(glReadBuffer)                                                                                                                                                                                    \
    X(glReadPixels)                                                                                                                                                                                    \
    X(glSamplerParameteri)                                                                                                                                                                             \
    X(glScissor)                                                                                                                                                                                       \
    X(glShaderSource)                                                                                                                                                                                  \
    X(glTexImage2D)                                                                                                                                                                                    \
    X(glTexParameteri)                                                                                                                                                                                 \
    X(glTexSubImage2D)                                                                                                                                                                                 \
    X(glUniform1i)                                                                                                                                                                                     \
    X(glUniformBlockBinding)                                                                                                                                                                           \
    X(glUseProgram)                                                                                                                                                                                    \
    X(glVertexAttribDivisor)                                                                                                                                                                           \
    X(glVertexAttribPointer)                                                                                                                                                                           \
    X(glViewport)                                                                                                                                                                                      \
    NT_GFX_GL_READBACK_CALLS(X)

/* Buffer readback is native only: WebGL has no buffer mapping. */
#if defined(NT_PLATFORM_WEB)
#define NT_GFX_GL_READBACK_CALLS(X)
#else
#define NT_GFX_GL_READBACK_CALLS(X) X(glBufferStorage) X(glClientWaitSync) X(glDeleteSync) X(glFenceSync) X(glMapBufferRange)
#endif

/* Issued-call detail (NT_GFX_GL_<name>). Integer args follow the GL signature;
 * pointer arguments become presence bits (payloads, outputs, labels) or object
 * names (gen/delete: count, then names); a returned value follows the arguments;
 * payload bytes are separate. Floats use values; no CPU pointers survive. */
#define NT_GFX_GL_CALL_ENUM_(name) NT_GFX_GL_##name,
typedef enum { NT_GFX_GL_NONE = 0, NT_GFX_GL_CALLS(NT_GFX_GL_CALL_ENUM_) NT_GFX_GL_COUNT } nt_gfx_gl_call_t;
#undef NT_GFX_GL_CALL_ENUM_

/* All fields are values. Submitted geometry is not shader/GPU work.
 * Backends without GL (the test fake) leave gl[] and uploads zero.
 * Vertices/indices multiply by instance count; instances counts instanced draws only.
 * accepted[] counts public operations by nt_gfx_operation_t that ended ACCEPTED,
 * nested ones included (default samplers, cascaded destroys); gl[] counts every issued GL call by nt_gfx_gl_call_t; uploads count calls with a
 * non-NULL payload and their bytes (NULL storage and generated mips excluded). */
typedef struct {
    uint64_t frame_sequence;
    uint64_t vertices;
    uint64_t indices;
    uint64_t instances; /* instanced draws only */
    uint64_t buffer_upload_calls;
    uint64_t buffer_upload_bytes;
    uint64_t texture_upload_calls;
    uint64_t texture_upload_bytes;
    uint32_t accepted[NT_GFX_OP_COUNT]; /* operations whose END result was ACCEPTED */
    uint32_t stream_bytes;              /* draw-phase command bytes the frame recorded */
    /* Frame storage bytes allocated in the frame and sent, padding included; final after end_frame. */
    uint32_t frame_bytes[NT_GFX_FRAME_STREAM_COUNT];
    uint32_t gl[NT_GFX_GL_COUNT];
} nt_gfx_counters_t;

/* Recorded draw calls of every kind; merged indexed draws count once. */
static inline uint32_t nt_gfx_draw_calls(const nt_gfx_counters_t *c) {
    return c->accepted[NT_GFX_OP_DRAW] + c->accepted[NT_GFX_OP_DRAW_INSTANCED] + c->accepted[NT_GFX_OP_DRAW_INDEXED] + c->accepted[NT_GFX_OP_DRAW_INDEXED_INSTANCED];
}

typedef enum {
    NT_GFX_EVENT_OP,
    NT_GFX_EVENT_BACKEND,
    NT_GFX_EVENT_SKIP,
    NT_GFX_EVENT_INITIAL,
    NT_GFX_EVENT_DEFINITION,
    NT_GFX_EVENT_ARGUMENT,   /* request argument of the OP its link names */
    NT_GFX_EVENT_NOTE,       /* caller context; its NOTE_FIELD records follow */
    NT_GFX_EVENT_NOTE_FIELD, /* one field of the preceding NOTE */
    NT_GFX_EVENT_MARKER_PUSH,
    NT_GFX_EVENT_MARKER_POP,
} nt_gfx_event_kind_t;

typedef enum {
    NT_GFX_NOTE_U32,
    NT_GFX_NOTE_I32,
    NT_GFX_NOTE_U64,
    NT_GFX_NOTE_F32,
    NT_GFX_NOTE_STR,
} nt_gfx_note_type_t;

typedef enum {
    NT_GFX_OBJECT_NONE,
    NT_GFX_OBJECT_SHADER,
    NT_GFX_OBJECT_PROGRAM,
    NT_GFX_OBJECT_PIPELINE,
    NT_GFX_OBJECT_VERTEX_INPUT,
    NT_GFX_OBJECT_BUFFER,
    NT_GFX_OBJECT_TEXTURE,
    NT_GFX_OBJECT_SAMPLER,
    NT_GFX_OBJECT_RENDER_TARGET,
    NT_GFX_OBJECT_INSTANCE_LAYOUT,
} nt_gfx_object_kind_t;

typedef enum {
    NT_GFX_RESULT_NONE,
    NT_GFX_RESULT_ACCEPTED,
    NT_GFX_RESULT_CACHE,
    NT_GFX_RESULT_INACTIVE,
    NT_GFX_RESULT_CONTEXT_LOST,
    NT_GFX_RESULT_INVALID_HANDLE,
    NT_GFX_RESULT_UNREADY,
    NT_GFX_RESULT_UNKNOWN,
} nt_gfx_result_t;

/* Pointer-free records. Each public operation is one OP record: the request at the call, its
 * result (and a creator's handle) once the call returns. Every gfx record names its operation by
 * link = OP record index + 1 (0: none); notes and markers have no link and overlay its bytes, so
 * check kind first. INITIAL and DEFINITION describe inherited or resource state and never
 * represent issued calls; ARGUMENT carries per-element request arguments of its OP.
 * object is a full typed frontend handle and the cross-context identity; raw names
 * live only in backend data and are valid within their context segment, which a
 * CONTEXT operation ends. Unknown inherited values are explicit. */
typedef union {
    struct {
        nt_gfx_event_kind_t kind;
        uint8_t operation;   /* nt_gfx_operation_t */
        uint8_t object_kind; /* nt_gfx_object_kind_t */
        uint8_t result;      /* nt_gfx_result_t */
        uint8_t arg_count;   /* issued calls: integer args (low nibble) and float values (high nibble) set */
        uint32_t object;
        uint32_t detail;
        uint32_t link;
        union {
            struct {
                uint64_t bytes;
                uint32_t args[12];
                float values[4];
            } backend;
            struct {
                uint64_t size;
                uint32_t backend, related[3], type, usage, width, height, format, levels, flags;
            } resource;
            struct {
                uint32_t first, count, vertices, instances, vertex_input;
            } draw;
            struct {
                uint32_t secondary, name, slot, offset, size;
            } binding;
            struct {
                uint32_t target;
                float color[4], depth;
                /* Flags are meaningful only in OP records; INITIAL holds cached clear values. */
                bool load_color, load_depth, discard_color, discard_depth;
            } pass;
            nt_clear_desc_t clear;
            struct {
                uint32_t buffer, offset, stride, location, type, count, normalized, divisor;
            } attribute;
            struct {
                uint32_t integers[14];
                float values[6];
            } state;
        } data;
    };
    struct {
        nt_gfx_event_kind_t kind;
        char label[100]; /* NOTE label or MARKER_PUSH name */
    } note;
    struct {
        nt_gfx_event_kind_t kind;
        nt_gfx_note_type_t type;
        char key[24];
        union {
            uint32_t u32;
            int32_t i32;
            uint64_t u64;
            float f32;
            char str[72];
        } value;
    } field;
} nt_gfx_event_t;

_Static_assert(sizeof(nt_gfx_event_t) == 104, "capture record layout must remain explicit");
_Static_assert(offsetof(nt_gfx_event_t, object) == 8, "arg_count fills the header padding");
_Static_assert(NT_GFX_OP_COUNT <= 256, "operation is one byte in a record");

/* detail of INITIAL/STATE records: which layer's inherited state they snapshot. */
enum {
    NT_GFX_INITIAL_FRONTEND = 0,
    NT_GFX_INITIAL_BACKEND = 1,
};

/* The platform a capture was recorded on: web names are a subset of native ones (see Capture text). */
typedef enum {
    NT_GFX_CAPTURE_NATIVE,
    NT_GFX_CAPTURE_WEB,
} nt_gfx_capture_platform_t;

typedef struct {
    bool overflow;
    nt_gfx_capture_platform_t platform;
    const nt_gfx_event_t *events;
    uint32_t count;
    uint32_t execute_begin, execute_end; /* records of the end_frame stream execution, frame storage uploads included; 0 until it ran */
    nt_gfx_counters_t counters;          /* the finished frame's */
} nt_gfx_capture_view_t;

#if NT_GFX_CAPTURE_ENABLED
/* One-shot: the next frame records, starting at the begin_frame that opens it.
 * Requires nonzero init capacity. The stub is inert. */
void nt_gfx_capture_request(void);
/* While on, every frame from the next begin_frame records; off lets the frame recording now finish.
 * On requires nonzero init capacity; the stub is inert. Init and shutdown turn it off. */
void nt_gfx_capture_set_continuous(bool on);
/* The last finished recorded frame (closed by a begin_frame), complete with its post-render and
 * between-frame work. Its records stay valid until the begin_frame that finishes the next recorded
 * frame, or shutdown, so a continuous capture is readable during the whole next frame. Copy count
 * records and metadata to keep. Empty (events=NULL) before any recorded frame finishes. */
nt_gfx_capture_view_t nt_gfx_capture_read(void);
#endif

/* Context from any code for the capture: a label plus typed fields. Called through NT_GFX_NOTE. */
typedef struct {
    const char *key;
    nt_gfx_note_type_t type;
    union {
        uint32_t u32;
        int32_t i32;
        uint64_t u64;
        float f32;
        const char *str;
    } value;
} nt_gfx_note_field_t;

#define NT_NOTE_U32(k, v) ((nt_gfx_note_field_t){.key = (k), .type = NT_GFX_NOTE_U32, .value.u32 = (v)})
#define NT_NOTE_I32(k, v) ((nt_gfx_note_field_t){.key = (k), .type = NT_GFX_NOTE_I32, .value.i32 = (v)})
#define NT_NOTE_U64(k, v) ((nt_gfx_note_field_t){.key = (k), .type = NT_GFX_NOTE_U64, .value.u64 = (v)})
#define NT_NOTE_F32(k, v) ((nt_gfx_note_field_t){.key = (k), .type = NT_GFX_NOTE_F32, .value.f32 = (v)})
#define NT_NOTE_STR(k, v) ((nt_gfx_note_field_t){.key = (k), .type = NT_GFX_NOTE_STR, .value.str = (v)})

#if NT_GFX_CAPTURE_ENABLED
void nt_gfx_note_record(const char *label, const nt_gfx_note_field_t *fields, uint32_t count);
/* Nested groups in call order; every push pops before the next nt_gfx_begin_frame. */
void nt_gfx_marker_push(const char *name);
void nt_gfx_marker_pop(void);
/* The trailing placeholder lets a note have no fields under C17; it is not recorded. */
#define NT_GFX_NOTE(...) NT_GFX_NOTE_(__VA_ARGS__, NT_NOTE_U32(NULL, 0))
/* An expression, not a do/if block: each note adds one branch to its caller. sizeof does not
 * evaluate its operand, so the fields are built once. */
#define NT_GFX_NOTE_(label, ...)                                                                                                                                                                       \
    (g_nt_gfx.capture_recording                                                                                                                                                                        \
         ? nt_gfx_note_record((label), (const nt_gfx_note_field_t[]){__VA_ARGS__}, (uint32_t)(sizeof((const nt_gfx_note_field_t[]){__VA_ARGS__}) / sizeof(nt_gfx_note_field_t)) - 1U)                  \
         : (void)0)
#define NT_GFX_MARKER_PUSH(name) nt_gfx_marker_push(name)
#define NT_GFX_MARKER_POP() nt_gfx_marker_pop()
#else
/* Arguments are not evaluated: they must be side-effect-free. */
#define NT_GFX_NOTE(...) ((void)0)
#define NT_GFX_MARKER_PUSH(name) ((void)0)
#define NT_GFX_MARKER_POP() ((void)0)
#endif
// #endregion

/* ---- GPU format capabilities ---- */

typedef struct {
    bool has_astc;                            /* ASTC 4x4 LDR (WEBGL_compressed_texture_astc / KHR_texture_compression_astc_ldr) */
    bool has_bc7;                             /* BC7 / BPTC (EXT_texture_compression_bptc / ARB_texture_compression_bptc) */
    bool has_etc2;                            /* ETC2 + EAC (WEBGL_compressed_texture_etc / core GL 4.3+) */
    bool has_float_render_target;             /* RGBA16F as a colour attachment (EXT_color_buffer_float / core GL 3.0+) */
    bool has_float_texture_linear;            /* RGBA32F filtering (OES_texture_float_linear / core GL 3.0+) */
    bool has_parallel_shader_compile;         /* link completion polled without blocking (KHR_parallel_shader_compile / KHR|ARB) */
    uint32_t max_texture_size;                /* GL_MAX_TEXTURE_SIZE, queried at init */
    uint32_t uniform_buffer_offset_alignment; /* GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT: uniform blocks start at a multiple; size frame_capacity by it */
    /* Shader resource limits: a driver may fail a link over them without a log. */
    uint32_t max_varying_components;          /* GL_MAX_VARYING_COMPONENTS */
    uint32_t max_vertex_uniform_components;   /* GL_MAX_VERTEX_UNIFORM_COMPONENTS */
    uint32_t max_fragment_uniform_components; /* GL_MAX_FRAGMENT_UNIFORM_COMPONENTS */
    uint32_t max_vertex_uniform_blocks;       /* GL_MAX_VERTEX_UNIFORM_BLOCKS */
    uint32_t max_fragment_uniform_blocks;     /* GL_MAX_FRAGMENT_UNIFORM_BLOCKS */
    uint32_t max_uniform_block_size;          /* GL_MAX_UNIFORM_BLOCK_SIZE, bytes */
} nt_gfx_gpu_caps_t;

typedef struct {
    char vendor[128];
    char renderer[256];
    char api_version[128];
    char caps[256]; /* nt_gfx_gpu_caps_t as text: the struct is reset with g_nt_gfx */
} nt_gfx_device_info_t;

/* ---- Global state ---- */

typedef struct {
    nt_gfx_counters_t counters;   /* live counters of the open frame; reset only by begin_frame */
    nt_gfx_counters_t last_frame; /* last closed frame; frame_sequence 0 before the first */
    nt_gfx_gpu_caps_t gpu_caps;
    bool context_lost;
    bool context_restored;
    bool initialized;
#if NT_GFX_CAPTURE_ENABLED
    bool capture_recording;  /* this frame records; read by NT_GFX_NOTE */
    bool capture_continuous; /* every frame records; nt_gfx_capture_set_continuous */
#endif
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
        .stream_capacity = 32U * 1024U,
        .frame_capacity = {[NT_GFX_FRAME_UNIFORM] = 16U * 1024U},
        .depth = true,
        .premultiplied_alpha = true,
    };
}

/* ---- Lifecycle ---- */

/* nt_gfx_stub has no resources: creates return INVALID, queries are empty and
 * commands are inert. Headless callers omit renderer initialization and draws. */
void nt_gfx_init(const nt_gfx_desc_t *desc);
void nt_gfx_shutdown(void);

/* GPU format capabilities (valid after nt_gfx_init) */
const nt_gfx_gpu_caps_t *nt_gfx_gpu_caps(void);

/* Gfx-owned, never NULL; stable address for the process, including before init and after shutdown.
 * Last observed GPU identity, refreshed on context creation; not a readiness query.
 * Empty before observation and in the stub. Caller must not modify or free it. */
const nt_gfx_device_info_t *nt_gfx_device_info(void);

/* ---- Frame / Pass ---- */

/* The one counter boundary, called first in every host iteration before any other
 * gfx use: nt_gfx_init opens the first frame and every begin_frame closes the open one,
 * syncs context loss and opens the next, so all gfx work between init and shutdown
 * belongs to a frame; shutdown discards the open one. Requires the open frame's
 * nt_gfx_end_frame; the init frame only loads and starts ended.
 * begin_frame copies the counters into g_nt_gfx.last_frame, then resets
 * g_nt_gfx.counters. The loss sync takes the browser's loss events: a new loss wipes
 * every backend name and sets context_lost; while lost it restores once the browser
 * reports the context back and sets context_restored until the next begin_frame. */
void nt_gfx_begin_frame(void);
/* Required once in every host iteration, also when nothing renders: after the last
 * end_pass and before nt_window_swap_buffers. Requires an open frame with no open pass.
 * Executes the frame's recorded draw-phase calls in call order. */
void nt_gfx_end_frame(void);
/* Passes do not nest and run only between begin_frame and end_frame. On a lost context they
 * record nothing but still open and close the pass, so the order and the frame rule assert. */
void nt_gfx_begin_pass(const nt_pass_desc_t *desc);
void nt_gfx_end_pass(void);
/* Requires an open pass and a non-NULL desc, borrowed only for this call.
 * Clears selected attachments under the current scissor (or fully when disabled).
 * Preserves draw state, including depth write mask. No selections or a lost context do no GPU work. */
void nt_gfx_clear(const nt_clear_desc_t *desc);

/* ---- Resource creation ---- */

nt_shader_t nt_gfx_make_shader(const nt_shader_desc_t *desc);
/* Starts linking valid stages; nt_gfx_begin_frame finishes the link, so the program is ready no
 * earlier than the next frame. Link errors, a standalone non-sampler uniform (values arrive in
 * uniform blocks) and >NT_GFX_MAX_TEXTURE_SLOTS samplers assert there. Returns invalid while the context is lost, and for a live stage
 * whose GPU object a loss discarded -- recreate the stages and relink. Only a stale stage handle asserts.
 * label is an optional debug name; static storage. */
nt_program_t nt_gfx_make_program(nt_shader_t vs, nt_shader_t fs, const char *label);
/* Returns the live pipeline of an equal desc (the label is not identity, and gfx keeps none) or
 * creates one; gfx owns it until its program is destroyed. Accepts a linking program; binding the
 * pipeline asserts until the program is ready. Creation preserves the currently bound pipeline. */
nt_pipeline_t nt_gfx_make_pipeline(const nt_pipeline_desc_t *desc);
/* Caller owns the result; destroy it with nt_gfx_destroy_vertex_input. The VI
 * borrows its buffers; creation borrows desc/label and preserves the bound VI.
 * INVALID means context loss; caller errors assert. */
nt_vertex_input_t nt_gfx_make_vertex_input(const nt_vertex_input_desc_t *desc);
/* Registers a per-instance layout (divisor 1) for nt_gfx_bind_vertex_input_instanced, or returns
 * the handle of an equal one. Works while the context is lost; layout errors and a full table
 * (NT_GFX_MAX_INSTANCE_LAYOUTS) assert. The handle is valid until nt_gfx_shutdown; owners
 * register again after the next nt_gfx_init. */
nt_instance_layout_t nt_gfx_make_instance_layout(const nt_vertex_layout_t *layout);
nt_buffer_t nt_gfx_make_buffer(const nt_buffer_desc_t *desc);
/* Invalid means a lost context; a zero size, a size above gpu_caps.max_texture_size and a missing
 * compressed format assert. A nonzero handle means uploads were issued, not that storage
 * allocation succeeded: creation does not poll GPU errors. */
nt_texture_t nt_gfx_make_texture(const nt_texture_desc_t *desc);
nt_sampler_t nt_gfx_make_sampler(const nt_sampler_desc_t *desc);
/* Caller owns the result; destroy it with nt_gfx_destroy_render_target. The target borrows its
 * textures; INVALID means context loss. Caller errors, RGBA16F color without
 * gpu_caps.has_float_render_target and an incomplete framebuffer on a live context assert. */
nt_render_target_t nt_gfx_make_render_target(const nt_render_target_desc_t *desc);

/* ---- Resource destruction ----
 *
 * Frame rule: destroying a live object asserts from the first nt_gfx_begin_pass until
 * nt_gfx_end_frame, also through nt_gfx_deactivate_*. */

/* Programs linked or linking from the stage stay usable; a pending link keeps its GPU object until
 * the link resolves, so a failed link still logs the stage. */
void nt_gfx_destroy_shader(nt_shader_t shd);
/* Destroys the program and its pipelines -- the only way a pipeline dies besides a context loss
 * and nt_gfx_shutdown; materials retain the now-unready handle. INVALID is a no-op; stale nonzero
 * handles assert. Clear the caller's handle to NT_PROGRAM_INVALID after destruction. */
void nt_gfx_destroy_program(nt_program_t prog);
/* Invalid and stale handles are no-ops because buffer destruction also
 * destroys dependent vertex inputs (see nt_gfx_destroy_buffer). */
void nt_gfx_destroy_vertex_input(nt_vertex_input_t vi);
/* Destroys VIs that borrow this buffer as their vertex or index buffer, and detaches it
 * from every VI whose instances it holds. */
void nt_gfx_destroy_buffer(nt_buffer_t buf);
/* Destroys render targets that borrow this texture. */
void nt_gfx_destroy_texture(nt_texture_t tex);
/* Leaves the borrowed textures alive. Invalid and stale handles are no-ops
 * because texture destruction and context loss also free render targets. */
void nt_gfx_destroy_render_target(nt_render_target_t rt);
/* Samplers have no destroy: nt_gfx_make_sampler dedupes against an internal
 * cache (NT_GFX_MAX_SAMPLERS), and all cached samplers are released by
 * nt_gfx_shutdown. The shared lifetime is intentional — multiple materials
 * and textures reference the same sampler handle. */

/* The borrowed color texture; INVALID for a stale target or one without color. */
nt_texture_t nt_gfx_render_target_color(nt_render_target_t rt);
/* Reports a live render-target slot; false after direct destruction, the
 * destroy_texture cascade, or a context loss (loss frees every render-target
 * slot; the owner recreates its textures and targets after restore). */
bool nt_gfx_render_target_valid(nt_render_target_t rt);
/* Reports a backend object not discarded by engine loss synchronization;
 * does not verify GPU storage allocation or upload success. */
bool nt_gfx_texture_ready(nt_texture_t tex);
/* Reports a live stage backend. Readiness lost to context loss never returns for that handle;
 * re-read the new handle from its resource after reactivation. */
bool nt_gfx_shader_ready(nt_shader_t shd);
/* Reports a live pool slot, independent of GPU readiness. */
bool nt_gfx_program_valid(nt_program_t prog);
/* Reports a live pipeline slot; false after its program's destruction or a
 * context loss (loss frees every pipeline slot -- pipelines are baked objects,
 * like vertex inputs). Renderers check a held selection with it. */
bool nt_gfx_pipeline_valid(nt_pipeline_t pip);
/* Reports a live vertex-input slot; false after direct destruction, the
 * destroy_buffer cascade, or a context loss (loss frees every vertex-input
 * slot -- they are baked objects their users remake). Renderer caches
 * check this on lookup and self-heal. */
bool nt_gfx_vertex_input_valid(nt_vertex_input_t vi);
/* True once nt_gfx_begin_frame finished the link on the current context; never turns true inside a frame.
 * Required to bind a pipeline on the program. Readiness lost to context loss never returns for that handle. */
bool nt_gfx_program_ready(nt_program_t prog);
/* The program the pipeline borrows; INVALID for an invalid or stale pipeline. */
nt_program_t nt_gfx_pipeline_program(nt_pipeline_t pip);
/* Writes logical dimensions. Outputs are required; invalid handles write zero and return false. */
bool nt_gfx_texture_size(nt_texture_t tex, uint16_t *out_width, uint16_t *out_height);
/* Returns INVALID for invalid or stale handles. */
nt_texture_format_t nt_gfx_texture_format(nt_texture_t tex);

/* ---- Draw state ---- Pipeline, vertex input, texture set, instance pointers and uniform
 * blocks are pass-scoped: set them inside a pass (asserted); nt_gfx_begin_pass
 * discards them, so every pass binds every block it reads. Physical texture/sampler binds are
 * context state. An equal bind, texture set, scissor or viewport
 * ends CACHE and records nothing; a texture set records only its changed units. */

void nt_gfx_bind_pipeline(nt_pipeline_t pip);
/* One backend bind selects the whole vertex-input state (layout + buffers +
 * index binding) for the following draws. Orthogonal to pipeline binding --
 * either may change without re-binding the other. Every draw requires a bound
 * vertex input (asserted); attribute-less draws bind an empty one. A vertex input
 * once bound with instances may not be bound here (asserted): its VAO keeps them enabled. */
void nt_gfx_bind_vertex_input(nt_vertex_input_t vi);
/* Binds vi with instances of `layout` at byte_offset of instance_buffer (NT_BUFFER_VERTEX): a
 * caller-owned buffer (draws stay inside its size) or a frame storage buffer (nt_gfx_frame_buffer;
 * draws stay inside this frame's allocations). The offset must be 4-byte aligned (WebGL2 rejects
 * unaligned attrib offsets), and the instance locations must not overlap vi's vertex ones;
 * asserted. The VAO keeps the layout, buffer and offset of its last bind, so a vertex input bound
 * with the same tuple every frame re-points nothing; a new layout also switches the enabled slots. The bind asserts that the first instance, and every instanced draw
 * that all its instances, lie inside the buffer or this frame's allocations. */
void nt_gfx_bind_vertex_input_instanced(nt_vertex_input_t vi, nt_instance_layout_t layout, nt_buffer_t instance_buffer, uint32_t byte_offset);
/* Applies the complete active sampler interface of the bound pipeline's program.
 * `bindings` is borrowed only for this call and may be NULL iff count is zero.
 * Inactive names are ignored before their handles are read. Contract violations
 * assert, including sampling an attachment of the active render target and a texture
 * husk the owner did not recreate after a restore. A context loss (also one a lazy
 * sampler recreate meets) publishes no set and no backend bind. A set that changes no
 * unit of the pass ends CACHE. */
void nt_gfx_apply_texture_bindings(const nt_gfx_texture_binding_t *bindings, uint8_t count);

/* ---- Scissor and viewport ----
 *
 * GL bottom-left convention. Callers thinking in top-left coordinates must
 * y-flip against framebuffer height; the wrapper does not. Pass-scoped: call
 * inside a pass; every pass starts with scissor disabled. */
void nt_gfx_set_scissor(int x, int y, int w, int h);
void nt_gfx_set_scissor_enabled(bool enabled);
void nt_gfx_set_viewport(int x, int y, int w, int h);

/* Returns NT_SAMPLER_INVALID for an invalid handle. A husk left by a context
 * loss keeps its default sampler until its owner destroys it. */
nt_sampler_t nt_gfx_get_texture_default_sampler(nt_texture_t tex);

/* ---- Draw calls ---- Every draw is a whole triangle list (count % 3 == 0, asserted). A
 * non-instanced draw that continues the previous one of the same kind, with no command
 * in between, joins it and ends CACHE. */

void nt_gfx_draw(uint32_t first_vertex, uint32_t num_vertices);
void nt_gfx_draw_instanced(uint32_t first_vertex, uint32_t num_vertices, uint32_t instance_count);
void nt_gfx_draw_indexed(uint32_t first_index, uint32_t num_indices, uint32_t num_vertices);
void nt_gfx_draw_indexed_instanced(uint32_t first_index, uint32_t num_indices, uint32_t num_vertices, uint32_t instance_count);

/* Reads an (x,y,w,h) sub-rect of `src` (0 = the window) into caller-owned `out`, only between
 * nt_gfx_end_frame and the next nt_gfx_begin_frame (the window: before the swap). (x, y) is the
 * rect's bottom-left corner, as in GL; `out` is rgba8 (row pitch w*4), TOP-LEFT origin (y-flipped
 * once here), straight alpha. A render target needs an RGBA8 color attachment the rect fits in;
 * after a pass that discarded its color the content is undefined. w<=0 || h<=0 and
 * w*h*4 > out_cap assert; returns false on a lost context. */
bool nt_gfx_read_pixels(nt_render_target_t src, int x, int y, int w, int h, uint8_t *out, uint32_t out_cap);

#if !defined(NT_PLATFORM_WEB)
/* Native only (WebGL has no buffer mapping). Starts a read of an (x,y,w,h) sub-rect of `src` into
 * `buf` (NT_BUFFER_READBACK, w*h*4 <= size) and returns without waiting for the GPU (the driver may
 * still make the call wait for its own thread). The bytes are
 * BGRA8 rows of w*4, BOTTOM-UP as GL stores them, read with nt_gfx_wait_readback. Frame point and
 * source rules as nt_gfx_read_pixels. The caller must not read into or destroy `buf` while anything
 * still uses its bytes: nt_gfx cannot see a consumer on another thread. A readback buffer may be
 * destroyed while a frame is being drawn: no recorded command names it. */
void nt_gfx_read_pixels_to_buffer(nt_render_target_t src, int x, int y, int w, int h, nt_buffer_t buf);
/* The bytes of `buf`'s last read, after waiting while the GPU still copies it: wait for a read issued
 * frames earlier, which normally returns at once. Valid, from any thread, until the next read into `buf` or its destroy; never NULL
 * (the gfx stub returns NULL). A `buf` never read asserts. Any frame state: the read was issued. */
const uint8_t *nt_gfx_wait_readback(nt_buffer_t buf);
#endif

/* ---- Uniform blocks ---- */

/* Shaders declare std140 blocks with vec4/ivec4/uvec4/mat4 members and column-major matrices
 * (types and matrix order asserted at link; rules in the API contracts spec).
 * A draw asserts that each block its program uses is bound in the pass at its full size. */

/* Copies size bytes (a multiple of 16) into the uniform frame stream and binds that range to slot.
 * Bytes equal to the slot's last staged block of the frame copy nothing (CACHE in its pass, a
 * rebind in a later one); other bytes copy again, so A, B, A copies three times. Inside a pass;
 * the bytes live for this frame. Budget frame_capacity[NT_GFX_FRAME_UNIFORM] at size + 256 per
 * bind: the bound of a frame is its binds of all passes. */
void nt_gfx_bind_uniform_block(uint32_t slot, const void *data, uint32_t size);
/* Unbinds slot without a backend call, so a draw whose program uses the slot asserts. Inside a pass. */
void nt_gfx_unbind_uniform_block(uint32_t slot);

/* update_buffer = glBufferSubData at byte offset; offset + size must fit the
 * buffer, data must point to size bytes (NULL only with size 0). Allowed at any time:
 * a write before nt_gfx_end_frame lands before every draw of the frame, so every draw
 * reads the frame's last write; a write after it belongs to the next frame. */
void nt_gfx_update_buffer(nt_buffer_t buf, uint32_t offset, const void *data, uint32_t size);

/* ---- Frame storage ----
 *
 * Per-frame vertex, index and uniform data at any point of the frame. Allocate and fill between
 * nt_gfx_begin_frame and nt_gfx_end_frame, which uploads each stream once before the
 * replay. Each vertex stream is its own buffer starting at offset 0 every frame: a pass that
 * writes its instances to its own stream keeps their offsets while its counts stay. Offsets and pointers are valid until the next nt_gfx_begin_frame. Align vertex data read by index
 * to its stride (vertex i at i * stride; indices are absolute), instance data to 4
 * and indices to 4 (first_index = offset / 4). The uniform stream is filled by
 * nt_gfx_bind_uniform_block. */

/* Allocator state, public only because the allocation is inline. */
typedef struct {
    uint8_t *staging;
    uint32_t used; /* bytes allocated this frame */
    uint32_t capacity;
    nt_buffer_t buffer;
} nt_gfx_frame_storage_t;

extern nt_gfx_frame_storage_t g_nt_gfx_frame_storage[NT_GFX_FRAME_STREAM_COUNT];

/* Logs the needed and free bytes and stops: the capacity is the game's budget. */
_Noreturn void nt_gfx_frame_alloc_overflow(uint32_t stream, uint32_t size, uint32_t align);

/* Returns size bytes at an offset that is a multiple of align; writes the offset to *out_offset. */
static inline void *nt_gfx_frame_alloc(uint32_t stream, uint32_t size, uint32_t align, uint32_t *out_offset) {
    NT_ASSERT(stream < NT_GFX_FRAME_STREAM_COUNT && size > 0 && align > 0 && out_offset != NULL);
    nt_gfx_frame_storage_t *s = &g_nt_gfx_frame_storage[stream];
    const uint64_t offset = ((uint64_t)s->used + align - 1U) / align * align;
    if (offset + size > s->capacity) {
        nt_gfx_frame_alloc_overflow(stream, size, align);
    }
    s->used = (uint32_t)(offset + size);
    *out_offset = (uint32_t)offset;
    return s->staging + offset;
}

/* Borrowed: never update or destroy it. Re-read every frame: a context restore
 * replaces it. The index buffer is NT_INDEX_UINT32. */
static inline nt_buffer_t nt_gfx_frame_buffer(uint32_t stream) {
    NT_ASSERT(stream < NT_GFX_FRAME_STREAM_COUNT);
    return g_nt_gfx_frame_storage[stream].buffer;
}

/* GPU TIME_ELAPSED segments cannot nest: GL allows only one active query.
 * Inside a frame. name must be non-NULL and have static lifetime: the pointer
 * is kept until the recorded commands execute; it also keys the hashed lookup and native
 * debug-group labels. */
void nt_gfx_begin_segment(const char *name);
void nt_gfx_end_segment(void);
/* out_ns is required. Compile OFF or stub: false with zero output, except on a
 * known loss. Other unsuccessful polls leave output unchanged. */
bool nt_gfx_poll_segment_time_ns(const char *name, uint64_t *out_ns);

/* Requires compiled support; the runtime choice starts enabled and survives context loss.
 * Disable cancels active/pending samples. Only between nt_gfx_end_frame and the next
 * nt_gfx_begin_frame: recorded segments execute at end_frame. Supported reports capability. */
void nt_gfx_set_gpu_timing_enabled(bool enabled);
bool nt_gfx_is_gpu_timing_supported(void);

/* ---- Texture update (uncompressed, non-mipmapped, non-depth textures only, level 0) ---- */

/* Allowed at any time: a write before nt_gfx_end_frame lands before every draw of the frame, so
 * every draw reads the frame's last write; a write after it belongs to the next frame. */
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
/* The vertex input that draws `mesh` with instances from instance_buffer: one per frame vertex
 * stream, so each stream's draws keep their own instance offsets, and one shared by every other
 * buffer. gfx owns it: made on first use with every stream at its NtStreamDesc.location, freed
 * with the mesh or by a context loss. INVALID when it must be made while the context is lost; a
 * live mesh and buffer are asserted. */
nt_vertex_input_t nt_gfx_mesh_vertex_input(nt_mesh_t mesh, nt_buffer_t instance_buffer);

// #region test_access
#ifdef NT_TEST_ACCESS
/* Read back the cached scissor rect [x, y, w, h] from the last
 * nt_gfx_set_scissor call. Out-param must be a 4-element int array. */
void nt_gfx_test_scissor_rect(int out[4]);
/* Read back the cached viewport rect [x, y, w, h] from the last
 * nt_gfx_set_viewport call. Out-param must be a 4-element int array. */
void nt_gfx_test_viewport_rect(int out[4]);
/* The scissor-enable mirror: false after begin_pass and after context restore. */
bool nt_gfx_test_scissor_enabled(void);
#endif
// #endregion

#endif /* NT_GFX_H */
