/*
 * Unified OpenGL backend for nt_gfx.
 *
 * Covers WebGL 2 (GLES 3.0) and OpenGL 3.3 Core (native desktop).
 * All platform calls (context create/destroy/loss) go through
 * nt_gfx_gl_ctx.h — zero Emscripten or OS API here.
 *
 * Only remaining #ifdef: GL headers and glClearDepthf vs glClearDepth.
 */

#include "core/nt_assert.h"
#include "core/nt_builtins.h"
#include "graphics/gl/nt_gfx_gl_ctx.h"
#include "graphics/nt_gfx_internal.h"
#include "hash/nt_hash.h"
#include "log/nt_log.h"
#include "window/nt_window.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "graphics/gl/nt_gfx_gl_calls.h"

/* Native desktop GL 3.3 has only the double variant; WebGL only the float one. */
#ifdef NT_PLATFORM_WEB
#define nt_gl_clear_depth(d) NT_GL(glClearDepthf, (d))
#else
#define nt_gl_clear_depth(d) NT_GL(glClearDepth, (double)(d))
#endif

/* KHR and ARB_parallel_shader_compile share the value; GLES3 headers lack it. */
#ifndef GL_COMPLETION_STATUS_KHR
#define GL_COMPLETION_STATUS_KHR 0x91B1
#endif

#if NT_GFX_GPU_TIMING_ENABLED
/* EXT_disjoint_timer_query_webgl2 / ARB_timer_query constants. Spec-fixed
 * values — define them inline so the file compiles on both GLES3 (where
 * these are extension-only) and core GL 3.3+ (where glad already exposes
 * them under the same names). The native build's glad pulls in the core
 * symbols, so the #ifndef guard is a no-op there. */
#ifndef GL_TIME_ELAPSED
#define GL_TIME_ELAPSED 0x88BF
#endif
#ifndef GL_QUERY_RESULT
#define GL_QUERY_RESULT 0x8866
#endif
#ifndef GL_QUERY_RESULT_AVAILABLE
#define GL_QUERY_RESULT_AVAILABLE 0x8867
#endif
#ifndef GL_GPU_DISJOINT_EXT
#define GL_GPU_DISJOINT_EXT 0x8FBB
#endif

/* KHR_debug constant — needed for glPushDebugGroup. Only used on native
 * (s_debug_groups_enabled is always false on WebGL2 since KHR_debug isn't
 * in the WebGL spec), but the symbol must compile. */
#ifndef GL_DEBUG_SOURCE_APPLICATION
#define GL_DEBUG_SOURCE_APPLICATION 0x824A
#endif

#endif

/* ---- Pipeline backend data ---- */

/* Longest reflected uniform name plus room for an expanded array index; asserted at link. */
#define NT_GFX_GL_MAX_UNIFORM_NAME 256

/* One entry per active sampler element; the unit is fixed at link. The location is needed
 * once, to write the unit. */
typedef struct {
    uint32_t name_hash;
    GLint location;
    uint8_t sampler_class;
} nt_gfx_gl_sampler_unit_t;

typedef struct {
    bool depth_test_enabled;
    bool depth_write_enabled;
    GLenum depth_func;
    uint8_t cull_mode;
    bool blend_enabled;
    GLenum blend_src_rgb;
    GLenum blend_dst_rgb;
    GLenum blend_src_alpha;
    GLenum blend_dst_alpha;
    GLenum blend_op_rgb;
    GLenum blend_op_alpha;
    float blend_constant_color[4];
    bool polygon_offset_enabled;
    float polygon_offset_factor;
    float polygon_offset_units;
    uint32_t program_slot; /* index into s_programs; the pipeline borrows it. 0 = free slot */
} nt_gfx_gl_pipeline_t;

/* Program reflection lives here, not on the pipelines that borrow the program. */
typedef struct {
    GLuint program;
    GLuint vs; /* attached stages, kept so a failed link can query them */
    GLuint fs;
    /* Sampler units are program state: assigned in reflection order at link,
     * written once, never rewritten by a material. */
    nt_gfx_gl_sampler_unit_t sampler_units[NT_GFX_MAX_TEXTURE_SLOTS];
    uint8_t sampler_count;
    uint32_t block_sizes[NT_GFX_MAX_UNIFORM_BUFFER_SLOTS]; /* GL_UNIFORM_BLOCK_DATA_SIZE per bound slot; 0 = none */
    const char *label;                                     /* debug name; static storage */
} nt_gfx_gl_program_t;

/* What a pointer call needs, packed: nt_vertex_attr_t pads its enum to 12 bytes. */
typedef struct {
    uint8_t location;
    uint8_t type; /* nt_vertex_type_t */
    uint8_t count;
    uint8_t normalized;
    uint16_t offset;
} nt_gfx_gl_attr_t;

/* One VAO with its vertex and index buffers baked in, plus a mirror of the instance
 * (layout, buffer, offset) it last received. Destroying the instance buffer detaches it from
 * every VAO that names it. */
typedef struct {
    GLuint vao; /* 0 = free slot */
    GLuint instance_buffer;
    uint32_t instance_offset;
    uint8_t instance_layout; /* registered layout slot; 0 = none */
    _Static_assert(NT_GFX_MAX_INSTANCE_LAYOUTS <= UINT8_MAX, "instance layout slot is a byte");
} nt_gfx_gl_vertex_input_t;

typedef struct {
    nt_gfx_gl_attr_t attrs[NT_GFX_MAX_VERTEX_ATTRS];
    uint16_t stride;
    uint16_t mask;
    uint8_t attr_count;
} nt_gfx_gl_instance_layout_t;

/* ---- File-scope state ---- */

/* Service VAO for index-buffer data ops: the ELEMENT_ARRAY_BUFFER bind is VAO
 * state, and core profile rejects it with VAO 0 bound (INVALID_OPERATION). */
static GLuint s_ebo_upload_vao;

static nt_gfx_gl_program_t *s_programs;           /* programs, linked or linking, indexed by slot */
static nt_gfx_gl_pipeline_t *s_pipelines;         /* pipeline data, indexed by slot */
static nt_gfx_gl_vertex_input_t *s_vertex_inputs; /* vertex-input VAOs, indexed by slot */
/* CPU data: no GL object, so a context loss keeps it. */
static nt_gfx_gl_instance_layout_t s_instance_layouts[NT_GFX_MAX_INSTANCE_LAYOUTS + 1];
static GLuint *s_buffer_gl;      /* GL buffer names, indexed by slot */
static GLenum *s_buffer_targets; /* GL_ARRAY_BUFFER, GL_ELEMENT_ARRAY_BUFFER or GL_PIXEL_PACK_BUFFER */
#if !defined(NT_PLATFORM_WEB)
static GLsync *s_buffer_fence; /* readback buffers: the fence after the last read, until mapped */
#endif
static GLuint *s_texture_gl;       /* GL texture names, indexed by slot */
static GLuint *s_render_target_gl; /* GL framebuffer names, indexed by slot */
static GLuint s_bound_framebuffer;
static GLbitfield s_pass_discard;

static nt_gfx_desc_t s_init_desc; /* resolved desc: defaults applied, used everywhere */

// #region GPU timer segments — types & state
#if NT_GFX_GPU_TIMING_ENABLED
/* GPU TIME_ELAPSED named segments (EXT_disjoint_timer_query_webgl2 / ARB_timer_query).
 *
 * Self-contained sub-system inside this GL backend. Lives in three clusters:
 *   1. types & state (this region)
 *   2. impl: helpers + begin/end (region "GPU timer segments — begin/end")
 *   3. impl: poll/drop/enable (region "GPU timer segments — poll/lifecycle")
 * Plus nt_gfx_backend_check_timer_disjoint, which begin_frame runs once per frame:
 * the disjoint flag clears on read.
 *
 * Ring depth 8 covers WebGL2 driver query latency (typically 1-4 frames, but
 * spikes happen on tab refocus / GPU power state transitions). 4 was
 * insufficient — observed ring-full in bunnymark on Chrome. */
#define NT_GFX_TIMER_RING 8
#define NT_GFX_TIMER_MAX_SEGMENTS 16

typedef struct {
    nt_hash32_t name_hash;
    GLuint queries[NT_GFX_TIMER_RING];
    bool in_flight[NT_GFX_TIMER_RING];
    uint8_t head;
    uint8_t tail;
} nt_gfx_segment_state_t;

static bool s_timer_enabled;             /* extension/core entry points present */
static bool s_timer_user_enabled = true; /* runtime toggle from nt_gfx_set_gpu_timing_enabled */
static bool s_debug_groups_enabled;      /* KHR_debug present — push/pop debug groups around segments */
static nt_gfx_segment_state_t s_segments[NT_GFX_TIMER_MAX_SEGMENTS];
static uint8_t s_segment_count;
static int8_t s_active_segment = -1; /* index in s_segments while a query is open, -1 otherwise */
static bool s_timer_warned;          /* one-shot ring-full warning; reset on re-enable */
// #endregion

#endif

/* ---- GL state cache (skip redundant JS interop calls) ----
 * Every direct GL call that changes a field here mirrors it or invalidates the
 * entry at the call site; ground state only at init and context restore. */

/* Uploads bind here instead of on a sampling unit: unit NT_GFX_MAX_TEXTURE_SLOTS
 * is below the 16 fragment units WebGL2/GL 3.3 guarantee. */
#define NT_GFX_GL_UPLOAD_TEXTURE_UNIT ((GLenum)(GL_TEXTURE0 + NT_GFX_MAX_TEXTURE_SLOTS))
_Static_assert(NT_GFX_MAX_TEXTURE_SLOTS < 16, "scratch upload unit must stay inside the guaranteed unit range");

static struct {
    GLuint vao;
    /* Context state, not VAO state: only glVertexAttribPointer captures it. */
    GLuint array_buffer;
    /* Every range uses the uniform frame buffer; init and restore reset these with that buffer. */
    struct {
        uint32_t offset;
        uint32_t size;
    } uniform_ranges[NT_GFX_MAX_UNIFORM_BUFFER_SLOTS];
    GLuint program;
    bool depth_test_enabled;
    bool depth_write_enabled;
    GLenum depth_func;
    uint8_t cull_mode;
    bool blend_enabled;
    GLenum blend_src_rgb;
    GLenum blend_dst_rgb;
    GLenum blend_src_alpha;
    GLenum blend_dst_alpha;
    GLenum blend_op_rgb;
    GLenum blend_op_alpha;
    float blend_constant_color[4];
    bool polygon_offset_enabled;
    float polygon_offset_factor;
    float polygon_offset_units;
    GLenum active_texture_unit;
    /* GL name per sampling slot; uploads use the scratch unit and never touch these. */
    GLuint bound_textures[NT_GFX_MAX_TEXTURE_SLOTS];
    /* GL auto-unbinds a deleted sampler, so destroy_sampler mirrors that here. */
    GLuint bound_samplers[NT_GFX_MAX_TEXTURE_SLOTS];
    int viewport[4];
    float clear_color[4];
    float clear_depth;
} s_gl_cache;

// #region capture of authoritative backend mirrors
#if NT_GFX_CAPTURE_ENABLED
static void capture_program_definition(uint32_t i) {
    const nt_gfx_gl_program_t *program = &s_programs[i];
    NT_GFX_RECORD(NT_GFX_EVENT_DEFINITION, NT_GFX_OP_STATE, event->detail = NT_GFX_OBJECT_PROGRAM; event->data.backend.args[0] = i; event->data.backend.args[1] = program->program;);
    for (uint32_t u = 0; u < program->sampler_count; u++) {
        NT_GFX_RECORD(NT_GFX_EVENT_INITIAL, NT_GFX_OP_SAMPLER, event->data.backend.args[0] = i; event->data.backend.args[1] = program->sampler_units[u].name_hash;
                      event->data.backend.args[2] = (uint32_t)program->sampler_units[u].location; event->data.backend.args[3] = u;
                      event->data.backend.args[4] = program->sampler_units[u].sampler_class;);
    }
}

static void capture_pipeline_definition(uint32_t slot) {
    const nt_gfx_gl_pipeline_t *pip = &s_pipelines[slot];
    NT_GFX_RECORD(NT_GFX_EVENT_DEFINITION, NT_GFX_OP_PIPELINE, event->detail = slot; event->data.state.integers[0] = pip->program_slot; event->data.state.integers[1] = pip->depth_test_enabled;
                  event->data.state.integers[2] = pip->depth_write_enabled; event->data.state.integers[3] = pip->depth_func; event->data.state.integers[4] = pip->cull_mode;
                  event->data.state.integers[5] = pip->blend_enabled; event->data.state.integers[6] = pip->blend_src_rgb; event->data.state.integers[7] = pip->blend_dst_rgb;
                  event->data.state.integers[8] = pip->blend_src_alpha; event->data.state.integers[9] = pip->blend_dst_alpha; event->data.state.integers[10] = pip->blend_op_rgb;
                  event->data.state.integers[11] = pip->blend_op_alpha; event->data.state.integers[12] = pip->polygon_offset_enabled;
                  memcpy(event->data.state.values, pip->blend_constant_color, 4 * sizeof(float)); event->data.state.values[4] = pip->polygon_offset_factor;
                  event->data.state.values[5] = pip->polygon_offset_units;);
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity) -- bounded snapshots of separate backend tables
void nt_gfx_backend_capture_initial_state(void) {
    NT_GFX_RECORD(NT_GFX_EVENT_INITIAL, NT_GFX_OP_STATE, event->data.backend.args[0] = s_gl_cache.program; event->data.backend.args[1] = s_gl_cache.vao;
                  event->data.backend.args[2] = s_bound_framebuffer; event->data.backend.args[3] = s_gl_cache.active_texture_unit; event->data.backend.args[4] = g_nt_window.fb_width;
                  event->data.backend.args[5] = g_nt_window.fb_height; event->data.backend.args[6] = s_ebo_upload_vao; event->data.backend.args[7] = s_gl_cache.array_buffer;
                  event->detail = NT_GFX_INITIAL_BACKEND;);
    NT_GFX_RECORD(NT_GFX_EVENT_INITIAL, NT_GFX_OP_VIEWPORT, for (uint32_t i = 0; i < 4; i++) { event->data.state.integers[i] = (uint32_t)s_gl_cache.viewport[i]; });
    NT_GFX_RECORD(NT_GFX_EVENT_INITIAL, NT_GFX_OP_PASS, memcpy(event->data.pass.color, s_gl_cache.clear_color, sizeof(event->data.pass.color)); event->data.pass.depth = s_gl_cache.clear_depth;);
    NT_GFX_RECORD(NT_GFX_EVENT_INITIAL, NT_GFX_OP_PIPELINE, event->data.state.integers[0] = s_gl_cache.program; event->data.state.integers[1] = s_gl_cache.depth_test_enabled;
                  event->data.state.integers[2] = s_gl_cache.depth_write_enabled; event->data.state.integers[3] = s_gl_cache.depth_func; event->data.state.integers[4] = s_gl_cache.cull_mode;
                  event->data.state.integers[5] = s_gl_cache.blend_enabled; event->data.state.integers[6] = s_gl_cache.blend_src_rgb; event->data.state.integers[7] = s_gl_cache.blend_dst_rgb;
                  event->data.state.integers[8] = s_gl_cache.blend_src_alpha; event->data.state.integers[9] = s_gl_cache.blend_dst_alpha; event->data.state.integers[10] = s_gl_cache.blend_op_rgb;
                  event->data.state.integers[11] = s_gl_cache.blend_op_alpha; event->data.state.integers[12] = s_gl_cache.polygon_offset_enabled;
                  memcpy(event->data.state.values, s_gl_cache.blend_constant_color, 4 * sizeof(float)); event->data.state.values[4] = s_gl_cache.polygon_offset_factor;
                  event->data.state.values[5] = s_gl_cache.polygon_offset_units;);
    for (uint32_t i = 1; i <= s_init_desc.max_pipelines; i++) {
        if (s_pipelines[i].program_slot != 0) {
            capture_pipeline_definition(i);
        }
    }
    for (uint32_t unit = 0; unit < NT_GFX_MAX_TEXTURE_SLOTS; unit++) {
        NT_GFX_RECORD(NT_GFX_EVENT_INITIAL, NT_GFX_OP_TEXTURE, event->data.backend.args[0] = unit; event->data.backend.args[1] = s_gl_cache.bound_textures[unit];
                      event->data.backend.args[2] = s_gl_cache.bound_samplers[unit];);
    }
    for (uint32_t i = 1; i <= s_init_desc.max_programs; i++) {
        const nt_gfx_gl_program_t *program = &s_programs[i];
        if (program->program == 0) {
            continue;
        }
        capture_program_definition(i);
    }
    for (uint32_t i = 1; i <= s_init_desc.max_buffers; i++) {
        if (s_buffer_gl[i] == 0) {
            continue;
        }
        NT_GFX_RECORD(NT_GFX_EVENT_DEFINITION, NT_GFX_OP_STATE, event->detail = NT_GFX_OBJECT_BUFFER; event->data.backend.args[0] = i; event->data.backend.args[1] = s_buffer_gl[i];);
    }
    for (uint32_t i = 1; i <= s_init_desc.max_textures; i++) {
        if (s_texture_gl[i] == 0) {
            continue;
        }
        NT_GFX_RECORD(NT_GFX_EVENT_DEFINITION, NT_GFX_OP_STATE, event->detail = NT_GFX_OBJECT_TEXTURE; event->data.backend.args[0] = i; event->data.backend.args[1] = s_texture_gl[i];);
    }
    for (uint32_t i = 1; i <= s_init_desc.max_vertex_inputs; i++) {
        if (s_vertex_inputs[i].vao == 0) {
            continue;
        }
        const nt_gfx_gl_vertex_input_t *vi = &s_vertex_inputs[i];
        NT_GFX_RECORD(NT_GFX_EVENT_DEFINITION, NT_GFX_OP_STATE, event->detail = NT_GFX_OBJECT_VERTEX_INPUT; event->data.backend.args[0] = i; event->data.backend.args[1] = vi->vao;
                      event->data.backend.args[2] = vi->instance_buffer; event->data.backend.args[3] = vi->instance_offset; event->data.backend.args[4] = vi->instance_layout;);
    }
    for (uint32_t i = 1; i <= s_init_desc.max_render_targets; i++) {
        if (s_render_target_gl[i] == 0) {
            continue;
        }
        NT_GFX_RECORD(NT_GFX_EVENT_DEFINITION, NT_GFX_OP_STATE, event->detail = NT_GFX_OBJECT_RENDER_TARGET; event->data.backend.args[0] = i; event->data.backend.args[1] = s_render_target_gl[i];);
    }
}
#endif
// #endregion

bool nt_gfx_backend_program_sampler_info(uint32_t program_backend, uint32_t name_hash, nt_gfx_sampler_info_t *out_info) {
    NT_ASSERT(program_backend != 0 && program_backend <= s_init_desc.max_programs && s_programs[program_backend].program != 0 && "program_sampler_info: requires a live program");
    NT_ASSERT(out_info != NULL && "program_sampler_info: out_info is required");
    const nt_gfx_gl_program_t *prog = &s_programs[program_backend];
    for (uint8_t i = 0; i < prog->sampler_count; i++) {
        if (prog->sampler_units[i].name_hash == name_hash) {
            out_info->unit = i;
            out_info->sampler_class = prog->sampler_units[i].sampler_class;
            return true;
        }
    }
    return false;
}

uint32_t nt_gfx_backend_program_sampler_mask(uint32_t program_backend) {
    NT_ASSERT(program_backend != 0 && program_backend <= s_init_desc.max_programs && s_programs[program_backend].program != 0 && "program_sampler_mask: requires a live program");
    /* Units are dense 0..n-1, so the count is the mask. */
    return (1U << s_programs[program_backend].sampler_count) - 1U;
}

const uint32_t *nt_gfx_backend_program_block_sizes(uint32_t program_backend) {
    NT_ASSERT(program_backend != 0 && program_backend <= s_init_desc.max_programs && s_programs[program_backend].program != 0 && "program_block_sizes: requires a live program");
    return s_programs[program_backend].block_sizes;
}

// #region test mirror reads
#ifdef NT_TEST_ACCESS
uint32_t nt_gfx_gl_test_cached_vao(void) { return s_gl_cache.vao; }
uint32_t nt_gfx_gl_test_cached_program(void) { return s_gl_cache.program; }

uint32_t nt_gfx_gl_test_cached_texture(uint32_t slot) {
    NT_ASSERT(slot < NT_GFX_MAX_TEXTURE_SLOTS && "cached_texture: slot out of range");
    return s_gl_cache.bound_textures[slot];
}

uint32_t nt_gfx_gl_test_cached_sampler(uint32_t slot) {
    NT_ASSERT(slot < NT_GFX_MAX_TEXTURE_SLOTS && "cached_sampler: slot out of range");
    return s_gl_cache.bound_samplers[slot];
}
#endif
// #endregion

/* The service VAO prevents EBO data operations from rewriting a draw VAO.
 * Detaching on exit lets deletion release the uploaded buffer's storage. */
static void ebo_upload_begin(void) { NT_GL(glBindVertexArray, s_ebo_upload_vao); }

static void ebo_upload_end(void) {
    NT_GL(glBindBuffer, GL_ELEMENT_ARRAY_BUFFER, 0);
    NT_GL(glBindVertexArray, s_gl_cache.vao);
}

static void gl_set_viewport(int x, int y, int w, int h) {
    const int rect[4] = {x, y, w, h};
    if (memcmp(s_gl_cache.viewport, rect, sizeof(rect)) == 0) {
        return;
    }
    memcpy(s_gl_cache.viewport, rect, sizeof(rect));
    NT_GL(glViewport, x, y, (GLsizei)w, (GLsizei)h);
}

/* Real GL calls, not cache defaults: the native context outlives init cycles and
 * restore reuses it. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity) -- diagnostic record and assert macros expand at owning sites
static void nt_gfx_gl_cache_ground_state(void) {
    NT_GL(glBindVertexArray, 0);
    NT_GL(glBindBuffer, GL_ARRAY_BUFFER, 0);
    NT_GL(glUseProgram, 0);
    NT_GL(glDisable, GL_DEPTH_TEST);
    NT_GL(glDepthMask, GL_TRUE);
    NT_GL(glDepthFunc, GL_LESS);
    NT_GL(glDisable, GL_CULL_FACE);
    NT_GL(glDisable, GL_BLEND);
    NT_GL(glBlendFuncSeparate, GL_ONE, GL_ZERO, GL_ONE, GL_ZERO);
    NT_GL(glBlendEquationSeparate, GL_FUNC_ADD, GL_FUNC_ADD);
    NT_GL(glBlendColor, 0.0F, 0.0F, 0.0F, 0.0F);
    NT_GL(glDisable, GL_POLYGON_OFFSET_FILL);
    NT_GL(glPolygonOffset, 0.0F, 0.0F);
    NT_GL(glDisable, GL_SCISSOR_TEST);
    NT_GL(glActiveTexture, GL_TEXTURE0);
    for (uint32_t unit = 0; unit < NT_GFX_MAX_TEXTURE_SLOTS; unit++) {
        NT_GL(glBindSampler, unit, 0);
    }
    /* A zero-size viewport is legal GL and never equals a real pass, so the first
     * pass after grounding always re-issues. */
    NT_GL(glViewport, 0, 0, 0, 0);
    NT_GL(glClearColor, 0.0F, 0.0F, 0.0F, 0.0F);
    nt_gl_clear_depth(1.0F);

    s_bound_framebuffer = 0;
    s_pass_discard = 0;
    s_gl_cache.vao = 0;
    s_gl_cache.array_buffer = 0;
    memset(s_gl_cache.uniform_ranges, 0, sizeof(s_gl_cache.uniform_ranges));
    s_gl_cache.program = 0;
    s_gl_cache.depth_test_enabled = false;
    s_gl_cache.depth_write_enabled = true; /* GL default: depth write enabled */
    s_gl_cache.depth_func = GL_LESS;       /* GL default */
    s_gl_cache.cull_mode = 0;
    s_gl_cache.blend_enabled = false;
    s_gl_cache.blend_src_rgb = GL_ONE;
    s_gl_cache.blend_dst_rgb = GL_ZERO;
    s_gl_cache.blend_src_alpha = GL_ONE;
    s_gl_cache.blend_dst_alpha = GL_ZERO;
    s_gl_cache.blend_op_rgb = GL_FUNC_ADD;
    s_gl_cache.blend_op_alpha = GL_FUNC_ADD;
    memset(s_gl_cache.blend_constant_color, 0, sizeof(s_gl_cache.blend_constant_color));
    s_gl_cache.polygon_offset_enabled = false;
    s_gl_cache.polygon_offset_factor = 0.0F;
    s_gl_cache.polygon_offset_units = 0.0F;
    s_gl_cache.active_texture_unit = GL_TEXTURE0;
    memset(s_gl_cache.bound_textures, 0, sizeof(s_gl_cache.bound_textures));
    memset(s_gl_cache.bound_samplers, 0, sizeof(s_gl_cache.bound_samplers));
    memset(s_gl_cache.viewport, 0, sizeof(s_gl_cache.viewport));
    memset(s_gl_cache.clear_color, 0, sizeof(s_gl_cache.clear_color));
    s_gl_cache.clear_depth = 1.0F;
}

/* ---- Helpers: enum mapping ---- */

static GLenum map_blend_factor(nt_blend_factor_t f) {
    switch (f) {
    case NT_BLEND_ZERO:
        return GL_ZERO;
    case NT_BLEND_ONE:
        return GL_ONE;
    case NT_BLEND_SRC_COLOR:
        return GL_SRC_COLOR;
    case NT_BLEND_ONE_MINUS_SRC_COLOR:
        return GL_ONE_MINUS_SRC_COLOR;
    case NT_BLEND_DST_COLOR:
        return GL_DST_COLOR;
    case NT_BLEND_ONE_MINUS_DST_COLOR:
        return GL_ONE_MINUS_DST_COLOR;
    case NT_BLEND_SRC_ALPHA:
        return GL_SRC_ALPHA;
    case NT_BLEND_ONE_MINUS_SRC_ALPHA:
        return GL_ONE_MINUS_SRC_ALPHA;
    case NT_BLEND_DST_ALPHA:
        return GL_DST_ALPHA;
    case NT_BLEND_ONE_MINUS_DST_ALPHA:
        return GL_ONE_MINUS_DST_ALPHA;
    case NT_BLEND_CONSTANT_COLOR:
        return GL_CONSTANT_COLOR;
    case NT_BLEND_ONE_MINUS_CONSTANT_COLOR:
        return GL_ONE_MINUS_CONSTANT_COLOR;
    case NT_BLEND_CONSTANT_ALPHA:
        return GL_CONSTANT_ALPHA;
    case NT_BLEND_ONE_MINUS_CONSTANT_ALPHA:
        return GL_ONE_MINUS_CONSTANT_ALPHA;
    case NT_BLEND_SRC_ALPHA_SATURATE:
        return GL_SRC_ALPHA_SATURATE;
    default:
        return GL_ONE;
    }
}

static GLenum map_blend_op(nt_blend_op_t op) {
    switch (op) {
    case NT_BLEND_OP_ADD:
        return GL_FUNC_ADD;
    case NT_BLEND_OP_SUBTRACT:
        return GL_FUNC_SUBTRACT;
    case NT_BLEND_OP_REVERSE_SUBTRACT:
        return GL_FUNC_REVERSE_SUBTRACT;
    case NT_BLEND_OP_MIN:
        return GL_MIN;
    case NT_BLEND_OP_MAX:
        return GL_MAX;
    default:
        return GL_FUNC_ADD;
    }
}

/* Bitwise, not ==: -0.0 and +0.0 compare equal as floats but are distinct
 * clear/blend values on a float render target, so the dedup must re-issue. */
// NOLINTNEXTLINE(bugprone-suspicious-memory-comparison,cert-exp42-c,cert-flp37-c) -- distinguishing the bit patterns is the point
static bool float4_equal(const float a[4], const float b[4]) { return memcmp(a, b, 4 * sizeof(float)) == 0; }

static GLenum map_depth_func(nt_depth_func_t f) {
    switch (f) {
    case NT_DEPTH_LESS:
        return GL_LESS;
    case NT_DEPTH_LEQUAL:
        return GL_LEQUAL;
    case NT_DEPTH_ALWAYS:
        return GL_ALWAYS;
    case NT_DEPTH_GEQUAL:
        return GL_GEQUAL;
    case NT_DEPTH_GREATER:
        return GL_GREATER;
    case NT_DEPTH_EQUAL:
        return GL_EQUAL;
    case NT_DEPTH_NOTEQUAL:
        return GL_NOTEQUAL;
    case NT_DEPTH_NEVER:
        return GL_NEVER;
    default:
        return GL_LESS;
    }
}

static GLenum map_vertex_type(nt_vertex_type_t t) {
    switch (t) {
    case NT_VERTEX_FLOAT:
        return GL_FLOAT;
    case NT_VERTEX_HALF:
        return GL_HALF_FLOAT;
    case NT_VERTEX_UINT8:
        return GL_UNSIGNED_BYTE;
    case NT_VERTEX_INT8:
        return GL_BYTE;
    case NT_VERTEX_UINT16:
        return GL_UNSIGNED_SHORT;
    case NT_VERTEX_INT16:
        return GL_SHORT;
    default:
        return GL_FLOAT;
    }
}

static GLenum map_buffer_usage(nt_buffer_usage_t u) {
    switch (u) {
    case NT_USAGE_IMMUTABLE:
        return GL_STATIC_DRAW;
    case NT_USAGE_DYNAMIC:
        return GL_DYNAMIC_DRAW;
    case NT_USAGE_STREAM:
        return GL_STREAM_DRAW;
    default:
        return GL_STATIC_DRAW;
    }
}

static GLenum map_texture_filter(nt_texture_filter_t f) {
    switch (f) {
    case NT_FILTER_NEAREST:
        return GL_NEAREST;
    case NT_FILTER_LINEAR:
        return GL_LINEAR;
    case NT_FILTER_NEAREST_MIPMAP_NEAREST:
        return GL_NEAREST_MIPMAP_NEAREST;
    case NT_FILTER_LINEAR_MIPMAP_NEAREST:
        return GL_LINEAR_MIPMAP_NEAREST;
    case NT_FILTER_NEAREST_MIPMAP_LINEAR:
        return GL_NEAREST_MIPMAP_LINEAR;
    case NT_FILTER_LINEAR_MIPMAP_LINEAR:
        return GL_LINEAR_MIPMAP_LINEAR;
    default:
        return GL_NEAREST;
    }
}

static GLenum map_compare_func(nt_compare_func_t f) {
    switch (f) {
    case NT_COMPARE_LESS:
        return GL_LESS;
    case NT_COMPARE_GEQUAL:
        return GL_GEQUAL;
    case NT_COMPARE_GREATER:
        return GL_GREATER;
    case NT_COMPARE_EQUAL:
        return GL_EQUAL;
    case NT_COMPARE_NOTEQUAL:
        return GL_NOTEQUAL;
    case NT_COMPARE_ALWAYS:
        return GL_ALWAYS;
    case NT_COMPARE_NEVER:
        return GL_NEVER;
    case NT_COMPARE_NONE:
    case NT_COMPARE_LEQUAL:
    default:
        /* GL keeps a comparison function even with the mode off; LEQUAL is its
         * own default, so an unused slot reads back as untouched state. */
        return GL_LEQUAL;
    }
}

static GLenum map_texture_wrap(nt_texture_wrap_t w) {
    switch (w) {
    case NT_WRAP_CLAMP_TO_EDGE:
        return GL_CLAMP_TO_EDGE;
    case NT_WRAP_REPEAT:
        return GL_REPEAT;
    case NT_WRAP_MIRRORED_REPEAT:
        return GL_MIRRORED_REPEAT;
    default:
        return GL_CLAMP_TO_EDGE;
    }
}

/* ==== Backend interface implementation ==== */

static void nt_gfx_gl_drain_errors(void) {
    while (NT_GL_RET0(glGetError) != GL_NO_ERROR) {
    }
}

/* WebGL names null only on a lost context, and native glGen* does not run out: a null name on a
 * live context is a bug. Asked after the failed call: a GPU-process death can fail the call before
 * the browser reports the loss. */
static void assert_name_failed_on_loss(const char *call, const char *label) {
    const bool lost = nt_gfx_gl_ctx_query_lost();
    if (!lost) {
        NT_LOG_ERROR("%s name: gl_error=0x%04X \"%s\"", call, (unsigned)NT_GL_RET0(glGetError), label != NULL ? label : "");
    }
    NT_ASSERT(lost && "backend could not name a GL object on a live context");
}

static void nt_gfx_gl_init_context_features(void) {
    /* Emscripten keeps a recorded error across contexts: calls that reached the
     * dead context must not fail the fresh one's first error check. */
    nt_gfx_gl_drain_errors();
#if NT_GFX_GPU_TIMING_ENABLED
    s_timer_enabled = nt_gfx_gl_ctx_enable_timer_query();
    s_debug_groups_enabled = nt_gfx_gl_ctx_enable_debug_groups();
    nt_gfx_backend_drop_timer_segments();
#endif
    nt_gfx_gl_ctx_enable_debug_callback();
    /* Fresh context (init or restore): the previous upload VAO died with it. */
    NT_GL_GEN(glGenVertexArrays, 1, &s_ebo_upload_vao);
    /* 0 with a live context would silently break every index-buffer upload on
     * core GL; 0 on an already-lost context is retried by the next restore. */
    if (s_ebo_upload_vao == 0) {
        assert_name_failed_on_loss("glGenVertexArrays upload", NULL);
    }
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity) -- NT_ASSERT expansion inflates the metric
void nt_gfx_backend_init(const nt_gfx_desc_t *desc) {
    NT_ASSERT(desc);
    s_init_desc = *desc;

    const bool created = nt_gfx_gl_ctx_create(&s_init_desc);
    NT_ASSERT(created && "gfx context creation failed");

    /* Allocate backend resource arrays (+1 because slots are 1-based) */
    s_programs = (nt_gfx_gl_program_t *)calloc(s_init_desc.max_programs + 1, sizeof(nt_gfx_gl_program_t));
    s_pipelines = (nt_gfx_gl_pipeline_t *)calloc(s_init_desc.max_pipelines + 1, sizeof(nt_gfx_gl_pipeline_t));
    s_vertex_inputs = (nt_gfx_gl_vertex_input_t *)calloc(s_init_desc.max_vertex_inputs + 1, sizeof(nt_gfx_gl_vertex_input_t));
    s_buffer_gl = (GLuint *)calloc(s_init_desc.max_buffers + 1, sizeof(GLuint));
    s_buffer_targets = (GLenum *)calloc(s_init_desc.max_buffers + 1, sizeof(GLenum));
    s_texture_gl = (GLuint *)calloc(s_init_desc.max_textures + 1, sizeof(GLuint));
    s_render_target_gl = (GLuint *)calloc(s_init_desc.max_render_targets + 1, sizeof(GLuint));
    /* Init-time OOM on a few KB of tables is not a state a game can recover from. */
    NT_ASSERT(s_programs && s_pipelines && s_vertex_inputs && s_buffer_gl && s_buffer_targets && s_texture_gl && s_render_target_gl && "gfx backend init: out of memory");
#if !defined(NT_PLATFORM_WEB)
    s_buffer_fence = (GLsync *)calloc(s_init_desc.max_buffers + 1, sizeof(GLsync));
    NT_ASSERT(s_buffer_fence != NULL && "gfx backend init: out of memory");
#endif

    nt_gfx_gl_cache_ground_state();

    nt_gfx_gl_init_context_features();
}

void nt_gfx_backend_shutdown(void) {
#if NT_GFX_GPU_TIMING_ENABLED
    if (s_timer_enabled && !nt_gfx_gl_ctx_query_lost()) {
        nt_gfx_backend_end_segment();
        for (uint8_t i = 0; i < s_segment_count; i++) {
            NT_GL_DELETE(glDeleteQueries, NT_GFX_TIMER_RING, s_segments[i].queries);
        }
    }
    nt_gfx_backend_drop_timer_segments();
    s_timer_enabled = false;
#endif

    free(s_programs);
    free(s_pipelines);
    free(s_vertex_inputs);
    free(s_buffer_gl);
    free(s_buffer_targets);
    free(s_texture_gl);
    free(s_render_target_gl);
#if !defined(NT_PLATFORM_WEB)
    free((void *)s_buffer_fence);
    s_buffer_fence = NULL;
#endif

    s_programs = NULL;
    s_pipelines = NULL;
    s_vertex_inputs = NULL;
    s_buffer_gl = NULL;
    s_buffer_targets = NULL;
    s_texture_gl = NULL;
    s_render_target_gl = NULL;

    s_bound_framebuffer = 0;
    /* A dead context already reclaimed the name; a GL call here would run
     * without a current context on web. */
    if (s_ebo_upload_vao != 0 && !nt_gfx_gl_ctx_query_lost()) {
        NT_GL_DELETE(glDeleteVertexArrays, 1, &s_ebo_upload_vao);
    }
    s_ebo_upload_vao = 0;

    nt_gfx_gl_ctx_destroy();
}

bool nt_gfx_backend_take_context_loss(void) { return nt_gfx_gl_ctx_take_loss(); }

bool nt_gfx_backend_query_context_lost(void) { return nt_gfx_gl_ctx_query_lost(); }

/* ---- Frame / Pass ---- */

// #region GPU timer segments — begin/end
#if NT_GFX_GPU_TIMING_ENABLED
/* Find existing segment by name hash, or allocate a new slot with its own
 * ring of GL_TIME_ELAPSED queries; -1 when the context is lost. Linear scan is fine for small N (<= 16). */
static int8_t segment_find_or_alloc(nt_hash32_t name_hash) {
    for (uint8_t i = 0; i < s_segment_count; i++) {
        if (s_segments[i].name_hash.value == name_hash.value) {
            return (int8_t)i;
        }
    }
    NT_ASSERT(s_segment_count < NT_GFX_TIMER_MAX_SEGMENTS && "raise NT_GFX_TIMER_MAX_SEGMENTS");
    nt_gfx_segment_state_t *seg = &s_segments[s_segment_count];
    seg->name_hash = name_hash;
    NT_GL_GEN(glGenQueries, NT_GFX_TIMER_RING, seg->queries);
    for (uint8_t i = 0; i < NT_GFX_TIMER_RING; i++) {
        if (seg->queries[i] == 0) {
            /* A lost context generates name 0, and beginQuery throws on it; the slot stays free. */
            return -1;
        }
    }
    memset(seg->in_flight, 0, sizeof(seg->in_flight));
    seg->head = 0;
    seg->tail = 0;
    return (int8_t)(s_segment_count++);
}

static int8_t segment_find(nt_hash32_t name_hash) {
    for (uint8_t i = 0; i < s_segment_count; i++) {
        if (s_segments[i].name_hash.value == name_hash.value) {
            return (int8_t)i;
        }
    }
    return -1;
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity) -- diagnostic record macros expand at owning sites
void nt_gfx_backend_begin_segment(const char *name) {
    if (!s_timer_enabled || !s_timer_user_enabled) {
        return;
    }
    nt_hash32_t name_hash = nt_hash32_str(name);
    NT_ASSERT(s_active_segment < 0 && "GL_TIME_ELAPSED cannot nest — close current segment first");
    int8_t idx = segment_find_or_alloc(name_hash);
    if (idx < 0) {
        return;
    }
    nt_gfx_segment_state_t *seg = &s_segments[idx];
    if (seg->in_flight[seg->head]) {
        /* Ring full — try to drain oldest first; it's likely ready by now. Only reset
         * (data loss) if even the oldest isn't available, which means GPU pipeline is
         * genuinely stuck (driver issue or background-tab freeze). */
        GLuint q_old = seg->queries[seg->tail];
        GLuint avail = 0;
        NT_GL(glGetQueryObjectuiv, q_old, GL_QUERY_RESULT_AVAILABLE, &avail);
        if (avail) {
            seg->in_flight[seg->tail] = false;
            seg->tail = (uint8_t)((seg->tail + 1U) % NT_GFX_TIMER_RING);
        } else {
            if (!s_timer_warned) {
                NT_LOG_WARN("gpu_timing: segment ring full and oldest query not ready — dropping pipeline");
                s_timer_warned = true;
            }
            memset(seg->in_flight, 0, sizeof(seg->in_flight));
            seg->head = 0;
            seg->tail = 0;
        }
    }
    /* Push debug group BEFORE glBeginQuery so RenderDoc / Apitrace shows the
     * named group around both the query and the wrapped draw calls.
     * Compiled out on WebGL — KHR_debug isn't in the WebGL2 spec, the
     * runtime probe always returns false there, and the symbols don't
     * exist in <GLES3/gl3.h>. */
#ifndef NT_PLATFORM_WEB
    if (s_debug_groups_enabled) {
        NT_GL(glPushDebugGroup, GL_DEBUG_SOURCE_APPLICATION, name_hash.value, -1, name);
    }
#endif
    NT_GL(glBeginQuery, GL_TIME_ELAPSED, seg->queries[seg->head]);
    s_active_segment = idx;
}

void nt_gfx_backend_end_segment(void) {
    if (s_active_segment < 0) {
        return;
    }
    nt_gfx_segment_state_t *seg = &s_segments[s_active_segment];
    NT_GL(glEndQuery, GL_TIME_ELAPSED);
#ifndef NT_PLATFORM_WEB
    if (s_debug_groups_enabled) {
        NT_GL0(glPopDebugGroup);
    }
#endif
    seg->in_flight[seg->head] = true;
    seg->head = (uint8_t)((seg->head + 1U) % NT_GFX_TIMER_RING);
    s_active_segment = -1;
}
#else
void nt_gfx_backend_begin_segment(const char *name) { (void)name; }
void nt_gfx_backend_end_segment(void) {}
#endif
// #endregion

void nt_gfx_backend_check_timer_disjoint(void) {
#if NT_GFX_GPU_TIMING_ENABLED && defined(NT_PLATFORM_WEB)
    if (s_timer_enabled && s_timer_user_enabled) {
        /* A full ring has head == tail too; in_flight owns pending state. */
        bool pending = s_active_segment >= 0;
        for (uint8_t i = 0; i < s_segment_count && !pending; i++) {
            pending = s_segments[i].in_flight[s_segments[i].tail];
        }
        if (pending) {
            GLint disjoint = 0;
            NT_GL(glGetIntegerv, GL_GPU_DISJOINT_EXT, &disjoint);
            if (disjoint) {
                nt_gfx_backend_end_segment();
                for (uint8_t i = 0; i < s_segment_count; i++) {
                    memset(s_segments[i].in_flight, 0, sizeof(s_segments[i].in_flight));
                    s_segments[i].tail = s_segments[i].head;
                }
            }
        }
    }
#endif
}

// #region GPU timer segments — poll/lifecycle
#if NT_GFX_GPU_TIMING_ENABLED
bool nt_gfx_backend_poll_segment_time_ns(const char *name, uint64_t *out_ns) {
    if (!s_timer_enabled || !s_timer_user_enabled) {
        return false;
    }
    nt_hash32_t name_hash = nt_hash32_str(name);

    /* The disjoint flag is checked once per frame at begin_frame, not per poll: a JS roundtrip each. */

    int8_t idx = segment_find(name_hash);
    if (idx < 0) {
        return false;
    }
    nt_gfx_segment_state_t *seg = &s_segments[idx];
    if (!seg->in_flight[seg->tail]) {
        return false;
    }

    GLuint q = seg->queries[seg->tail];
    GLuint available = 0;
    NT_GL(glGetQueryObjectuiv, q, GL_QUERY_RESULT_AVAILABLE, &available);
    if (!available) {
        return false;
    }

#ifdef NT_PLATFORM_WEB
    /* The JS bridge issues the same 64-bit result read. */
    NT_GL_ISSUED(glGetQueryObjectui64v, q, GL_QUERY_RESULT, (const void *)out_ns);
    *out_ns = (uint64_t)nt_gfx_gl_ctx_query_result(q);
#else
    GLuint64 result = 0;
    NT_GL(glGetQueryObjectui64v, q, GL_QUERY_RESULT, &result);
    *out_ns = (uint64_t)result;
#endif
    seg->in_flight[seg->tail] = false;
    seg->tail = (uint8_t)((seg->tail + 1U) % NT_GFX_TIMER_RING);
    /* Re-arm ring-full warning: a successful drain means the system recovered.
     * If it breaks again later, we want to see the warn again. */
    s_timer_warned = false;
    return true;
}

void nt_gfx_backend_drop_timer_segments(void) {
    /* Context is gone — forget queries without glDeleteQueries (would error). */
    s_segment_count = 0;
    s_active_segment = -1;
    s_timer_warned = false;
}

void nt_gfx_backend_set_gpu_timing_enabled(bool enabled) {
    if (!enabled && s_timer_user_enabled && s_timer_enabled) {
        if (nt_gfx_gl_ctx_query_lost()) {
            nt_gfx_backend_drop_timer_segments();
        } else {
            nt_gfx_backend_end_segment();
            for (uint8_t i = 0; i < s_segment_count; i++) {
                memset(s_segments[i].in_flight, 0, sizeof(s_segments[i].in_flight));
                s_segments[i].head = 0;
                s_segments[i].tail = 0;
            }
        }
    }
    if (enabled && !s_timer_user_enabled) {
        s_timer_warned = false;
    }
    s_timer_user_enabled = enabled;
}

bool nt_gfx_backend_is_gpu_timing_supported(void) { return s_timer_enabled; }
#else
bool nt_gfx_backend_poll_segment_time_ns(const char *name, uint64_t *out_ns) {
    (void)name;
    *out_ns = 0;
    return false;
}
void nt_gfx_backend_drop_timer_segments(void) {}
void nt_gfx_backend_set_gpu_timing_enabled(bool enabled) { (void)enabled; }
bool nt_gfx_backend_is_gpu_timing_supported(void) { return false; }
#endif
// #endregion

/* Attachment enums differ between the window framebuffer and user FBOs. Missing attachments are ignored by GL. */
static void invalidate_attachments(GLbitfield mask) {
    if (mask == 0) {
        return;
    }
#ifndef NT_PLATFORM_WEB
    if (glad_glInvalidateFramebuffer == NULL) {
        return;
    }
#endif
    GLenum attachments[3];
    GLsizei count = 0;
    if ((mask & GL_COLOR_BUFFER_BIT) != 0) {
        attachments[count++] = s_bound_framebuffer != 0 ? GL_COLOR_ATTACHMENT0 : GL_COLOR;
    }
    if ((mask & GL_DEPTH_BUFFER_BIT) != 0) {
        attachments[count++] = s_bound_framebuffer != 0 ? GL_DEPTH_ATTACHMENT : GL_DEPTH;
    }
    if ((mask & GL_STENCIL_BUFFER_BIT) != 0) {
        attachments[count++] = s_bound_framebuffer != 0 ? GL_STENCIL_ATTACHMENT : GL_STENCIL;
    }
    NT_GL_ATTACHMENTS(glInvalidateFramebuffer, GL_FRAMEBUFFER, count, attachments);
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void nt_gfx_backend_begin_pass(const nt_pass_desc_t *desc, uint32_t render_target_backend, uint16_t width, uint16_t height) {
    NT_ASSERT(desc != NULL);
    GLsizei viewport_w = (GLsizei)g_nt_window.fb_width;
    GLsizei viewport_h = (GLsizei)g_nt_window.fb_height;
    GLuint fbo = 0;
    if (render_target_backend != 0) {
        NT_ASSERT(render_target_backend <= s_init_desc.max_render_targets && s_render_target_gl[render_target_backend] != 0 && "begin_pass: requires a live render target");
        fbo = s_render_target_gl[render_target_backend];
        viewport_w = (GLsizei)width;
        viewport_h = (GLsizei)height;
    }
    if (s_bound_framebuffer != fbo) {
        NT_GL(glBindFramebuffer, GL_FRAMEBUFFER, fbo);
        s_bound_framebuffer = fbo;
    }
    gl_set_viewport(0, 0, (int)viewport_w, (int)viewport_h);
    /* Stencil has no pass API; it shares the depth lifetime so a packed depth-stencil buffer is dropped whole. */
    s_pass_discard = (desc->discard_color ? GL_COLOR_BUFFER_BIT : 0U) | (desc->discard_depth ? GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT : 0U);
    GLbitfield clear = 0;
    if (!desc->load_color) {
        clear |= GL_COLOR_BUFFER_BIT;
        if (!float4_equal(s_gl_cache.clear_color, desc->clear_color)) {
            memcpy(s_gl_cache.clear_color, desc->clear_color, sizeof(s_gl_cache.clear_color));
            NT_GL(glClearColor, desc->clear_color[0], desc->clear_color[1], desc->clear_color[2], desc->clear_color[3]);
        }
    }
    if (!desc->load_depth) {
        clear |= GL_DEPTH_BUFFER_BIT;
        if (s_gl_cache.clear_depth != desc->clear_depth) {
            s_gl_cache.clear_depth = desc->clear_depth;
            nt_gl_clear_depth(desc->clear_depth);
        }
        /* Depth clear ignores the previous draw's write mask; the next pipeline restores its own. */
        if (!s_gl_cache.depth_write_enabled) {
            NT_GL(glDepthMask, GL_TRUE);
            s_gl_cache.depth_write_enabled = true;
        }
    }
    if (clear != 0) {
        NT_GL(glClear, clear); /* scissor is off: the front-end disables it before every pass */
    }
}

void nt_gfx_backend_end_pass(void) {
    invalidate_attachments(s_pass_discard);
    s_pass_discard = 0;
    if (s_bound_framebuffer != 0) {
        NT_GL(glBindFramebuffer, GL_FRAMEBUFFER, 0);
        s_bound_framebuffer = 0;
    }
}

void nt_gfx_backend_clear(const nt_clear_desc_t *desc) {
    GLbitfield mask = 0;
    if (desc->color) {
        mask |= GL_COLOR_BUFFER_BIT;
        if (!float4_equal(s_gl_cache.clear_color, desc->clear_color)) {
            memcpy(s_gl_cache.clear_color, desc->clear_color, sizeof(s_gl_cache.clear_color));
            NT_GL(glClearColor, desc->clear_color[0], desc->clear_color[1], desc->clear_color[2], desc->clear_color[3]);
        }
    }
    if (desc->depth) {
        mask |= GL_DEPTH_BUFFER_BIT;
        if (s_gl_cache.clear_depth != desc->clear_depth) {
            s_gl_cache.clear_depth = desc->clear_depth;
            nt_gl_clear_depth(desc->clear_depth);
        }
    }
    if (mask == 0) {
        return;
    }
    /* Unlike pass initialization, an in-pass clear must preserve the bound pipeline's mask. */
    const bool restore_depth_mask = desc->depth && !s_gl_cache.depth_write_enabled;
    if (restore_depth_mask) {
        NT_GL(glDepthMask, GL_TRUE);
    }
    NT_GL(glClear, mask);
    if (restore_depth_mask) {
        NT_GL(glDepthMask, GL_FALSE);
    }
}

/* ---- Scissor and viewport ----
 *
 * Raw GL bottom-left convention. Callers are expected to y-flip if they
 * think in top-left space. */

/* Uncached: the UI emits a distinct rect per clipped element, so a diff never hits. */
void nt_gfx_backend_set_scissor(int x, int y, int w, int h) { NT_GL(glScissor, x, y, (GLsizei)w, (GLsizei)h); }

void nt_gfx_backend_set_scissor_enabled(bool enabled) {
    if (enabled) {
        NT_GL(glEnable, GL_SCISSOR_TEST);
    } else {
        NT_GL(glDisable, GL_SCISSOR_TEST);
    }
}

void nt_gfx_backend_set_viewport(int x, int y, int w, int h) { gl_set_viewport(x, y, w, h); }

/* Raw GL readback, bottom-left origin. Y-flip to top-left is done once in
 * the shared layer (nt_gfx_read_pixels). rgba8 rows are 4*w bytes -> already
 * 4-aligned; set GL_PACK_ALIGNMENT=4 explicitly so it never depends on state. */
bool nt_gfx_backend_read_pixels(uint32_t render_target_backend, int x, int y, int w, int h, void *out_rgba8) {
    /* Between frames the window is bound; a target is bound for this read only. */
    const GLuint fbo = render_target_backend != 0 ? s_render_target_gl[render_target_backend] : 0;
    if (fbo != 0) {
        NT_GL(glBindFramebuffer, GL_FRAMEBUFFER, fbo);
    }
    NT_GL(glPixelStorei, GL_PACK_ALIGNMENT, 4);
    /* Drain any stale GL error so the post-read check is attributable to THIS readback. */
    nt_gfx_gl_drain_errors();
    NT_GL(glReadPixels, x, y, (GLsizei)w, (GLsizei)h, GL_RGBA, GL_UNSIGNED_BYTE, out_rgba8);
    /* A GL error here means a lost context (asserted below); false keeps the capture from encoding
       the untouched buffer. */
    const bool ok = NT_GL_RET0(glGetError) == GL_NO_ERROR;
    if (!ok) {
        const bool lost = nt_gfx_gl_ctx_query_lost();
        NT_ASSERT(lost && "read_pixels: GL error on a live context");
    }
    if (fbo != 0) {
        NT_GL(glBindFramebuffer, GL_FRAMEBUFFER, 0);
    }
    return ok;
}

#if !defined(NT_PLATFORM_WEB)
// #region buffer readback
/* BGRA is the desktop drivers' window format: RGBA would make the CPU swizzle every pixel. */
void nt_gfx_backend_read_pixels_to_buffer(uint32_t render_target_backend, int x, int y, int w, int h, uint32_t buffer_backend) {
    const GLuint fbo = render_target_backend != 0 ? s_render_target_gl[render_target_backend] : 0;
    if (fbo != 0) {
        NT_GL(glBindFramebuffer, GL_FRAMEBUFFER, fbo);
    }
    NT_GL(glBindBuffer, GL_PIXEL_PACK_BUFFER, s_buffer_gl[buffer_backend]);
    NT_GL(glPixelStorei, GL_PACK_ALIGNMENT, 4);
    NT_GL(glReadPixels, x, y, (GLsizei)w, (GLsizei)h, GL_BGRA, GL_UNSIGNED_BYTE, NULL); /* NULL: offset 0 into the bound pack buffer */
    NT_GL(glBindBuffer, GL_PIXEL_PACK_BUFFER, 0);
    if (fbo != 0) {
        NT_GL(glBindFramebuffer, GL_FRAMEBUFFER, 0);
    }
    if (s_buffer_fence[buffer_backend] != NULL) {
        NT_GL(glDeleteSync, s_buffer_fence[buffer_backend]);
    }
    s_buffer_fence[buffer_backend] = NT_GL_RET(glFenceSync, GL_SYNC_GPU_COMMANDS_COMPLETE, 0U);
}

bool nt_gfx_backend_readback_ready(uint32_t buffer_backend, bool wait) {
    GLsync fence = s_buffer_fence[buffer_backend];
    /* The flush makes a fence the driver still holds reach the GPU, so a later wait can end. */
    GLenum status = NT_GL_RET(glClientWaitSync, fence, GL_SYNC_FLUSH_COMMANDS_BIT, (GLuint64)0);
    while (wait && status == GL_TIMEOUT_EXPIRED) {
        status = NT_GL_RET(glClientWaitSync, fence, 0U, (GLuint64)1000000000);
    }
    if (status == GL_TIMEOUT_EXPIRED) {
        return false;
    }
    NT_ASSERT(status != GL_WAIT_FAILED && "readback: fence wait failed");
    NT_GL(glDeleteSync, fence);
    s_buffer_fence[buffer_backend] = NULL;
    return true;
}

const void *nt_gfx_backend_map_buffer(uint32_t buffer_backend, uint32_t size) {
    NT_GL(glBindBuffer, GL_PIXEL_PACK_BUFFER, s_buffer_gl[buffer_backend]);
    const void *bytes = NT_GL_RET(glMapBufferRange, GL_PIXEL_PACK_BUFFER, (GLintptr)0, (GLsizeiptr)size, (GLbitfield)GL_MAP_READ_BIT);
    NT_GL(glBindBuffer, GL_PIXEL_PACK_BUFFER, 0);
    NT_ASSERT(bytes != NULL && "map_buffer: map failed");
    return bytes;
}

void nt_gfx_backend_unmap_buffer(uint32_t buffer_backend) {
    NT_GL(glBindBuffer, GL_PIXEL_PACK_BUFFER, s_buffer_gl[buffer_backend]);
    (void)NT_GL_RET(glUnmapBuffer, GL_PIXEL_PACK_BUFFER);
    NT_GL(glBindBuffer, GL_PIXEL_PACK_BUFFER, 0);
}
// #endregion
#endif

/* ---- Pipeline bind ---- */

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void nt_gfx_backend_bind_pipeline(uint32_t backend_handle) {
    NT_ASSERT(backend_handle != 0 && backend_handle <= s_init_desc.max_pipelines && "bind_pipeline: handle out of range");
    nt_gfx_gl_pipeline_t *pip = &s_pipelines[backend_handle];
    /* A zeroed record (context loss, destroyed pipeline) would bind program 0
     * and turn every following draw into a silent GL_INVALID_OPERATION. */
    NT_ASSERT(pip->program_slot != 0 && pip->program_slot <= s_init_desc.max_programs && "bind_pipeline: pipeline record without a live program");

    GLuint program = s_programs[pip->program_slot].program;
    if (s_gl_cache.program != program) {
        NT_GL(glUseProgram, program);
        s_gl_cache.program = program;
    } else {
        NT_GFX_RECORD(NT_GFX_EVENT_SKIP, NT_GFX_OP_PIPELINE, event->result = NT_GFX_RESULT_CACHE; event->detail = NT_GFX_GL_glUseProgram; event->data.backend.args[0] = program;);
    }

    /* Depth test */
    if (s_gl_cache.depth_test_enabled != pip->depth_test_enabled) {
        if (pip->depth_test_enabled) {
            NT_GL(glEnable, GL_DEPTH_TEST);
        } else {
            NT_GL(glDisable, GL_DEPTH_TEST);
        }
        s_gl_cache.depth_test_enabled = pip->depth_test_enabled;
    }
    if (pip->depth_test_enabled && s_gl_cache.depth_func != pip->depth_func) {
        NT_GL(glDepthFunc, pip->depth_func);
        s_gl_cache.depth_func = pip->depth_func;
    }
    if (s_gl_cache.depth_write_enabled != pip->depth_write_enabled) {
        NT_GL(glDepthMask, pip->depth_write_enabled ? GL_TRUE : GL_FALSE);
        s_gl_cache.depth_write_enabled = pip->depth_write_enabled;
    }

    /* Cull mode */
    if (s_gl_cache.cull_mode != pip->cull_mode) {
        if (pip->cull_mode == 0) {
            NT_GL(glDisable, GL_CULL_FACE);
        } else {
            NT_GL(glEnable, GL_CULL_FACE);
            NT_GL(glCullFace, pip->cull_mode == 2 ? GL_FRONT : GL_BACK);
        }
        s_gl_cache.cull_mode = pip->cull_mode;
    }

    /* Blend */
    if (s_gl_cache.blend_enabled != pip->blend_enabled) {
        if (pip->blend_enabled) {
            NT_GL(glEnable, GL_BLEND);
        } else {
            NT_GL(glDisable, GL_BLEND);
        }
        s_gl_cache.blend_enabled = pip->blend_enabled;
    }
    if (pip->blend_enabled && (s_gl_cache.blend_src_rgb != pip->blend_src_rgb || s_gl_cache.blend_dst_rgb != pip->blend_dst_rgb || s_gl_cache.blend_src_alpha != pip->blend_src_alpha ||
                               s_gl_cache.blend_dst_alpha != pip->blend_dst_alpha)) {
        NT_GL(glBlendFuncSeparate, pip->blend_src_rgb, pip->blend_dst_rgb, pip->blend_src_alpha, pip->blend_dst_alpha);
        s_gl_cache.blend_src_rgb = pip->blend_src_rgb;
        s_gl_cache.blend_dst_rgb = pip->blend_dst_rgb;
        s_gl_cache.blend_src_alpha = pip->blend_src_alpha;
        s_gl_cache.blend_dst_alpha = pip->blend_dst_alpha;
    }
    if (pip->blend_enabled && (s_gl_cache.blend_op_rgb != pip->blend_op_rgb || s_gl_cache.blend_op_alpha != pip->blend_op_alpha)) {
        NT_GL(glBlendEquationSeparate, pip->blend_op_rgb, pip->blend_op_alpha);
        s_gl_cache.blend_op_rgb = pip->blend_op_rgb;
        s_gl_cache.blend_op_alpha = pip->blend_op_alpha;
    }
    if (pip->blend_enabled && !float4_equal(s_gl_cache.blend_constant_color, pip->blend_constant_color)) {
        NT_GL(glBlendColor, pip->blend_constant_color[0], pip->blend_constant_color[1], pip->blend_constant_color[2], pip->blend_constant_color[3]);
        memcpy(s_gl_cache.blend_constant_color, pip->blend_constant_color, sizeof(pip->blend_constant_color));
    }

    /* Polygon offset */
    if (s_gl_cache.polygon_offset_enabled != pip->polygon_offset_enabled) {
        if (pip->polygon_offset_enabled) {
            NT_GL(glEnable, GL_POLYGON_OFFSET_FILL);
        } else {
            NT_GL(glDisable, GL_POLYGON_OFFSET_FILL);
        }
        s_gl_cache.polygon_offset_enabled = pip->polygon_offset_enabled;
    }
    if (pip->polygon_offset_enabled && (s_gl_cache.polygon_offset_factor != pip->polygon_offset_factor || s_gl_cache.polygon_offset_units != pip->polygon_offset_units)) {
        NT_GL(glPolygonOffset, pip->polygon_offset_factor, pip->polygon_offset_units);
        s_gl_cache.polygon_offset_factor = pip->polygon_offset_factor;
        s_gl_cache.polygon_offset_units = pip->polygon_offset_units;
    }
}

/* ---- Draw calls ---- */

void nt_gfx_backend_draw(uint32_t first_vertex, uint32_t num_vertices, uint32_t instance_count) {
    if (instance_count == 1) {
        NT_GL(glDrawArrays, GL_TRIANGLES, (GLint)first_vertex, (GLsizei)num_vertices);
    } else {
        NT_GL(glDrawArraysInstanced, GL_TRIANGLES, (GLint)first_vertex, (GLsizei)num_vertices, (GLsizei)instance_count);
    }
}

void nt_gfx_backend_draw_indexed(uint32_t first_index, uint32_t num_indices, uint32_t instance_count, uint8_t index_type) {
    const bool wide = index_type == NT_INDEX_UINT32;
    const GLenum gl_type = wide ? GL_UNSIGNED_INT : GL_UNSIGNED_SHORT;
    const void *offset = nt_gl_offset((uintptr_t)first_index * (wide ? sizeof(uint32_t) : sizeof(uint16_t)));
    if (instance_count == 1) {
        NT_GL(glDrawElements, GL_TRIANGLES, (GLsizei)num_indices, gl_type, offset);
    } else {
        NT_GL(glDrawElementsInstanced, GL_TRIANGLES, (GLsizei)num_indices, gl_type, offset, (GLsizei)instance_count);
    }
}

static NT_NOINLINE uint32_t gl_limit(GLenum name) {
    GLint value = 0;
    NT_GL(glGetIntegerv, name, &value);
    return (uint32_t)value;
}

void nt_gfx_gl_read_limits(nt_gfx_gpu_caps_t *caps) {
    caps->max_texture_size = gl_limit(GL_MAX_TEXTURE_SIZE);
    caps->uniform_buffer_offset_alignment = gl_limit(GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT);
    caps->max_varying_components = gl_limit(GL_MAX_VARYING_COMPONENTS);
    caps->max_vertex_uniform_components = gl_limit(GL_MAX_VERTEX_UNIFORM_COMPONENTS);
    caps->max_fragment_uniform_components = gl_limit(GL_MAX_FRAGMENT_UNIFORM_COMPONENTS);
    caps->max_vertex_uniform_blocks = gl_limit(GL_MAX_VERTEX_UNIFORM_BLOCKS);
    caps->max_fragment_uniform_blocks = gl_limit(GL_MAX_FRAGMENT_UNIFORM_BLOCKS);
    caps->max_uniform_block_size = gl_limit(GL_MAX_UNIFORM_BLOCK_SIZE);
}

/* ---- Resource management (shader / buffer / pipeline) ---- */

uint32_t nt_gfx_backend_create_shader(const nt_shader_desc_t *desc) {
    GLenum gl_type = (desc->type == NT_SHADER_VERTEX) ? GL_VERTEX_SHADER : GL_FRAGMENT_SHADER;
    GLuint shader = NT_GL_RET(glCreateShader, gl_type);
    if (shader == 0) {
        assert_name_failed_on_loss(desc->type == NT_SHADER_VERTEX ? "glCreateShader vertex" : "glCreateShader fragment", desc->label);
        return 0;
    }
    /* Emscripten gives WebGL's null shader a nonzero name, and glShaderSource throws on it: only
     * the browser flag shows the loss. */
    if (nt_gfx_gl_ctx_query_lost()) {
        return 0;
    }

#ifdef NT_PLATFORM_WEB
    const char *prefix = "#version 300 es\n";
#else
    const char *prefix = "#version 330 core\n";
#endif
    const char *sources[2] = {prefix, desc->source};
    NT_GL(glShaderSource, shader, 2, sources, NULL);

    NT_GL(glCompileShader, shader);
    /* Per MDN best practice: do NOT check GL_COMPILE_STATUS here.
     * Checking forces synchronous compilation.  Errors surface when nt_gfx_begin_frame finishes the link. */
    return (uint32_t)shader;
}

/* Log shader/program info line-by-line (query actual length, no fixed buffer) */
#if NT_LOG_MIN_LEVEL <= 2
static void nt_gfx_gl_log_lines(const char *log) {
    const char *line = log;
    for (const char *p = log; *p; p++) {
        if (*p == '\n') {
            if (p > line) {
                NT_LOG_ERROR("%.*s", (int)(p - line), line);
            }
            line = p + 1;
        }
    }
    if (*line) {
        NT_LOG_ERROR("%s", line);
    }
}

static void nt_gfx_gl_log_shader(uint32_t shader, const char *stage) {
    GLint len = 0;
    NT_GL(glGetShaderiv, (GLuint)shader, GL_INFO_LOG_LENGTH, &len);
    if (len <= 1) {
        return;
    }
    char *log = (char *)malloc((size_t)len);
    if (!log) {
        return;
    }
    NT_GL(glGetShaderInfoLog, (GLuint)shader, len, NULL, log);
    NT_LOG_ERROR("%s shader:", stage);
    nt_gfx_gl_log_lines(log);
    free(log);
}

static void nt_gfx_gl_log_program(uint32_t program) {
    GLint len = 0;
    NT_GL(glGetProgramiv, (GLuint)program, GL_INFO_LOG_LENGTH, &len);
    if (len <= 1) {
        return;
    }
    char *log = (char *)malloc((size_t)len);
    if (!log) {
        return;
    }
    NT_GL(glGetProgramInfoLog, (GLuint)program, len, NULL, log);
    NT_LOG_ERROR("program link:");
    nt_gfx_gl_log_lines(log);
    free(log);
}

#endif

void nt_gfx_backend_destroy_shader(uint32_t backend_handle) {
    /* Zero reaches here from nt_gfx_shutdown's sweep over free slots, and that
     * sweep also runs when nt_gfx_init failed before glad loaded an entry point. */
    if (backend_handle == 0) {
        return;
    }
    NT_GL(glDeleteShader, (GLuint)backend_handle);
}

static void nt_gfx_gl_write_array_index(char *suffix, GLint element) {
    char digits[10];
    uint8_t count = 0;
    do {
        digits[count++] = (char)('0' + (element % 10));
        element /= 10;
    } while (element != 0);
    *suffix++ = '[';
    while (count != 0) {
        *suffix++ = digits[--count];
    }
    *suffix++ = ']';
    *suffix = '\0';
}

/* A field report keeps only logs, so the record names the program and the value the driver returned.
 * The unbounded label goes last: the log line cap truncates it, not the cause. */
static NT_NOINLINE void log_program_failure(const nt_gfx_gl_program_t *rec, const char *phase, const char *reason, const char *name, uint32_t value) {
    (void)rec;
    (void)phase;
    (void)reason;
    (void)name;
    (void)value;
    NT_LOG_ERROR("program %u/GL %u %s: %s: %s=0x%X \"%s\"", (unsigned)(rec - s_programs), rec->program, phase, reason, name, value,
                 rec->label != NULL && rec->label[0] != '\0' ? rec->label : "<unnamed>");
}

/* Driver-result checks log before TRAP discards the assert expression. */
#define NT_GFX_PROGRAM_ASSERT(rec, phase, condition, reason, name, value)                                                                                                                              \
    do {                                                                                                                                                                                               \
        if (!(condition)) {                                                                                                                                                                            \
            log_program_failure((rec), (phase), (reason), (name), (uint32_t)(value));                                                                                                                  \
            NT_ASSERT((condition) && (reason) != NULL);                                                                                                                                                \
        }                                                                                                                                                                                              \
    } while (0)

#if NT_LOG_MIN_LEVEL <= 2
static GLint stage_compile_status(GLuint shader) {
    if (!NT_GL_RET(glIsShader, shader)) {
        return -1;
    }
    GLint compiled = 0;
    NT_GL(glGetShaderiv, shader, GL_COMPILE_STATUS, &compiled);
    return compiled;
}

#ifdef NT_PLATFORM_WEB
/* The browser's translation is what the driver compiled; it can differ per GPU. */
static void log_translated_source(GLuint shader, const char *stage) {
    char text[2048];
    if (nt_gfx_gl_ctx_translated_source(shader, text, sizeof(text))) {
        NT_LOG_ERROR("%s translated:", stage);
        nt_gfx_gl_log_lines(text);
    }
}
#endif
#endif

/* Compile errors surface only here: shader creation never reads COMPILE_STATUS. */
static void nt_gfx_gl_log_link_failure(const nt_gfx_gl_program_t *rec) {
#if NT_LOG_MIN_LEVEL <= 2
    const GLenum gl_error = NT_GL_RET0(glGetError);
    const GLint vs_compile = stage_compile_status(rec->vs);
    const GLint fs_compile = stage_compile_status(rec->fs);
#ifdef NT_PLATFORM_WEB
    /* Before the driver logs, so a journal that keeps its tail drops this text first. */
    if (vs_compile >= 0) {
        log_translated_source(rec->vs, "vertex");
    }
    if (fs_compile >= 0) {
        log_translated_source(rec->fs, "fragment");
    }
#endif
    nt_gfx_gl_log_program(rec->program);
    /* -1: the owner destroyed the stage and Emscripten dropped its name; native GL keeps an attached stage readable. */
    if (vs_compile >= 0) {
        nt_gfx_gl_log_shader(rec->vs, "vertex");
    }
    if (fs_compile >= 0) {
        nt_gfx_gl_log_shader(rec->fs, "fragment");
    }
    /* After the driver lines, so a journal that keeps its tail keeps the classification and identity. */
    NT_LOG_ERROR("program link stages: vs_compile=%d fs_compile=%d", vs_compile, fs_compile);
    log_program_failure(rec, "link", "link failed", "gl_error", gl_error);
#else
    (void)rec;
#endif
}

typedef enum { UNIFORM_NOT_SAMPLER, UNIFORM_SAMPLER, UNIFORM_SIGNED_SAMPLER, UNIFORM_UNSUPPORTED_SAMPLER } uniform_kind_t;

/* Only 2D samplers have a texture-unit story here; the rest would bind
 * silently wrong, so they are rejected at link instead. */
static uniform_kind_t uniform_kind(GLenum utype, nt_gfx_sampler_class_t *out_class) {
    switch (utype) {
    case GL_SAMPLER_2D:
        *out_class = NT_GFX_SAMPLER_CLASS_FLOAT;
        return UNIFORM_SAMPLER;
    case GL_SAMPLER_2D_SHADOW:
        *out_class = NT_GFX_SAMPLER_CLASS_SHADOW;
        return UNIFORM_SAMPLER;
    case GL_UNSIGNED_INT_SAMPLER_2D:
        *out_class = NT_GFX_SAMPLER_CLASS_UINT;
        return UNIFORM_SAMPLER;
    case GL_INT_SAMPLER_2D:
        return UNIFORM_SIGNED_SAMPLER;
    case GL_SAMPLER_3D:
    case GL_SAMPLER_CUBE:
    case GL_SAMPLER_CUBE_SHADOW:
    case GL_SAMPLER_2D_ARRAY:
    case GL_SAMPLER_2D_ARRAY_SHADOW:
    case GL_INT_SAMPLER_3D:
    case GL_INT_SAMPLER_CUBE:
    case GL_INT_SAMPLER_2D_ARRAY:
    case GL_UNSIGNED_INT_SAMPLER_3D:
    case GL_UNSIGNED_INT_SAMPLER_CUBE:
    case GL_UNSIGNED_INT_SAMPLER_2D_ARRAY:
#ifndef NT_PLATFORM_WEB
    /* Desktop-only families; absent from the GLES3 headers. */
    case GL_SAMPLER_CUBE_MAP_ARRAY_ARB:
    case GL_SAMPLER_CUBE_MAP_ARRAY_SHADOW_ARB:
    case GL_INT_SAMPLER_CUBE_MAP_ARRAY_ARB:
    case GL_UNSIGNED_INT_SAMPLER_CUBE_MAP_ARRAY_ARB:
    case GL_SAMPLER_1D:
    case GL_SAMPLER_1D_SHADOW:
    case GL_SAMPLER_1D_ARRAY:
    case GL_SAMPLER_1D_ARRAY_SHADOW:
    case GL_SAMPLER_2D_RECT:
    case GL_SAMPLER_2D_RECT_SHADOW:
    case GL_SAMPLER_2D_MULTISAMPLE:
    case GL_SAMPLER_2D_MULTISAMPLE_ARRAY:
    case GL_SAMPLER_BUFFER:
    case GL_INT_SAMPLER_1D:
    case GL_INT_SAMPLER_1D_ARRAY:
    case GL_INT_SAMPLER_2D_RECT:
    case GL_INT_SAMPLER_2D_MULTISAMPLE:
    case GL_INT_SAMPLER_2D_MULTISAMPLE_ARRAY:
    case GL_INT_SAMPLER_BUFFER:
    case GL_UNSIGNED_INT_SAMPLER_1D:
    case GL_UNSIGNED_INT_SAMPLER_1D_ARRAY:
    case GL_UNSIGNED_INT_SAMPLER_2D_RECT:
    case GL_UNSIGNED_INT_SAMPLER_2D_MULTISAMPLE:
    case GL_UNSIGNED_INT_SAMPLER_2D_MULTISAMPLE_ARRAY:
    case GL_UNSIGNED_INT_SAMPLER_BUFFER:
#endif
        return UNIFORM_UNSUPPORTED_SAMPLER;
    default:
        return UNIFORM_NOT_SAMPLER;
    }
}

/* Checks uniforms and reflects sampler units off the hot path; NT_ASSERT expansion inflates complexity. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static bool nt_gfx_gl_reflect_uniforms(GLuint program, nt_gfx_gl_program_t *rec) {
    rec->sampler_count = 0;
    GLint active_uniforms = -1;
    NT_GL(glGetProgramiv, program, GL_ACTIVE_UNIFORMS, &active_uniforms);
    if (active_uniforms < 0) {
        return false;
    }
    if (active_uniforms == 0) {
        return true;
    }
    GLint max_name_length = 0;
    NT_GL(glGetProgramiv, program, GL_ACTIVE_UNIFORM_MAX_LENGTH, &max_name_length);
    if (max_name_length <= 0) {
        return false;
    }
    /* Expanded indices can be wider than reflection's terminal [0]. */
    NT_GFX_PROGRAM_ASSERT(rec, "uniforms", (size_t)max_name_length + 10U <= NT_GFX_GL_MAX_UNIFORM_NAME, "uniform name too long", "max_len", max_name_length);
    char uname[NT_GFX_GL_MAX_UNIFORM_NAME];
    for (GLint ui = 0; ui < active_uniforms; ui++) {
        GLsizei ulen = 0;
        GLint usize = 0;
        GLenum utype = 0;
        NT_GL(glGetActiveUniform, program, (GLuint)ui, max_name_length, &ulen, &usize, &utype, uname);
        if (ulen <= 0 || usize <= 0) {
            return false;
        }
        /* Block members are fed by their buffer, not by location writes. Emscripten
         * hands out a location for any active name, so the block index decides. */
        const GLuint index = (GLuint)ui;
        GLint block_index = -2; /* a lost context writes nothing */
        NT_GL(glGetActiveUniformsiv, program, 1, &index, GL_UNIFORM_BLOCK_INDEX, &block_index);
        if (block_index == -2) {
            return false;
        }
        if (block_index != -1) {
            /* These std140 members map directly to C arrays without padding between fields. */
            NT_GFX_PROGRAM_ASSERT(rec, "uniforms", utype == GL_FLOAT_VEC4 || utype == GL_INT_VEC4 || utype == GL_UNSIGNED_INT_VEC4 || utype == GL_FLOAT_MAT4, "block member not vec4/ivec4/uvec4/mat4",
                                  uname, utype);
            if (utype == GL_FLOAT_MAT4) {
                GLint row_major = -1;
                NT_GL(glGetActiveUniformsiv, program, 1, &index, GL_UNIFORM_IS_ROW_MAJOR, &row_major);
                if (row_major == -1) {
                    return false; /* the context was lost after the block index */
                }
                /* The C side is column-major. */
                NT_GFX_PROGRAM_ASSERT(rec, "uniforms", row_major == 0, "row_major block matrix", uname, row_major);
            }
            continue;
        }
        nt_gfx_sampler_class_t sampler_class = NT_GFX_SAMPLER_CLASS_FLOAT;
        const uniform_kind_t kind = uniform_kind(utype, &sampler_class);
        if (kind == UNIFORM_NOT_SAMPLER) {
            /* Built-ins (gl_DepthRange) are not program inputs; anything else could never receive a value. */
            NT_GFX_PROGRAM_ASSERT(rec, "uniforms", strncmp(uname, "gl_", 3) == 0, "standalone non-sampler uniform", uname, utype);
            continue;
        }
        NT_GFX_PROGRAM_ASSERT(rec, "uniforms", kind != UNIFORM_SIGNED_SAMPLER, "signed integer sampler", uname, utype);
        NT_GFX_PROGRAM_ASSERT(rec, "uniforms", kind != UNIFORM_UNSUPPORTED_SAMPLER, "unsupported sampler type", uname, utype);
        NT_GFX_PROGRAM_ASSERT(rec, "uniforms", usize == 1 || (ulen >= 3 && strcmp(uname + ulen - 3, "[0]") == 0), "sampler array without [0]", uname, usize);
        for (GLint element = 0; element < usize; element++) {
            if (element > 0) {
                size_t suffix = (size_t)ulen - 3U;
                nt_gfx_gl_write_array_index(uname + suffix, element);
            }
            GLint loc = NT_GL_RET(glGetUniformLocation, program, uname);
            if (loc >= 0) {
                NT_GFX_PROGRAM_ASSERT(rec, "uniforms", rec->sampler_count < NT_GFX_MAX_TEXTURE_SLOTS, "more samplers than NT_GFX_MAX_TEXTURE_SLOTS", uname, rec->sampler_count);
                const uint8_t unit = rec->sampler_count;
                rec->sampler_units[unit].name_hash = nt_hash32_str(uname).value;
                rec->sampler_units[unit].location = loc;
                rec->sampler_units[unit].sampler_class = (uint8_t)sampler_class;
                rec->sampler_count++;
            }
        }
    }
    return true;
}

/* Every active block binds to the slot of its name; an unknown name would sit on binding 0 and read
 * Globals. Names are unique per program and per slot (init asserts the list), so one block owns a slot. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity) -- NT_ASSERT expansion inflates the metric
static bool nt_gfx_gl_bind_blocks(GLuint program, nt_gfx_gl_program_t *rec) {
    memset(rec->block_sizes, 0, sizeof(rec->block_sizes));
    GLint block_count = -1;
    NT_GL(glGetProgramiv, program, GL_ACTIVE_UNIFORM_BLOCKS, &block_count);
    if (block_count <= 0) {
        return block_count == 0;
    }
    char name[NT_GFX_GL_MAX_UNIFORM_NAME];
    GLint max_name_length = 0;
    NT_GL(glGetProgramiv, program, GL_ACTIVE_UNIFORM_BLOCK_MAX_NAME_LENGTH, &max_name_length);
    /* A truncated name would match a listed prefix or report the wrong cause. */
    NT_GFX_PROGRAM_ASSERT(rec, "blocks", max_name_length <= (GLint)sizeof(name), "block name too long", "max_len", max_name_length);
    for (GLint bi = 0; bi < block_count; bi++) {
        GLsizei length = 0;
        NT_GL(glGetActiveUniformBlockName, program, (GLuint)bi, (GLsizei)sizeof(name), &length, name);
        GLint size = 0;
        NT_GL(glGetActiveUniformBlockiv, program, (GLuint)bi, GL_UNIFORM_BLOCK_DATA_SIZE, &size);
        if (length <= 0 || size <= 0) {
            return false;
        }
        const uint32_t slot = nt_gfx_block_slot(s_init_desc.game_blocks, name);
        NT_GFX_PROGRAM_ASSERT(rec, "blocks", slot < NT_GFX_MAX_UNIFORM_BUFFER_SLOTS, "block neither reserved nor in game_blocks", name, size);
        NT_GL(glUniformBlockBinding, program, (GLuint)bi, (GLuint)slot);
        rec->block_sizes[slot] = (uint32_t)size;
    }
    return true;
}

/* The units are written once, here: linking may happen with a pipeline bound,
 * and the cache mirror must keep naming that pipeline's program. */
static void write_sampler_units(GLuint program, const nt_gfx_gl_program_t *rec) {
    if (rec->sampler_count == 0) {
        return;
    }
    const GLuint saved = s_gl_cache.program;
    NT_GL(glUseProgram, program);
    for (uint8_t i = 0; i < rec->sampler_count; i++) {
        NT_GL(glUniform1i, rec->sampler_units[i].location, (GLint)i);
    }
    NT_GL(glUseProgram, saved);
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity) -- issued-call records expand at owning sites
bool nt_gfx_backend_create_program(uint32_t vs_backend, uint32_t fs_backend, uint32_t slot, const char *label) {
    NT_ASSERT(slot > 0 && slot <= s_init_desc.max_programs && s_programs[slot].program == 0 && "create_program: the record at the frontend slot must be empty");
    /* Emscripten's glCreateProgram throws on the null program some browsers return on a lost context. */
    if (nt_gfx_gl_ctx_query_lost()) {
        return false;
    }
    GLuint program = NT_GL_RET0(glCreateProgram);
    if (program == 0) {
        assert_name_failed_on_loss("glCreateProgram", label);
        return false;
    }
    NT_GL(glAttachShader, program, (GLuint)vs_backend);
    NT_GL(glAttachShader, program, (GLuint)fs_backend);
    NT_GL(glLinkProgram, program);
    s_programs[slot].program = program;
    s_programs[slot].vs = (GLuint)vs_backend;
    s_programs[slot].fs = (GLuint)fs_backend;
    s_programs[slot].label = label;
    return true;
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity) -- issued-call records expand at owning sites
nt_gfx_result_t nt_gfx_backend_finish_program(uint32_t backend_handle, bool poll) {
    NT_ASSERT(backend_handle != 0 && backend_handle <= s_init_desc.max_programs && s_programs[backend_handle].program != 0);
    nt_gfx_gl_program_t *rec = &s_programs[backend_handle];
    /* Any other program query before completion blocks until the driver finishes the link. */
    GLint done = GL_TRUE;
    if (poll) {
        NT_GL(glGetProgramiv, rec->program, GL_COMPLETION_STATUS_KHR, &done);
    }
    if (!done) {
        return NT_GFX_RESULT_UNREADY;
    }
    GLint linked = 0;
    NT_GL(glGetProgramiv, rec->program, GL_LINK_STATUS, &linked);
    if (!linked) {
        /* A loss fails the link before its event arrives; the caller latches it. */
        const bool lost = nt_gfx_gl_ctx_query_lost();
        if (!lost) {
            nt_gfx_gl_log_link_failure(rec);
        }
        NT_ASSERT(lost && "program link failed on a live context");
        return NT_GFX_RESULT_CONTEXT_LOST;
    }
    const char *phase = "blocks";
    bool reflected = nt_gfx_gl_bind_blocks(rec->program, rec);
    if (reflected) {
        phase = "uniforms";
        reflected = nt_gfx_gl_reflect_uniforms(rec->program, rec);
    }
    if (!reflected) {
        const bool lost = nt_gfx_gl_ctx_query_lost();
        if (!lost) {
            log_program_failure(rec, phase, "no data", "gl_error", NT_GL_RET0(glGetError));
        }
        NT_ASSERT(lost && "program uniform reflection failed");
        return NT_GFX_RESULT_CONTEXT_LOST;
    }
    write_sampler_units(rec->program, rec);
#if NT_GFX_CAPTURE_ENABLED
    capture_program_definition(backend_handle);
#endif
    return NT_GFX_RESULT_ACCEPTED;
}

void nt_gfx_backend_destroy_program(uint32_t backend_handle) {
    NT_ASSERT(backend_handle != 0 && backend_handle <= s_init_desc.max_programs && s_programs[backend_handle].program != 0 && "destroy_program: requires a live record");
    GLuint program = s_programs[backend_handle].program;
    /* GL defers deletion while a program remains current. */
    if (s_gl_cache.program == program) {
        NT_GL(glUseProgram, 0);
        s_gl_cache.program = 0;
    }
    NT_GL(glDeleteProgram, program);
    memset(&s_programs[backend_handle], 0, sizeof(s_programs[backend_handle]));
}

void nt_gfx_backend_create_pipeline(const nt_pipeline_desc_t *desc, uint32_t program_backend, uint32_t slot) {
    /* The frontend pool owns slot allocation; the backend table mirrors it. */
    NT_ASSERT(program_backend > 0 && program_backend <= s_init_desc.max_programs);
    NT_ASSERT(slot > 0 && slot <= s_init_desc.max_pipelines && s_pipelines[slot].program_slot == 0);

    /* Store pipeline data (fixed-function state + borrowed program only) */
    nt_gfx_gl_pipeline_t *pip = &s_pipelines[slot];
    pip->program_slot = program_backend;
    pip->depth_test_enabled = desc->depth_test;
    pip->depth_write_enabled = desc->depth_write;
    pip->depth_func = map_depth_func(desc->depth_func);
    pip->cull_mode = desc->cull_mode;
    pip->blend_enabled = desc->blend.enabled;
    pip->blend_src_rgb = map_blend_factor(desc->blend.src_rgb);
    pip->blend_dst_rgb = map_blend_factor(desc->blend.dst_rgb);
    pip->blend_src_alpha = map_blend_factor(desc->blend.src_alpha);
    pip->blend_dst_alpha = map_blend_factor(desc->blend.dst_alpha);
    pip->blend_op_rgb = map_blend_op(desc->blend.op_rgb);
    pip->blend_op_alpha = map_blend_op(desc->blend.op_alpha);
    memcpy(pip->blend_constant_color, desc->blend.constant_color, sizeof(pip->blend_constant_color));
    pip->polygon_offset_enabled = desc->polygon_offset;
    pip->polygon_offset_factor = desc->polygon_offset_factor;
    pip->polygon_offset_units = desc->polygon_offset_units;

#if NT_GFX_CAPTURE_ENABLED
    capture_pipeline_definition(slot);
#endif
}

void nt_gfx_backend_destroy_pipeline(uint32_t backend_handle) {
    if (backend_handle == 0) {
        return;
    }
    NT_ASSERT(backend_handle <= s_init_desc.max_pipelines && "destroy_pipeline: handle out of range");
    /* The program is not ours to delete -- it outlives every pipeline built
     * on it; the pipeline owns no GL objects of its own. */
    memset(&s_pipelines[backend_handle], 0, sizeof(s_pipelines[backend_handle]));
}

static nt_gfx_gl_attr_t pack_attr(const nt_vertex_attr_t *attr) {
    return (nt_gfx_gl_attr_t){.location = attr->location, .type = (uint8_t)attr->type, .count = attr->count, .normalized = attr->normalized ? 1U : 0U, .offset = attr->offset};
}

static void gl_attrib_pointer(const nt_gfx_gl_attr_t *attr, uint16_t stride, uint32_t base) {
    NT_GL(glVertexAttribPointer, attr->location, attr->count, map_vertex_type((nt_vertex_type_t)attr->type), attr->normalized ? GL_TRUE : GL_FALSE, (GLsizei)stride, nt_gl_offset(base + attr->offset));
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity) -- NT_ASSERT expansion inflates the metric
bool nt_gfx_backend_create_vertex_input(const nt_vertex_input_desc_t *desc, uint32_t vbo_backend, uint32_t ibo_backend, uint32_t slot) {
    NT_ASSERT(desc != NULL);
    NT_ASSERT(slot > 0 && slot <= s_init_desc.max_vertex_inputs && s_vertex_inputs[slot].vao == 0);
    GLuint vao = 0;
    NT_GL_GEN(glGenVertexArrays, 1, &vao);
    if (vao == 0) {
        assert_name_failed_on_loss("glGenVertexArrays", desc->label);
        return false;
    }
    nt_gfx_gl_vertex_input_t *vi = &s_vertex_inputs[slot];
    *vi = (nt_gfx_gl_vertex_input_t){.vao = vao};
    NT_GL(glBindVertexArray, vao);
    const GLuint vbo = vbo_backend != 0 && vbo_backend <= s_init_desc.max_buffers ? s_buffer_gl[vbo_backend] : 0;
    if (vbo != 0) {
        /* Buffer bound before the pointer calls -- satisfies the WebGL "no
         * pointer without a bound ARRAY_BUFFER" rule at creation time. */
        NT_GL(glBindBuffer, GL_ARRAY_BUFFER, vbo);
        s_gl_cache.array_buffer = vbo;
        for (uint8_t i = 0; i < desc->layout.attr_count; i++) {
            const nt_gfx_gl_attr_t attr = pack_attr(&desc->layout.attrs[i]);
            NT_GL(glEnableVertexAttribArray, attr.location);
            gl_attrib_pointer(&attr, desc->layout.stride, 0);
        }
    }
    const GLuint ibo = ibo_backend != 0 && ibo_backend <= s_init_desc.max_buffers ? s_buffer_gl[ibo_backend] : 0;
    if (ibo != 0) {
        NT_GL(glBindBuffer, GL_ELEMENT_ARRAY_BUFFER, ibo); /* captured by the VAO */
    }
    NT_GL(glBindVertexArray, s_gl_cache.vao);
    NT_GFX_RECORD(NT_GFX_EVENT_DEFINITION, NT_GFX_OP_STATE, event->detail = NT_GFX_OBJECT_VERTEX_INPUT; event->data.backend.args[0] = slot; event->data.backend.args[1] = vao;);
    return true;
}

void nt_gfx_backend_register_instance_layout(uint32_t slot, const nt_vertex_layout_t *layout) {
    nt_gfx_gl_instance_layout_t *dst = &s_instance_layouts[slot];
    *dst = (nt_gfx_gl_instance_layout_t){.stride = layout->stride, .attr_count = layout->attr_count};
    for (uint8_t i = 0; i < layout->attr_count; i++) {
        dst->attrs[i] = pack_attr(&layout->attrs[i]);
        dst->mask = (uint16_t)(dst->mask | (1U << layout->attrs[i].location));
    }
}

static void bind_array_buffer(GLuint buf) {
    if (s_gl_cache.array_buffer != buf) {
        NT_GL(glBindBuffer, GL_ARRAY_BUFFER, buf);
        s_gl_cache.array_buffer = buf;
    }
}

/* Disables the bound VAO's slots in mask and points them at no buffer (offset 0, legal in
 * WebGL2), so no attrib stays enabled on, or attached to, storage the VAO no longer holds. */
static void detach_instance_slots(uint16_t mask) {
    if (mask == 0) {
        return;
    }
    bind_array_buffer(0);
    for (uint32_t loc = 0; loc < NT_GFX_MAX_VERTEX_ATTRS; loc++) {
        if ((mask & (1U << loc)) != 0) {
            NT_GL(glDisableVertexAttribArray, loc);
            NT_GL(glVertexAttribPointer, loc, 4, GL_FLOAT, GL_FALSE, 0, nt_gl_offset(0));
        }
    }
}

/* Cold path of buffer destruction: WebGL rejects draws through an enabled attrib whose buffer
 * was deleted, and GL detaches a deleted buffer only from the bound VAO. */
static void detach_instance_buffer(GLuint buf) {
    bool rebound = false;
    for (uint32_t i = 1; i <= s_init_desc.max_vertex_inputs; i++) {
        nt_gfx_gl_vertex_input_t *vi = &s_vertex_inputs[i];
        if (vi->vao == 0 || vi->instance_buffer != buf) {
            continue;
        }
        NT_GL(glBindVertexArray, vi->vao);
        rebound = true;
        detach_instance_slots(s_instance_layouts[vi->instance_layout].mask);
        vi->instance_buffer = 0;
        vi->instance_offset = 0;
        vi->instance_layout = 0;
    }
    if (rebound) {
        NT_GL(glBindVertexArray, s_gl_cache.vao);
    }
}

void nt_gfx_backend_destroy_vertex_input(uint32_t backend_handle) {
    if (backend_handle == 0) {
        return;
    }
    NT_ASSERT(backend_handle <= s_init_desc.max_vertex_inputs && "destroy_vertex_input: handle out of range");
    nt_gfx_gl_vertex_input_t *vi = &s_vertex_inputs[backend_handle];
    if (vi->vao != 0) {
        /* Deleting the bound VAO reverts the GL binding to 0 -- mirror it. */
        if (s_gl_cache.vao == vi->vao) {
            s_gl_cache.vao = 0;
        }
        NT_GL_DELETE(glDeleteVertexArrays, 1, &vi->vao);
    }
    memset(vi, 0, sizeof(*vi));
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity) -- diagnostic record and assert macros expand at owning sites
void nt_gfx_backend_bind_vertex_input(uint32_t backend_handle, uint32_t layout_slot, uint32_t buffer_backend, uint32_t byte_offset) {
    /* A zeroed record (context loss, destroyed vertex input) would leave the
     * previous VAO bound and draw the wrong geometry. */
    NT_ASSERT(backend_handle != 0 && backend_handle <= s_init_desc.max_vertex_inputs && s_vertex_inputs[backend_handle].vao != 0 && "bind_vertex_input: requires a live vertex input");
    nt_gfx_gl_vertex_input_t *vi = &s_vertex_inputs[backend_handle];
    if (s_gl_cache.vao != vi->vao) {
        NT_GL(glBindVertexArray, vi->vao);
        s_gl_cache.vao = vi->vao;
    } else {
        NT_GFX_RECORD(NT_GFX_EVENT_SKIP, NT_GFX_OP_VERTEX_INPUT, event->result = NT_GFX_RESULT_CACHE; event->detail = NT_GFX_GL_glBindVertexArray; event->data.backend.args[0] = vi->vao;);
    }
    if (layout_slot == 0) {
        return;
    }
    NT_ASSERT(buffer_backend != 0 && buffer_backend <= s_init_desc.max_buffers && s_buffer_gl[buffer_backend] != 0 && "bind_vertex_input: requires a live instance buffer");
    const GLuint buf = s_buffer_gl[buffer_backend];
    /* The tuple this VAO last received: a vertex input bound with one tuple never re-points. */
    if (vi->instance_layout == layout_slot && vi->instance_buffer == buf && vi->instance_offset == byte_offset) {
        NT_GFX_RECORD(NT_GFX_EVENT_SKIP, NT_GFX_OP_VERTEX_INPUT, event->result = NT_GFX_RESULT_CACHE; event->detail = NT_GFX_GL_glVertexAttribPointer; event->data.backend.args[0] = vi->vao;
                      event->data.backend.args[1] = buf; event->data.backend.args[2] = byte_offset; event->data.backend.args[3] = layout_slot;);
        return;
    }
    const nt_gfx_gl_instance_layout_t *layout = &s_instance_layouts[layout_slot];
    /* Old 0 = first bind, or its buffer was destroyed. Dropped slots point at no buffer, so the
     * buffer-destroy walk, which knows only the current layout, finds every attachment. */
    if (vi->instance_layout != layout_slot) {
        const uint16_t old_mask = vi->instance_layout != 0 ? s_instance_layouts[vi->instance_layout].mask : 0U;
        detach_instance_slots((uint16_t)(old_mask & ~layout->mask));
        for (uint8_t i = 0; i < layout->attr_count; i++) {
            const uint8_t loc = layout->attrs[i].location;
            if ((old_mask & (1U << loc)) == 0) {
                NT_GL(glEnableVertexAttribArray, loc);
                NT_GL(glVertexAttribDivisor, loc, 1);
            }
        }
        vi->instance_layout = (uint8_t)layout_slot;
    }
    /* Another VAO's replay may have left a foreign ARRAY_BUFFER bound. */
    bind_array_buffer(buf);
    vi->instance_buffer = buf;
    vi->instance_offset = byte_offset;
    for (uint8_t i = 0; i < layout->attr_count; i++) {
        gl_attrib_pointer(&layout->attrs[i], layout->stride, byte_offset);
    }
}

/* GL unbinds a deleted buffer from the context. */
static void forget_deleted_buffer(GLuint buf) {
    if (s_gl_cache.array_buffer == buf) {
        s_gl_cache.array_buffer = 0;
    }
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity) -- diagnostic record and assert macros expand at owning sites
bool nt_gfx_backend_create_buffer(const nt_buffer_desc_t *desc, uint32_t slot) {
    NT_ASSERT(slot > 0 && slot <= s_init_desc.max_buffers && s_buffer_gl[slot] == 0 && "create_buffer: the record at the frontend slot must be empty");
    GLuint buf;
    NT_GL_GEN(glGenBuffers, 1, &buf);
    if (buf == 0) {
        assert_name_failed_on_loss("glGenBuffers", desc->label);
        return false;
    }
    GLenum target = desc->type == NT_BUFFER_INDEX ? GL_ELEMENT_ARRAY_BUFFER : GL_ARRAY_BUFFER;
    GLenum usage = map_buffer_usage(desc->usage);
#if !defined(NT_PLATFORM_WEB)
    if (desc->type == NT_BUFFER_READBACK) {
        target = GL_PIXEL_PACK_BUFFER;
        usage = GL_STREAM_READ;
    }
#endif
    bool unhook_vao = target == GL_ELEMENT_ARRAY_BUFFER;
    if (unhook_vao) {
        ebo_upload_begin();
    }
    NT_GL(glBindBuffer, target, buf);
    if (target == GL_ARRAY_BUFFER) {
        s_gl_cache.array_buffer = buf;
    }
    NT_GL_BUFFER_UPLOAD(desc->data, desc->size, glBufferData, target, (GLsizeiptr)desc->size, desc->data, usage);
    if (unhook_vao) {
        ebo_upload_end();
    }
#if !defined(NT_PLATFORM_WEB)
    if (target == GL_PIXEL_PACK_BUFFER) {
        NT_GL(glBindBuffer, GL_PIXEL_PACK_BUFFER, 0); /* a bound pack buffer turns every glReadPixels pointer into an offset */
    }
#endif

    s_buffer_gl[slot] = buf;
    s_buffer_targets[slot] = target;
    NT_GFX_RECORD(NT_GFX_EVENT_DEFINITION, NT_GFX_OP_STATE, event->detail = NT_GFX_OBJECT_BUFFER; event->data.backend.args[0] = slot; event->data.backend.args[1] = buf;);
    return true;
}

void nt_gfx_backend_destroy_buffer(uint32_t backend_handle) {
    NT_ASSERT(backend_handle != 0 && backend_handle <= s_init_desc.max_buffers && s_buffer_gl[backend_handle] != 0 && "destroy_buffer: requires a live record");
    GLuint buf = s_buffer_gl[backend_handle];
    if (s_buffer_targets[backend_handle] == GL_ARRAY_BUFFER) {
        detach_instance_buffer(buf);
    }
    forget_deleted_buffer(buf);
#if !defined(NT_PLATFORM_WEB)
    if (s_buffer_fence[backend_handle] != NULL) {
        NT_GL(glDeleteSync, s_buffer_fence[backend_handle]);
        s_buffer_fence[backend_handle] = NULL;
    }
#endif
    NT_GL_DELETE(glDeleteBuffers, 1, &buf);
    s_buffer_gl[backend_handle] = 0;
    s_buffer_targets[backend_handle] = 0;
}

void nt_gfx_backend_update_buffer(uint32_t backend_handle, uint32_t offset, const void *data, uint32_t size) {
    if (backend_handle == 0 || backend_handle > s_init_desc.max_buffers) {
        return;
    }
    GLuint buf = s_buffer_gl[backend_handle];
    GLenum target = s_buffer_targets[backend_handle];
    bool unhook_vao = target == GL_ELEMENT_ARRAY_BUFFER;
    if (unhook_vao) {
        ebo_upload_begin();
    }
    if (target != GL_ARRAY_BUFFER || s_gl_cache.array_buffer != buf) {
        NT_GL(glBindBuffer, target, buf);
    } else {
        NT_GFX_RECORD(NT_GFX_EVENT_SKIP, NT_GFX_OP_BUFFER_UPLOAD, event->result = NT_GFX_RESULT_CACHE; event->detail = NT_GFX_GL_glBindBuffer; event->data.backend.args[0] = buf;);
    }
    if (target == GL_ARRAY_BUFFER) {
        s_gl_cache.array_buffer = buf;
    }
    NT_GL_BUFFER_UPLOAD(data, size, glBufferSubData, target, (GLintptr)offset, (GLsizeiptr)size, data);
    if (unhook_vao) {
        ebo_upload_end();
    }
}

/* ---- Uniform buffer ---- */

void nt_gfx_backend_bind_uniform_buffer(uint32_t backend_handle, uint32_t slot, uint32_t offset, uint32_t size) {
    NT_ASSERT(backend_handle != 0 && backend_handle <= s_init_desc.max_buffers && s_buffer_gl[backend_handle] != 0 && "bind_uniform_buffer: requires a live buffer");
    const GLuint buf = s_buffer_gl[backend_handle];
    if (s_gl_cache.uniform_ranges[slot].offset == offset && s_gl_cache.uniform_ranges[slot].size == size) {
        NT_GFX_RECORD(NT_GFX_EVENT_SKIP, NT_GFX_OP_UBO, event->result = NT_GFX_RESULT_CACHE; event->detail = NT_GFX_GL_glBindBufferRange; event->data.backend.args[0] = buf;);
        return;
    }
    NT_GL(glBindBufferRange, GL_UNIFORM_BUFFER, slot, buf, (GLintptr)offset, (GLsizeiptr)size);
    s_gl_cache.uniform_ranges[slot].offset = offset;
    s_gl_cache.uniform_ranges[slot].size = size;
}

/* ---- Texture management ---- */

/* Complete nt_texture_format_t → GL mapping.
   Single source of truth for internal format, upload format, upload type, and alignment. */
typedef struct {
    GLenum internal; /* sized: GL_RGBA8, GL_RGBA16F, ... */
    GLenum format;   /* upload layout: GL_RGBA, GL_RG_INTEGER, ... */
    GLenum type;     /* component type: GL_UNSIGNED_BYTE, GL_HALF_FLOAT, ... */
    bool align4;     /* true if rows are naturally 4-byte aligned */
    bool compressed; /* upload through glCompressedTexImage2D, sized in bytes */
} nt_gfx_gl_fmt_t;

/* Not every GL header defines all four tokens; the values are spec-fixed. */
#ifndef GL_COMPRESSED_RGB8_ETC2
#define GL_COMPRESSED_RGB8_ETC2 0x9274
#endif
#ifndef GL_COMPRESSED_RGBA8_ETC2_EAC
#define GL_COMPRESSED_RGBA8_ETC2_EAC 0x9278
#endif
#ifndef GL_COMPRESSED_RGBA_BPTC_UNORM
#define GL_COMPRESSED_RGBA_BPTC_UNORM 0x8E8C
#endif
#ifndef GL_COMPRESSED_RGBA_ASTC_4x4_KHR
#define GL_COMPRESSED_RGBA_ASTC_4x4_KHR 0x93B0
#endif

static nt_gfx_gl_fmt_t nt_gfx_gl_texture_format(nt_texture_format_t fmt) {
    switch (fmt) {
    case NT_TEXTURE_FORMAT_RGB8:
        return (nt_gfx_gl_fmt_t){GL_RGB8, GL_RGB, GL_UNSIGNED_BYTE, false, false};
    case NT_TEXTURE_FORMAT_RG8:
        return (nt_gfx_gl_fmt_t){GL_RG8, GL_RG, GL_UNSIGNED_BYTE, false, false};
    case NT_TEXTURE_FORMAT_R8:
        return (nt_gfx_gl_fmt_t){GL_R8, GL_RED, GL_UNSIGNED_BYTE, false, false};
    case NT_TEXTURE_FORMAT_RGBA16F:
        return (nt_gfx_gl_fmt_t){GL_RGBA16F, GL_RGBA, GL_HALF_FLOAT, true, false};
    case NT_TEXTURE_FORMAT_RG16UI:
        return (nt_gfx_gl_fmt_t){GL_RG16UI, GL_RG_INTEGER, GL_UNSIGNED_SHORT, true, false};
    case NT_TEXTURE_FORMAT_RGBA32F:
        return (nt_gfx_gl_fmt_t){GL_RGBA32F, GL_RGBA, GL_FLOAT, true, false};
    case NT_TEXTURE_FORMAT_DEPTH16:
        return (nt_gfx_gl_fmt_t){GL_DEPTH_COMPONENT16, GL_DEPTH_COMPONENT, GL_UNSIGNED_SHORT, true, false};
    case NT_TEXTURE_FORMAT_DEPTH24:
        return (nt_gfx_gl_fmt_t){GL_DEPTH_COMPONENT24, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, true, false};
    case NT_TEXTURE_FORMAT_DEPTH32F:
        return (nt_gfx_gl_fmt_t){GL_DEPTH_COMPONENT32F, GL_DEPTH_COMPONENT, GL_FLOAT, true, false};
    case NT_TEXTURE_FORMAT_RGBA8:
        return (nt_gfx_gl_fmt_t){GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, true, false};
    case NT_TEXTURE_FORMAT_ETC2_RGB8:
        return (nt_gfx_gl_fmt_t){GL_COMPRESSED_RGB8_ETC2, GL_RGB, GL_UNSIGNED_BYTE, true, true};
    case NT_TEXTURE_FORMAT_ETC2_RGBA8:
        return (nt_gfx_gl_fmt_t){GL_COMPRESSED_RGBA8_ETC2_EAC, GL_RGBA, GL_UNSIGNED_BYTE, true, true};
    case NT_TEXTURE_FORMAT_BC7_RGBA:
        return (nt_gfx_gl_fmt_t){GL_COMPRESSED_RGBA_BPTC_UNORM, GL_RGBA, GL_UNSIGNED_BYTE, true, true};
    case NT_TEXTURE_FORMAT_ASTC_4x4_RGBA:
        return (nt_gfx_gl_fmt_t){GL_COMPRESSED_RGBA_ASTC_4x4_KHR, GL_RGBA, GL_UNSIGNED_BYTE, true, true};
    case NT_TEXTURE_FORMAT_INVALID:
    default:
        NT_ASSERT(0 && "unsupported texture format");
        return (nt_gfx_gl_fmt_t){0};
    }
}

static void nt_gfx_gl_forget_texture(GLuint tex) {
    for (uint32_t i = 0; i < NT_GFX_MAX_TEXTURE_SLOTS; i++) {
        if (s_gl_cache.bound_textures[i] == tex) {
            s_gl_cache.bound_textures[i] = 0;
        }
    }
}

/* Data and parameter ops go to the scratch unit, so no sampling slot is disturbed
 * and the cache stays truthful without invalidation. */
static void nt_gfx_gl_bind_texture_for_upload(GLuint tex) {
    if (s_gl_cache.active_texture_unit != NT_GFX_GL_UPLOAD_TEXTURE_UNIT) {
        NT_GL(glActiveTexture, NT_GFX_GL_UPLOAD_TEXTURE_UNIT);
        s_gl_cache.active_texture_unit = NT_GFX_GL_UPLOAD_TEXTURE_UNIT;
    }
    NT_GL(glBindTexture, GL_TEXTURE_2D, tex);
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity) -- diagnostic record and assert macros expand at owning sites
static GLuint nt_gfx_gl_create_texture_name(const nt_texture_desc_t *desc) {
    GLuint tex;
    NT_GL_GEN(glGenTextures, 1, &tex);
    if (tex == 0) {
        assert_name_failed_on_loss("glGenTextures", desc->label);
        return 0;
    }
    nt_gfx_gl_bind_texture_for_upload(tex);

    /* Filter and wrap live on the sampler object every bind carries; the
       texture object keeps GL defaults, which no sampling path reads. */
    nt_gfx_gl_fmt_t gl = nt_gfx_gl_texture_format(desc->format);

    /* Declared levels lie back to back in desc->data (NULL = storage only). */
    const uint8_t levels = desc->level_count > 1 ? desc->level_count : 1;
    if (!gl.align4) {
        NT_GL(glPixelStorei, GL_UNPACK_ALIGNMENT, 1);
    }
    const uint8_t *level_data = (const uint8_t *)desc->data;
    for (uint8_t level = 0; level < levels; level++) {
        const uint32_t level_w = nt_texture_level_extent(desc->width, level);
        const uint32_t level_h = nt_texture_level_extent(desc->height, level);
        const uint64_t level_bytes = nt_texture_level_bytes(desc->format, level_w, level_h);
        if (gl.compressed) {
            NT_GL_TEXTURE_UPLOAD(level_data, level_bytes, glCompressedTexImage2D, GL_TEXTURE_2D, (GLint)level, gl.internal, (GLsizei)level_w, (GLsizei)level_h, 0, (GLsizei)level_bytes, level_data);
        } else {
            NT_GL_TEXTURE_UPLOAD(level_data, level_bytes, glTexImage2D, GL_TEXTURE_2D, (GLint)level, (GLint)gl.internal, (GLsizei)level_w, (GLsizei)level_h, 0, gl.format, gl.type, level_data);
        }
        if (level_data != NULL) {
            level_data += level_bytes;
        }
    }
    if (!gl.align4) {
        NT_GL(glPixelStorei, GL_UNPACK_ALIGNMENT, 4);
    }

    /* Generate mipmaps after base level upload if requested and data present */
    if (desc->gen_mipmaps && desc->data) {
        NT_GL(glGenerateMipmap, GL_TEXTURE_2D);
    }

    const uint8_t top_level = (desc->gen_mipmaps && desc->data) ? nt_texture_full_chain_levels(desc->width, desc->height) : levels;
    NT_GL(glTexParameteri, GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, (GLint)(top_level - 1));

    /* A nonzero WebGL name can survive loss. Query it without waiting for uploads;
     * glGetError would synchronize with the GPU process, so live upload errors stay unchecked. */
    if (nt_gfx_gl_ctx_query_lost()) {
        NT_GL_DELETE(glDeleteTextures, 1, &tex);
        return 0;
    }

    return tex;
}

bool nt_gfx_backend_create_texture(const nt_texture_desc_t *desc, uint32_t slot) {
    NT_ASSERT(slot > 0 && slot <= s_init_desc.max_textures && s_texture_gl[slot] == 0 && "create_texture: the record at the frontend slot must be empty");
    GLuint tex = nt_gfx_gl_create_texture_name(desc);
    if (tex == 0) {
        return false;
    }
    s_texture_gl[slot] = tex;

    NT_GFX_RECORD(NT_GFX_EVENT_DEFINITION, NT_GFX_OP_STATE, event->detail = NT_GFX_OBJECT_TEXTURE; event->data.backend.args[0] = slot; event->data.backend.args[1] = tex;);
    return true;
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity) -- diagnostic record and assert macros expand at owning sites
void nt_gfx_backend_update_texture(uint32_t backend_handle, uint16_t x, uint16_t y, uint16_t w, uint16_t h, nt_texture_format_t format, const void *data) {
    NT_ASSERT(backend_handle != 0 && backend_handle <= s_init_desc.max_textures && "backend_update_texture: invalid handle");
    GLuint tex = s_texture_gl[backend_handle];
    NT_ASSERT(tex != 0 && "backend_update_texture: no GL texture at handle");

    nt_gfx_gl_bind_texture_for_upload(tex);

    nt_gfx_gl_fmt_t gl = nt_gfx_gl_texture_format(format);

    if (!gl.align4) {
        NT_GL(glPixelStorei, GL_UNPACK_ALIGNMENT, 1);
    }

    NT_GL_TEXTURE_UPLOAD(data, nt_texture_level_bytes(format, w, h), glTexSubImage2D, GL_TEXTURE_2D, 0, (GLint)x, (GLint)y, (GLsizei)w, (GLsizei)h, gl.format, gl.type, data);

    if (!gl.align4) {
        NT_GL(glPixelStorei, GL_UNPACK_ALIGNMENT, 4);
    }
}

void nt_gfx_backend_destroy_texture(uint32_t backend_handle) {
    NT_ASSERT(backend_handle != 0 && backend_handle <= s_init_desc.max_textures && s_texture_gl[backend_handle] != 0 && "destroy_texture: requires a live record");
    GLuint tex = s_texture_gl[backend_handle];
    nt_gfx_gl_forget_texture(tex);
    NT_GL_DELETE(glDeleteTextures, 1, &tex);
    s_texture_gl[backend_handle] = 0;
}

static const GLenum s_rt_attachment_points[NT_GFX_RT_ATTACHMENTS] = {GL_COLOR_ATTACHMENT0, GL_DEPTH_ATTACHMENT};

/* Resolves texture backends to GL names; the frontend passes only live backends. */
static void nt_gfx_gl_render_target_textures(const uint32_t textures[NT_GFX_RT_ATTACHMENTS], GLuint out[NT_GFX_RT_ATTACHMENTS]) {
    NT_ASSERT(s_texture_gl != NULL && (textures[NT_GFX_RT_COLOR] != 0 || textures[NT_GFX_RT_DEPTH] != 0) && "render target: no attachment");
    for (int i = 0; i < NT_GFX_RT_ATTACHMENTS; i++) {
        NT_ASSERT(textures[i] <= s_init_desc.max_textures && (textures[i] == 0 || s_texture_gl[textures[i]] != 0) && "render target: attachment texture has no live GL name");
        out[i] = s_texture_gl[textures[i]];
    }
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity) -- diagnostic record and assert macros expand at owning sites
bool nt_gfx_backend_create_render_target(const uint32_t textures[NT_GFX_RT_ATTACHMENTS], uint32_t slot, const char *label, uint16_t width, uint16_t height, nt_texture_format_t color,
                                         nt_texture_format_t depth) {
    NT_ASSERT(slot > 0 && slot <= s_init_desc.max_render_targets && s_render_target_gl[slot] == 0);
    GLuint names[NT_GFX_RT_ATTACHMENTS];
    nt_gfx_gl_render_target_textures(textures, names);
    GLuint restore_fbo = s_bound_framebuffer;
    GLuint fbo = 0;
    NT_GL_GEN(glGenFramebuffers, 1, &fbo);
    if (fbo == 0) {
        assert_name_failed_on_loss("glGenFramebuffers", label);
        return false;
    }
    NT_GL(glBindFramebuffer, GL_FRAMEBUFFER, fbo);
    s_bound_framebuffer = fbo;
    for (int i = 0; i < NT_GFX_RT_ATTACHMENTS; i++) {
        if (names[i] != 0) {
            NT_GL(glFramebufferTexture2D, GL_FRAMEBUFFER, s_rt_attachment_points[i], GL_TEXTURE_2D, names[i], 0);
        }
    }
    /* GL 3.3 core reports a draw/read buffer without an attachment as incomplete. */
    if (names[NT_GFX_RT_COLOR] == 0) {
        const GLenum none = GL_NONE;
        NT_GL(glDrawBuffers, 1, &none);
        NT_GL(glReadBuffer, GL_NONE);
    }

    GLenum status = NT_GL_RET(glCheckFramebufferStatus, GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        /* The engine's formats are required framebuffer formats (GLES 3.0 "Required Framebuffer
         * Formats"; RGBA16F with EXT_color_buffer_float): on a live context incompleteness is a bug,
         * or storage an unpolled allocation failure left out. */
        const bool lost = nt_gfx_gl_ctx_query_lost();
        if (!lost) {
            NT_LOG_ERROR("framebuffer status=0x%04X gl_error=0x%04X size=%ux%u color=%u depth=%u \"%s\"", (unsigned)status, (unsigned)NT_GL_RET0(glGetError), (unsigned)width, (unsigned)height,
                         (unsigned)color, (unsigned)depth, label != NULL ? label : "");
        }
        NT_ASSERT(lost && "render target incomplete on a live context");
        NT_GL_DELETE(glDeleteFramebuffers, 1, &fbo);
        NT_GL(glBindFramebuffer, GL_FRAMEBUFFER, restore_fbo);
        s_bound_framebuffer = restore_fbo;
        return false;
    }

    s_render_target_gl[slot] = fbo;
    NT_GL(glBindFramebuffer, GL_FRAMEBUFFER, restore_fbo);
    s_bound_framebuffer = restore_fbo;
    NT_GFX_RECORD(NT_GFX_EVENT_DEFINITION, NT_GFX_OP_STATE, event->detail = NT_GFX_OBJECT_RENDER_TARGET; event->data.backend.args[0] = slot; event->data.backend.args[1] = s_render_target_gl[slot];);
    return true;
}

void nt_gfx_backend_destroy_render_target(uint32_t backend_handle) {
    if (backend_handle == 0) {
        return;
    }
    NT_ASSERT(backend_handle <= s_init_desc.max_render_targets && s_render_target_gl != NULL && "destroy_render_target: invalid GL backend handle");
    GLuint *fbo = &s_render_target_gl[backend_handle];
    if (s_bound_framebuffer == *fbo) {
        NT_GL(glBindFramebuffer, GL_FRAMEBUFFER, 0);
        s_bound_framebuffer = 0;
    }
    if (*fbo != 0) {
        NT_GL_DELETE(glDeleteFramebuffers, 1, fbo);
    }
    *fbo = 0;
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity) -- diagnostic record and assert macros expand at owning sites
static void bind_texture(uint32_t backend_handle, uint32_t slot) {
    NT_ASSERT(slot < NT_GFX_MAX_TEXTURE_SLOTS && "bind_texture: slot out of range");
    NT_ASSERT(backend_handle != 0 && backend_handle <= s_init_desc.max_textures && s_texture_gl[backend_handle] != 0 && "bind_texture: requires a live texture");
    GLuint tex = s_texture_gl[backend_handle];
    if (s_gl_cache.bound_textures[slot] == tex) {
        NT_GFX_RECORD(NT_GFX_EVENT_SKIP, NT_GFX_OP_TEXTURE, event->detail = NT_GFX_GL_glBindTexture; event->result = NT_GFX_RESULT_CACHE; event->data.backend.args[0] = tex;
                      event->data.backend.args[1] = slot;);
        return; /* already bound to this slot */
    }
    GLenum unit = GL_TEXTURE0 + slot;
    if (s_gl_cache.active_texture_unit != unit) {
        NT_GL(glActiveTexture, unit);
        s_gl_cache.active_texture_unit = unit;
    }
    NT_GL(glBindTexture, GL_TEXTURE_2D, tex);
    s_gl_cache.bound_textures[slot] = tex;
}

/* Sampler objects (WebGL2 / GL 3.3+). The backend "handle" is the raw
 * GLuint sampler id — nt_gfx caches them on its side, so the backend
 * does not maintain its own array. */

// NOLINTNEXTLINE(readability-function-cognitive-complexity) -- diagnostic record and assert macros expand at owning sites
uint32_t nt_gfx_backend_create_sampler(const nt_sampler_desc_t *desc) {
    GLuint s = 0;
    NT_GL_GEN(glGenSamplers, 1, &s);
    if (s == 0) {
        assert_name_failed_on_loss("glGenSamplers", desc->label);
        return 0;
    }
    NT_GL(glSamplerParameteri, s, GL_TEXTURE_MIN_FILTER, (GLint)map_texture_filter(desc->min_filter));
    NT_GL(glSamplerParameteri, s, GL_TEXTURE_MAG_FILTER, (GLint)map_texture_filter(desc->mag_filter));
    NT_GL(glSamplerParameteri, s, GL_TEXTURE_WRAP_S, (GLint)map_texture_wrap(desc->wrap_u));
    NT_GL(glSamplerParameteri, s, GL_TEXTURE_WRAP_T, (GLint)map_texture_wrap(desc->wrap_v));
    /* Set unconditionally rather than relying on the object's default NONE —
     * sampler state stays fully described by the descriptor. */
    NT_GL(glSamplerParameteri, s, GL_TEXTURE_COMPARE_MODE, desc->compare_func != NT_COMPARE_NONE ? GL_COMPARE_REF_TO_TEXTURE : GL_NONE);
    NT_GL(glSamplerParameteri, s, GL_TEXTURE_COMPARE_FUNC, (GLint)map_compare_func(desc->compare_func));
    return (uint32_t)s;
}

void nt_gfx_backend_destroy_sampler(uint32_t backend_handle) {
    if (backend_handle == 0) {
        return;
    }
    GLuint s = (GLuint)backend_handle;
    NT_GL_DELETE(glDeleteSamplers, 1, &s);
    /* GL unbinds the deleted name from every unit; the mirror must say so too. */
    for (uint32_t u = 0; u < NT_GFX_MAX_TEXTURE_SLOTS; u++) {
        if (s_gl_cache.bound_samplers[u] == s) {
            s_gl_cache.bound_samplers[u] = 0;
        }
    }
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity) -- issued-call records expand at owning sites
static void bind_sampler(uint32_t backend_handle, uint32_t slot) {
    NT_ASSERT(slot < NT_GFX_MAX_TEXTURE_SLOTS && "bind_sampler: slot out of range");
    NT_ASSERT(backend_handle != 0 && "bind_sampler: sampling without a sampler object");
    GLuint sampler = (GLuint)backend_handle;
    if (s_gl_cache.bound_samplers[slot] == sampler) {
        NT_GFX_RECORD(NT_GFX_EVENT_SKIP, NT_GFX_OP_SAMPLER, event->detail = NT_GFX_GL_glBindSampler; event->result = NT_GFX_RESULT_CACHE; event->data.backend.args[0] = sampler;
                      event->data.backend.args[1] = slot;);
        return;
    }
    NT_GL(glBindSampler, slot, sampler);
    s_gl_cache.bound_samplers[slot] = sampler;
}

void nt_gfx_backend_bind_texture_unit(uint32_t texture_backend, uint32_t sampler_backend, uint32_t slot) {
    bind_texture(texture_backend, slot);
    bind_sampler(sampler_backend, slot);
}

/* ---- Context loss recovery ---- */

bool nt_gfx_backend_recreate_all_resources(void) {
    /* Destroy old context and create a fresh one. */
    nt_gfx_gl_ctx_destroy();
    if (!nt_gfx_gl_ctx_create(&s_init_desc)) {
        return false;
    }

    /* Zero out all backend-side arrays -- old GL names are invalid. */
    if (s_programs) {
        memset(s_programs, 0, (s_init_desc.max_programs + 1) * sizeof(nt_gfx_gl_program_t));
    }
    if (s_pipelines) {
        memset(s_pipelines, 0, (s_init_desc.max_pipelines + 1) * sizeof(nt_gfx_gl_pipeline_t));
    }
    if (s_vertex_inputs) {
        memset(s_vertex_inputs, 0, (s_init_desc.max_vertex_inputs + 1) * sizeof(nt_gfx_gl_vertex_input_t));
    }
    if (s_buffer_gl) {
        memset(s_buffer_gl, 0, (s_init_desc.max_buffers + 1) * sizeof(GLuint));
    }
    if (s_buffer_targets) {
        memset(s_buffer_targets, 0, (s_init_desc.max_buffers + 1) * sizeof(GLenum));
    }
#if !defined(NT_PLATFORM_WEB)
    if (s_buffer_fence) {
        memset((void *)s_buffer_fence, 0, (s_init_desc.max_buffers + 1) * sizeof(GLsync));
    }
#endif
    if (s_texture_gl) {
        memset(s_texture_gl, 0, (s_init_desc.max_textures + 1) * sizeof(GLuint));
    }
    if (s_render_target_gl) {
        memset(s_render_target_gl, 0, (s_init_desc.max_render_targets + 1) * sizeof(GLuint));
    }
    nt_gfx_gl_cache_ground_state();
    nt_gfx_gl_init_context_features();
    return true;
}
