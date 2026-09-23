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
#include "core/nt_platform.h"
#include "graphics/gl/nt_gfx_gl_ctx.h"
#include "graphics/nt_gfx_internal.h"
#include "hash/nt_hash.h"
#include "log/nt_log.h"
#include "window/nt_window.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* ---- GL headers ---- */

#ifdef NT_PLATFORM_WEB
#include <GLES3/gl3.h>
#define nt_gl_clear_depth(d) glClearDepthf(d)
#else
#include <glad/gl.h>
#define nt_gl_clear_depth(d) glClearDepth((double)(d))
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

#if NT_GFX_CAPTURE_ENABLED
_Static_assert(NT_GFX_TIMER_RING < 12, "query ring names must fit the backend record after its count");
static void capture_query_names(nt_gfx_gl_call_t call, const GLuint *queries) {
    NT_GFX_RECORD(
        NT_GFX_EVENT_BACKEND, NT_GFX_OP_STATE, event.detail = (uint32_t)call; event.data.backend.args[0] = NT_GFX_TIMER_RING;
        for (uint32_t i = 0; i < NT_GFX_TIMER_RING; i++) { event.data.backend.args[1 + i] = queries[i]; });
}
#else
#define capture_query_names(call, queries) ((void)0)
#endif
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
    if (g_nt_gfx_capture.view.overflow) {
        return;
    }
    NT_GFX_RECORD(NT_GFX_EVENT_INITIAL, NT_GFX_OP_STATE, event.data.backend.args[0] = s_gl_cache.program; event.data.backend.args[1] = s_gl_cache.vao; event.data.backend.args[2] = s_bound_framebuffer;
                  event.data.backend.args[3] = s_gl_cache.active_texture_unit; event.data.backend.args[4] = g_nt_window.fb_width; event.data.backend.args[5] = g_nt_window.fb_height;
                  event.data.backend.args[6] = s_ebo_upload_vao; event.detail = 1;);
    NT_GFX_RECORD(NT_GFX_EVENT_INITIAL, NT_GFX_OP_VIEWPORT, for (uint32_t i = 0; i < 4; i++) { event.data.state.integers[i] = (uint32_t)s_gl_cache.viewport[i]; });
    NT_GFX_RECORD(NT_GFX_EVENT_INITIAL, NT_GFX_OP_PASS, memcpy(event.data.pass.color, s_gl_cache.clear_color, sizeof(event.data.pass.color)); event.data.pass.depth = s_gl_cache.clear_depth;);
    NT_GFX_RECORD(NT_GFX_EVENT_INITIAL, NT_GFX_OP_PIPELINE, event.detail = 0; event.data.state.integers[0] = s_gl_cache.program; event.data.state.integers[1] = s_gl_cache.depth_test_enabled;
                  event.data.state.integers[2] = s_gl_cache.depth_write_enabled; event.data.state.integers[3] = s_gl_cache.depth_func; event.data.state.integers[4] = s_gl_cache.cull_mode;
                  event.data.state.integers[5] = s_gl_cache.blend_enabled; event.data.state.integers[6] = s_gl_cache.blend_src_rgb; event.data.state.integers[7] = s_gl_cache.blend_dst_rgb;
                  event.data.state.integers[8] = s_gl_cache.blend_src_alpha; event.data.state.integers[9] = s_gl_cache.blend_dst_alpha; event.data.state.integers[10] = s_gl_cache.blend_op_rgb;
                  event.data.state.integers[11] = s_gl_cache.blend_op_alpha; event.data.state.integers[12] = s_gl_cache.polygon_offset_enabled;
                  memcpy(event.data.state.values, s_gl_cache.blend_constant_color, 4 * sizeof(float)); event.data.state.values[4] = s_gl_cache.polygon_offset_factor;
                  event.data.state.values[5] = s_gl_cache.polygon_offset_units;);
    for (uint32_t i = 1; i <= s_init_desc.max_pipelines && !g_nt_gfx_capture.view.overflow; i++) {
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
    for (uint32_t i = 1; i <= s_init_desc.max_programs && !g_nt_gfx_capture.view.overflow; i++) {
        const nt_gfx_gl_program_t *program = &s_programs[i];
        if (program->program == 0) {
            continue;
        }
        capture_program_definition(i);
    }
    for (uint32_t i = 1; i <= s_init_desc.max_buffers && !g_nt_gfx_capture.view.overflow; i++) {
        if (s_buffer_gl[i] == 0) {
            continue;
        }
        NT_GFX_RECORD(NT_GFX_EVENT_DEFINITION, NT_GFX_OP_STATE, event.detail = NT_GFX_OBJECT_BUFFER; event.data.backend.args[0] = i; event.data.backend.args[1] = s_buffer_gl[i];);
    }
    for (uint32_t i = 1; i <= s_init_desc.max_textures && !g_nt_gfx_capture.view.overflow; i++) {
        if (s_texture_gl[i] == 0) {
            continue;
        }
        NT_GFX_RECORD(NT_GFX_EVENT_DEFINITION, NT_GFX_OP_STATE, event.detail = NT_GFX_OBJECT_TEXTURE; event.data.backend.args[0] = i; event.data.backend.args[1] = s_texture_gl[i];);
    }
    for (uint32_t i = 1; i <= s_init_desc.max_vertex_inputs && !g_nt_gfx_capture.view.overflow; i++) {
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
    for (uint32_t i = 1; i <= s_init_desc.max_render_targets && !g_nt_gfx_capture.view.overflow; i++) {
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

// #region test counters
#ifdef NT_TEST_ACCESS
static uint32_t s_test_static_attrib_pointer_calls;   /* divisor-0 glVertexAttribPointer */
static uint32_t s_test_instance_attrib_pointer_calls; /* divisor-1 glVertexAttribPointer */
static uint32_t s_test_vao_binds;
static uint32_t s_test_sampler_binds;

void nt_gfx_gl_test_reset_counters(void) {
    s_test_static_attrib_pointer_calls = 0;
    s_test_instance_attrib_pointer_calls = 0;
    s_test_vao_binds = 0;
    s_test_sampler_binds = 0;
}

uint32_t nt_gfx_gl_test_static_attrib_pointer_calls(void) { return s_test_static_attrib_pointer_calls; }
uint32_t nt_gfx_gl_test_instance_attrib_pointer_calls(void) { return s_test_instance_attrib_pointer_calls; }
uint32_t nt_gfx_gl_test_vao_binds(void) { return s_test_vao_binds; }
uint32_t nt_gfx_gl_test_sampler_binds(void) { return s_test_sampler_binds; }

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

/* Every VAO bind goes through here so the test counter sees them all;
 * s_gl_cache.vao bookkeeping stays at the call sites. */
static void gl_bind_vao(GLuint vao) {
#ifdef NT_TEST_ACCESS
    s_test_vao_binds++;
#endif
    glBindVertexArray(vao);
    NT_GFX_RECORD(NT_GFX_EVENT_BACKEND, NT_GFX_OP_STATE, event.detail = NT_GFX_GL_BINDVERTEXARRAY; event.data.backend.args[0] = (uint32_t)(vao););
    NT_GFX_COUNT(vao_calls);
}

/* The service VAO prevents EBO data operations from rewriting a draw VAO.
 * Detaching on exit lets deletion release the uploaded buffer's storage. */
static void ebo_upload_begin(void) { gl_bind_vao(s_ebo_upload_vao); }

static void ebo_upload_end(void) {
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    NT_GFX_RECORD(NT_GFX_EVENT_BACKEND, NT_GFX_OP_STATE, event.detail = NT_GFX_GL_BINDBUFFER; event.data.backend.args[0] = (uint32_t)(GL_ELEMENT_ARRAY_BUFFER);
                  event.data.backend.args[1] = (uint32_t)(0););
    gl_bind_vao(s_gl_cache.vao);
}

static void gl_set_viewport(int x, int y, int w, int h) {
    const int rect[4] = {x, y, w, h};
    if (memcmp(s_gl_cache.viewport, rect, sizeof(rect)) == 0) {
        return;
    }
    memcpy(s_gl_cache.viewport, rect, sizeof(rect));
    glViewport(x, y, (GLsizei)w, (GLsizei)h);
    NT_GFX_RECORD(NT_GFX_EVENT_BACKEND, NT_GFX_OP_STATE, event.detail = NT_GFX_GL_VIEWPORT; event.data.backend.args[0] = (uint32_t)(x); event.data.backend.args[1] = (uint32_t)(y);
                  event.data.backend.args[2] = (uint32_t)((GLsizei)w); event.data.backend.args[3] = (uint32_t)((GLsizei)h););
}

/* Real GL calls, not cache defaults: the native context outlives init cycles and
 * restore reuses it. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity) -- diagnostic record and assert macros expand at owning sites
static void nt_gfx_gl_cache_ground_state(void) {
    gl_bind_vao(0);
    glUseProgram(0);
    NT_GFX_RECORD(NT_GFX_EVENT_BACKEND, NT_GFX_OP_STATE, event.detail = NT_GFX_GL_USEPROGRAM; event.data.backend.args[0] = (uint32_t)(0););
    NT_GFX_COUNT(program_calls);
    glDisable(GL_DEPTH_TEST);
    NT_GFX_RECORD(NT_GFX_EVENT_BACKEND, NT_GFX_OP_STATE, event.detail = NT_GFX_GL_DISABLE; event.data.backend.args[0] = (uint32_t)(GL_DEPTH_TEST););
    glDepthMask(GL_TRUE);
    NT_GFX_RECORD(NT_GFX_EVENT_BACKEND, NT_GFX_OP_STATE, event.detail = NT_GFX_GL_DEPTHMASK; event.data.backend.args[0] = (uint32_t)(GL_TRUE););
    glDepthFunc(GL_LESS);
    NT_GFX_RECORD(NT_GFX_EVENT_BACKEND, NT_GFX_OP_STATE, event.detail = NT_GFX_GL_DEPTHFUNC; event.data.backend.args[0] = (uint32_t)(GL_LESS););
    glDisable(GL_CULL_FACE);
    NT_GFX_RECORD(NT_GFX_EVENT_BACKEND, NT_GFX_OP_STATE, event.detail = NT_GFX_GL_DISABLE; event.data.backend.args[0] = (uint32_t)(GL_CULL_FACE););
    glDisable(GL_BLEND);
    NT_GFX_RECORD(NT_GFX_EVENT_BACKEND, NT_GFX_OP_STATE, event.detail = NT_GFX_GL_DISABLE; event.data.backend.args[0] = (uint32_t)(GL_BLEND););
    glBlendFuncSeparate(GL_ONE, GL_ZERO, GL_ONE, GL_ZERO);
    NT_GFX_RECORD(NT_GFX_EVENT_BACKEND, NT_GFX_OP_STATE, event.detail = NT_GFX_GL_BLENDFUNCSEPARATE; event.data.backend.args[0] = (uint32_t)(GL_ONE); event.data.backend.args[1] = (uint32_t)(GL_ZERO);
                  event.data.backend.args[2] = (uint32_t)(GL_ONE); event.data.backend.args[3] = (uint32_t)(GL_ZERO););
    glBlendEquationSeparate(GL_FUNC_ADD, GL_FUNC_ADD);
    NT_GFX_RECORD(NT_GFX_EVENT_BACKEND, NT_GFX_OP_STATE, event.detail = NT_GFX_GL_BLENDEQUATIONSEPARATE; event.data.backend.args[0] = (uint32_t)(GL_FUNC_ADD);
                  event.data.backend.args[1] = (uint32_t)(GL_FUNC_ADD););
    glBlendColor(0.0F, 0.0F, 0.0F, 0.0F);
    NT_GFX_RECORD(NT_GFX_EVENT_BACKEND, NT_GFX_OP_STATE, event.detail = NT_GFX_GL_BLENDCOLOR; event.data.backend.values[0] = 0.0F; event.data.backend.values[1] = 0.0F;
                  event.data.backend.values[2] = 0.0F; event.data.backend.values[3] = 0.0F;);
    glDisable(GL_POLYGON_OFFSET_FILL);
    NT_GFX_RECORD(NT_GFX_EVENT_BACKEND, NT_GFX_OP_STATE, event.detail = NT_GFX_GL_DISABLE; event.data.backend.args[0] = (uint32_t)(GL_POLYGON_OFFSET_FILL););
    glPolygonOffset(0.0F, 0.0F);
    NT_GFX_RECORD(NT_GFX_EVENT_BACKEND, NT_GFX_OP_STATE, event.detail = NT_GFX_GL_POLYGONOFFSET; event.data.backend.values[0] = 0.0F; event.data.backend.values[1] = 0.0F;);
    glDisable(GL_SCISSOR_TEST);
    NT_GFX_RECORD(NT_GFX_EVENT_BACKEND, NT_GFX_OP_STATE, event.detail = NT_GFX_GL_DISABLE; event.data.backend.args[0] = (uint32_t)(GL_SCISSOR_TEST););
    glActiveTexture(GL_TEXTURE0);
    NT_GFX_RECORD(NT_GFX_EVENT_BACKEND, NT_GFX_OP_STATE, event.detail = NT_GFX_GL_ACTIVETEXTURE; event.data.backend.args[0] = (uint32_t)(GL_TEXTURE0););
    for (uint32_t unit = 0; unit < NT_GFX_MAX_TEXTURE_SLOTS; unit++) {
        glBindSampler(unit, 0);
        NT_GFX_RECORD(NT_GFX_EVENT_BACKEND, NT_GFX_OP_STATE, event.detail = NT_GFX_GL_BINDSAMPLER; event.data.backend.args[0] = (uint32_t)(unit); event.data.backend.args[1] = (uint32_t)(0););
        NT_GFX_COUNT(sampler_calls);
    }
    /* A zero-size viewport is legal GL and never equals a real pass, so the first
     * pass after grounding always re-issues. */
    glViewport(0, 0, 0, 0);
    NT_GFX_RECORD(NT_GFX_EVENT_BACKEND, NT_GFX_OP_STATE, event.detail = NT_GFX_GL_VIEWPORT; event.data.backend.args[0] = (uint32_t)(0); event.data.backend.args[1] = (uint32_t)(0);
                  event.data.backend.args[2] = (uint32_t)(0); event.data.backend.args[3] = (uint32_t)(0););
    glClearColor(0.0F, 0.0F, 0.0F, 0.0F);
    NT_GFX_RECORD(NT_GFX_EVENT_BACKEND, NT_GFX_OP_STATE, event.detail = NT_GFX_GL_CLEARCOLOR; event.data.backend.values[0] = 0.0F; event.data.backend.values[1] = 0.0F;
                  event.data.backend.values[2] = 0.0F; event.data.backend.values[3] = 0.0F;);
    nt_gl_clear_depth(1.0F);
    NT_GFX_RECORD(NT_GFX_EVENT_BACKEND, NT_GFX_OP_STATE, event.detail = NT_GFX_GL_CLEARDEPTH; event.data.backend.values[0] = 1.0F;);

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

#if NT_GFX_GPU_TIMING_ENABLED
    s_timer_enabled = nt_gfx_gl_ctx_enable_timer_query();
    s_debug_groups_enabled = nt_gfx_gl_ctx_enable_debug_groups();
    nt_gfx_backend_drop_timer_segments();
#endif
    nt_gfx_gl_ctx_enable_debug_callback();
    /* Fresh context (init or restore): the previous upload VAO died with it. */
    glGenVertexArrays(1, &s_ebo_upload_vao);
    NT_GFX_RECORD(NT_GFX_EVENT_BACKEND, NT_GFX_OP_STATE, event.detail = NT_GFX_GL_GENVERTEXARRAYS; event.data.backend.args[0] = (uint32_t)(1);
                  event.data.backend.args[1] = (uint32_t)(*(&s_ebo_upload_vao)););
    /* 0 with a live context would silently break every index-buffer upload on
     * core GL; 0 on an already-lost context is retried by the next restore. */
    NT_ASSERT(s_ebo_upload_vao != 0 || nt_gfx_gl_ctx_is_lost());
}

bool nt_gfx_backend_init(const nt_gfx_desc_t *desc) {
    NT_ASSERT(desc);
    s_init_desc = *desc;
#ifdef NT_PLATFORM_WEB
    g_nt_gfx_observation.backend = NT_GFX_BACKEND_WEBGL;
#else
    g_nt_gfx_observation.backend = NT_GFX_BACKEND_OPENGL;
#endif

    if (!nt_gfx_gl_ctx_create(&s_init_desc)) {
        NT_GFX_RESULT(NT_GFX_OP_CONTEXT, NT_GFX_OBJECT_NONE, 0, NT_GFX_REASON_BACKEND_FAILURE);
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
    NT_GFX_RESULT(NT_GFX_OP_CONTEXT, NT_GFX_OBJECT_NONE, 0, NT_GFX_REASON_ACCEPTED);
    return true;
}

void nt_gfx_backend_shutdown(void) {
#if NT_GFX_GPU_TIMING_ENABLED
    if (s_timer_enabled && !nt_gfx_gl_ctx_is_lost()) {
        nt_gfx_backend_end_segment();
        for (uint8_t i = 0; i < s_segment_count; i++) {
            glDeleteQueries(NT_GFX_TIMER_RING, s_segments[i].queries);
            capture_query_names(NT_GFX_GL_DELETEQUERIES, s_segments[i].queries);
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
        glDeleteVertexArrays(1, &s_ebo_upload_vao);
        NT_GFX_RECORD(NT_GFX_EVENT_BACKEND, NT_GFX_OP_STATE, event.detail = NT_GFX_GL_DELETEVERTEXARRAYS; event.data.backend.args[0] = (uint32_t)(1);
                      event.data.backend.args[1] = (uint32_t)(*(&s_ebo_upload_vao)););
    }
    s_ebo_upload_vao = 0;

    nt_gfx_gl_ctx_destroy();
}

bool nt_gfx_backend_is_context_lost(void) { return nt_gfx_gl_ctx_is_lost(); }

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
    glGenQueries(NT_GFX_TIMER_RING, seg->queries);
    capture_query_names(NT_GFX_GL_GENQUERIES, seg->queries);
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
        glGetQueryObjectuiv(q_old, GL_QUERY_RESULT_AVAILABLE, &avail);
        NT_GFX_RECORD(NT_GFX_EVENT_BACKEND, NT_GFX_OP_STATE, event.detail = NT_GFX_GL_GETQUERYOBJECTUIV; event.data.backend.args[0] = q_old; event.data.backend.args[1] = GL_QUERY_RESULT_AVAILABLE;
                      event.data.backend.args[2] = 1;);
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
        glPushDebugGroup(GL_DEBUG_SOURCE_APPLICATION, name_hash.value, -1, name);
        NT_GFX_RECORD(NT_GFX_EVENT_BACKEND, NT_GFX_OP_STATE, event.detail = NT_GFX_GL_PUSHDEBUGGROUP; event.data.backend.args[0] = GL_DEBUG_SOURCE_APPLICATION;
                      event.data.backend.args[1] = name_hash.value; event.data.backend.args[2] = (uint32_t)-1; event.data.backend.args[3] = 1;);
    }
#endif
    glBeginQuery(GL_TIME_ELAPSED, seg->queries[seg->head]);
    NT_GFX_RECORD(NT_GFX_EVENT_BACKEND, NT_GFX_OP_STATE, event.detail = NT_GFX_GL_BEGINQUERY; event.data.backend.args[0] = GL_TIME_ELAPSED; event.data.backend.args[1] = seg->queries[seg->head];);
    s_active_segment = idx;
}

void nt_gfx_backend_end_segment(void) {
    if (s_active_segment < 0) {
        return;
    }
    nt_gfx_segment_state_t *seg = &s_segments[s_active_segment];
    glEndQuery(GL_TIME_ELAPSED);
    NT_GFX_RECORD(NT_GFX_EVENT_BACKEND, NT_GFX_OP_STATE, event.detail = NT_GFX_GL_ENDQUERY; event.data.backend.args[0] = GL_TIME_ELAPSED;);
#ifndef NT_PLATFORM_WEB
    if (s_debug_groups_enabled) {
        glPopDebugGroup();
        NT_GFX_RECORD(NT_GFX_EVENT_BACKEND, NT_GFX_OP_STATE, event.detail = NT_GFX_GL_POPDEBUGGROUP;);
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
            glGetIntegerv(GL_GPU_DISJOINT_EXT, &disjoint);
            NT_GFX_RECORD(NT_GFX_EVENT_BACKEND, NT_GFX_OP_STATE, event.detail = NT_GFX_GL_GETINTEGERV; event.data.backend.args[0] = GL_GPU_DISJOINT_EXT; event.data.backend.args[1] = 1;);
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
    glGetQueryObjectuiv(q, GL_QUERY_RESULT_AVAILABLE, &available);
    NT_GFX_RECORD(NT_GFX_EVENT_BACKEND, NT_GFX_OP_STATE, event.detail = NT_GFX_GL_GETQUERYOBJECTUIV; event.data.backend.args[0] = q; event.data.backend.args[1] = GL_QUERY_RESULT_AVAILABLE;
                  event.data.backend.args[2] = 1;);
    if (!available) {
        return false;
    }

#ifdef NT_PLATFORM_WEB
    *out_ns = (uint64_t)nt_gfx_gl_ctx_query_result(q);
#else
    GLuint64 result = 0;
    glGetQueryObjectui64v(q, GL_QUERY_RESULT, &result);
    *out_ns = (uint64_t)result;
#endif
    /* The web bridge issues the same query-result read. */
    NT_GFX_RECORD(NT_GFX_EVENT_BACKEND, NT_GFX_OP_STATE, event.detail = NT_GFX_GL_GETQUERYOBJECTUI64V; event.data.backend.args[0] = q; event.data.backend.args[1] = GL_QUERY_RESULT;
                  event.data.backend.args[2] = 1;);
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
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        NT_GFX_RECORD(NT_GFX_EVENT_BACKEND, NT_GFX_OP_STATE, event.detail = NT_GFX_GL_BINDFRAMEBUFFER; event.data.backend.args[0] = (uint32_t)(GL_FRAMEBUFFER);
                      event.data.backend.args[1] = (uint32_t)(fbo););
        s_bound_framebuffer = fbo;
    }
    gl_set_viewport(0, 0, (int)viewport_w, (int)viewport_h);
    if (!float4_equal(s_gl_cache.clear_color, desc->clear_color)) {
        memcpy(s_gl_cache.clear_color, desc->clear_color, sizeof(s_gl_cache.clear_color));
        glClearColor(desc->clear_color[0], desc->clear_color[1], desc->clear_color[2], desc->clear_color[3]);
        NT_GFX_RECORD(NT_GFX_EVENT_BACKEND, NT_GFX_OP_STATE, event.detail = NT_GFX_GL_CLEARCOLOR; event.data.backend.values[0] = desc->clear_color[0];
                      event.data.backend.values[1] = desc->clear_color[1]; event.data.backend.values[2] = desc->clear_color[2]; event.data.backend.values[3] = desc->clear_color[3];);
    }
    if (s_gl_cache.clear_depth != desc->clear_depth) {
        s_gl_cache.clear_depth = desc->clear_depth;
        nt_gl_clear_depth(desc->clear_depth);
        NT_GFX_RECORD(NT_GFX_EVENT_BACKEND, NT_GFX_OP_STATE, event.detail = NT_GFX_GL_CLEARDEPTH; event.data.backend.values[0] = desc->clear_depth;);
    }
    /* The clear must not inherit the previous pipeline's depth-write mask, and
     * leaves it on: the pass's first pipeline bind re-applies its own. */
    if (!s_gl_cache.depth_write_enabled) {
        glDepthMask(GL_TRUE);
        NT_GFX_RECORD(NT_GFX_EVENT_BACKEND, NT_GFX_OP_STATE, event.detail = NT_GFX_GL_DEPTHMASK; event.data.backend.args[0] = (uint32_t)(GL_TRUE););
        s_gl_cache.depth_write_enabled = true;
    }
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    NT_GFX_RECORD(NT_GFX_EVENT_BACKEND, NT_GFX_OP_STATE, event.detail = NT_GFX_GL_CLEAR; event.data.backend.args[0] = (uint32_t)(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT););
}

void nt_gfx_backend_end_pass(void) {
    if (s_bound_framebuffer != 0) {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        NT_GFX_RECORD(NT_GFX_EVENT_BACKEND, NT_GFX_OP_STATE, event.detail = NT_GFX_GL_BINDFRAMEBUFFER; event.data.backend.args[0] = (uint32_t)(GL_FRAMEBUFFER);
                      event.data.backend.args[1] = (uint32_t)(0););
        s_bound_framebuffer = 0;
    }
}

/* ---- Scissor and viewport ----
 *
 * Raw GL bottom-left convention. Callers are expected to y-flip if they
 * think in top-left space. */

/* Uncached: the UI emits a distinct rect per clipped element, so a diff never hits. */
void nt_gfx_backend_set_scissor(int x, int y, int w, int h) {
    glScissor(x, y, (GLsizei)w, (GLsizei)h);
    NT_GFX_RECORD(NT_GFX_EVENT_BACKEND, NT_GFX_OP_STATE, event.detail = NT_GFX_GL_SCISSOR; event.data.backend.args[0] = (uint32_t)(x); event.data.backend.args[1] = (uint32_t)(y);
                  event.data.backend.args[2] = (uint32_t)((GLsizei)w); event.data.backend.args[3] = (uint32_t)((GLsizei)h););
}

void nt_gfx_backend_set_scissor_enabled(bool enabled) {
    if (enabled) {
        glEnable(GL_SCISSOR_TEST);
        NT_GFX_RECORD(NT_GFX_EVENT_BACKEND, NT_GFX_OP_STATE, event.detail = NT_GFX_GL_ENABLE; event.data.backend.args[0] = (uint32_t)(GL_SCISSOR_TEST););
    } else {
        glDisable(GL_SCISSOR_TEST);
        NT_GFX_RECORD(NT_GFX_EVENT_BACKEND, NT_GFX_OP_STATE, event.detail = NT_GFX_GL_DISABLE; event.data.backend.args[0] = (uint32_t)(GL_SCISSOR_TEST););
    }
}

void nt_gfx_backend_set_viewport(int x, int y, int w, int h) { gl_set_viewport(x, y, w, h); }

/* Raw GL readback, bottom-left origin. Y-flip to top-left is done once in
 * the shared layer (nt_gfx_read_pixels). rgba8 rows are 4*w bytes -> already
 * 4-aligned; set GL_PACK_ALIGNMENT=4 explicitly so it never depends on state. */
bool nt_gfx_backend_read_pixels(int x, int y, int w, int h, void *out_rgba8) {
    glPixelStorei(GL_PACK_ALIGNMENT, 4);
    NT_GFX_RECORD(NT_GFX_EVENT_BACKEND, NT_GFX_OP_STATE, event.detail = NT_GFX_GL_PIXELSTOREI; event.data.backend.args[0] = (uint32_t)(GL_PACK_ALIGNMENT); event.data.backend.args[1] = (uint32_t)(4););
    /* Drain any stale GL error so the post-read check is attributable to THIS readback. */
    while (glGetError() != GL_NO_ERROR) {
    }
    glReadPixels(x, y, (GLsizei)w, (GLsizei)h, GL_RGBA, GL_UNSIGNED_BYTE, out_rgba8);
    NT_GFX_RECORD(NT_GFX_EVENT_BACKEND, NT_GFX_OP_STATE, event.detail = NT_GFX_GL_READPIXELS; event.data.backend.args[0] = (uint32_t)x; event.data.backend.args[1] = (uint32_t)y;
                  event.data.backend.args[2] = (uint32_t)w; event.data.backend.args[3] = (uint32_t)h; event.data.backend.args[4] = GL_RGBA; event.data.backend.args[5] = GL_UNSIGNED_BYTE;
                  event.data.backend.args[6] = out_rgba8 != NULL ? 1U : 0U;);
    /* A failed read (incomplete FB, invalid read buffer, no current context) leaves out_rgba8
       partly/wholly untouched — report it so the dev-only capture path yields capture_failed, not garbage. */
    return glGetError() == GL_NO_ERROR;
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
        glUseProgram(program);
        NT_GFX_RECORD(NT_GFX_EVENT_BACKEND, NT_GFX_OP_STATE, event.detail = NT_GFX_GL_USEPROGRAM; event.data.backend.args[0] = (uint32_t)(program););
        NT_GFX_COUNT(program_calls);
        s_gl_cache.program = program;
    } else {
        NT_GFX_RECORD(NT_GFX_EVENT_SKIP, NT_GFX_OP_PIPELINE, event.reason = NT_GFX_REASON_CACHE; event.detail = NT_GFX_GL_USEPROGRAM; event.data.backend.args[0] = program;);
    }

    /* Depth test */
    if (s_gl_cache.depth_test_enabled != pip->depth_test_enabled) {
        if (pip->depth_test_enabled) {
            glEnable(GL_DEPTH_TEST);
            NT_GFX_RECORD(NT_GFX_EVENT_BACKEND, NT_GFX_OP_STATE, event.detail = NT_GFX_GL_ENABLE; event.data.backend.args[0] = (uint32_t)(GL_DEPTH_TEST););
        } else {
            glDisable(GL_DEPTH_TEST);
            NT_GFX_RECORD(NT_GFX_EVENT_BACKEND, NT_GFX_OP_STATE, event.detail = NT_GFX_GL_DISABLE; event.data.backend.args[0] = (uint32_t)(GL_DEPTH_TEST););
        }
        s_gl_cache.depth_test_enabled = pip->depth_test_enabled;
    }
    if (pip->depth_test_enabled && s_gl_cache.depth_func != pip->depth_func) {
        glDepthFunc(pip->depth_func);
        NT_GFX_RECORD(NT_GFX_EVENT_BACKEND, NT_GFX_OP_STATE, event.detail = NT_GFX_GL_DEPTHFUNC; event.data.backend.args[0] = (uint32_t)(pip->depth_func););
        s_gl_cache.depth_func = pip->depth_func;
    }
    if (s_gl_cache.depth_write_enabled != pip->depth_write_enabled) {
        glDepthMask(pip->depth_write_enabled ? GL_TRUE : GL_FALSE);
        NT_GFX_RECORD(NT_GFX_EVENT_BACKEND, NT_GFX_OP_STATE, event.detail = NT_GFX_GL_DEPTHMASK; event.data.backend.args[0] = (uint32_t)(pip->depth_write_enabled ? GL_TRUE : GL_FALSE););
        s_gl_cache.depth_write_enabled = pip->depth_write_enabled;
    }

    /* Cull mode */
    if (s_gl_cache.cull_mode != pip->cull_mode) {
        if (pip->cull_mode == 0) {
            glDisable(GL_CULL_FACE);
            NT_GFX_RECORD(NT_GFX_EVENT_BACKEND, NT_GFX_OP_STATE, event.detail = NT_GFX_GL_DISABLE; event.data.backend.args[0] = (uint32_t)(GL_CULL_FACE););
        } else {
            glEnable(GL_CULL_FACE);
            NT_GFX_RECORD(NT_GFX_EVENT_BACKEND, NT_GFX_OP_STATE, event.detail = NT_GFX_GL_ENABLE; event.data.backend.args[0] = (uint32_t)(GL_CULL_FACE););
            glCullFace(pip->cull_mode == 2 ? GL_FRONT : GL_BACK);
            NT_GFX_RECORD(NT_GFX_EVENT_BACKEND, NT_GFX_OP_STATE, event.detail = NT_GFX_GL_CULLFACE; event.data.backend.args[0] = (uint32_t)(pip->cull_mode == 2 ? GL_FRONT : GL_BACK););
        }
        s_gl_cache.cull_mode = pip->cull_mode;
    }

    /* Blend */
    if (s_gl_cache.blend_enabled != pip->blend_enabled) {
        if (pip->blend_enabled) {
            glEnable(GL_BLEND);
            NT_GFX_RECORD(NT_GFX_EVENT_BACKEND, NT_GFX_OP_STATE, event.detail = NT_GFX_GL_ENABLE; event.data.backend.args[0] = (uint32_t)(GL_BLEND););
        } else {
            glDisable(GL_BLEND);
            NT_GFX_RECORD(NT_GFX_EVENT_BACKEND, NT_GFX_OP_STATE, event.detail = NT_GFX_GL_DISABLE; event.data.backend.args[0] = (uint32_t)(GL_BLEND););
        }
        s_gl_cache.blend_enabled = pip->blend_enabled;
    }
    if (pip->blend_enabled && (s_gl_cache.blend_src_rgb != pip->blend_src_rgb || s_gl_cache.blend_dst_rgb != pip->blend_dst_rgb || s_gl_cache.blend_src_alpha != pip->blend_src_alpha ||
                               s_gl_cache.blend_dst_alpha != pip->blend_dst_alpha)) {
        glBlendFuncSeparate(pip->blend_src_rgb, pip->blend_dst_rgb, pip->blend_src_alpha, pip->blend_dst_alpha);
        NT_GFX_RECORD(NT_GFX_EVENT_BACKEND, NT_GFX_OP_STATE, event.detail = NT_GFX_GL_BLENDFUNCSEPARATE; event.data.backend.args[0] = (uint32_t)(pip->blend_src_rgb);
                      event.data.backend.args[1] = (uint32_t)(pip->blend_dst_rgb); event.data.backend.args[2] = (uint32_t)(pip->blend_src_alpha);
                      event.data.backend.args[3] = (uint32_t)(pip->blend_dst_alpha););
        s_gl_cache.blend_src_rgb = pip->blend_src_rgb;
        s_gl_cache.blend_dst_rgb = pip->blend_dst_rgb;
        s_gl_cache.blend_src_alpha = pip->blend_src_alpha;
        s_gl_cache.blend_dst_alpha = pip->blend_dst_alpha;
    }
    if (pip->blend_enabled && (s_gl_cache.blend_op_rgb != pip->blend_op_rgb || s_gl_cache.blend_op_alpha != pip->blend_op_alpha)) {
        glBlendEquationSeparate(pip->blend_op_rgb, pip->blend_op_alpha);
        NT_GFX_RECORD(NT_GFX_EVENT_BACKEND, NT_GFX_OP_STATE, event.detail = NT_GFX_GL_BLENDEQUATIONSEPARATE; event.data.backend.args[0] = (uint32_t)(pip->blend_op_rgb);
                      event.data.backend.args[1] = (uint32_t)(pip->blend_op_alpha););
        s_gl_cache.blend_op_rgb = pip->blend_op_rgb;
        s_gl_cache.blend_op_alpha = pip->blend_op_alpha;
    }
    if (pip->blend_enabled && !float4_equal(s_gl_cache.blend_constant_color, pip->blend_constant_color)) {
        glBlendColor(pip->blend_constant_color[0], pip->blend_constant_color[1], pip->blend_constant_color[2], pip->blend_constant_color[3]);
        NT_GFX_RECORD(NT_GFX_EVENT_BACKEND, NT_GFX_OP_STATE, event.detail = NT_GFX_GL_BLENDCOLOR; event.data.backend.values[0] = pip->blend_constant_color[0];
                      event.data.backend.values[1] = pip->blend_constant_color[1]; event.data.backend.values[2] = pip->blend_constant_color[2];
                      event.data.backend.values[3] = pip->blend_constant_color[3];);
        memcpy(s_gl_cache.blend_constant_color, pip->blend_constant_color, sizeof(pip->blend_constant_color));
    }

    /* Polygon offset */
    if (s_gl_cache.polygon_offset_enabled != pip->polygon_offset_enabled) {
        if (pip->polygon_offset_enabled) {
            glEnable(GL_POLYGON_OFFSET_FILL);
            NT_GFX_RECORD(NT_GFX_EVENT_BACKEND, NT_GFX_OP_STATE, event.detail = NT_GFX_GL_ENABLE; event.data.backend.args[0] = (uint32_t)(GL_POLYGON_OFFSET_FILL););
        } else {
            glDisable(GL_POLYGON_OFFSET_FILL);
            NT_GFX_RECORD(NT_GFX_EVENT_BACKEND, NT_GFX_OP_STATE, event.detail = NT_GFX_GL_DISABLE; event.data.backend.args[0] = (uint32_t)(GL_POLYGON_OFFSET_FILL););
        }
        s_gl_cache.polygon_offset_enabled = pip->polygon_offset_enabled;
    }
    if (pip->polygon_offset_enabled && (s_gl_cache.polygon_offset_factor != pip->polygon_offset_factor || s_gl_cache.polygon_offset_units != pip->polygon_offset_units)) {
        glPolygonOffset(pip->polygon_offset_factor, pip->polygon_offset_units);
        NT_GFX_RECORD(NT_GFX_EVENT_BACKEND, NT_GFX_OP_STATE, event.detail = NT_GFX_GL_POLYGONOFFSET; event.data.backend.values[0] = pip->polygon_offset_factor;
                      event.data.backend.values[1] = pip->polygon_offset_units;);
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
    glUniformMatrix4fv(s_programs[program_backend].uniforms[index].location, 1, GL_FALSE, matrix);
    NT_GFX_RECORD(NT_GFX_EVENT_BACKEND, NT_GFX_OP_UNIFORM_MAT4, event.detail = NT_GFX_GL_UNIFORMMATRIX4FV; event.data.uniform.name = (uint32_t)(s_programs[program_backend].uniforms[index].location);
                  event.data.uniform.count = 16; memcpy(event.data.uniform.values, matrix, 16 * sizeof(float)););
    NT_GFX_COUNT(uniform_calls);
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
    glUniform4fv(prog->uniforms[index].location, 1, vec);
    NT_GFX_RECORD(NT_GFX_EVENT_BACKEND, NT_GFX_OP_UNIFORM_VEC4, event.detail = NT_GFX_GL_UNIFORM4FV; event.data.uniform.name = (uint32_t)(prog->uniforms[index].location); event.data.uniform.count = 4;
                  memcpy(event.data.uniform.values, vec, 4 * sizeof(float)););
    NT_GFX_COUNT(uniform_calls);
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
    glUniform1f(s_programs[program_backend].uniforms[index].location, val);
    NT_GFX_RECORD(NT_GFX_EVENT_BACKEND, NT_GFX_OP_UNIFORM_FLOAT, event.detail = NT_GFX_GL_UNIFORM1F; event.data.backend.args[0] = (uint32_t)(s_programs[program_backend].uniforms[index].location);
                  event.data.backend.values[0] = val;);
    NT_GFX_COUNT(uniform_calls);
}

void nt_gfx_backend_set_uniform_int(uint32_t program_backend, uint32_t name_hash, int val) {
    int index = program_get_uniform_index(program_backend, name_hash);
    nt_gfx_sampler_info_t sampler_info = {0};
    const bool is_sampler = nt_gfx_backend_program_sampler_info(program_backend, name_hash, &sampler_info);
    NT_ASSERT(!is_sampler && "sampler uniforms are immutable; use nt_gfx_apply_texture_bindings");
    if (index < 0) {
        NT_GFX_RECORD(NT_GFX_EVENT_SKIP, NT_GFX_OP_UNIFORM_INT, event.reason = NT_GFX_REASON_INACTIVE; event.data.binding.name = name_hash; event.data.binding.secondary = program_backend;);
        return;
    }
    glUniform1i(s_programs[program_backend].uniforms[index].location, val);
    NT_GFX_RECORD(NT_GFX_EVENT_BACKEND, NT_GFX_OP_UNIFORM_INT, event.detail = NT_GFX_GL_UNIFORM1I; event.data.backend.args[0] = (uint32_t)(s_programs[program_backend].uniforms[index].location);
                  event.data.backend.args[1] = (uint32_t)(val););
    NT_GFX_COUNT(uniform_calls);
}

/* ---- Draw calls ---- */

void nt_gfx_backend_draw(uint32_t first_vertex, uint32_t num_vertices) {
    glDrawArrays(GL_TRIANGLES, (GLint)first_vertex, (GLsizei)num_vertices);
    NT_GFX_RECORD(NT_GFX_EVENT_BACKEND, NT_GFX_OP_DRAW, event.detail = NT_GFX_GL_DRAWARRAYS; event.data.backend.args[0] = (uint32_t)(GL_TRIANGLES);
                  event.data.backend.args[1] = (uint32_t)((GLint)first_vertex); event.data.backend.args[2] = (uint32_t)((GLsizei)num_vertices););
}

void nt_gfx_backend_draw_indexed(uint32_t first_index, uint32_t num_indices, uint8_t index_type) {
    GLenum gl_type = (index_type == 2) ? GL_UNSIGNED_INT : GL_UNSIGNED_SHORT;
    uint32_t stride = (index_type == 2) ? sizeof(uint32_t) : sizeof(uint16_t);
    glDrawElements(GL_TRIANGLES, (GLsizei)num_indices, gl_type,
                   (void *)(uintptr_t)(first_index * stride)); // NOLINT(performance-no-int-to-ptr)
    NT_GFX_RECORD(NT_GFX_EVENT_BACKEND, NT_GFX_OP_DRAW, event.detail = NT_GFX_GL_DRAWELEMENTS; event.data.backend.args[0] = (uint32_t)(GL_TRIANGLES);
                  event.data.backend.args[1] = (uint32_t)((GLsizei)num_indices); event.data.backend.args[2] = (uint32_t)(gl_type); event.data.backend.args[3] = (uint32_t)((first_index * stride)););
}

/* ---- Resource management (shader / buffer / pipeline) ---- */

uint32_t nt_gfx_backend_create_shader(const nt_shader_desc_t *desc) {
    GLenum gl_type = (desc->type == NT_SHADER_VERTEX) ? GL_VERTEX_SHADER : GL_FRAGMENT_SHADER;
    GLuint shader = glCreateShader(gl_type);
    NT_GFX_RECORD(NT_GFX_EVENT_BACKEND, NT_GFX_OP_CREATE, event.detail = NT_GFX_GL_CREATESHADER; event.data.backend.args[0] = shader; event.data.backend.args[1] = (uint32_t)(gl_type););

#ifdef NT_PLATFORM_WEB
    const char *prefix = "#version 300 es\n";
#else
    const char *prefix = "#version 330 core\n";
#endif
    const char *sources[2] = {prefix, desc->source};
    glShaderSource(shader, 2, sources, NULL);
    NT_GFX_RECORD(NT_GFX_EVENT_BACKEND, NT_GFX_OP_STATE, event.detail = NT_GFX_GL_SHADERSOURCE; event.data.backend.args[0] = (uint32_t)(shader); event.data.backend.args[1] = (uint32_t)(2););

    glCompileShader(shader);
    NT_GFX_RECORD(NT_GFX_EVENT_BACKEND, NT_GFX_OP_STATE, event.detail = NT_GFX_GL_COMPILESHADER; event.data.backend.args[0] = (uint32_t)(shader););
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
    glGetShaderiv((GLuint)shader, GL_INFO_LOG_LENGTH, &len);
    if (len <= 1) {
        return;
    }
    char *log = (char *)malloc((size_t)len);
    if (!log) {
        return;
    }
    glGetShaderInfoLog((GLuint)shader, len, NULL, log);
    NT_LOG_ERROR("%s shader:", stage);
    nt_gfx_gl_log_lines(log);
    free(log);
}

static void nt_gfx_gl_log_program(uint32_t program) {
    GLint len = 0;
    glGetProgramiv((GLuint)program, GL_INFO_LOG_LENGTH, &len);
    if (len <= 1) {
        return;
    }
    char *log = (char *)malloc((size_t)len);
    if (!log) {
        return;
    }
    glGetProgramInfoLog((GLuint)program, len, NULL, log);
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
    glDeleteShader((GLuint)backend_handle);
    NT_GFX_RECORD(NT_GFX_EVENT_BACKEND, NT_GFX_OP_STATE, event.detail = NT_GFX_GL_DELETESHADER; event.data.backend.args[0] = (uint32_t)((GLuint)backend_handle););
}

/* Links a stage pair and binds every registered global UBO block. Returns 0 on
 * link failure, after logging both stages and the program log. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity) -- diagnostic record and assert macros expand at owning sites
static GLuint nt_gfx_gl_link_program(uint32_t vs_backend, uint32_t fs_backend) {
    GLuint program = glCreateProgram();
    NT_GFX_RECORD(NT_GFX_EVENT_BACKEND, NT_GFX_OP_CREATE, event.detail = NT_GFX_GL_CREATEPROGRAM; event.data.backend.args[0] = program;);
    glAttachShader(program, (GLuint)vs_backend);
    NT_GFX_RECORD(NT_GFX_EVENT_BACKEND, NT_GFX_OP_STATE, event.detail = NT_GFX_GL_ATTACHSHADER; event.data.backend.args[0] = (uint32_t)(program);
                  event.data.backend.args[1] = (uint32_t)((GLuint)vs_backend););
    glAttachShader(program, (GLuint)fs_backend);
    NT_GFX_RECORD(NT_GFX_EVENT_BACKEND, NT_GFX_OP_STATE, event.detail = NT_GFX_GL_ATTACHSHADER; event.data.backend.args[0] = (uint32_t)(program);
                  event.data.backend.args[1] = (uint32_t)((GLuint)fs_backend););
    glLinkProgram(program);
    NT_GFX_RECORD(NT_GFX_EVENT_BACKEND, NT_GFX_OP_STATE, event.detail = NT_GFX_GL_LINKPROGRAM; event.data.backend.args[0] = (uint32_t)(program););

    GLint linked = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    if (!linked) {
        nt_gfx_gl_log_shader(vs_backend, "vertex");
        nt_gfx_gl_log_shader(fs_backend, "fragment");
        nt_gfx_gl_log_program(program);

        glDeleteProgram(program);
        NT_GFX_RECORD(NT_GFX_EVENT_BACKEND, NT_GFX_OP_STATE, event.detail = NT_GFX_GL_DELETEPROGRAM; event.data.backend.args[0] = (uint32_t)(program););
        return 0;
    }

    const nt_global_block_t *blocks;
    uint32_t block_count;
    nt_gfx_get_global_blocks(&blocks, &block_count);
    for (uint32_t bi = 0; bi < block_count; bi++) {
        GLuint block_index = glGetUniformBlockIndex(program, blocks[bi].name);
        if (block_index != GL_INVALID_INDEX) {
            NT_GFX_COUNT(uniform_calls);
            glUniformBlockBinding(program, block_index, (GLuint)blocks[bi].binding_slot);
            NT_GFX_RECORD(NT_GFX_EVENT_BACKEND, NT_GFX_OP_UNIFORM_BLOCK, event.detail = NT_GFX_GL_UNIFORMBLOCKBINDING; event.data.backend.args[0] = (uint32_t)(program);
                          event.data.backend.args[1] = (uint32_t)(block_index); event.data.backend.args[2] = (uint32_t)((GLuint)blocks[bi].binding_slot););
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
    glGetProgramiv(program, GL_ACTIVE_UNIFORMS, &active_uniforms);
    if (active_uniforms < 0) {
        return false;
    }
    if (active_uniforms == 0) {
        return true;
    }
    GLint max_name_length = 0;
    glGetProgramiv(program, GL_ACTIVE_UNIFORM_MAX_LENGTH, &max_name_length);
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
        glGetActiveUniform(program, (GLuint)ui, max_name_length, &ulen, &usize, &utype, uname);
        if (ulen <= 0 || usize <= 0) {
            return false;
        }
        NT_ASSERT(usize == 1 || (ulen >= 3 && strcmp(uname + ulen - 3, "[0]") == 0));
        for (GLint element = 0; element < usize; element++) {
            if (element > 0) {
                size_t suffix = (size_t)ulen - 3U;
                nt_gfx_gl_write_array_index(uname + suffix, element);
            }
            GLint loc = glGetUniformLocation(program, uname);
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
    glUseProgram(program);
    NT_GFX_RECORD(NT_GFX_EVENT_BACKEND, NT_GFX_OP_STATE, event.detail = NT_GFX_GL_USEPROGRAM; event.data.backend.args[0] = (uint32_t)(program););
    NT_GFX_COUNT(program_calls);
    for (uint8_t i = 0; i < rec->sampler_count; i++) {
        glUniform1i(rec->sampler_units[i].location, (GLint)i);
        NT_GFX_RECORD(NT_GFX_EVENT_BACKEND, NT_GFX_OP_UNIFORM_INT, event.detail = NT_GFX_GL_UNIFORM1I; event.data.backend.args[0] = (uint32_t)(rec->sampler_units[i].location);
                      event.data.backend.args[1] = (uint32_t)((GLint)i););
        NT_GFX_COUNT(uniform_calls);
    }
    glUseProgram(saved);
    NT_GFX_RECORD(NT_GFX_EVENT_BACKEND, NT_GFX_OP_STATE, event.detail = NT_GFX_GL_USEPROGRAM; event.data.backend.args[0] = (uint32_t)(saved););
    NT_GFX_COUNT(program_calls);
}

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
        glDeleteProgram(program);
        NT_GFX_RECORD(NT_GFX_EVENT_BACKEND, NT_GFX_OP_STATE, event.detail = NT_GFX_GL_DELETEPROGRAM; event.data.backend.args[0] = (uint32_t)(program););
        return 0; /* no free slots */
    }

    if (!nt_gfx_gl_cache_uniforms(program, &s_programs[slot]) || nt_gfx_backend_is_context_lost()) {
        glDeleteProgram(program);
        NT_GFX_RECORD(NT_GFX_EVENT_BACKEND, NT_GFX_OP_STATE, event.detail = NT_GFX_GL_DELETEPROGRAM; event.data.backend.args[0] = (uint32_t)(program););
        return 0;
    }
    write_sampler_units(program, &s_programs[slot]);
    s_programs[slot].program = program;
#if NT_GFX_CAPTURE_ENABLED
    if (g_nt_gfx_capture.recording && !g_nt_gfx_capture.view.overflow) {
        capture_program_definition(slot);
    }
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
        glUseProgram(0);
        NT_GFX_RECORD(NT_GFX_EVENT_BACKEND, NT_GFX_OP_STATE, event.detail = NT_GFX_GL_USEPROGRAM; event.data.backend.args[0] = (uint32_t)(0););
        NT_GFX_COUNT(program_calls);
        s_gl_cache.program = 0;
    }
    glDeleteProgram(program);
    NT_GFX_RECORD(NT_GFX_EVENT_BACKEND, NT_GFX_OP_STATE, event.detail = NT_GFX_GL_DELETEPROGRAM; event.data.backend.args[0] = (uint32_t)(program););
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
    glGenVertexArrays(1, &vao);
    NT_GFX_RECORD(NT_GFX_EVENT_BACKEND, NT_GFX_OP_STATE, event.detail = NT_GFX_GL_GENVERTEXARRAYS; event.data.backend.args[0] = (uint32_t)(1); event.data.backend.args[1] = (uint32_t)(*(&vao)););
    if (vao == 0) {
        return 0;
    }
    gl_bind_vao(vao);
    if (vbo_backend != 0 && vbo_backend <= s_init_desc.max_buffers) {
        /* Buffer bound before the pointer calls -- satisfies the WebGL "no
         * pointer without a bound ARRAY_BUFFER" rule at creation time. */
        glBindBuffer(GL_ARRAY_BUFFER, s_buffer_gl[vbo_backend]);
        NT_GFX_RECORD(NT_GFX_EVENT_BACKEND, NT_GFX_OP_STATE, event.detail = NT_GFX_GL_BINDBUFFER; event.data.backend.args[0] = (uint32_t)(GL_ARRAY_BUFFER);
                      event.data.backend.args[1] = (uint32_t)(s_buffer_gl[vbo_backend]););
        for (uint8_t i = 0; i < desc->layout.attr_count; i++) {
            const nt_vertex_attr_t *attr = &desc->layout.attrs[i];
            glEnableVertexAttribArray(attr->location);
            NT_GFX_RECORD(NT_GFX_EVENT_BACKEND, NT_GFX_OP_STATE, event.detail = NT_GFX_GL_ENABLEVERTEXATTRIBARRAY; event.data.backend.args[0] = (uint32_t)(attr->location););
            glVertexAttribPointer(attr->location, attr->count, map_vertex_type(attr->type), attr->normalized ? GL_TRUE : GL_FALSE, (GLsizei)desc->layout.stride,
                                  (void *)(uintptr_t)attr->offset); // NOLINT(performance-no-int-to-ptr)
            NT_GFX_RECORD(NT_GFX_EVENT_BACKEND, NT_GFX_OP_STATE, event.detail = NT_GFX_GL_VERTEXATTRIBPOINTER; event.data.backend.args[0] = (uint32_t)(attr->location);
                          event.data.backend.args[1] = (uint32_t)(attr->count); event.data.backend.args[2] = (uint32_t)(map_vertex_type(attr->type));
                          event.data.backend.args[3] = (uint32_t)(attr->normalized ? GL_TRUE : GL_FALSE); event.data.backend.args[4] = (uint32_t)((GLsizei)desc->layout.stride);
                          event.data.backend.args[5] = (uint32_t)(attr->offset););
            NT_GFX_COUNT(static_attribute_calls);
#ifdef NT_TEST_ACCESS
            s_test_static_attrib_pointer_calls++;
#endif
        }
    }
    if (ibo_backend != 0 && ibo_backend <= s_init_desc.max_buffers) {
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, s_buffer_gl[ibo_backend]); /* captured by the VAO */
        NT_GFX_RECORD(NT_GFX_EVENT_BACKEND, NT_GFX_OP_STATE, event.detail = NT_GFX_GL_BINDBUFFER; event.data.backend.args[0] = (uint32_t)(GL_ELEMENT_ARRAY_BUFFER);
                      event.data.backend.args[1] = (uint32_t)(s_buffer_gl[ibo_backend]););
    }
    for (uint8_t i = 0; i < desc->instance_layout.attr_count; i++) {
        GLuint loc = desc->instance_layout.attrs[i].location;
        glEnableVertexAttribArray(loc);
        NT_GFX_RECORD(NT_GFX_EVENT_BACKEND, NT_GFX_OP_STATE, event.detail = NT_GFX_GL_ENABLEVERTEXATTRIBARRAY; event.data.backend.args[0] = (uint32_t)(loc););
        glVertexAttribDivisor(loc, 1); /* pointers deferred to bind_instance_buffer */
        NT_GFX_RECORD(NT_GFX_EVENT_BACKEND, NT_GFX_OP_STATE, event.detail = NT_GFX_GL_VERTEXATTRIBDIVISOR; event.data.backend.args[0] = (uint32_t)(loc); event.data.backend.args[1] = (uint32_t)(1););
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
        glDeleteVertexArrays(1, &vi->vao);
        NT_GFX_RECORD(NT_GFX_EVENT_BACKEND, NT_GFX_OP_STATE, event.detail = NT_GFX_GL_DELETEVERTEXARRAYS; event.data.backend.args[0] = (uint32_t)(1);
                      event.data.backend.args[1] = (uint32_t)(*(&vi->vao)););
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
        NT_GFX_RECORD(NT_GFX_EVENT_SKIP, NT_GFX_OP_VERTEX_INPUT, event.reason = NT_GFX_REASON_CACHE; event.detail = NT_GFX_GL_BINDVERTEXARRAY; event.data.backend.args[0] = vao;);
    }
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity) -- diagnostic record and assert macros expand at owning sites
uint32_t nt_gfx_backend_create_buffer(const nt_buffer_desc_t *desc) {
    GLuint buf;
    glGenBuffers(1, &buf);
    NT_GFX_RECORD(NT_GFX_EVENT_BACKEND, NT_GFX_OP_STATE, event.detail = NT_GFX_GL_GENBUFFERS; event.data.backend.args[0] = (uint32_t)(1); event.data.backend.args[1] = (uint32_t)(*(&buf)););
    if (buf == 0) {
        return 0; /* lost context: storing name 0 would alias the free-slot sentinel */
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
    glBindBuffer(target, buf);
    NT_GFX_RECORD(NT_GFX_EVENT_BACKEND, NT_GFX_OP_STATE, event.detail = NT_GFX_GL_BINDBUFFER; event.data.backend.args[0] = (uint32_t)(target); event.data.backend.args[1] = (uint32_t)(buf););
    glBufferData(target, (GLsizeiptr)desc->size, desc->data, usage);
    NT_GFX_RECORD(NT_GFX_EVENT_BACKEND, NT_GFX_OP_BUFFER_UPLOAD, event.detail = NT_GFX_GL_BUFFERDATA; event.data.backend.args[0] = (uint32_t)(target);
                  event.data.backend.args[1] = (uint32_t)((GLsizeiptr)desc->size); event.data.backend.args[2] = (uint32_t)((desc->data != NULL)); event.data.backend.args[3] = (uint32_t)(usage);
                  event.data.backend.bytes = desc->data != NULL ? desc->size : 0;);
    NT_GFX_COUNT_UPLOAD(false, desc->data, desc->size);
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
        glDeleteBuffers(1, &buf);
        NT_GFX_RECORD(NT_GFX_EVENT_BACKEND, NT_GFX_OP_STATE, event.detail = NT_GFX_GL_DELETEBUFFERS; event.data.backend.args[0] = (uint32_t)(1); event.data.backend.args[1] = (uint32_t)(*(&buf)););
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
        glDeleteBuffers(1, &buf);
        NT_GFX_RECORD(NT_GFX_EVENT_BACKEND, NT_GFX_OP_STATE, event.detail = NT_GFX_GL_DELETEBUFFERS; event.data.backend.args[0] = (uint32_t)(1); event.data.backend.args[1] = (uint32_t)(*(&buf)););
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
    glBindBuffer(target, buf);
    NT_GFX_RECORD(NT_GFX_EVENT_BACKEND, NT_GFX_OP_STATE, event.detail = NT_GFX_GL_BINDBUFFER; event.data.backend.args[0] = (uint32_t)(target); event.data.backend.args[1] = (uint32_t)(buf););
    glBufferSubData(target, (GLintptr)offset, (GLsizeiptr)size, data);
    NT_GFX_RECORD(NT_GFX_EVENT_BACKEND, NT_GFX_OP_BUFFER_UPLOAD, event.detail = NT_GFX_GL_BUFFERSUBDATA; event.data.backend.args[0] = (uint32_t)(target);
                  event.data.backend.args[1] = (uint32_t)((GLintptr)offset); event.data.backend.args[2] = (uint32_t)((GLsizeiptr)size); event.data.backend.args[3] = (uint32_t)((data != NULL));
                  event.data.backend.bytes = data != NULL ? size : 0;);
    NT_GFX_COUNT_UPLOAD(false, data, size);
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
    glBindBuffer(target, buf);
    NT_GFX_RECORD(NT_GFX_EVENT_BACKEND, NT_GFX_OP_STATE, event.detail = NT_GFX_GL_BINDBUFFER; event.data.backend.args[0] = (uint32_t)(target); event.data.backend.args[1] = (uint32_t)(buf););
    /* glBufferData with non-NULL data both orphans the existing storage and
     * uploads in one call. The driver may allocate fresh memory for the new
     * contents and reclaim the old block once the GPU finishes consuming it,
     * avoiding the pipeline stall that glBufferSubData can introduce when
     * rewriting a buffer that's still in flight. */
    glBufferData(target, (GLsizeiptr)size, data, GL_DYNAMIC_DRAW);
    NT_GFX_RECORD(NT_GFX_EVENT_BACKEND, NT_GFX_OP_BUFFER_UPLOAD, event.detail = NT_GFX_GL_BUFFERDATA; event.data.backend.args[0] = (uint32_t)(target);
                  event.data.backend.args[1] = (uint32_t)((GLsizeiptr)size); event.data.backend.args[2] = (uint32_t)((data != NULL)); event.data.backend.args[3] = (uint32_t)(GL_DYNAMIC_DRAW);
                  event.data.backend.bytes = data != NULL ? size : 0;);
    NT_GFX_COUNT_UPLOAD(false, data, size);
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
    glBindBuffer(GL_ARRAY_BUFFER, buf);
    NT_GFX_RECORD(NT_GFX_EVENT_BACKEND, NT_GFX_OP_STATE, event.detail = NT_GFX_GL_BINDBUFFER; event.data.backend.args[0] = (uint32_t)(GL_ARRAY_BUFFER); event.data.backend.args[1] = (uint32_t)(buf););

    const nt_gfx_gl_vertex_input_t *vi = &s_vertex_inputs[vertex_input_backend];
    for (uint8_t i = 0; i < vi->instance_attr_count; i++) {
        const nt_vertex_attr_t *attr = &vi->instance_attrs[i];
        glVertexAttribPointer(attr->location, attr->count, map_vertex_type(attr->type), attr->normalized ? GL_TRUE : GL_FALSE, (GLsizei)vi->instance_stride,
                              (void *)(uintptr_t)(attr->offset + byte_offset)); // NOLINT(performance-no-int-to-ptr)
        NT_GFX_RECORD(NT_GFX_EVENT_BACKEND, NT_GFX_OP_STATE, event.detail = NT_GFX_GL_VERTEXATTRIBPOINTER; event.data.backend.args[0] = (uint32_t)(attr->location);
                      event.data.backend.args[1] = (uint32_t)(attr->count); event.data.backend.args[2] = (uint32_t)(map_vertex_type(attr->type));
                      event.data.backend.args[3] = (uint32_t)(attr->normalized ? GL_TRUE : GL_FALSE); event.data.backend.args[4] = (uint32_t)((GLsizei)vi->instance_stride);
                      event.data.backend.args[5] = (uint32_t)((attr->offset + byte_offset)););
        NT_GFX_COUNT(instance_attribute_calls);
#ifdef NT_TEST_ACCESS
        s_test_instance_attrib_pointer_calls++;
#endif
    }
}

void nt_gfx_backend_set_vertex_attrib_default(uint8_t location, float x, float y, float z, float w) {
    glVertexAttrib4f((GLuint)location, x, y, z, w);
    NT_GFX_RECORD(NT_GFX_EVENT_BACKEND, NT_GFX_OP_STATE, event.detail = NT_GFX_GL_VERTEXATTRIB4F; event.data.backend.args[0] = (uint32_t)((GLuint)location); event.data.backend.values[0] = x;
                  event.data.backend.values[1] = y; event.data.backend.values[2] = z; event.data.backend.values[3] = w;);
}

/* ---- Uniform buffer ---- */

void nt_gfx_backend_bind_uniform_buffer(uint32_t backend_handle, uint32_t slot) {
    NT_ASSERT(backend_handle != 0 && backend_handle <= s_init_desc.max_buffers && s_buffer_gl[backend_handle] != 0 && "bind_uniform_buffer: requires a live buffer");
    GLuint buf = s_buffer_gl[backend_handle];
    glBindBufferBase(GL_UNIFORM_BUFFER, slot, buf);
    NT_GFX_RECORD(NT_GFX_EVENT_BACKEND, NT_GFX_OP_STATE, event.detail = NT_GFX_GL_BINDBUFFERBASE; event.data.backend.args[0] = (uint32_t)(GL_UNIFORM_BUFFER);
                  event.data.backend.args[1] = (uint32_t)(slot); event.data.backend.args[2] = (uint32_t)(buf););
    NT_GFX_COUNT(ubo_calls);
}

void nt_gfx_backend_set_uniform_block(uint32_t program_backend, const char *block_name, uint32_t slot) {
    if (program_backend == 0 || program_backend > s_init_desc.max_programs) {
        return;
    }
    GLuint program = s_programs[program_backend].program;
    if (program == 0) {
        return;
    }
    GLuint block_index = glGetUniformBlockIndex(program, block_name);
    if (block_index != GL_INVALID_INDEX) {
        NT_GFX_COUNT(uniform_calls);
        glUniformBlockBinding(program, block_index, slot);
        NT_GFX_RECORD(NT_GFX_EVENT_BACKEND, NT_GFX_OP_UNIFORM_BLOCK, event.detail = NT_GFX_GL_UNIFORMBLOCKBINDING; event.data.backend.args[0] = (uint32_t)(program);
                      event.data.backend.args[1] = (uint32_t)(block_index); event.data.backend.args[2] = (uint32_t)(slot););
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
        glActiveTexture(NT_GFX_GL_UPLOAD_TEXTURE_UNIT);
        NT_GFX_RECORD(NT_GFX_EVENT_BACKEND, NT_GFX_OP_STATE, event.detail = NT_GFX_GL_ACTIVETEXTURE; event.data.backend.args[0] = (uint32_t)(NT_GFX_GL_UPLOAD_TEXTURE_UNIT););
        s_gl_cache.active_texture_unit = NT_GFX_GL_UPLOAD_TEXTURE_UNIT;
    }
    glBindTexture(GL_TEXTURE_2D, tex);
    NT_GFX_RECORD(NT_GFX_EVENT_BACKEND, NT_GFX_OP_STATE, event.detail = NT_GFX_GL_BINDTEXTURE; event.data.backend.args[0] = (uint32_t)(GL_TEXTURE_2D); event.data.backend.args[1] = (uint32_t)(tex););
    NT_GFX_COUNT(texture_calls);
}

/* For upload paths that check glGetError afterwards — a stale error would be
 * misattributed to this upload. */
static bool nt_gfx_gl_begin_texture_upload(GLuint tex) {
    GLenum pending_error = glGetError();
    /* WebGL reports a loss once through glGetError; that is a recoverable
       outcome the caller rolls back, not a programmer error. */
    bool context_lost = pending_error == 0x9242U /* GL_CONTEXT_LOST_WEBGL */ || (pending_error != GL_NO_ERROR && nt_gfx_gl_ctx_is_lost());
    if (context_lost) {
        nt_gfx_observe_context_loss();
    }
    NT_ASSERT((pending_error == GL_NO_ERROR || context_lost) && "pending GL error before texture upload");
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
    glGenTextures(1, &tex);
    NT_GFX_RECORD(NT_GFX_EVENT_BACKEND, NT_GFX_OP_STATE, event.detail = NT_GFX_GL_GENTEXTURES; event.data.backend.args[0] = (uint32_t)(1); event.data.backend.args[1] = (uint32_t)(*(&tex)););
    if (tex == 0 || !nt_gfx_gl_begin_texture_upload(tex)) {
        if (tex != 0) {
            glDeleteTextures(1, &tex);
            NT_GFX_RECORD(NT_GFX_EVENT_BACKEND, NT_GFX_OP_STATE, event.detail = NT_GFX_GL_DELETETEXTURES; event.data.backend.args[0] = (uint32_t)(1);
                          event.data.backend.args[1] = (uint32_t)(*(&tex)););
        }
        return 0;
    }

    /* Filter and wrap live on the sampler object every bind carries; the
       texture object keeps GL defaults, which no sampling path reads. */
    nt_gfx_gl_fmt_t gl = nt_gfx_gl_texture_format(desc->format);
    if (gl.internal == 0) {
        glDeleteTextures(1, &tex);
        NT_GFX_RECORD(NT_GFX_EVENT_BACKEND, NT_GFX_OP_STATE, event.detail = NT_GFX_GL_DELETETEXTURES; event.data.backend.args[0] = (uint32_t)(1); event.data.backend.args[1] = (uint32_t)(*(&tex)););
        return 0;
    }

    /* Declared levels lie back to back in desc->data (NULL = storage only). */
    const uint8_t levels = desc->level_count > 1 ? desc->level_count : 1;
    if (!gl.align4) {
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        NT_GFX_RECORD(NT_GFX_EVENT_BACKEND, NT_GFX_OP_STATE, event.detail = NT_GFX_GL_PIXELSTOREI; event.data.backend.args[0] = (uint32_t)(GL_UNPACK_ALIGNMENT);
                      event.data.backend.args[1] = (uint32_t)(1););
    }
    const uint8_t *level_data = (const uint8_t *)desc->data;
    for (uint8_t level = 0; level < levels; level++) {
        const uint32_t level_w = nt_texture_level_extent(desc->width, level);
        const uint32_t level_h = nt_texture_level_extent(desc->height, level);
        const uint64_t level_bytes = nt_texture_level_bytes(desc->format, level_w, level_h);
        if (gl.compressed) {
            glCompressedTexImage2D(GL_TEXTURE_2D, (GLint)level, gl.internal, (GLsizei)level_w, (GLsizei)level_h, 0, (GLsizei)level_bytes, level_data);
            NT_GFX_RECORD(NT_GFX_EVENT_BACKEND, NT_GFX_OP_TEXTURE_UPLOAD, event.detail = NT_GFX_GL_COMPRESSEDTEXIMAGE2D; event.data.backend.args[0] = (uint32_t)(GL_TEXTURE_2D);
                          event.data.backend.args[1] = (uint32_t)((GLint)level); event.data.backend.args[2] = (uint32_t)(gl.internal); event.data.backend.args[3] = (uint32_t)((GLsizei)level_w);
                          event.data.backend.args[4] = (uint32_t)((GLsizei)level_h); event.data.backend.args[5] = (uint32_t)(0); event.data.backend.args[6] = (uint32_t)((GLsizei)level_bytes);
                          event.data.backend.args[7] = (uint32_t)((level_data != NULL)); event.data.backend.bytes = level_data != NULL ? level_bytes : 0;);
        } else {
            glTexImage2D(GL_TEXTURE_2D, (GLint)level, (GLint)gl.internal, (GLsizei)level_w, (GLsizei)level_h, 0, gl.format, gl.type, level_data);
            NT_GFX_RECORD(NT_GFX_EVENT_BACKEND, NT_GFX_OP_TEXTURE_UPLOAD, event.detail = NT_GFX_GL_TEXIMAGE2D; event.data.backend.args[0] = (uint32_t)(GL_TEXTURE_2D);
                          event.data.backend.args[1] = (uint32_t)((GLint)level); event.data.backend.args[2] = (uint32_t)((GLint)gl.internal); event.data.backend.args[3] = (uint32_t)((GLsizei)level_w);
                          event.data.backend.args[4] = (uint32_t)((GLsizei)level_h); event.data.backend.args[5] = (uint32_t)(0); event.data.backend.args[6] = (uint32_t)(gl.format);
                          event.data.backend.args[7] = (uint32_t)(gl.type); event.data.backend.args[8] = (uint32_t)((level_data != NULL));
                          event.data.backend.bytes = level_data != NULL ? level_bytes : 0;);
        }
        NT_GFX_COUNT_UPLOAD(true, level_data, level_bytes);
        if (level_data != NULL) {
            level_data += level_bytes;
        }
    }
    if (!gl.align4) {
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
        NT_GFX_RECORD(NT_GFX_EVENT_BACKEND, NT_GFX_OP_STATE, event.detail = NT_GFX_GL_PIXELSTOREI; event.data.backend.args[0] = (uint32_t)(GL_UNPACK_ALIGNMENT);
                      event.data.backend.args[1] = (uint32_t)(4););
    }

    /* Generate mipmaps after base level upload if requested and data present */
    if (desc->gen_mipmaps && desc->data) {
        glGenerateMipmap(GL_TEXTURE_2D);
        NT_GFX_RECORD(NT_GFX_EVENT_BACKEND, NT_GFX_OP_STATE, event.detail = NT_GFX_GL_GENERATEMIPMAP; event.data.backend.args[0] = (uint32_t)(GL_TEXTURE_2D););
    }

    const uint8_t top_level = (desc->gen_mipmaps && desc->data) ? nt_texture_full_chain_levels(desc->width, desc->height) : levels;
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, (GLint)(top_level - 1));
    NT_GFX_RECORD(NT_GFX_EVENT_BACKEND, NT_GFX_OP_STATE, event.detail = NT_GFX_GL_TEXPARAMETERI; event.data.backend.args[0] = (uint32_t)(GL_TEXTURE_2D);
                  event.data.backend.args[1] = (uint32_t)(GL_TEXTURE_MAX_LEVEL); event.data.backend.args[2] = (uint32_t)((GLint)(top_level - 1)););

    GLenum first_error = GL_NO_ERROR;
    for (GLenum e = glGetError(); e != GL_NO_ERROR; e = glGetError()) {
        if (e == 0x9242U) { /* GL_CONTEXT_LOST_WEBGL */
            nt_gfx_observe_context_loss();
        }
        if (first_error == GL_NO_ERROR) {
            first_error = e;
        }
    }
    if (first_error != GL_NO_ERROR) {
        NT_LOG_ERROR("texture creation failed: GL error 0x%04X", (unsigned)first_error);
        glDeleteTextures(1, &tex);
        NT_GFX_RECORD(NT_GFX_EVENT_BACKEND, NT_GFX_OP_STATE, event.detail = NT_GFX_GL_DELETETEXTURES; event.data.backend.args[0] = (uint32_t)(1); event.data.backend.args[1] = (uint32_t)(*(&tex)););
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
        glDeleteTextures(1, &tex);
        NT_GFX_RECORD(NT_GFX_EVENT_BACKEND, NT_GFX_OP_STATE, event.detail = NT_GFX_GL_DELETETEXTURES; event.data.backend.args[0] = (uint32_t)(1); event.data.backend.args[1] = (uint32_t)(*(&tex)););
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
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        NT_GFX_RECORD(NT_GFX_EVENT_BACKEND, NT_GFX_OP_STATE, event.detail = NT_GFX_GL_PIXELSTOREI; event.data.backend.args[0] = (uint32_t)(GL_UNPACK_ALIGNMENT);
                      event.data.backend.args[1] = (uint32_t)(1););
    }

    glTexSubImage2D(GL_TEXTURE_2D, 0, (GLint)x, (GLint)y, (GLsizei)w, (GLsizei)h, gl.format, gl.type, data);
    NT_GFX_RECORD(NT_GFX_EVENT_BACKEND, NT_GFX_OP_TEXTURE_UPLOAD, event.detail = NT_GFX_GL_TEXSUBIMAGE2D; event.data.backend.args[0] = (uint32_t)(GL_TEXTURE_2D);
                  event.data.backend.args[1] = (uint32_t)(0); event.data.backend.args[2] = (uint32_t)((GLint)x); event.data.backend.args[3] = (uint32_t)((GLint)y);
                  event.data.backend.args[4] = (uint32_t)((GLsizei)w); event.data.backend.args[5] = (uint32_t)((GLsizei)h); event.data.backend.args[6] = (uint32_t)(gl.format);
                  event.data.backend.args[7] = (uint32_t)(gl.type); event.data.backend.args[8] = (uint32_t)((data != NULL));
                  event.data.backend.bytes = data != NULL ? nt_texture_level_bytes(format, w, h) : 0;);
    NT_GFX_COUNT_UPLOAD(true, data, nt_texture_level_bytes(format, w, h));

    if (!gl.align4) {
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
        NT_GFX_RECORD(NT_GFX_EVENT_BACKEND, NT_GFX_OP_STATE, event.detail = NT_GFX_GL_PIXELSTOREI; event.data.backend.args[0] = (uint32_t)(GL_UNPACK_ALIGNMENT);
                      event.data.backend.args[1] = (uint32_t)(4););
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
        glDeleteTextures(1, &tex);
        NT_GFX_RECORD(NT_GFX_EVENT_BACKEND, NT_GFX_OP_STATE, event.detail = NT_GFX_GL_DELETETEXTURES; event.data.backend.args[0] = (uint32_t)(1); event.data.backend.args[1] = (uint32_t)(*(&tex)););
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
    glGenFramebuffers(1, &fbo);
    NT_GFX_RECORD(NT_GFX_EVENT_BACKEND, NT_GFX_OP_STATE, event.detail = NT_GFX_GL_GENFRAMEBUFFERS; event.data.backend.args[0] = (uint32_t)(1); event.data.backend.args[1] = (uint32_t)(*(&fbo)););
    if (fbo == 0) {
        return false;
    }
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    NT_GFX_RECORD(NT_GFX_EVENT_BACKEND, NT_GFX_OP_STATE, event.detail = NT_GFX_GL_BINDFRAMEBUFFER; event.data.backend.args[0] = (uint32_t)(GL_FRAMEBUFFER);
                  event.data.backend.args[1] = (uint32_t)(fbo););
    s_bound_framebuffer = fbo;
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, color, 0);
    NT_GFX_RECORD(NT_GFX_EVENT_BACKEND, NT_GFX_OP_STATE, event.detail = NT_GFX_GL_FRAMEBUFFERTEXTURE2D; event.data.backend.args[0] = (uint32_t)(GL_FRAMEBUFFER);
                  event.data.backend.args[1] = (uint32_t)(GL_COLOR_ATTACHMENT0); event.data.backend.args[2] = (uint32_t)(GL_TEXTURE_2D); event.data.backend.args[3] = (uint32_t)(color);
                  event.data.backend.args[4] = (uint32_t)(0););

    if (desc->depth_storage == NT_RT_DEPTH_BUFFER) {
        glGenRenderbuffers(1, &depth_rbo);
        NT_GFX_RECORD(NT_GFX_EVENT_BACKEND, NT_GFX_OP_STATE, event.detail = NT_GFX_GL_GENRENDERBUFFERS; event.data.backend.args[0] = (uint32_t)(1);
                      event.data.backend.args[1] = (uint32_t)(*(&depth_rbo)););
        if (depth_rbo == 0) {
            glDeleteFramebuffers(1, &fbo);
            NT_GFX_RECORD(NT_GFX_EVENT_BACKEND, NT_GFX_OP_STATE, event.detail = NT_GFX_GL_DELETEFRAMEBUFFERS; event.data.backend.args[0] = (uint32_t)(1);
                          event.data.backend.args[1] = (uint32_t)(*(&fbo)););
            glBindFramebuffer(GL_FRAMEBUFFER, restore_fbo);
            NT_GFX_RECORD(NT_GFX_EVENT_BACKEND, NT_GFX_OP_STATE, event.detail = NT_GFX_GL_BINDFRAMEBUFFER; event.data.backend.args[0] = (uint32_t)(GL_FRAMEBUFFER);
                          event.data.backend.args[1] = (uint32_t)(restore_fbo););
            s_bound_framebuffer = restore_fbo;
            return false;
        }
        glBindRenderbuffer(GL_RENDERBUFFER, depth_rbo);
        NT_GFX_RECORD(NT_GFX_EVENT_BACKEND, NT_GFX_OP_STATE, event.detail = NT_GFX_GL_BINDRENDERBUFFER; event.data.backend.args[0] = (uint32_t)(GL_RENDERBUFFER);
                      event.data.backend.args[1] = (uint32_t)(depth_rbo););
        nt_gfx_gl_fmt_t depth_fmt = nt_gfx_gl_texture_format(desc->depth_format);
        glRenderbufferStorage(GL_RENDERBUFFER, depth_fmt.internal, (GLsizei)desc->width, (GLsizei)desc->height);
        NT_GFX_RECORD(NT_GFX_EVENT_BACKEND, NT_GFX_OP_STATE, event.detail = NT_GFX_GL_RENDERBUFFERSTORAGE; event.data.backend.args[0] = (uint32_t)(GL_RENDERBUFFER);
                      event.data.backend.args[1] = (uint32_t)(depth_fmt.internal); event.data.backend.args[2] = (uint32_t)((GLsizei)desc->width);
                      event.data.backend.args[3] = (uint32_t)((GLsizei)desc->height););
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depth_rbo);
        NT_GFX_RECORD(NT_GFX_EVENT_BACKEND, NT_GFX_OP_STATE, event.detail = NT_GFX_GL_FRAMEBUFFERRENDERBUFFER; event.data.backend.args[0] = (uint32_t)(GL_FRAMEBUFFER);
                      event.data.backend.args[1] = (uint32_t)(GL_DEPTH_ATTACHMENT); event.data.backend.args[2] = (uint32_t)(GL_RENDERBUFFER); event.data.backend.args[3] = (uint32_t)(depth_rbo););
        glBindRenderbuffer(GL_RENDERBUFFER, 0);
        NT_GFX_RECORD(NT_GFX_EVENT_BACKEND, NT_GFX_OP_STATE, event.detail = NT_GFX_GL_BINDRENDERBUFFER; event.data.backend.args[0] = (uint32_t)(GL_RENDERBUFFER);
                      event.data.backend.args[1] = (uint32_t)(0););
    } else if (desc->depth_storage == NT_RT_DEPTH_TEXTURE) {
        if (depth == 0) {
            glDeleteFramebuffers(1, &fbo);
            NT_GFX_RECORD(NT_GFX_EVENT_BACKEND, NT_GFX_OP_STATE, event.detail = NT_GFX_GL_DELETEFRAMEBUFFERS; event.data.backend.args[0] = (uint32_t)(1);
                          event.data.backend.args[1] = (uint32_t)(*(&fbo)););
            glBindFramebuffer(GL_FRAMEBUFFER, restore_fbo);
            NT_GFX_RECORD(NT_GFX_EVENT_BACKEND, NT_GFX_OP_STATE, event.detail = NT_GFX_GL_BINDFRAMEBUFFER; event.data.backend.args[0] = (uint32_t)(GL_FRAMEBUFFER);
                          event.data.backend.args[1] = (uint32_t)(restore_fbo););
            s_bound_framebuffer = restore_fbo;
            return false;
        }
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, depth, 0);
        NT_GFX_RECORD(NT_GFX_EVENT_BACKEND, NT_GFX_OP_STATE, event.detail = NT_GFX_GL_FRAMEBUFFERTEXTURE2D; event.data.backend.args[0] = (uint32_t)(GL_FRAMEBUFFER);
                      event.data.backend.args[1] = (uint32_t)(GL_DEPTH_ATTACHMENT); event.data.backend.args[2] = (uint32_t)(GL_TEXTURE_2D); event.data.backend.args[3] = (uint32_t)(depth);
                      event.data.backend.args[4] = (uint32_t)(0););
    }

    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        NT_LOG_ERROR("render target incomplete: GL status 0x%04X", (unsigned)status);
        if (depth_rbo != 0) {
            glDeleteRenderbuffers(1, &depth_rbo);
            NT_GFX_RECORD(NT_GFX_EVENT_BACKEND, NT_GFX_OP_STATE, event.detail = NT_GFX_GL_DELETERENDERBUFFERS; event.data.backend.args[0] = (uint32_t)(1);
                          event.data.backend.args[1] = (uint32_t)(*(&depth_rbo)););
        }
        glDeleteFramebuffers(1, &fbo);
        NT_GFX_RECORD(NT_GFX_EVENT_BACKEND, NT_GFX_OP_STATE, event.detail = NT_GFX_GL_DELETEFRAMEBUFFERS; event.data.backend.args[0] = (uint32_t)(1);
                      event.data.backend.args[1] = (uint32_t)(*(&fbo)););
        glBindFramebuffer(GL_FRAMEBUFFER, restore_fbo);
        NT_GFX_RECORD(NT_GFX_EVENT_BACKEND, NT_GFX_OP_STATE, event.detail = NT_GFX_GL_BINDFRAMEBUFFER; event.data.backend.args[0] = (uint32_t)(GL_FRAMEBUFFER);
                      event.data.backend.args[1] = (uint32_t)(restore_fbo););
        s_bound_framebuffer = restore_fbo;
        return false;
    }

    *out_rt = (nt_gfx_gl_render_target_t){
        .fbo = fbo,
        .depth_rbo = depth_rbo,
        .width = desc->width,
        .height = desc->height,
    };
    glBindFramebuffer(GL_FRAMEBUFFER, restore_fbo);
    NT_GFX_RECORD(NT_GFX_EVENT_BACKEND, NT_GFX_OP_STATE, event.detail = NT_GFX_GL_BINDFRAMEBUFFER; event.data.backend.args[0] = (uint32_t)(GL_FRAMEBUFFER);
                  event.data.backend.args[1] = (uint32_t)(restore_fbo););
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
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        NT_GFX_RECORD(NT_GFX_EVENT_BACKEND, NT_GFX_OP_STATE, event.detail = NT_GFX_GL_BINDFRAMEBUFFER; event.data.backend.args[0] = (uint32_t)(GL_FRAMEBUFFER);
                      event.data.backend.args[1] = (uint32_t)(0););
        s_bound_framebuffer = 0;
    }
    if (rt->depth_rbo != 0) {
        glDeleteRenderbuffers(1, &rt->depth_rbo);
        NT_GFX_RECORD(NT_GFX_EVENT_BACKEND, NT_GFX_OP_STATE, event.detail = NT_GFX_GL_DELETERENDERBUFFERS; event.data.backend.args[0] = (uint32_t)(1);
                      event.data.backend.args[1] = (uint32_t)(*(&rt->depth_rbo)););
    }
    if (rt->fbo != 0) {
        glDeleteFramebuffers(1, &rt->fbo);
        NT_GFX_RECORD(NT_GFX_EVENT_BACKEND, NT_GFX_OP_STATE, event.detail = NT_GFX_GL_DELETEFRAMEBUFFERS; event.data.backend.args[0] = (uint32_t)(1);
                      event.data.backend.args[1] = (uint32_t)(*(&rt->fbo)););
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
        glDeleteRenderbuffers(1, &staging->target.depth_rbo);
        NT_GFX_RECORD(NT_GFX_EVENT_BACKEND, NT_GFX_OP_STATE, event.detail = NT_GFX_GL_DELETERENDERBUFFERS; event.data.backend.args[0] = (uint32_t)(1);
                      event.data.backend.args[1] = (uint32_t)(*(&staging->target.depth_rbo)););
    }
    if (staging->target.fbo != 0) {
        glDeleteFramebuffers(1, &staging->target.fbo);
        NT_GFX_RECORD(NT_GFX_EVENT_BACKEND, NT_GFX_OP_STATE, event.detail = NT_GFX_GL_DELETEFRAMEBUFFERS; event.data.backend.args[0] = (uint32_t)(1);
                      event.data.backend.args[1] = (uint32_t)(*(&staging->target.fbo)););
    }
    if (staging->depth != 0) {
        glDeleteTextures(1, &staging->depth);
        NT_GFX_RECORD(NT_GFX_EVENT_BACKEND, NT_GFX_OP_STATE, event.detail = NT_GFX_GL_DELETETEXTURES; event.data.backend.args[0] = (uint32_t)(1);
                      event.data.backend.args[1] = (uint32_t)(*(&staging->depth)););
    }
    if (staging->color != 0) {
        glDeleteTextures(1, &staging->color);
        NT_GFX_RECORD(NT_GFX_EVENT_BACKEND, NT_GFX_OP_STATE, event.detail = NT_GFX_GL_DELETETEXTURES; event.data.backend.args[0] = (uint32_t)(1);
                      event.data.backend.args[1] = (uint32_t)(*(&staging->color)););
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
    glDeleteTextures(1, &old_color);
    NT_GFX_RECORD(NT_GFX_EVENT_BACKEND, NT_GFX_OP_STATE, event.detail = NT_GFX_GL_DELETETEXTURES; event.data.backend.args[0] = (uint32_t)(1); event.data.backend.args[1] = (uint32_t)(*(&old_color)););

    if (desc->depth_storage == NT_RT_DEPTH_TEXTURE) {
        GLuint old_depth = s_texture_gl[depth_backend];
        s_texture_gl[depth_backend] = staging->depth;
        NT_GFX_RECORD(NT_GFX_EVENT_DEFINITION, NT_GFX_OP_STATE, event.detail = NT_GFX_OBJECT_TEXTURE; event.data.backend.args[0] = depth_backend; event.data.backend.args[1] = staging->depth;);
        nt_gfx_gl_forget_texture(old_depth);
        glDeleteTextures(1, &old_depth);
        NT_GFX_RECORD(NT_GFX_EVENT_BACKEND, NT_GFX_OP_STATE, event.detail = NT_GFX_GL_DELETETEXTURES; event.data.backend.args[0] = (uint32_t)(1);
                      event.data.backend.args[1] = (uint32_t)(*(&old_depth)););
    }
    s_render_targets[backend_handle] = staging->target;
    NT_GFX_RECORD(NT_GFX_EVENT_DEFINITION, NT_GFX_OP_STATE, event.detail = NT_GFX_OBJECT_RENDER_TARGET; event.data.backend.args[0] = backend_handle; event.data.backend.args[1] = staging->target.fbo;
                  event.data.backend.args[2] = staging->target.depth_rbo;);
    if (s_bound_framebuffer == old.fbo) {
        glBindFramebuffer(GL_FRAMEBUFFER, staging->target.fbo);
        NT_GFX_RECORD(NT_GFX_EVENT_BACKEND, NT_GFX_OP_STATE, event.detail = NT_GFX_GL_BINDFRAMEBUFFER; event.data.backend.args[0] = (uint32_t)(GL_FRAMEBUFFER);
                      event.data.backend.args[1] = (uint32_t)(staging->target.fbo););
        s_bound_framebuffer = staging->target.fbo;
    }
    if (old.depth_rbo != 0) {
        glDeleteRenderbuffers(1, &old.depth_rbo);
        NT_GFX_RECORD(NT_GFX_EVENT_BACKEND, NT_GFX_OP_STATE, event.detail = NT_GFX_GL_DELETERENDERBUFFERS; event.data.backend.args[0] = (uint32_t)(1);
                      event.data.backend.args[1] = (uint32_t)(*(&old.depth_rbo)););
    }
    glDeleteFramebuffers(1, &old.fbo);
    NT_GFX_RECORD(NT_GFX_EVENT_BACKEND, NT_GFX_OP_STATE, event.detail = NT_GFX_GL_DELETEFRAMEBUFFERS; event.data.backend.args[0] = (uint32_t)(1);
                  event.data.backend.args[1] = (uint32_t)(*(&old.fbo)););
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
        NT_GFX_RECORD(NT_GFX_EVENT_SKIP, NT_GFX_OP_TEXTURE, event.detail = NT_GFX_GL_BINDTEXTURE; event.reason = NT_GFX_REASON_CACHE; event.data.backend.args[0] = tex;
                      event.data.backend.args[1] = slot;);
        return; /* already bound to this slot */
    }
    GLenum unit = GL_TEXTURE0 + slot;
    if (s_gl_cache.active_texture_unit != unit) {
        glActiveTexture(unit);
        NT_GFX_RECORD(NT_GFX_EVENT_BACKEND, NT_GFX_OP_STATE, event.detail = NT_GFX_GL_ACTIVETEXTURE; event.data.backend.args[0] = (uint32_t)(unit););
        s_gl_cache.active_texture_unit = unit;
    }
    glBindTexture(GL_TEXTURE_2D, tex);
    NT_GFX_RECORD(NT_GFX_EVENT_BACKEND, NT_GFX_OP_STATE, event.detail = NT_GFX_GL_BINDTEXTURE; event.data.backend.args[0] = (uint32_t)(GL_TEXTURE_2D); event.data.backend.args[1] = (uint32_t)(tex););
    NT_GFX_COUNT(texture_calls);
    s_gl_cache.bound_textures[slot] = tex;
}

/* Sampler objects (WebGL2 / GL 3.3+). The backend "handle" is the raw
 * GLuint sampler id — nt_gfx caches them on its side, so the backend
 * does not maintain its own array. */

// NOLINTNEXTLINE(readability-function-cognitive-complexity) -- diagnostic record and assert macros expand at owning sites
uint32_t nt_gfx_backend_create_sampler(const nt_sampler_desc_t *desc) {
    GLuint s = 0;
    glGenSamplers(1, &s);
    NT_GFX_RECORD(NT_GFX_EVENT_BACKEND, NT_GFX_OP_STATE, event.detail = NT_GFX_GL_GENSAMPLERS; event.data.backend.args[0] = (uint32_t)(1); event.data.backend.args[1] = (uint32_t)(*(&s)););
    if (s == 0) {
        return 0;
    }
    glSamplerParameteri(s, GL_TEXTURE_MIN_FILTER, (GLint)map_texture_filter(desc->min_filter));
    NT_GFX_RECORD(NT_GFX_EVENT_BACKEND, NT_GFX_OP_STATE, event.detail = NT_GFX_GL_SAMPLERPARAMETERI; event.data.backend.args[0] = (uint32_t)(s);
                  event.data.backend.args[1] = (uint32_t)(GL_TEXTURE_MIN_FILTER); event.data.backend.args[2] = (uint32_t)((GLint)map_texture_filter(desc->min_filter)););
    glSamplerParameteri(s, GL_TEXTURE_MAG_FILTER, (GLint)map_texture_filter(desc->mag_filter));
    NT_GFX_RECORD(NT_GFX_EVENT_BACKEND, NT_GFX_OP_STATE, event.detail = NT_GFX_GL_SAMPLERPARAMETERI; event.data.backend.args[0] = (uint32_t)(s);
                  event.data.backend.args[1] = (uint32_t)(GL_TEXTURE_MAG_FILTER); event.data.backend.args[2] = (uint32_t)((GLint)map_texture_filter(desc->mag_filter)););
    glSamplerParameteri(s, GL_TEXTURE_WRAP_S, (GLint)map_texture_wrap(desc->wrap_u));
    NT_GFX_RECORD(NT_GFX_EVENT_BACKEND, NT_GFX_OP_STATE, event.detail = NT_GFX_GL_SAMPLERPARAMETERI; event.data.backend.args[0] = (uint32_t)(s);
                  event.data.backend.args[1] = (uint32_t)(GL_TEXTURE_WRAP_S); event.data.backend.args[2] = (uint32_t)((GLint)map_texture_wrap(desc->wrap_u)););
    glSamplerParameteri(s, GL_TEXTURE_WRAP_T, (GLint)map_texture_wrap(desc->wrap_v));
    NT_GFX_RECORD(NT_GFX_EVENT_BACKEND, NT_GFX_OP_STATE, event.detail = NT_GFX_GL_SAMPLERPARAMETERI; event.data.backend.args[0] = (uint32_t)(s);
                  event.data.backend.args[1] = (uint32_t)(GL_TEXTURE_WRAP_T); event.data.backend.args[2] = (uint32_t)((GLint)map_texture_wrap(desc->wrap_v)););
    /* Set unconditionally rather than relying on the object's default NONE —
     * sampler state stays fully described by the descriptor. */
    glSamplerParameteri(s, GL_TEXTURE_COMPARE_MODE, desc->compare_func != NT_COMPARE_NONE ? GL_COMPARE_REF_TO_TEXTURE : GL_NONE);
    NT_GFX_RECORD(NT_GFX_EVENT_BACKEND, NT_GFX_OP_STATE, event.detail = NT_GFX_GL_SAMPLERPARAMETERI; event.data.backend.args[0] = (uint32_t)(s);
                  event.data.backend.args[1] = (uint32_t)(GL_TEXTURE_COMPARE_MODE);
                  event.data.backend.args[2] = (uint32_t)(desc->compare_func != NT_COMPARE_NONE ? GL_COMPARE_REF_TO_TEXTURE : GL_NONE););
    glSamplerParameteri(s, GL_TEXTURE_COMPARE_FUNC, (GLint)map_compare_func(desc->compare_func));
    NT_GFX_RECORD(NT_GFX_EVENT_BACKEND, NT_GFX_OP_STATE, event.detail = NT_GFX_GL_SAMPLERPARAMETERI; event.data.backend.args[0] = (uint32_t)(s);
                  event.data.backend.args[1] = (uint32_t)(GL_TEXTURE_COMPARE_FUNC); event.data.backend.args[2] = (uint32_t)((GLint)map_compare_func(desc->compare_func)););
    return (uint32_t)s;
}

void nt_gfx_backend_destroy_sampler(uint32_t backend_handle) {
    if (backend_handle == 0) {
        return;
    }
    GLuint s = (GLuint)backend_handle;
    glDeleteSamplers(1, &s);
    NT_GFX_RECORD(NT_GFX_EVENT_BACKEND, NT_GFX_OP_STATE, event.detail = NT_GFX_GL_DELETESAMPLERS; event.data.backend.args[0] = (uint32_t)(1); event.data.backend.args[1] = (uint32_t)(*(&s)););
    /* GL unbinds the deleted name from every unit; the mirror must say so too. */
    for (uint32_t u = 0; u < NT_GFX_MAX_TEXTURE_SLOTS; u++) {
        if (s_gl_cache.bound_samplers[u] == s) {
            s_gl_cache.bound_samplers[u] = 0;
        }
    }
}

void nt_gfx_backend_bind_sampler(uint32_t backend_handle, uint32_t slot) {
    NT_ASSERT(slot < NT_GFX_MAX_TEXTURE_SLOTS && "bind_sampler: slot out of range");
    NT_ASSERT(backend_handle != 0 && "bind_sampler: sampling without a sampler object");
    GLuint sampler = (GLuint)backend_handle;
    if (s_gl_cache.bound_samplers[slot] == sampler) {
        NT_GFX_RECORD(NT_GFX_EVENT_SKIP, NT_GFX_OP_SAMPLER, event.detail = NT_GFX_GL_BINDSAMPLER; event.reason = NT_GFX_REASON_CACHE; event.data.backend.args[0] = sampler;
                      event.data.backend.args[1] = slot;);
        return;
    }
#ifdef NT_TEST_ACCESS
    s_test_sampler_binds++;
#endif
    glBindSampler(slot, sampler);
    NT_GFX_RECORD(NT_GFX_EVENT_BACKEND, NT_GFX_OP_STATE, event.detail = NT_GFX_GL_BINDSAMPLER; event.data.backend.args[0] = (uint32_t)(slot); event.data.backend.args[1] = (uint32_t)(sampler););
    NT_GFX_COUNT(sampler_calls);
    s_gl_cache.bound_samplers[slot] = sampler;
}

void nt_gfx_backend_draw_instanced(uint32_t first_vertex, uint32_t num_vertices, uint32_t instance_count) {
    glDrawArraysInstanced(GL_TRIANGLES, (GLint)first_vertex, (GLsizei)num_vertices, (GLsizei)instance_count);
    NT_GFX_RECORD(NT_GFX_EVENT_BACKEND, NT_GFX_OP_DRAW, event.detail = NT_GFX_GL_DRAWARRAYSINSTANCED; event.data.backend.args[0] = (uint32_t)(GL_TRIANGLES);
                  event.data.backend.args[1] = (uint32_t)((GLint)first_vertex); event.data.backend.args[2] = (uint32_t)((GLsizei)num_vertices);
                  event.data.backend.args[3] = (uint32_t)((GLsizei)instance_count););
}

void nt_gfx_backend_draw_indexed_instanced(uint32_t first_index, uint32_t num_indices, uint32_t instance_count, uint8_t index_type) {
    GLenum gl_type = (index_type == 2) ? GL_UNSIGNED_INT : GL_UNSIGNED_SHORT;
    uint32_t stride = (index_type == 2) ? sizeof(uint32_t) : sizeof(uint16_t);
    glDrawElementsInstanced(GL_TRIANGLES, (GLsizei)num_indices, gl_type,
                            (void *)(uintptr_t)(first_index * stride), // NOLINT(performance-no-int-to-ptr)
                            (GLsizei)instance_count);
    NT_GFX_RECORD(NT_GFX_EVENT_BACKEND, NT_GFX_OP_DRAW, event.detail = NT_GFX_GL_DRAWELEMENTSINSTANCED; event.data.backend.args[0] = (uint32_t)(GL_TRIANGLES);
                  event.data.backend.args[1] = (uint32_t)((GLsizei)num_indices); event.data.backend.args[2] = (uint32_t)(gl_type); event.data.backend.args[3] = (uint32_t)((first_index * stride));
                  event.data.backend.args[4] = (uint32_t)((GLsizei)instance_count););
}

/* ---- Context loss recovery ---- */

bool nt_gfx_backend_recreate_all_resources(void) {
    NT_GFX_RECORD(NT_GFX_EVENT_BEGIN, NT_GFX_OP_CONTEXT, event.reason = NT_GFX_REASON_NONE;);
    /* Destroy old context and create a fresh one. */
    nt_gfx_gl_ctx_destroy();
    if (!nt_gfx_gl_ctx_create(&s_init_desc)) {
        NT_GFX_RESULT(NT_GFX_OP_CONTEXT, NT_GFX_OBJECT_NONE, 0, NT_GFX_REASON_BACKEND_FAILURE);
        return false;
    }
#if NT_GFX_CAPTURE_ENABLED
    NT_ASSERT(g_nt_gfx_capture.context_sequence != UINT64_MAX);
    g_nt_gfx_capture.context_sequence++;
#endif

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
    NT_GFX_RESULT(NT_GFX_OP_CONTEXT, NT_GFX_OBJECT_NONE, 0, NT_GFX_REASON_ACCEPTED);
    return true;
}
