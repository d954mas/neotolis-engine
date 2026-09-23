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

/* glGetError value WebGL reports once on context loss; no GL header here defines it. */
#ifndef GL_CONTEXT_LOST_WEBGL
#define GL_CONTEXT_LOST_WEBGL 0x9242U
#endif

/* ---- Pipeline backend data ---- */

/* Per-program standalone locations, including each array element. */
#define NT_MAX_CACHED_UNIFORMS 16
_Static_assert(NT_MAX_CACHED_UNIFORMS <= 16, "vec4_mask and vec4_valid are 16-bit");
/* Longest reflected uniform name plus room for an expanded array index; asserted at link. */
#define NT_GFX_GL_MAX_UNIFORM_NAME 256

typedef struct {
    uint32_t name_hash;
    GLint location;
} nt_cached_uniform_t;

/* One entry per active sampler element; the unit is fixed at link. Samplers live only
 * here, never in the uniform table; the location is needed once, to write the unit. */
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

/* Uniform locations are per-program, so the cache lives here, not on the
 * pipelines that borrow the program. */
typedef struct {
    GLuint program;
    nt_cached_uniform_t uniforms[NT_MAX_CACHED_UNIFORMS];
    uint8_t uniform_count;
    uint16_t vec4_mask;
    uint16_t vec4_valid;
    uint8_t vec4_values[NT_MAX_CACHED_UNIFORMS][4 * sizeof(float)];
    /* Sampler units are program state: assigned in reflection order at link,
     * written once, never rewritten by a material. */
    nt_gfx_gl_sampler_unit_t sampler_units[NT_GFX_MAX_TEXTURE_SLOTS];
    uint8_t sampler_count;
} nt_gfx_gl_program_t;

typedef struct {
    GLuint fbo;
    GLuint depth_rbo;
    uint16_t width;
    uint16_t height;
} nt_gfx_gl_render_target_t;

/* Static attrs and EBO are baked; instance pointers are re-pointed per draw.
 * Keeping only the instance layout avoids ~200 B per vertex-input slot. */
typedef struct {
    GLuint vao; /* 0 = free slot */
    nt_vertex_attr_t instance_attrs[NT_GFX_MAX_INSTANCE_ATTRS];
    uint8_t instance_attr_count;
    uint16_t instance_stride;
} nt_gfx_gl_vertex_input_t;

/* ---- File-scope state ---- */

/* Service VAO for index-buffer data ops: the ELEMENT_ARRAY_BUFFER bind is VAO
 * state, and core profile rejects it with VAO 0 bound (INVALID_OPERATION). */
static GLuint s_ebo_upload_vao;

static nt_gfx_gl_program_t *s_programs;           /* linked programs, indexed by slot */
static nt_gfx_gl_pipeline_t *s_pipelines;         /* pipeline data, indexed by slot */
static nt_gfx_gl_vertex_input_t *s_vertex_inputs; /* vertex-input VAOs, indexed by slot */
static GLuint *s_buffer_gl;                       /* GL buffer names, indexed by slot */
static GLenum *s_buffer_targets;                  /* GL_ARRAY_BUFFER or GL_ELEMENT_ARRAY_BUFFER */
static GLuint *s_texture_gl;                      /* GL texture names, indexed by slot */
static nt_gfx_gl_render_target_t *s_render_targets;
static GLuint s_bound_framebuffer;

static nt_gfx_desc_t s_init_desc; /* resolved desc: defaults applied, used everywhere */

// #region GPU timer segments — types & state
#if NT_GFX_GPU_TIMING_ENABLED
/* GPU TIME_ELAPSED named segments (EXT_disjoint_timer_query_webgl2 / ARB_timer_query).
 *
 * Self-contained sub-system inside this GL backend. Lives in three clusters:
 *   1. types & state (this region)
 *   2. impl: helpers + begin/end (region "GPU timer segments — begin/end")
 *   3. impl: poll/drop/enable (region "GPU timer segments — poll/lifecycle")
 * Plus a one-line disjoint check inside nt_gfx_backend_begin_frame (cross-cut
 * with frame lifecycle, intentional — disjoint clears on read so it must
 * happen exactly once per frame).
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
_Static_assert(NT_GFX_TIMER_RING < 12, "query ring names must fit the backend record after its count");
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
    NT_GFX_RECORD(NT_GFX_EVENT_DEFINITION, NT_GFX_OP_STATE, event.detail = NT_GFX_OBJECT_PROGRAM; event.data.backend.args[0] = i; event.data.backend.args[1] = program->program;);
    for (uint32_t u = 0; u < program->uniform_count; u++) {
        if ((program->vec4_mask & (1U << u)) == 0) {
            continue;
        }
        NT_GFX_RECORD(
            NT_GFX_EVENT_INITIAL, NT_GFX_OP_UNIFORM_VEC4, event.data.backend.args[0] = i; event.data.backend.args[1] = program->uniforms[u].name_hash;
            event.data.backend.args[2] = (uint32_t)program->uniforms[u].location; if ((program->vec4_valid & (1U << u)) != 0) {
                memcpy(event.data.backend.values, program->vec4_values[u], sizeof(event.data.backend.values));
            } else { event.reason = NT_GFX_REASON_UNKNOWN; });
    }
    for (uint32_t u = 0; u < program->sampler_count; u++) {
        NT_GFX_RECORD(NT_GFX_EVENT_INITIAL, NT_GFX_OP_SAMPLER, event.data.backend.args[0] = i; event.data.backend.args[1] = program->sampler_units[u].name_hash;
                      event.data.backend.args[2] = (uint32_t)program->sampler_units[u].location; event.data.backend.args[3] = u; event.data.backend.args[4] = program->sampler_units[u].sampler_class;);
    }
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity) -- bounded snapshots of separate backend tables
void nt_gfx_backend_capture_initial_state(void) {
    NT_GFX_RECORD(NT_GFX_EVENT_INITIAL, NT_GFX_OP_STATE, event.data.backend.args[0] = s_gl_cache.program; event.data.backend.args[1] = s_gl_cache.vao; event.data.backend.args[2] = s_bound_framebuffer;
                  event.data.backend.args[3] = s_gl_cache.active_texture_unit; event.data.backend.args[4] = g_nt_window.fb_width; event.data.backend.args[5] = g_nt_window.fb_height;
                  event.data.backend.args[6] = s_ebo_upload_vao; event.detail = NT_GFX_INITIAL_BACKEND;);
    NT_GFX_RECORD(NT_GFX_EVENT_INITIAL, NT_GFX_OP_VIEWPORT, for (uint32_t i = 0; i < 4; i++) { event.data.state.integers[i] = (uint32_t)s_gl_cache.viewport[i]; });
    NT_GFX_RECORD(NT_GFX_EVENT_INITIAL, NT_GFX_OP_PASS, memcpy(event.data.pass.color, s_gl_cache.clear_color, sizeof(event.data.pass.color)); event.data.pass.depth = s_gl_cache.clear_depth;);
    NT_GFX_RECORD(NT_GFX_EVENT_INITIAL, NT_GFX_OP_PIPELINE, event.data.state.integers[0] = s_gl_cache.program; event.data.state.integers[1] = s_gl_cache.depth_test_enabled;
                  event.data.state.integers[2] = s_gl_cache.depth_write_enabled; event.data.state.integers[3] = s_gl_cache.depth_func; event.data.state.integers[4] = s_gl_cache.cull_mode;
                  event.data.state.integers[5] = s_gl_cache.blend_enabled; event.data.state.integers[6] = s_gl_cache.blend_src_rgb; event.data.state.integers[7] = s_gl_cache.blend_dst_rgb;
                  event.data.state.integers[8] = s_gl_cache.blend_src_alpha; event.data.state.integers[9] = s_gl_cache.blend_dst_alpha; event.data.state.integers[10] = s_gl_cache.blend_op_rgb;
                  event.data.state.integers[11] = s_gl_cache.blend_op_alpha; event.data.state.integers[12] = s_gl_cache.polygon_offset_enabled;
                  memcpy(event.data.state.values, s_gl_cache.blend_constant_color, 4 * sizeof(float)); event.data.state.values[4] = s_gl_cache.polygon_offset_factor;
                  event.data.state.values[5] = s_gl_cache.polygon_offset_units;);
    for (uint32_t i = 1; i <= s_init_desc.max_pipelines; i++) {
        if (s_pipelines[i].program_slot == 0) {
            continue;
        }
        NT_GFX_RECORD(NT_GFX_EVENT_DEFINITION, NT_GFX_OP_PIPELINE, event.detail = i; event.data.state.integers[0] = s_pipelines[i].program_slot;
                      event.data.state.integers[1] = s_pipelines[i].depth_test_enabled; event.data.state.integers[2] = s_pipelines[i].depth_write_enabled;
                      event.data.state.integers[3] = s_pipelines[i].depth_func; event.data.state.integers[4] = s_pipelines[i].cull_mode; event.data.state.integers[5] = s_pipelines[i].blend_enabled;
                      event.data.state.integers[6] = s_pipelines[i].blend_src_rgb; event.data.state.integers[7] = s_pipelines[i].blend_dst_rgb;
                      event.data.state.integers[8] = s_pipelines[i].blend_src_alpha; event.data.state.integers[9] = s_pipelines[i].blend_dst_alpha;
                      event.data.state.integers[10] = s_pipelines[i].blend_op_rgb; event.data.state.integers[11] = s_pipelines[i].blend_op_alpha;
                      event.data.state.integers[12] = s_pipelines[i].polygon_offset_enabled; memcpy(event.data.state.values, s_pipelines[i].blend_constant_color, 4 * sizeof(float));
                      event.data.state.values[4] = s_pipelines[i].polygon_offset_factor; event.data.state.values[5] = s_pipelines[i].polygon_offset_units;);
    }
    for (uint32_t unit = 0; unit < NT_GFX_MAX_TEXTURE_SLOTS; unit++) {
        NT_GFX_RECORD(NT_GFX_EVENT_INITIAL, NT_GFX_OP_TEXTURE, event.data.backend.args[0] = unit; event.data.backend.args[1] = s_gl_cache.bound_textures[unit];
                      event.data.backend.args[2] = s_gl_cache.bound_samplers[unit];);
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
        NT_GFX_RECORD(NT_GFX_EVENT_DEFINITION, NT_GFX_OP_STATE, event.detail = NT_GFX_OBJECT_BUFFER; event.data.backend.args[0] = i; event.data.backend.args[1] = s_buffer_gl[i];);
    }
    for (uint32_t i = 1; i <= s_init_desc.max_textures; i++) {
        if (s_texture_gl[i] == 0) {
            continue;
        }
        NT_GFX_RECORD(NT_GFX_EVENT_DEFINITION, NT_GFX_OP_STATE, event.detail = NT_GFX_OBJECT_TEXTURE; event.data.backend.args[0] = i; event.data.backend.args[1] = s_texture_gl[i];);
    }
    for (uint32_t i = 1; i <= s_init_desc.max_vertex_inputs; i++) {
        if (s_vertex_inputs[i].vao == 0) {
            continue;
        }
        NT_GFX_RECORD(NT_GFX_EVENT_DEFINITION, NT_GFX_OP_STATE, event.detail = NT_GFX_OBJECT_VERTEX_INPUT; event.data.backend.args[0] = i; event.data.backend.args[1] = s_vertex_inputs[i].vao;);
        for (uint32_t a = 0; a < s_vertex_inputs[i].instance_attr_count; a++) {
            const nt_vertex_attr_t *attr = &s_vertex_inputs[i].instance_attrs[a];
            NT_GFX_RECORD(NT_GFX_EVENT_DEFINITION, NT_GFX_OP_ATTRIBUTE, event.detail = i; event.reason = NT_GFX_REASON_UNKNOWN; event.data.attribute.location = attr->location;
                          event.data.attribute.type = (uint32_t)attr->type; event.data.attribute.count = attr->count; event.data.attribute.normalized = attr->normalized;
                          event.data.attribute.offset = attr->offset; event.data.attribute.stride = s_vertex_inputs[i].instance_stride; event.data.attribute.divisor = 1;);
        }
    }
    for (uint32_t i = 1; i <= s_init_desc.max_render_targets; i++) {
        if (s_render_targets[i].fbo == 0) {
            continue;
        }
        NT_GFX_RECORD(NT_GFX_EVENT_DEFINITION, NT_GFX_OP_STATE, event.detail = NT_GFX_OBJECT_RENDER_TARGET; event.data.backend.args[0] = i; event.data.backend.args[1] = s_render_targets[i].fbo;
                      event.data.backend.args[2] = s_render_targets[i].depth_rbo;);
    }
}
#endif
// #endregion

/* ---- Per-program uniform location lookup ---- */

static int program_uniform_index(const nt_gfx_gl_program_t *prog, uint32_t name_hash) {
    for (uint8_t i = 0; i < prog->uniform_count; i++) {
        if (prog->uniforms[i].name_hash == name_hash) {
            return i;
        }
    }
    return -1;
}

static int program_get_uniform_index(uint32_t program_backend, uint32_t name_hash) {
    NT_ASSERT(program_backend != 0 && program_backend <= s_init_desc.max_programs && s_programs[program_backend].program != 0 && "set_uniform: requires a live program");
    /* glUniform* writes into the current program, so the named one must be it. */
    NT_ASSERT(s_programs[program_backend].program == s_gl_cache.program && "uniform write targets a program that is not current");
    return program_uniform_index(&s_programs[program_backend], name_hash);
}

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

/* s_gl_cache.vao bookkeeping stays at the call sites. */
static void gl_bind_vao(GLuint vao) { NT_GL(glBindVertexArray, vao); }

/* The service VAO prevents EBO data operations from rewriting a draw VAO.
 * Detaching on exit lets deletion release the uploaded buffer's storage. */
static void ebo_upload_begin(void) { gl_bind_vao(s_ebo_upload_vao); }

static void ebo_upload_end(void) {
    NT_GL(glBindBuffer, GL_ELEMENT_ARRAY_BUFFER, 0);
    gl_bind_vao(s_gl_cache.vao);
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
    gl_bind_vao(0);
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
    s_gl_cache.vao = 0;
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

static void nt_gfx_gl_init_context_features(void) {
    /* Emscripten keeps a recorded error across contexts: calls that reached the
     * dead context must not fail the fresh one's first error check. */
    while (NT_GL_RET0(glGetError) != GL_NO_ERROR) {
    }
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
    const bool lost = s_ebo_upload_vao == 0 && nt_gfx_gl_ctx_query_lost();
    NT_ASSERT(s_ebo_upload_vao != 0 || lost);
}

bool nt_gfx_backend_init(const nt_gfx_desc_t *desc) {
    NT_ASSERT(desc);
    s_init_desc = *desc;

    if (!nt_gfx_gl_ctx_create(&s_init_desc)) {
        return false;
    }

    /* Allocate backend resource arrays (+1 because slots are 1-based) */
    s_programs = (nt_gfx_gl_program_t *)calloc(s_init_desc.max_programs + 1, sizeof(nt_gfx_gl_program_t));
    s_pipelines = (nt_gfx_gl_pipeline_t *)calloc(s_init_desc.max_pipelines + 1, sizeof(nt_gfx_gl_pipeline_t));
    s_vertex_inputs = (nt_gfx_gl_vertex_input_t *)calloc(s_init_desc.max_vertex_inputs + 1, sizeof(nt_gfx_gl_vertex_input_t));
    s_buffer_gl = (GLuint *)calloc(s_init_desc.max_buffers + 1, sizeof(GLuint));
    s_buffer_targets = (GLenum *)calloc(s_init_desc.max_buffers + 1, sizeof(GLenum));
    s_texture_gl = (GLuint *)calloc(s_init_desc.max_textures + 1, sizeof(GLuint));
    s_render_targets = (nt_gfx_gl_render_target_t *)calloc(s_init_desc.max_render_targets + 1, sizeof(nt_gfx_gl_render_target_t));
    /* Init-time OOM on a few KB of tables is not a state a game can recover from. */
    NT_ASSERT(s_programs && s_pipelines && s_vertex_inputs && s_buffer_gl && s_buffer_targets && s_texture_gl && s_render_targets && "gfx backend init: out of memory");

    nt_gfx_gl_cache_ground_state();

    nt_gfx_gl_init_context_features();
    return true;
}

void nt_gfx_backend_shutdown(void) {
#if NT_GFX_GPU_TIMING_ENABLED
    if (s_timer_enabled && !nt_gfx_gl_ctx_is_lost()) {
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
    free(s_render_targets);

    s_programs = NULL;
    s_pipelines = NULL;
    s_vertex_inputs = NULL;
    s_buffer_gl = NULL;
    s_buffer_targets = NULL;
    s_texture_gl = NULL;
    s_render_targets = NULL;

    s_bound_framebuffer = 0;
    /* A dead context already reclaimed the name; a GL call here would run
     * without a current context on web. */
    if (s_ebo_upload_vao != 0 && !nt_gfx_gl_ctx_is_lost()) {
        NT_GL_DELETE(glDeleteVertexArrays, 1, &s_ebo_upload_vao);
    }
    s_ebo_upload_vao = 0;

    nt_gfx_gl_ctx_destroy();
}

bool nt_gfx_backend_is_context_lost(void) { return nt_gfx_gl_ctx_is_lost(); }

void nt_gfx_backend_ack_context_loss(void) { nt_gfx_gl_ctx_ack_loss(); }

/* ---- Frame / Pass ---- */

// #region GPU timer segments — begin/end
#if NT_GFX_GPU_TIMING_ENABLED
/* Find existing segment by name hash, or allocate a new slot with its own
 * ring of GL_TIME_ELAPSED queries. Linear scan is fine for small N (<= 16). */
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

void nt_gfx_backend_begin_frame(void) {
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

    /* Disjoint check moved to nt_gfx_backend_begin_frame — runs once per frame
     * instead of once per poll, avoiding GLE roundtrip in the drain loop. */

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
        if (nt_gfx_backend_is_context_lost()) {
            if (!g_nt_gfx.context_lost) {
                nt_gfx_observe_context_loss();
            }
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

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void nt_gfx_backend_begin_pass(const nt_pass_desc_t *desc, uint32_t render_target_backend) {
    NT_ASSERT(desc != NULL);
    if (desc == NULL) {
        return;
    }
    GLsizei viewport_w = (GLsizei)g_nt_window.fb_width;
    GLsizei viewport_h = (GLsizei)g_nt_window.fb_height;
    GLuint fbo = 0;
    if (render_target_backend != 0) {
        bool valid_backend = render_target_backend <= s_init_desc.max_render_targets && s_render_targets != NULL;
        NT_ASSERT(valid_backend && "begin_pass: invalid GL render target backend");
        if (!valid_backend) {
            return;
        }
        const nt_gfx_gl_render_target_t *rt = &s_render_targets[render_target_backend];
        NT_ASSERT(rt->fbo != 0 && "begin_pass: invalid GL render target");
        if (rt->fbo == 0) {
            return;
        }
        fbo = rt->fbo;
        viewport_w = (GLsizei)rt->width;
        viewport_h = (GLsizei)rt->height;
    }
    if (s_bound_framebuffer != fbo) {
        NT_GL(glBindFramebuffer, GL_FRAMEBUFFER, fbo);
        s_bound_framebuffer = fbo;
    }
    gl_set_viewport(0, 0, (int)viewport_w, (int)viewport_h);
    if (!float4_equal(s_gl_cache.clear_color, desc->clear_color)) {
        memcpy(s_gl_cache.clear_color, desc->clear_color, sizeof(s_gl_cache.clear_color));
        NT_GL(glClearColor, desc->clear_color[0], desc->clear_color[1], desc->clear_color[2], desc->clear_color[3]);
    }
    if (s_gl_cache.clear_depth != desc->clear_depth) {
        s_gl_cache.clear_depth = desc->clear_depth;
        nt_gl_clear_depth(desc->clear_depth);
    }
    /* The clear must not inherit the previous pipeline's depth-write mask, and
     * leaves it on: the pass's first pipeline bind re-applies its own. */
    if (!s_gl_cache.depth_write_enabled) {
        NT_GL(glDepthMask, GL_TRUE);
        s_gl_cache.depth_write_enabled = true;
    }
    NT_GL(glClear, GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void nt_gfx_backend_end_pass(void) {
    if (s_bound_framebuffer != 0) {
        NT_GL(glBindFramebuffer, GL_FRAMEBUFFER, 0);
        s_bound_framebuffer = 0;
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
bool nt_gfx_backend_read_pixels(int x, int y, int w, int h, void *out_rgba8) {
    NT_GL(glPixelStorei, GL_PACK_ALIGNMENT, 4);
    /* Drain any stale GL error so the post-read check is attributable to THIS readback. */
    while (NT_GL_RET0(glGetError) != GL_NO_ERROR) {
    }
    NT_GL(glReadPixels, x, y, (GLsizei)w, (GLsizei)h, GL_RGBA, GL_UNSIGNED_BYTE, out_rgba8);
    /* A failed read (incomplete FB, invalid read buffer, no current context) leaves out_rgba8
       partly/wholly untouched — report it so the dev-only capture path yields capture_failed, not garbage. */
    return NT_GL_RET0(glGetError) == GL_NO_ERROR;
}

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
        NT_GFX_RECORD(NT_GFX_EVENT_SKIP, NT_GFX_OP_PIPELINE, event.reason = NT_GFX_REASON_CACHE; event.detail = NT_GFX_GL_glUseProgram; event.data.backend.args[0] = program;);
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

/* ---- Uniforms ---- */

void nt_gfx_backend_set_uniform_mat4(uint32_t program_backend, uint32_t name_hash, const float *matrix) {
    int index = program_get_uniform_index(program_backend, name_hash);
    if (index < 0) {
        NT_GFX_RECORD(NT_GFX_EVENT_SKIP, NT_GFX_OP_UNIFORM_MAT4, event.reason = NT_GFX_REASON_INACTIVE; event.data.binding.name = name_hash; event.data.binding.secondary = program_backend;);
        return;
    }
    NT_GL_UNIFORM(glUniformMatrix4fv, 16, s_programs[program_backend].uniforms[index].location, 1, GL_FALSE, matrix);
}

void nt_gfx_backend_set_uniform_vec4(uint32_t program_backend, uint32_t name_hash, const float *vec) {
    int index = program_get_uniform_index(program_backend, name_hash);
    if (index < 0) {
        NT_GFX_RECORD(NT_GFX_EVENT_SKIP, NT_GFX_OP_UNIFORM_VEC4, event.reason = NT_GFX_REASON_INACTIVE; event.data.binding.name = name_hash; event.data.binding.secondary = program_backend;);
        return;
    }
    nt_gfx_gl_program_t *prog = &s_programs[program_backend];
    const uint16_t bit = (uint16_t)(1U << (uint32_t)index);
    if ((prog->vec4_valid & bit) != 0 && memcmp(prog->vec4_values[index], (const void *)vec, sizeof(prog->vec4_values[index])) == 0) {
        NT_GFX_RECORD(NT_GFX_EVENT_SKIP, NT_GFX_OP_UNIFORM_VEC4, event.reason = NT_GFX_REASON_CACHE; event.data.binding.name = name_hash; event.data.binding.secondary = program_backend;);
        return;
    }
    NT_GL_UNIFORM(glUniform4fv, 4, prog->uniforms[index].location, 1, vec);
    // Preserve uncached GL handling for other uniform types.
    if ((prog->vec4_mask & bit) != 0) {
        memcpy(prog->vec4_values[index], vec, sizeof(prog->vec4_values[index]));
        prog->vec4_valid |= bit;
    }
}

void nt_gfx_backend_set_uniform_float(uint32_t program_backend, uint32_t name_hash, float val) {
    int index = program_get_uniform_index(program_backend, name_hash);
    if (index < 0) {
        NT_GFX_RECORD(NT_GFX_EVENT_SKIP, NT_GFX_OP_UNIFORM_FLOAT, event.reason = NT_GFX_REASON_INACTIVE; event.data.binding.name = name_hash; event.data.binding.secondary = program_backend;);
        return;
    }
    NT_GL(glUniform1f, s_programs[program_backend].uniforms[index].location, val);
}

void nt_gfx_backend_set_uniform_int(uint32_t program_backend, uint32_t name_hash, int val) {
    int index = program_get_uniform_index(program_backend, name_hash);
    if (index < 0) {
        /* Samplers never enter the uniform table, so only a miss can name one. */
        nt_gfx_sampler_info_t sampler_info = {0};
        const bool is_sampler = nt_gfx_backend_program_sampler_info(program_backend, name_hash, &sampler_info);
        NT_ASSERT(!is_sampler && "sampler uniforms are immutable; use nt_gfx_apply_texture_bindings");
        NT_GFX_RECORD(NT_GFX_EVENT_SKIP, NT_GFX_OP_UNIFORM_INT, event.reason = NT_GFX_REASON_INACTIVE; event.data.binding.name = name_hash; event.data.binding.secondary = program_backend;);
        return;
    }
    NT_GL(glUniform1i, s_programs[program_backend].uniforms[index].location, val);
}

/* ---- Draw calls ---- */

void nt_gfx_backend_draw(uint32_t first_vertex, uint32_t num_vertices) { NT_GL(glDrawArrays, GL_TRIANGLES, (GLint)first_vertex, (GLsizei)num_vertices); }

void nt_gfx_backend_draw_indexed(uint32_t first_index, uint32_t num_indices, uint8_t index_type) {
    GLenum gl_type = (index_type == 2) ? GL_UNSIGNED_INT : GL_UNSIGNED_SHORT;
    uint32_t stride = (index_type == 2) ? sizeof(uint32_t) : sizeof(uint16_t);
    NT_GL(glDrawElements, GL_TRIANGLES, (GLsizei)num_indices, gl_type, nt_gl_offset((uintptr_t)first_index * stride));
}

/* ---- Resource management (shader / buffer / pipeline) ---- */

uint32_t nt_gfx_backend_create_shader(const nt_shader_desc_t *desc) {
    /* A browser that returns a null shader on a lost context makes Emscripten's glShaderSource throw. */
    if (nt_gfx_gl_ctx_query_lost()) {
        return 0;
    }
    GLenum gl_type = (desc->type == NT_SHADER_VERTEX) ? GL_VERTEX_SHADER : GL_FRAGMENT_SHADER;
    GLuint shader = NT_GL_RET(glCreateShader, gl_type);

#ifdef NT_PLATFORM_WEB
    const char *prefix = "#version 300 es\n";
#else
    const char *prefix = "#version 330 core\n";
#endif
    const char *sources[2] = {prefix, desc->source};
    NT_GL(glShaderSource, shader, 2, sources, NULL);

    NT_GL(glCompileShader, shader);
    /* Per MDN best practice: do NOT check GL_COMPILE_STATUS here.
     * Checking forces synchronous compilation.  Errors surface at link time. */
    return (uint32_t)shader;
}

/* Log shader/program info line-by-line (query actual length, no fixed buffer) */
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

void nt_gfx_backend_destroy_shader(uint32_t backend_handle) {
    /* Zero reaches here from nt_gfx_shutdown's sweep over free slots, and that
     * sweep also runs when nt_gfx_init failed before glad loaded an entry point. */
    if (backend_handle == 0) {
        return;
    }
    NT_GL(glDeleteShader, (GLuint)backend_handle);
}

/* Links a stage pair and binds every registered global UBO block. Returns 0 on
 * link failure, after logging both stages and the program log. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity) -- diagnostic record and assert macros expand at owning sites
static GLuint nt_gfx_gl_link_program(uint32_t vs_backend, uint32_t fs_backend) {
    /* Emscripten's glCreateProgram throws on the null program some browsers return on a lost context. */
    if (nt_gfx_gl_ctx_query_lost()) {
        return 0;
    }
    GLuint program = NT_GL_RET0(glCreateProgram);
    NT_GL(glAttachShader, program, (GLuint)vs_backend);
    NT_GL(glAttachShader, program, (GLuint)fs_backend);
    NT_GL(glLinkProgram, program);

    GLint linked = 0;
    NT_GL(glGetProgramiv, program, GL_LINK_STATUS, &linked);
    if (!linked) {
        /* A loss fails the link before its event arrives; the query latches it for the caller. */
        if (!nt_gfx_gl_ctx_query_lost()) {
            nt_gfx_gl_log_shader(vs_backend, "vertex");
            nt_gfx_gl_log_shader(fs_backend, "fragment");
            nt_gfx_gl_log_program(program);
        }

        NT_GL(glDeleteProgram, program);
        return 0;
    }

    const nt_global_block_t *blocks;
    uint32_t block_count;
    nt_gfx_get_global_blocks(&blocks, &block_count);
    for (uint32_t bi = 0; bi < block_count; bi++) {
        GLuint block_index = NT_GL_RET(glGetUniformBlockIndex, program, blocks[bi].name);
        if (block_index != GL_INVALID_INDEX) {
            NT_GL(glUniformBlockBinding, program, block_index, (GLuint)blocks[bi].binding_slot);
        }
    }
    return program;
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

/* Only 2D samplers have a texture-unit story here; the rest would bind
 * silently wrong, so they are rejected at link instead. */
static bool uniform_sampler_class(GLenum utype, nt_gfx_sampler_class_t *out_class) {
    switch (utype) {
    case GL_SAMPLER_2D:
        *out_class = NT_GFX_SAMPLER_CLASS_FLOAT;
        return true;
    case GL_SAMPLER_2D_SHADOW:
        *out_class = NT_GFX_SAMPLER_CLASS_SHADOW;
        return true;
    case GL_UNSIGNED_INT_SAMPLER_2D:
        *out_class = NT_GFX_SAMPLER_CLASS_UINT;
        return true;
    case GL_INT_SAMPLER_2D:
        NT_ASSERT(false && "program sampler requires a signed integer texture format");
        return false;
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
        NT_ASSERT(false && "program declares an unsupported sampler type");
        return false;
    default:
        return false;
    }
}

/* Cache locations off the hot path; NT_ASSERT expansion inflates complexity. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static bool nt_gfx_gl_cache_uniforms(GLuint program, nt_gfx_gl_program_t *rec) {
    nt_cached_uniform_t *out = rec->uniforms;
    uint8_t *out_count = &rec->uniform_count;
    rec->sampler_count = 0;
    rec->vec4_mask = 0;
    rec->vec4_valid = 0;
    *out_count = 0;
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
    NT_ASSERT((size_t)max_name_length + 10U <= NT_GFX_GL_MAX_UNIFORM_NAME && "uniform name exceeds NT_GFX_GL_MAX_UNIFORM_NAME");
    char uname[NT_GFX_GL_MAX_UNIFORM_NAME];
    uint32_t uniform_count = 0;
    for (GLint ui = 0; ui < active_uniforms; ui++) {
        GLsizei ulen = 0;
        GLint usize = 0;
        GLenum utype = 0;
        NT_GL(glGetActiveUniform, program, (GLuint)ui, max_name_length, &ulen, &usize, &utype, uname);
        if (ulen <= 0 || usize <= 0) {
            return false;
        }
        NT_ASSERT(usize == 1 || (ulen >= 3 && strcmp(uname + ulen - 3, "[0]") == 0));
        for (GLint element = 0; element < usize; element++) {
            if (element > 0) {
                size_t suffix = (size_t)ulen - 3U;
                nt_gfx_gl_write_array_index(uname + suffix, element);
            }
            GLint loc = NT_GL_RET(glGetUniformLocation, program, uname);
            if (loc >= 0) {
                const uint32_t name_hash = nt_hash32_str(uname).value;
                nt_gfx_sampler_class_t sampler_class = NT_GFX_SAMPLER_CLASS_FLOAT;
                if (uniform_sampler_class(utype, &sampler_class)) {
                    NT_ASSERT(rec->sampler_count < NT_GFX_MAX_TEXTURE_SLOTS && "program declares more samplers than NT_GFX_MAX_TEXTURE_SLOTS");
                    const uint8_t unit = rec->sampler_count;
                    rec->sampler_units[unit].name_hash = name_hash;
                    rec->sampler_units[unit].location = loc;
                    rec->sampler_units[unit].sampler_class = (uint8_t)sampler_class;
                    rec->sampler_count++;
                } else {
                    if (uniform_count < NT_MAX_CACHED_UNIFORMS) {
                        out[uniform_count].name_hash = name_hash;
                        out[uniform_count].location = loc;
                        if (utype == GL_FLOAT_VEC4) {
                            rec->vec4_mask |= (uint16_t)(1U << uniform_count);
                        }
                    }
                    uniform_count++;
                }
            }
        }
    }
    NT_ASSERT(uniform_count <= NT_MAX_CACHED_UNIFORMS && "program exceeds standalone uniform cache capacity");
    *out_count = (uint8_t)uniform_count;
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
uint32_t nt_gfx_backend_create_program(uint32_t vs_backend, uint32_t fs_backend) {
    GLuint program = nt_gfx_gl_link_program(vs_backend, fs_backend);
    if (program == 0) {
        return 0;
    }

    uint32_t slot = 0;
    for (uint32_t i = 1; i <= s_init_desc.max_programs; i++) {
        if (s_programs[i].program == 0) {
            slot = i;
            break;
        }
    }
    if (slot == 0) {
        NT_GL(glDeleteProgram, program);
        return 0; /* no free slots */
    }

    if (!nt_gfx_gl_cache_uniforms(program, &s_programs[slot])) {
        if (!nt_gfx_gl_ctx_query_lost()) {
            NT_LOG_ERROR("program uniform reflection failed");
        }
        NT_GL(glDeleteProgram, program);
        return 0;
    }
    write_sampler_units(program, &s_programs[slot]);
    s_programs[slot].program = program;
#if NT_GFX_CAPTURE_ENABLED
    capture_program_definition(slot);
#endif
    return slot;
}

void nt_gfx_backend_destroy_program(uint32_t backend_handle) {
    if (backend_handle == 0) {
        return;
    }
    NT_ASSERT(backend_handle <= s_init_desc.max_programs && "destroy_program: handle out of range");
    GLuint program = s_programs[backend_handle].program;
    if (program == 0) {
        return;
    }
    /* GL defers deletion while a program remains current. */
    if (s_gl_cache.program == program) {
        NT_GL(glUseProgram, 0);
        s_gl_cache.program = 0;
    }
    NT_GL(glDeleteProgram, program);
    memset(&s_programs[backend_handle], 0, sizeof(s_programs[backend_handle]));
}

uint32_t nt_gfx_backend_create_pipeline(const nt_pipeline_desc_t *desc, uint32_t program_backend, uint32_t slot) {
    if (program_backend == 0 || program_backend > s_init_desc.max_programs) {
        return 0;
    }
    /* The frontend pool owns slot allocation; the backend table mirrors it. */
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

    NT_GFX_RECORD(NT_GFX_EVENT_DEFINITION, NT_GFX_OP_PIPELINE, event.detail = slot; event.data.state.integers[0] = pip->program_slot; event.data.state.integers[1] = pip->depth_test_enabled;
                  event.data.state.integers[2] = pip->depth_write_enabled; event.data.state.integers[3] = pip->depth_func; event.data.state.integers[4] = pip->cull_mode;
                  event.data.state.integers[5] = pip->blend_enabled; event.data.state.integers[6] = pip->blend_src_rgb; event.data.state.integers[7] = pip->blend_dst_rgb;
                  event.data.state.integers[8] = pip->blend_src_alpha; event.data.state.integers[9] = pip->blend_dst_alpha; event.data.state.integers[10] = pip->blend_op_rgb;
                  event.data.state.integers[11] = pip->blend_op_alpha; event.data.state.integers[12] = pip->polygon_offset_enabled;
                  memcpy(event.data.state.values, pip->blend_constant_color, 4 * sizeof(float)); event.data.state.values[4] = pip->polygon_offset_factor;
                  event.data.state.values[5] = pip->polygon_offset_units;);
    return slot;
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

// NOLINTNEXTLINE(readability-function-cognitive-complexity) -- NT_ASSERT expansion inflates the metric
uint32_t nt_gfx_backend_create_vertex_input(const nt_vertex_input_desc_t *desc, uint32_t vbo_backend, uint32_t ibo_backend, uint32_t slot) {
    NT_ASSERT(desc != NULL);
    /* The frontend pool owns slot allocation; the backend table mirrors it. */
    NT_ASSERT(slot > 0 && slot <= s_init_desc.max_vertex_inputs && s_vertex_inputs[slot].vao == 0);

    GLuint vao = 0;
    NT_GL_GEN(glGenVertexArrays, 1, &vao);
    if (vao == 0) {
        (void)nt_gfx_gl_ctx_query_lost(); /* latches a loss whose event has not arrived yet */
        return 0;
    }
    gl_bind_vao(vao);
    if (vbo_backend != 0 && vbo_backend <= s_init_desc.max_buffers) {
        /* Buffer bound before the pointer calls -- satisfies the WebGL "no
         * pointer without a bound ARRAY_BUFFER" rule at creation time. */
        NT_GL(glBindBuffer, GL_ARRAY_BUFFER, s_buffer_gl[vbo_backend]);
        for (uint8_t i = 0; i < desc->layout.attr_count; i++) {
            const nt_vertex_attr_t *attr = &desc->layout.attrs[i];
            NT_GL(glEnableVertexAttribArray, attr->location);
            NT_GL(glVertexAttribPointer, attr->location, attr->count, map_vertex_type(attr->type), attr->normalized ? GL_TRUE : GL_FALSE, (GLsizei)desc->layout.stride, nt_gl_offset(attr->offset));
        }
    }
    if (ibo_backend != 0 && ibo_backend <= s_init_desc.max_buffers) {
        NT_GL(glBindBuffer, GL_ELEMENT_ARRAY_BUFFER, s_buffer_gl[ibo_backend]); /* captured by the VAO */
    }
    for (uint8_t i = 0; i < desc->instance_layout.attr_count; i++) {
        GLuint loc = desc->instance_layout.attrs[i].location;
        NT_GL(glEnableVertexAttribArray, loc);
        NT_GL(glVertexAttribDivisor, loc, 1); /* pointers deferred to bind_instance_buffer */
    }
    gl_bind_vao(s_gl_cache.vao);

    s_vertex_inputs[slot].vao = vao;
    uint8_t inst_count = desc->instance_layout.attr_count;
    if (inst_count > NT_GFX_MAX_INSTANCE_ATTRS) {
        inst_count = NT_GFX_MAX_INSTANCE_ATTRS;
    }
    for (uint8_t i = 0; i < inst_count; i++) {
        s_vertex_inputs[slot].instance_attrs[i] = desc->instance_layout.attrs[i];
    }
    s_vertex_inputs[slot].instance_attr_count = inst_count;
    s_vertex_inputs[slot].instance_stride = desc->instance_layout.stride;
    NT_GFX_RECORD(NT_GFX_EVENT_DEFINITION, NT_GFX_OP_STATE, event.detail = NT_GFX_OBJECT_VERTEX_INPUT; event.data.backend.args[0] = slot; event.data.backend.args[1] = vao;);
    return slot;
}

void nt_gfx_backend_destroy_vertex_input(uint32_t backend_handle) {
    if (backend_handle == 0) {
        return;
    }
    NT_ASSERT(backend_handle <= s_init_desc.max_vertex_inputs && "destroy_vertex_input: handle out of range");
    nt_gfx_gl_vertex_input_t *vi = &s_vertex_inputs[backend_handle];
    /* Deleting the bound VAO reverts the GL binding to 0 -- mirror it. */
    if (vi->vao && s_gl_cache.vao == vi->vao) {
        s_gl_cache.vao = 0;
    }
    if (vi->vao) {
        NT_GL_DELETE(glDeleteVertexArrays, 1, &vi->vao);
    }
    memset(vi, 0, sizeof(*vi));
}

void nt_gfx_backend_bind_vertex_input(uint32_t backend_handle) {
    /* A zeroed record (context loss, destroyed vertex input) would leave the
     * previous VAO bound and draw the wrong geometry. */
    NT_ASSERT(backend_handle != 0 && backend_handle <= s_init_desc.max_vertex_inputs && s_vertex_inputs[backend_handle].vao != 0 && "bind_vertex_input: requires a live vertex input");
    GLuint vao = s_vertex_inputs[backend_handle].vao;
    if (s_gl_cache.vao != vao) {
        gl_bind_vao(vao);
        s_gl_cache.vao = vao;
    } else {
        NT_GFX_RECORD(NT_GFX_EVENT_SKIP, NT_GFX_OP_VERTEX_INPUT, event.reason = NT_GFX_REASON_CACHE; event.detail = NT_GFX_GL_glBindVertexArray; event.data.backend.args[0] = vao;);
    }
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity) -- diagnostic record and assert macros expand at owning sites
uint32_t nt_gfx_backend_create_buffer(const nt_buffer_desc_t *desc) {
    GLuint buf;
    NT_GL_GEN(glGenBuffers, 1, &buf);
    if (buf == 0) {
        (void)nt_gfx_gl_ctx_query_lost(); /* latches a loss whose event has not arrived yet */
        return 0;                         /* storing name 0 would alias the free-slot sentinel */
    }
    GLenum target;
    switch (desc->type) {
    case NT_BUFFER_VERTEX:
        target = GL_ARRAY_BUFFER;
        break;
    case NT_BUFFER_INDEX:
        target = GL_ELEMENT_ARRAY_BUFFER;
        break;
    case NT_BUFFER_UNIFORM:
        target = GL_UNIFORM_BUFFER;
        break;
    default:
        target = GL_ARRAY_BUFFER;
        break;
    }
    GLenum usage = map_buffer_usage(desc->usage);
    bool unhook_vao = target == GL_ELEMENT_ARRAY_BUFFER;
    if (unhook_vao) {
        ebo_upload_begin();
    }
    NT_GL(glBindBuffer, target, buf);
    NT_GL_UPLOAD(desc->data, desc->size, glBufferData, target, (GLsizeiptr)desc->size, desc->data, usage);
    if (unhook_vao) {
        ebo_upload_end();
    }

    /* Find free buffer slot */
    uint32_t slot = 0;
    for (uint32_t i = 1; i <= s_init_desc.max_buffers; i++) {
        if (s_buffer_gl[i] == 0) {
            slot = i;
            break;
        }
    }
    if (slot == 0) {
        NT_GL_DELETE(glDeleteBuffers, 1, &buf);
        return 0;
    }

    s_buffer_gl[slot] = buf;
    s_buffer_targets[slot] = target;
    NT_GFX_RECORD(NT_GFX_EVENT_DEFINITION, NT_GFX_OP_STATE, event.detail = NT_GFX_OBJECT_BUFFER; event.data.backend.args[0] = slot; event.data.backend.args[1] = buf;);
    return slot;
}

void nt_gfx_backend_destroy_buffer(uint32_t backend_handle) {
    if (backend_handle == 0) {
        return;
    }
    NT_ASSERT(backend_handle <= s_init_desc.max_buffers && "destroy_buffer: handle out of range");
    GLuint buf = s_buffer_gl[backend_handle];
    if (buf) {
        NT_GL_DELETE(glDeleteBuffers, 1, &buf);
    }
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
    NT_GL(glBindBuffer, target, buf);
    NT_GL_UPLOAD(data, size, glBufferSubData, target, (GLintptr)offset, (GLsizeiptr)size, data);
    if (unhook_vao) {
        ebo_upload_end();
    }
}

void nt_gfx_backend_orphan_buffer(uint32_t backend_handle, const void *data, uint32_t size) {
    if (backend_handle == 0 || backend_handle > s_init_desc.max_buffers) {
        return;
    }
    GLuint buf = s_buffer_gl[backend_handle];
    GLenum target = s_buffer_targets[backend_handle];
    bool unhook_vao = target == GL_ELEMENT_ARRAY_BUFFER;
    if (unhook_vao) {
        ebo_upload_begin();
    }
    NT_GL(glBindBuffer, target, buf);
    /* glBufferData with non-NULL data both orphans the existing storage and
     * uploads in one call. The driver may allocate fresh memory for the new
     * contents and reclaim the old block once the GPU finishes consuming it,
     * avoiding the pipeline stall that glBufferSubData can introduce when
     * rewriting a buffer that's still in flight. */
    NT_GL_UPLOAD(data, size, glBufferData, target, (GLsizeiptr)size, data, GL_DYNAMIC_DRAW);
    if (unhook_vao) {
        ebo_upload_end();
    }
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity) -- diagnostic record and assert macros expand at owning sites
void nt_gfx_backend_bind_instance_buffer(uint32_t vertex_input_backend, uint32_t buffer_backend, uint32_t byte_offset) {
    NT_ASSERT(buffer_backend != 0 && buffer_backend <= s_init_desc.max_buffers && s_buffer_gl[buffer_backend] != 0 && "bind_instance_buffer: requires a live buffer");
    NT_ASSERT(vertex_input_backend != 0 && vertex_input_backend <= s_init_desc.max_vertex_inputs && s_vertex_inputs[vertex_input_backend].vao != 0 &&
              "bind_instance_buffer: requires a live vertex input");
    /* The pointers land in whatever VAO is bound, so the named one must be it. */
    NT_ASSERT(s_vertex_inputs[vertex_input_backend].vao == s_gl_cache.vao && "bind_instance_buffer: named vertex input is not the bound VAO");
    GLuint buf = s_buffer_gl[buffer_backend];
    NT_GL(glBindBuffer, GL_ARRAY_BUFFER, buf);

    const nt_gfx_gl_vertex_input_t *vi = &s_vertex_inputs[vertex_input_backend];
    for (uint8_t i = 0; i < vi->instance_attr_count; i++) {
        const nt_vertex_attr_t *attr = &vi->instance_attrs[i];
        NT_GL(glVertexAttribPointer, attr->location, attr->count, map_vertex_type(attr->type), attr->normalized ? GL_TRUE : GL_FALSE, (GLsizei)vi->instance_stride,
              nt_gl_offset(attr->offset + byte_offset));
    }
}

void nt_gfx_backend_set_vertex_attrib_default(uint8_t location, float x, float y, float z, float w) { NT_GL(glVertexAttrib4f, (GLuint)location, x, y, z, w); }

/* ---- Uniform buffer ---- */

void nt_gfx_backend_bind_uniform_buffer(uint32_t backend_handle, uint32_t slot) {
    NT_ASSERT(backend_handle != 0 && backend_handle <= s_init_desc.max_buffers && s_buffer_gl[backend_handle] != 0 && "bind_uniform_buffer: requires a live buffer");
    GLuint buf = s_buffer_gl[backend_handle];
    NT_GL(glBindBufferBase, GL_UNIFORM_BUFFER, slot, buf);
}

void nt_gfx_backend_set_uniform_block(uint32_t program_backend, const char *block_name, uint32_t slot) {
    if (program_backend == 0 || program_backend > s_init_desc.max_programs) {
        return;
    }
    GLuint program = s_programs[program_backend].program;
    if (program == 0) {
        return;
    }
    GLuint block_index = NT_GL_RET(glGetUniformBlockIndex, program, block_name);
    if (block_index != GL_INVALID_INDEX) {
        NT_GL(glUniformBlockBinding, program, block_index, slot);
    }
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

/* For upload paths that check glGetError afterwards — a stale error would be
 * misattributed to this upload. */
static bool nt_gfx_gl_begin_texture_upload(GLuint tex) {
    GLenum pending_error = NT_GL_RET0(glGetError);
    /* A loss the browser confirms is a recoverable outcome the caller rolls back,
       not a programmer error; the query also latches it before its event arrives. */
    if (pending_error != GL_NO_ERROR && nt_gfx_gl_ctx_query_lost()) {
        nt_gfx_observe_context_loss();
        return false;
    }
    NT_ASSERT(pending_error == GL_NO_ERROR && "pending GL error before texture upload");
    if (pending_error != GL_NO_ERROR) {
        NT_LOG_ERROR("pending GL error before texture upload: 0x%04X", (unsigned)pending_error);
        return false;
    }
    nt_gfx_gl_bind_texture_for_upload(tex);
    return true;
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity) -- diagnostic record and assert macros expand at owning sites
static GLuint nt_gfx_gl_create_texture_name(const nt_texture_desc_t *desc) {
    GLuint tex;
    NT_GL_GEN(glGenTextures, 1, &tex);
    if (tex == 0) {
        (void)nt_gfx_gl_ctx_query_lost(); /* latches a loss whose event has not arrived yet */
        return 0;
    }
    if (!nt_gfx_gl_begin_texture_upload(tex)) {
        NT_GL_DELETE(glDeleteTextures, 1, &tex);
        return 0;
    }

    /* Filter and wrap live on the sampler object every bind carries; the
       texture object keeps GL defaults, which no sampling path reads. */
    nt_gfx_gl_fmt_t gl = nt_gfx_gl_texture_format(desc->format);
    if (gl.internal == 0) {
        NT_GL_DELETE(glDeleteTextures, 1, &tex);
        return 0;
    }

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
            NT_GL_UPLOAD(level_data, level_bytes, glCompressedTexImage2D, GL_TEXTURE_2D, (GLint)level, gl.internal, (GLsizei)level_w, (GLsizei)level_h, 0, (GLsizei)level_bytes, level_data);
        } else {
            NT_GL_UPLOAD(level_data, level_bytes, glTexImage2D, GL_TEXTURE_2D, (GLint)level, (GLint)gl.internal, (GLsizei)level_w, (GLsizei)level_h, 0, gl.format, gl.type, level_data);
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

    GLenum first_error = GL_NO_ERROR;
    for (GLenum e = NT_GL_RET0(glGetError); e != GL_NO_ERROR; e = NT_GL_RET0(glGetError)) {
        if (e == GL_CONTEXT_LOST_WEBGL) {
            nt_gfx_observe_context_loss();
        }
        if (first_error == GL_NO_ERROR) {
            first_error = e;
        }
    }
    if (first_error != GL_NO_ERROR) {
        NT_LOG_ERROR("texture creation failed: GL error 0x%04X", (unsigned)first_error);
        NT_GL_DELETE(glDeleteTextures, 1, &tex);
        return 0;
    }

    return tex;
}

uint32_t nt_gfx_backend_create_texture(const nt_texture_desc_t *desc) {
    GLuint tex = nt_gfx_gl_create_texture_name(desc);
    if (tex == 0) {
        return 0;
    }

    /* Find free texture slot (1-based, 0 is reserved invalid) */
    uint32_t slot = 0;
    for (uint32_t i = 1; i <= s_init_desc.max_textures; i++) {
        if (s_texture_gl[i] == 0) {
            slot = i;
            break;
        }
    }
    if (slot == 0) {
        NT_GL_DELETE(glDeleteTextures, 1, &tex);
        return 0;
    }

    s_texture_gl[slot] = tex;

    NT_GFX_RECORD(NT_GFX_EVENT_DEFINITION, NT_GFX_OP_STATE, event.detail = NT_GFX_OBJECT_TEXTURE; event.data.backend.args[0] = slot; event.data.backend.args[1] = tex;);
    return slot;
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

    NT_GL_UPLOAD(data, nt_texture_level_bytes(format, w, h), glTexSubImage2D, GL_TEXTURE_2D, 0, (GLint)x, (GLint)y, (GLsizei)w, (GLsizei)h, gl.format, gl.type, data);

    if (!gl.align4) {
        NT_GL(glPixelStorei, GL_UNPACK_ALIGNMENT, 4);
    }
}

void nt_gfx_backend_destroy_texture(uint32_t backend_handle) {
    if (backend_handle == 0) {
        return;
    }
    NT_ASSERT(backend_handle <= s_init_desc.max_textures && "destroy_texture: handle out of range");
    GLuint tex = s_texture_gl[backend_handle];
    if (tex) {
        nt_gfx_gl_forget_texture(tex);
        NT_GL_DELETE(glDeleteTextures, 1, &tex);
    }
    s_texture_gl[backend_handle] = 0;
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity) -- diagnostic record and assert macros expand at owning sites
static bool nt_gfx_gl_build_render_target(const nt_render_target_desc_t *desc, GLuint color, GLuint depth, nt_gfx_gl_render_target_t *out_rt) {
    NT_ASSERT(desc != NULL && color != 0 && out_rt != NULL);
    if (desc == NULL || color == 0 || out_rt == NULL) {
        return false;
    }

    GLuint restore_fbo = s_bound_framebuffer;
    GLuint fbo = 0;
    GLuint depth_rbo = 0;
    NT_GL_GEN(glGenFramebuffers, 1, &fbo);
    if (fbo == 0) {
        (void)nt_gfx_gl_ctx_query_lost(); /* latches a loss whose event has not arrived yet */
        return false;
    }
    NT_GL(glBindFramebuffer, GL_FRAMEBUFFER, fbo);
    s_bound_framebuffer = fbo;
    NT_GL(glFramebufferTexture2D, GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, color, 0);

    if (desc->depth_storage == NT_RT_DEPTH_BUFFER) {
        NT_GL_GEN(glGenRenderbuffers, 1, &depth_rbo);
        if (depth_rbo == 0) {
            (void)nt_gfx_gl_ctx_query_lost(); /* latches a loss whose event has not arrived yet */
            NT_GL_DELETE(glDeleteFramebuffers, 1, &fbo);
            NT_GL(glBindFramebuffer, GL_FRAMEBUFFER, restore_fbo);
            s_bound_framebuffer = restore_fbo;
            return false;
        }
        NT_GL(glBindRenderbuffer, GL_RENDERBUFFER, depth_rbo);
        nt_gfx_gl_fmt_t depth_fmt = nt_gfx_gl_texture_format(desc->depth_format);
        NT_GL(glRenderbufferStorage, GL_RENDERBUFFER, depth_fmt.internal, (GLsizei)desc->width, (GLsizei)desc->height);
        NT_GL(glFramebufferRenderbuffer, GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depth_rbo);
        NT_GL(glBindRenderbuffer, GL_RENDERBUFFER, 0);
    } else if (desc->depth_storage == NT_RT_DEPTH_TEXTURE) {
        if (depth == 0) {
            NT_GL_DELETE(glDeleteFramebuffers, 1, &fbo);
            NT_GL(glBindFramebuffer, GL_FRAMEBUFFER, restore_fbo);
            s_bound_framebuffer = restore_fbo;
            return false;
        }
        NT_GL(glFramebufferTexture2D, GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, depth, 0);
    }

    GLenum status = NT_GL_RET(glCheckFramebufferStatus, GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        if (!nt_gfx_gl_ctx_query_lost()) {
            NT_LOG_ERROR("render target incomplete: GL status 0x%04X", (unsigned)status);
        }
        if (depth_rbo != 0) {
            NT_GL_DELETE(glDeleteRenderbuffers, 1, &depth_rbo);
        }
        NT_GL_DELETE(glDeleteFramebuffers, 1, &fbo);
        NT_GL(glBindFramebuffer, GL_FRAMEBUFFER, restore_fbo);
        s_bound_framebuffer = restore_fbo;
        return false;
    }

    *out_rt = (nt_gfx_gl_render_target_t){
        .fbo = fbo,
        .depth_rbo = depth_rbo,
        .width = desc->width,
        .height = desc->height,
    };
    NT_GL(glBindFramebuffer, GL_FRAMEBUFFER, restore_fbo);
    s_bound_framebuffer = restore_fbo;
    return true;
}

static bool nt_gfx_gl_create_render_target_in_slot(uint32_t slot, const nt_render_target_desc_t *desc, uint32_t color_backend, uint32_t depth_texture_backend) {
    bool valid_args =
        slot != 0 && slot <= s_init_desc.max_render_targets && desc != NULL && color_backend != 0 && color_backend <= s_init_desc.max_textures && s_render_targets != NULL && s_texture_gl != NULL;
    NT_ASSERT(valid_args && "render target: invalid backend create arguments");
    if (!valid_args) {
        return false;
    }
    GLuint color = s_texture_gl[color_backend];
    GLuint depth = 0;
    if (color == 0) {
        return false;
    }
    if (desc->depth_storage == NT_RT_DEPTH_TEXTURE) {
        bool valid_depth = depth_texture_backend != 0 && depth_texture_backend <= s_init_desc.max_textures;
        NT_ASSERT(valid_depth && "render target: invalid depth texture backend");
        if (!valid_depth || s_texture_gl[depth_texture_backend] == 0) {
            return false;
        }
        depth = s_texture_gl[depth_texture_backend];
    }
    return nt_gfx_gl_build_render_target(desc, color, depth, &s_render_targets[slot]);
}

uint32_t nt_gfx_backend_create_render_target(const nt_render_target_desc_t *desc, uint32_t color_backend, uint32_t depth_texture_backend) {
    NT_ASSERT(s_render_targets != NULL && "render target backend is not initialized");
    if (s_render_targets == NULL) {
        return 0;
    }
    uint32_t slot = 0;
    for (uint32_t i = 1; i <= s_init_desc.max_render_targets; i++) {
        if (s_render_targets[i].fbo == 0) {
            slot = i;
            break;
        }
    }
    NT_ASSERT(slot != 0 && "render target backend slots exhausted before shared pool");
    if (slot == 0) {
        return 0;
    }
    if (!nt_gfx_gl_create_render_target_in_slot(slot, desc, color_backend, depth_texture_backend)) {
        return 0;
    }
    NT_GFX_RECORD(NT_GFX_EVENT_DEFINITION, NT_GFX_OP_STATE, event.detail = NT_GFX_OBJECT_RENDER_TARGET; event.data.backend.args[0] = slot; event.data.backend.args[1] = s_render_targets[slot].fbo;
                  event.data.backend.args[2] = s_render_targets[slot].depth_rbo;);
    return slot;
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity) -- diagnostic record and assert macros expand at owning sites
void nt_gfx_backend_destroy_render_target(uint32_t backend_handle) {
    if (backend_handle == 0) {
        return;
    }
    NT_ASSERT(backend_handle <= s_init_desc.max_render_targets && s_render_targets != NULL && "destroy_render_target: invalid GL backend handle");
    nt_gfx_gl_render_target_t *rt = &s_render_targets[backend_handle];
    if (s_bound_framebuffer == rt->fbo) {
        NT_GL(glBindFramebuffer, GL_FRAMEBUFFER, 0);
        s_bound_framebuffer = 0;
    }
    if (rt->depth_rbo != 0) {
        NT_GL_DELETE(glDeleteRenderbuffers, 1, &rt->depth_rbo);
    }
    if (rt->fbo != 0) {
        NT_GL_DELETE(glDeleteFramebuffers, 1, &rt->fbo);
    }
    memset(rt, 0, sizeof(*rt));
}

typedef struct {
    nt_gfx_gl_render_target_t target;
    GLuint color;
    GLuint depth;
} nt_gfx_gl_resize_staging_t;

// NOLINTNEXTLINE(readability-function-cognitive-complexity) -- diagnostic record and assert macros expand at owning sites
static void nt_gfx_gl_discard_resize_staging(nt_gfx_gl_resize_staging_t *staging) {
    if (staging->target.depth_rbo != 0) {
        NT_GL_DELETE(glDeleteRenderbuffers, 1, &staging->target.depth_rbo);
    }
    if (staging->target.fbo != 0) {
        NT_GL_DELETE(glDeleteFramebuffers, 1, &staging->target.fbo);
    }
    if (staging->depth != 0) {
        NT_GL_DELETE(glDeleteTextures, 1, &staging->depth);
    }
    if (staging->color != 0) {
        NT_GL_DELETE(glDeleteTextures, 1, &staging->color);
    }
    memset(staging, 0, sizeof(*staging));
}

static bool nt_gfx_gl_stage_render_target_resize(const nt_render_target_desc_t *desc, nt_gfx_gl_resize_staging_t *out) {
    nt_texture_desc_t color_desc = {
        .width = desc->width,
        .height = desc->height,
        .format = desc->color_format,
        .min_filter = desc->color_min_filter,
        .mag_filter = desc->color_mag_filter,
        .wrap_u = desc->color_wrap_u,
        .wrap_v = desc->color_wrap_v,
    };
    out->color = nt_gfx_gl_create_texture_name(&color_desc);
    if (out->color == 0) {
        return false;
    }
    if (desc->depth_storage == NT_RT_DEPTH_TEXTURE) {
        nt_texture_desc_t depth_desc = {
            .width = desc->width,
            .height = desc->height,
            .format = desc->depth_format,
            .min_filter = desc->depth_texture_min_filter,
            .mag_filter = desc->depth_texture_mag_filter,
            .wrap_u = desc->depth_texture_wrap_u,
            .wrap_v = desc->depth_texture_wrap_v,
        };
        out->depth = nt_gfx_gl_create_texture_name(&depth_desc);
        if (out->depth == 0) {
            nt_gfx_gl_discard_resize_staging(out);
            return false;
        }
    }
    // NOLINTNEXTLINE(readability-function-cognitive-complexity) -- diagnostic record and assert macros expand at owning sites
    if (!nt_gfx_gl_build_render_target(desc, out->color, out->depth, &out->target)) {
        nt_gfx_gl_discard_resize_staging(out);
        return false;
    }
    return true;
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity) -- diagnostic record and assert macros expand at owning sites
static void nt_gfx_gl_commit_render_target_resize(uint32_t backend_handle, uint32_t color_backend, uint32_t depth_backend, const nt_render_target_desc_t *desc, nt_gfx_gl_resize_staging_t *staging) {
    nt_gfx_gl_render_target_t old = s_render_targets[backend_handle];
    GLuint old_color = s_texture_gl[color_backend];
    s_texture_gl[color_backend] = staging->color;
    NT_GFX_RECORD(NT_GFX_EVENT_DEFINITION, NT_GFX_OP_STATE, event.detail = NT_GFX_OBJECT_TEXTURE; event.data.backend.args[0] = color_backend; event.data.backend.args[1] = staging->color;);
    nt_gfx_gl_forget_texture(old_color);
    NT_GL_DELETE(glDeleteTextures, 1, &old_color);

    if (desc->depth_storage == NT_RT_DEPTH_TEXTURE) {
        GLuint old_depth = s_texture_gl[depth_backend];
        s_texture_gl[depth_backend] = staging->depth;
        NT_GFX_RECORD(NT_GFX_EVENT_DEFINITION, NT_GFX_OP_STATE, event.detail = NT_GFX_OBJECT_TEXTURE; event.data.backend.args[0] = depth_backend; event.data.backend.args[1] = staging->depth;);
        nt_gfx_gl_forget_texture(old_depth);
        NT_GL_DELETE(glDeleteTextures, 1, &old_depth);
    }
    s_render_targets[backend_handle] = staging->target;
    NT_GFX_RECORD(NT_GFX_EVENT_DEFINITION, NT_GFX_OP_STATE, event.detail = NT_GFX_OBJECT_RENDER_TARGET; event.data.backend.args[0] = backend_handle; event.data.backend.args[1] = staging->target.fbo;
                  event.data.backend.args[2] = staging->target.depth_rbo;);
    if (s_bound_framebuffer == old.fbo) {
        NT_GL(glBindFramebuffer, GL_FRAMEBUFFER, staging->target.fbo);
        s_bound_framebuffer = staging->target.fbo;
    }
    if (old.depth_rbo != 0) {
        NT_GL_DELETE(glDeleteRenderbuffers, 1, &old.depth_rbo);
    }
    NT_GL_DELETE(glDeleteFramebuffers, 1, &old.fbo);
    memset(staging, 0, sizeof(*staging));
}

static bool nt_gfx_gl_render_target_resize_args_valid(uint32_t backend_handle, const nt_render_target_desc_t *desc, uint32_t color_backend) {
    return backend_handle != 0 && backend_handle <= s_init_desc.max_render_targets && desc != NULL && color_backend != 0 && color_backend <= s_init_desc.max_textures && s_render_targets != NULL &&
           s_texture_gl != NULL;
}

static bool nt_gfx_gl_render_target_resize_depth_valid(const nt_render_target_desc_t *desc, uint32_t depth_backend) {
    return desc->depth_storage != NT_RT_DEPTH_TEXTURE || (depth_backend != 0 && depth_backend <= s_init_desc.max_textures && s_texture_gl[depth_backend] != 0);
}

bool nt_gfx_backend_resize_render_target(uint32_t backend_handle, const nt_render_target_desc_t *desc, uint32_t color_backend, uint32_t depth_texture_backend) {
    bool valid_args = nt_gfx_gl_render_target_resize_args_valid(backend_handle, desc, color_backend);
    NT_ASSERT(valid_args && "resize_render_target: invalid GL backend arguments");
    if (!valid_args) {
        return false;
    }
    if (s_render_targets[backend_handle].fbo == 0 || s_texture_gl[color_backend] == 0) {
        return false;
    }
    bool valid_depth = nt_gfx_gl_render_target_resize_depth_valid(desc, depth_texture_backend);
    NT_ASSERT(valid_depth && "resize_render_target: invalid depth texture backend");
    if (!valid_depth) {
        return false;
    }

    nt_gfx_gl_resize_staging_t staging = {0};
    if (!nt_gfx_gl_stage_render_target_resize(desc, &staging)) {
        return false;
    }
    nt_gfx_gl_commit_render_target_resize(backend_handle, color_backend, depth_texture_backend, desc, &staging);
    return true;
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity) -- diagnostic record and assert macros expand at owning sites
void nt_gfx_backend_bind_texture(uint32_t backend_handle, uint32_t slot) {
    NT_ASSERT(slot < NT_GFX_MAX_TEXTURE_SLOTS && "bind_texture: slot out of range");
    NT_ASSERT(backend_handle != 0 && backend_handle <= s_init_desc.max_textures && s_texture_gl[backend_handle] != 0 && "bind_texture: requires a live texture");
    GLuint tex = s_texture_gl[backend_handle];
    if (s_gl_cache.bound_textures[slot] == tex) {
        NT_GFX_RECORD(NT_GFX_EVENT_SKIP, NT_GFX_OP_TEXTURE, event.detail = NT_GFX_GL_glBindTexture; event.reason = NT_GFX_REASON_CACHE; event.data.backend.args[0] = tex;
                      event.data.backend.args[1] = slot;);
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
        (void)nt_gfx_gl_ctx_query_lost(); /* latches a loss whose event has not arrived yet */
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
void nt_gfx_backend_bind_sampler(uint32_t backend_handle, uint32_t slot) {
    NT_ASSERT(slot < NT_GFX_MAX_TEXTURE_SLOTS && "bind_sampler: slot out of range");
    NT_ASSERT(backend_handle != 0 && "bind_sampler: sampling without a sampler object");
    GLuint sampler = (GLuint)backend_handle;
    if (s_gl_cache.bound_samplers[slot] == sampler) {
        NT_GFX_RECORD(NT_GFX_EVENT_SKIP, NT_GFX_OP_SAMPLER, event.detail = NT_GFX_GL_glBindSampler; event.reason = NT_GFX_REASON_CACHE; event.data.backend.args[0] = sampler;
                      event.data.backend.args[1] = slot;);
        return;
    }
    NT_GL(glBindSampler, slot, sampler);
    s_gl_cache.bound_samplers[slot] = sampler;
}

void nt_gfx_backend_draw_instanced(uint32_t first_vertex, uint32_t num_vertices, uint32_t instance_count) {
    NT_GL(glDrawArraysInstanced, GL_TRIANGLES, (GLint)first_vertex, (GLsizei)num_vertices, (GLsizei)instance_count);
}

void nt_gfx_backend_draw_indexed_instanced(uint32_t first_index, uint32_t num_indices, uint32_t instance_count, uint8_t index_type) {
    GLenum gl_type = (index_type == 2) ? GL_UNSIGNED_INT : GL_UNSIGNED_SHORT;
    uint32_t stride = (index_type == 2) ? sizeof(uint32_t) : sizeof(uint16_t);
    NT_GL(glDrawElementsInstanced, GL_TRIANGLES, (GLsizei)num_indices, gl_type, nt_gl_offset((uintptr_t)first_index * stride), (GLsizei)instance_count);
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
    if (s_texture_gl) {
        memset(s_texture_gl, 0, (s_init_desc.max_textures + 1) * sizeof(GLuint));
    }
    if (s_render_targets) {
        memset(s_render_targets, 0, (s_init_desc.max_render_targets + 1) * sizeof(nt_gfx_gl_render_target_t));
    }
    nt_gfx_gl_cache_ground_state();
    nt_gfx_gl_init_context_features();
    return true;
}
