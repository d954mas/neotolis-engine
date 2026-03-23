/*
 * Unified OpenGL backend for nt_gfx.
 *
 * Covers WebGL 2 (GLES 3.0) and OpenGL 3.3 Core (native desktop).
 * All platform calls (context create/destroy/loss) go through
 * nt_gfx_gl_ctx.h — zero Emscripten or OS API here.
 *
 * Only remaining #ifdef: GL headers and glClearDepthf vs glClearDepth.
 */

#include "basisu/nt_basisu_transcoder.h"
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

/* ---- Pipeline backend data ---- */

/* Per-pipeline uniform location cache (populated at link time) */
#define NT_MAX_CACHED_UNIFORMS 16

typedef struct {
    uint32_t name_hash;
    GLint location;
} nt_cached_uniform_t;

typedef struct {
    GLuint vao;
    GLuint program;
    bool depth_test;
    bool depth_write;
    GLenum depth_func;
    uint8_t cull_mode;
    bool blend;
    GLenum blend_src;
    GLenum blend_dst;
    bool polygon_offset;
    float po_factor;
    float po_units;
    /* Store layouts for re-applying vertex attrib pointers on buffer bind */
    nt_vertex_layout_t layout;
    nt_vertex_layout_t instance_layout;
    /* Uniform location cache (filled at glLinkProgram time) */
    nt_cached_uniform_t uniforms[NT_MAX_CACHED_UNIFORMS];
    uint8_t uniform_count;
} nt_gfx_gl_pipeline_t;

/* ---- File-scope state ---- */

static GLuint s_bound_program;         /* currently bound GL program (for uniforms) */
static uint32_t s_bound_pipeline_slot; /* currently bound pipeline index */

static nt_gfx_gl_pipeline_t *s_pipelines; /* pipeline data, indexed by slot */
static GLuint *s_buffer_gl;               /* GL buffer names, indexed by slot */
static GLenum *s_buffer_targets;          /* GL_ARRAY_BUFFER or GL_ELEMENT_ARRAY_BUFFER */
static GLuint *s_texture_gl;              /* GL texture names, indexed by slot */
static GLuint s_instance_gl_buf;          /* GL name of last bound instance buffer */

static nt_gfx_desc_t s_init_desc; /* resolved desc: defaults applied, used everywhere */

/* ---- GL state cache (skip redundant JS interop calls) ---- */

static struct {
    GLuint vao;
    GLuint program;
    bool depth_test;
    bool depth_write;
    GLenum depth_func;
    uint8_t cull_mode;
    bool blend;
    GLenum blend_src;
    GLenum blend_dst;
    bool polygon_offset;
    float po_factor;
    float po_units;
    GLenum active_texture;                           /* current glActiveTexture unit */
    GLuint bound_textures[NT_GFX_MAX_TEXTURE_SLOTS]; /* GL name per slot */
} s_gl_cache;

/* ---- Per-pipeline uniform location lookup ---- */

static GLint pipeline_get_uniform(const char *name) {
    if (s_bound_pipeline_slot == 0 || s_bound_pipeline_slot > s_init_desc.max_pipelines) {
        return -1;
    }
    const nt_gfx_gl_pipeline_t *pip = &s_pipelines[s_bound_pipeline_slot];
    uint32_t h = nt_hash32_str(name).value;
    for (uint8_t i = 0; i < pip->uniform_count; i++) {
        if (pip->uniforms[i].name_hash == h) {
            return pip->uniforms[i].location;
        }
    }
    return -1;
}

static void nt_gfx_gl_cache_reset(void) {
    s_gl_cache.vao = 0;
    s_gl_cache.program = 0;
    s_gl_cache.depth_test = false;
    s_gl_cache.depth_write = true;   /* GL default: depth write enabled */
    s_gl_cache.depth_func = GL_LESS; /* GL default */
    s_gl_cache.cull_mode = 0;
    s_gl_cache.blend = false;
    s_gl_cache.blend_src = GL_ONE;  /* GL default */
    s_gl_cache.blend_dst = GL_ZERO; /* GL default */
    s_gl_cache.polygon_offset = false;
    s_gl_cache.po_factor = 0.0F;
    s_gl_cache.po_units = 0.0F;
    s_gl_cache.active_texture = GL_TEXTURE0;
    memset(s_gl_cache.bound_textures, 0, sizeof(s_gl_cache.bound_textures));
    s_instance_gl_buf = 0;
}

/* ---- Helpers: enum mapping ---- */

static GLenum map_blend_factor(nt_blend_factor_t f) {
    switch (f) {
    case NT_BLEND_ZERO:
        return GL_ZERO;
    case NT_BLEND_ONE:
        return GL_ONE;
    case NT_BLEND_SRC_ALPHA:
        return GL_SRC_ALPHA;
    case NT_BLEND_ONE_MINUS_SRC_ALPHA:
        return GL_ONE_MINUS_SRC_ALPHA;
    default:
        return GL_ONE;
    }
}

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

static void get_format_params(nt_vertex_format_t fmt, GLint *size, GLenum *type, GLboolean *normalized) {
    switch (fmt) {
    case NT_FORMAT_FLOAT:
        *size = 1;
        *type = GL_FLOAT;
        *normalized = GL_FALSE;
        break;
    case NT_FORMAT_FLOAT2:
        *size = 2;
        *type = GL_FLOAT;
        *normalized = GL_FALSE;
        break;
    case NT_FORMAT_FLOAT3:
        *size = 3;
        *type = GL_FLOAT;
        *normalized = GL_FALSE;
        break;
    case NT_FORMAT_FLOAT4:
        *size = 4;
        *type = GL_FLOAT;
        *normalized = GL_FALSE;
        break;
    case NT_FORMAT_HALF:
        *size = 1;
        *type = GL_HALF_FLOAT;
        *normalized = GL_FALSE;
        break;
    case NT_FORMAT_HALF2:
        *size = 2;
        *type = GL_HALF_FLOAT;
        *normalized = GL_FALSE;
        break;
    case NT_FORMAT_HALF3:
        *size = 3;
        *type = GL_HALF_FLOAT;
        *normalized = GL_FALSE;
        break;
    case NT_FORMAT_HALF4:
        *size = 4;
        *type = GL_HALF_FLOAT;
        *normalized = GL_FALSE;
        break;
    case NT_FORMAT_SHORT2:
        *size = 2;
        *type = GL_SHORT;
        *normalized = GL_FALSE;
        break;
    case NT_FORMAT_SHORT2N:
        *size = 2;
        *type = GL_SHORT;
        *normalized = GL_TRUE;
        break;
    case NT_FORMAT_SHORT4:
        *size = 4;
        *type = GL_SHORT;
        *normalized = GL_FALSE;
        break;
    case NT_FORMAT_SHORT4N:
        *size = 4;
        *type = GL_SHORT;
        *normalized = GL_TRUE;
        break;
    case NT_FORMAT_UBYTE4:
        *size = 4;
        *type = GL_UNSIGNED_BYTE;
        *normalized = GL_FALSE;
        break;
    case NT_FORMAT_UBYTE4N:
        *size = 4;
        *type = GL_UNSIGNED_BYTE;
        *normalized = GL_TRUE;
        break;
    case NT_FORMAT_BYTE4N:
        *size = 4;
        *type = GL_BYTE;
        *normalized = GL_TRUE;
        break;
    default:
        *size = 4;
        *type = GL_FLOAT;
        *normalized = GL_FALSE;
        break;
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

bool nt_gfx_backend_init(const nt_gfx_desc_t *desc) {
    NT_ASSERT(desc);
    s_init_desc = *desc;

    if (!nt_gfx_gl_ctx_create(&s_init_desc)) {
        return false;
    }

    /* Allocate backend resource arrays (+1 because slots are 1-based) */
    s_pipelines = (nt_gfx_gl_pipeline_t *)calloc(s_init_desc.max_pipelines + 1, sizeof(nt_gfx_gl_pipeline_t));
    s_buffer_gl = (GLuint *)calloc(s_init_desc.max_buffers + 1, sizeof(GLuint));
    s_buffer_targets = (GLenum *)calloc(s_init_desc.max_buffers + 1, sizeof(GLenum));
    s_texture_gl = (GLuint *)calloc(s_init_desc.max_textures + 1, sizeof(GLuint));

    s_bound_program = 0;
    s_bound_pipeline_slot = 0;
    nt_gfx_gl_cache_reset();
    return true;
}

void nt_gfx_backend_shutdown(void) {
    free(s_pipelines);
    free(s_buffer_gl);
    free(s_buffer_targets);
    free(s_texture_gl);

    s_pipelines = NULL;
    s_buffer_gl = NULL;
    s_buffer_targets = NULL;
    s_texture_gl = NULL;

    s_bound_program = 0;
    s_bound_pipeline_slot = 0;

    nt_gfx_gl_ctx_destroy();
}

bool nt_gfx_backend_is_context_lost(void) { return nt_gfx_gl_ctx_is_lost(); }

/* ---- Frame / Pass ---- */

void nt_gfx_backend_begin_frame(void) {
    /* No-op: state machine is in nt_gfx.c.
     * Emscripten handles swap via requestAnimationFrame.
     * Desktop swap is handled by the window layer. */
}

void nt_gfx_backend_end_frame(void) { /* No-op: swap handled externally. */ }

void nt_gfx_backend_begin_pass(const nt_pass_desc_t *desc) {
    glViewport(0, 0, (GLsizei)g_nt_window.fb_width, (GLsizei)g_nt_window.fb_height);
    glClearColor(desc->clear_color[0], desc->clear_color[1], desc->clear_color[2], desc->clear_color[3]);
    nt_gl_clear_depth(desc->clear_depth);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void nt_gfx_backend_end_pass(void) {}

/* ---- Pipeline bind ---- */

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void nt_gfx_backend_bind_pipeline(uint32_t backend_handle) {
    if (backend_handle == 0 || backend_handle > s_init_desc.max_pipelines) {
        return;
    }
    nt_gfx_gl_pipeline_t *pip = &s_pipelines[backend_handle];

    if (s_gl_cache.vao != pip->vao) {
        glBindVertexArray(pip->vao);
        s_gl_cache.vao = pip->vao;
    }
    if (s_gl_cache.program != pip->program) {
        glUseProgram(pip->program);
        s_gl_cache.program = pip->program;
    }
    s_bound_program = pip->program;
    s_bound_pipeline_slot = backend_handle;

    /* Depth test */
    if (s_gl_cache.depth_test != pip->depth_test) {
        if (pip->depth_test) {
            glEnable(GL_DEPTH_TEST);
        } else {
            glDisable(GL_DEPTH_TEST);
        }
        s_gl_cache.depth_test = pip->depth_test;
    }
    if (pip->depth_test && s_gl_cache.depth_func != pip->depth_func) {
        glDepthFunc(pip->depth_func);
        s_gl_cache.depth_func = pip->depth_func;
    }
    if (s_gl_cache.depth_write != pip->depth_write) {
        glDepthMask(pip->depth_write ? GL_TRUE : GL_FALSE);
        s_gl_cache.depth_write = pip->depth_write;
    }

    /* Cull mode */
    if (s_gl_cache.cull_mode != pip->cull_mode) {
        if (pip->cull_mode == 0) {
            glDisable(GL_CULL_FACE);
        } else {
            glEnable(GL_CULL_FACE);
            glCullFace(pip->cull_mode == 2 ? GL_FRONT : GL_BACK);
        }
        s_gl_cache.cull_mode = pip->cull_mode;
    }

    /* Blend */
    if (s_gl_cache.blend != pip->blend) {
        if (pip->blend) {
            glEnable(GL_BLEND);
        } else {
            glDisable(GL_BLEND);
        }
        s_gl_cache.blend = pip->blend;
    }
    if (pip->blend && (s_gl_cache.blend_src != pip->blend_src || s_gl_cache.blend_dst != pip->blend_dst)) {
        glBlendFunc(pip->blend_src, pip->blend_dst);
        s_gl_cache.blend_src = pip->blend_src;
        s_gl_cache.blend_dst = pip->blend_dst;
    }

    /* Polygon offset */
    if (s_gl_cache.polygon_offset != pip->polygon_offset) {
        if (pip->polygon_offset) {
            glEnable(GL_POLYGON_OFFSET_FILL);
        } else {
            glDisable(GL_POLYGON_OFFSET_FILL);
        }
        s_gl_cache.polygon_offset = pip->polygon_offset;
    }
    if (pip->polygon_offset && (s_gl_cache.po_factor != pip->po_factor || s_gl_cache.po_units != pip->po_units)) {
        glPolygonOffset(pip->po_factor, pip->po_units);
        s_gl_cache.po_factor = pip->po_factor;
        s_gl_cache.po_units = pip->po_units;
    }
}

/* ---- Uniforms ---- */

void nt_gfx_backend_set_uniform_mat4(const char *name, const float *matrix) {
    GLint loc = pipeline_get_uniform(name);
    if (loc >= 0) {
        glUniformMatrix4fv(loc, 1, GL_FALSE, matrix);
    }
}

void nt_gfx_backend_set_uniform_vec4(const char *name, const float *vec) {
    GLint loc = pipeline_get_uniform(name);
    if (loc >= 0) {
        glUniform4fv(loc, 1, vec);
    }
}

void nt_gfx_backend_set_uniform_float(const char *name, float val) {
    GLint loc = pipeline_get_uniform(name);
    if (loc >= 0) {
        glUniform1f(loc, val);
    }
}

void nt_gfx_backend_set_uniform_int(const char *name, int val) {
    GLint loc = pipeline_get_uniform(name);
    if (loc >= 0) {
        glUniform1i(loc, val);
    }
}

/* ---- Draw calls ---- */

void nt_gfx_backend_draw(uint32_t first_vertex, uint32_t num_vertices) { glDrawArrays(GL_TRIANGLES, (GLint)first_vertex, (GLsizei)num_vertices); }

void nt_gfx_backend_draw_indexed(uint32_t first_index, uint32_t num_indices, uint8_t index_type) {
    GLenum gl_type = (index_type == 2) ? GL_UNSIGNED_INT : GL_UNSIGNED_SHORT;
    uint32_t stride = (index_type == 2) ? sizeof(uint32_t) : sizeof(uint16_t);
    glDrawElements(GL_TRIANGLES, (GLsizei)num_indices, gl_type,
                   (void *)(uintptr_t)(first_index * stride)); // NOLINT(performance-no-int-to-ptr)
}

/* ---- Resource management (shader / buffer / pipeline) ---- */

uint32_t nt_gfx_backend_create_shader(const nt_shader_desc_t *desc) {
    GLenum gl_type = (desc->type == NT_SHADER_VERTEX) ? GL_VERTEX_SHADER : GL_FRAGMENT_SHADER;
    GLuint shader = glCreateShader(gl_type);

#ifdef NT_PLATFORM_WEB
    const char *prefix = "#version 300 es\n";
#else
    const char *prefix = "#version 330 core\n";
#endif
    const char *sources[2] = {prefix, desc->source};
    glShaderSource(shader, 2, sources, NULL);

    glCompileShader(shader);
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
    GLuint shader = (GLuint)backend_handle;
    glDeleteShader(shader);
}

uint32_t nt_gfx_backend_create_pipeline(const nt_pipeline_desc_t *desc, uint32_t vs_backend, uint32_t fs_backend) {
    /* Link program */
    GLuint program = glCreateProgram();
    glAttachShader(program, (GLuint)vs_backend);
    glAttachShader(program, (GLuint)fs_backend);
    glLinkProgram(program);

    GLint linked = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    if (!linked) {
        nt_gfx_gl_log_shader(vs_backend, "vertex");
        nt_gfx_gl_log_shader(fs_backend, "fragment");
        nt_gfx_gl_log_program(program);

        glDeleteProgram(program);
        return 0;
    }

    /* Auto-bind all registered global UBO blocks */
    {
        const nt_global_block_t *blocks;
        uint32_t block_count;
        nt_gfx_get_global_blocks(&blocks, &block_count);
        for (uint32_t bi = 0; bi < block_count; bi++) {
            GLuint block_index = glGetUniformBlockIndex(program, blocks[bi].name);
            if (block_index != GL_INVALID_INDEX) {
                glUniformBlockBinding(program, block_index, (GLuint)blocks[bi].binding_slot);
            }
        }
    }

    /* Find free pipeline slot */
    uint32_t slot = 0;
    for (uint32_t i = 1; i <= s_init_desc.max_pipelines; i++) {
        if (s_pipelines[i].vao == 0) {
            slot = i;
            break;
        }
    }
    if (slot == 0) {
        glDeleteProgram(program);
        return 0; /* no free slots */
    }

    /* Create VAO with vertex layout.
     * Only enable attributes here — actual glVertexAttribPointer calls happen
     * in bind_vertex_buffer() once a buffer is bound.  WebGL requires a bound
     * ARRAY_BUFFER for non-zero offsets, so calling it here would be an error. */
    GLuint vao;
    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);
    for (uint8_t i = 0; i < desc->layout.attr_count; i++) {
        glEnableVertexAttribArray(desc->layout.attrs[i].location);
    }
    for (uint8_t i = 0; i < desc->instance_layout.attr_count; i++) {
        GLuint loc = desc->instance_layout.attrs[i].location;
        glEnableVertexAttribArray(loc);
        glVertexAttribDivisor(loc, 1);
    }
    glBindVertexArray(0);
    s_gl_cache.vao = 0; /* create dirtied VAO binding */

    /* Store pipeline data */
    nt_gfx_gl_pipeline_t *pip = &s_pipelines[slot];
    pip->vao = vao;
    pip->program = program;
    pip->depth_test = desc->depth_test;
    pip->depth_write = desc->depth_write;
    pip->depth_func = map_depth_func(desc->depth_func);
    pip->cull_mode = desc->cull_mode;
    pip->blend = desc->blend;
    pip->blend_src = map_blend_factor(desc->blend_src);
    pip->blend_dst = map_blend_factor(desc->blend_dst);
    pip->polygon_offset = desc->polygon_offset;
    pip->po_factor = desc->polygon_offset_factor;
    pip->po_units = desc->polygon_offset_units;
    pip->layout = desc->layout;
    pip->instance_layout = desc->instance_layout;

    /* Cache active uniform locations (avoids glGetUniformLocation per frame) */
    pip->uniform_count = 0;
    GLint active_uniforms = 0;
    glGetProgramiv(program, GL_ACTIVE_UNIFORMS, &active_uniforms);
    for (GLint ui = 0; ui < active_uniforms && pip->uniform_count < NT_MAX_CACHED_UNIFORMS; ui++) {
        char uname[64];
        GLsizei ulen = 0;
        GLint usize = 0;
        GLenum utype = 0;
        glGetActiveUniform(program, (GLuint)ui, (GLsizei)sizeof(uname), &ulen, &usize, &utype, uname);
        GLint loc = glGetUniformLocation(program, uname);
        if (loc >= 0) {
            pip->uniforms[pip->uniform_count].name_hash = nt_hash32_str(uname).value;
            pip->uniforms[pip->uniform_count].location = loc;
            pip->uniform_count++;
        }
    }

    return slot;
}

void nt_gfx_backend_destroy_pipeline(uint32_t backend_handle) {
    if (backend_handle == 0 || backend_handle > s_init_desc.max_pipelines) {
        return;
    }
    nt_gfx_gl_pipeline_t *pip = &s_pipelines[backend_handle];
    /* Invalidate cache if this pipeline's GL objects are currently bound */
    if (pip->vao && s_gl_cache.vao == pip->vao) {
        s_gl_cache.vao = 0;
    }
    if (pip->program && s_gl_cache.program == pip->program) {
        s_gl_cache.program = 0;
    }
    if (pip->vao) {
        glDeleteVertexArrays(1, &pip->vao);
    }
    if (pip->program) {
        glDeleteProgram(pip->program);
    }
    memset(pip, 0, sizeof(*pip));
}

uint32_t nt_gfx_backend_create_buffer(const nt_buffer_desc_t *desc) {
    GLuint buf;
    glGenBuffers(1, &buf);
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
    glBindBuffer(target, buf);
    glBufferData(target, (GLsizeiptr)desc->size, desc->data, usage);

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
        return 0;
    }

    s_buffer_gl[slot] = buf;
    s_buffer_targets[slot] = target;
    return slot;
}

void nt_gfx_backend_destroy_buffer(uint32_t backend_handle) {
    if (backend_handle == 0 || backend_handle > s_init_desc.max_buffers) {
        return;
    }
    GLuint buf = s_buffer_gl[backend_handle];
    if (buf) {
        glDeleteBuffers(1, &buf);
    }
    s_buffer_gl[backend_handle] = 0;
    s_buffer_targets[backend_handle] = 0;
}

void nt_gfx_backend_update_buffer(uint32_t backend_handle, const void *data, uint32_t size) {
    if (backend_handle == 0 || backend_handle > s_init_desc.max_buffers) {
        return;
    }
    GLuint buf = s_buffer_gl[backend_handle];
    GLenum target = s_buffer_targets[backend_handle];
    glBindBuffer(target, buf);
    glBufferSubData(target, 0, (GLsizeiptr)size, data);
}

void nt_gfx_backend_bind_vertex_buffer(uint32_t backend_handle) {
    if (backend_handle == 0 || backend_handle > s_init_desc.max_buffers) {
        return;
    }
    GLuint buf = s_buffer_gl[backend_handle];
    glBindBuffer(GL_ARRAY_BUFFER, buf);

    /* Re-apply vertex attribute pointers from the currently bound pipeline.
     * VAOs record the buffer binding per attribute, so when a different
     * buffer is bound we must re-set the pointers. */
    if (s_bound_pipeline_slot != 0 && s_bound_pipeline_slot <= s_init_desc.max_pipelines) {
        const nt_vertex_layout_t *layout = &s_pipelines[s_bound_pipeline_slot].layout;
        for (uint8_t i = 0; i < layout->attr_count; i++) {
            const nt_vertex_attr_t *attr = &layout->attrs[i];
            GLint size;
            GLenum type;
            GLboolean normalized;
            get_format_params(attr->format, &size, &type, &normalized);
            glVertexAttribPointer(attr->location, size, type, normalized, (GLsizei)layout->stride,
                                  (void *)(uintptr_t)attr->offset); // NOLINT(performance-no-int-to-ptr)
        }
    }
}

void nt_gfx_backend_bind_index_buffer(uint32_t backend_handle) {
    if (backend_handle == 0 || backend_handle > s_init_desc.max_buffers) {
        return;
    }
    GLuint buf = s_buffer_gl[backend_handle];
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, buf);
}

void nt_gfx_backend_bind_instance_buffer(uint32_t backend_handle) {
    if (backend_handle == 0 || backend_handle > s_init_desc.max_buffers) {
        return;
    }
    GLuint buf = s_buffer_gl[backend_handle];
    s_instance_gl_buf = buf;
    glBindBuffer(GL_ARRAY_BUFFER, buf);

    /* Re-apply instance attribute pointers from the currently bound pipeline. */
    if (s_bound_pipeline_slot != 0 && s_bound_pipeline_slot <= s_init_desc.max_pipelines) {
        const nt_vertex_layout_t *layout = &s_pipelines[s_bound_pipeline_slot].instance_layout;
        for (uint8_t i = 0; i < layout->attr_count; i++) {
            const nt_vertex_attr_t *attr = &layout->attrs[i];
            GLint size;
            GLenum type;
            GLboolean normalized;
            get_format_params(attr->format, &size, &type, &normalized);
            glVertexAttribPointer(attr->location, size, type, normalized, (GLsizei)layout->stride,
                                  (void *)(uintptr_t)attr->offset); // NOLINT(performance-no-int-to-ptr)
        }
    }
}

void nt_gfx_backend_set_instance_offset(uint32_t byte_offset) {
    NT_ASSERT(s_instance_gl_buf != 0); /* must call bind_instance_buffer first */
    if (s_bound_pipeline_slot == 0 || s_bound_pipeline_slot > s_init_desc.max_pipelines) {
        return;
    }
    /* Re-bind instance buffer so glVertexAttribPointer captures the right source */
    glBindBuffer(GL_ARRAY_BUFFER, s_instance_gl_buf);
    const nt_vertex_layout_t *layout = &s_pipelines[s_bound_pipeline_slot].instance_layout;
    for (uint8_t i = 0; i < layout->attr_count; i++) {
        const nt_vertex_attr_t *attr = &layout->attrs[i];
        GLint size;
        GLenum type;
        GLboolean normalized;
        get_format_params(attr->format, &size, &type, &normalized);
        glVertexAttribPointer(attr->location, size, type, normalized, (GLsizei)layout->stride,
                              (void *)(uintptr_t)(attr->offset + byte_offset)); // NOLINT(performance-no-int-to-ptr)
    }
}

void nt_gfx_backend_set_vertex_attrib_default(uint8_t location, float x, float y, float z, float w) { glVertexAttrib4f((GLuint)location, x, y, z, w); }

/* ---- Uniform buffer ---- */

void nt_gfx_backend_bind_uniform_buffer(uint32_t backend_handle, uint32_t slot) {
    if (backend_handle == 0 || backend_handle > s_init_desc.max_buffers) {
        return;
    }
    GLuint buf = s_buffer_gl[backend_handle];
    glBindBufferBase(GL_UNIFORM_BUFFER, slot, buf);
}

void nt_gfx_backend_set_uniform_block(uint32_t pipeline_backend, const char *block_name, uint32_t slot) {
    if (pipeline_backend == 0 || pipeline_backend > s_init_desc.max_pipelines) {
        return;
    }
    GLuint program = s_pipelines[pipeline_backend].program;
    if (program == 0) {
        return;
    }
    GLuint block_index = glGetUniformBlockIndex(program, block_name);
    if (block_index != GL_INVALID_INDEX) {
        glUniformBlockBinding(program, block_index, slot);
    }
}

/* ---- Texture management ---- */

uint32_t nt_gfx_backend_create_texture(const nt_texture_desc_t *desc) {
    GLuint tex;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);

    /* Set filter and wrap parameters BEFORE uploading data */
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, (GLint)map_texture_filter(desc->min_filter));
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, (GLint)map_texture_filter(desc->mag_filter));
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, (GLint)map_texture_wrap(desc->wrap_u));
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, (GLint)map_texture_wrap(desc->wrap_v));

    /* Map pixel format to GL constants */
    GLenum internal_fmt = GL_RGBA8;
    GLenum pixel_fmt = GL_RGBA;
    GLenum pixel_type = GL_UNSIGNED_BYTE;
    switch (desc->format) {
    case NT_PIXEL_RGB8:
        internal_fmt = GL_RGB8;
        pixel_fmt = GL_RGB;
        pixel_type = GL_UNSIGNED_BYTE;
        break;
    case NT_PIXEL_RG8:
        internal_fmt = GL_RG8;
        pixel_fmt = GL_RG;
        pixel_type = GL_UNSIGNED_BYTE;
        break;
    case NT_PIXEL_R8:
        internal_fmt = GL_R8;
        pixel_fmt = GL_RED;
        pixel_type = GL_UNSIGNED_BYTE;
        break;
    case NT_PIXEL_RGBA8:
    default:
        internal_fmt = GL_RGBA8;
        pixel_fmt = GL_RGBA;
        pixel_type = GL_UNSIGNED_BYTE;
        break;
    }

    /* Set alignment for non-4-byte-aligned formats (RGB8=3, RG8=2, R8=1) */
    if (desc->format != NT_PIXEL_RGBA8) {
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    }

    /* Upload pixel data (may be NULL for context-loss recovery placeholder) */
    glTexImage2D(GL_TEXTURE_2D, 0, (GLint)internal_fmt, (GLsizei)desc->width, (GLsizei)desc->height, 0, pixel_fmt, pixel_type, desc->data);

    /* Restore default alignment */
    if (desc->format != NT_PIXEL_RGBA8) {
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    }

    /* Generate mipmaps after base level upload if requested and data present */
    if (desc->gen_mipmaps && desc->data) {
        glGenerateMipmap(GL_TEXTURE_2D);
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
        return 0;
    }

    s_texture_gl[slot] = tex;

    /* Invalidate cache: glBindTexture above dirtied the active unit's binding */
    uint32_t active_slot = s_gl_cache.active_texture - GL_TEXTURE0;
    if (active_slot < NT_GFX_MAX_TEXTURE_SLOTS) {
        s_gl_cache.bound_textures[active_slot] = 0;
    }

    return slot;
}

/* Per-mip transcode + compressed upload (Basis Universal) */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
uint32_t nt_gfx_backend_create_texture_compressed(const uint8_t *basis_data, uint32_t basis_size, uint32_t base_width, uint32_t base_height, uint32_t level_count, nt_texture_filter_t min_filter,
                                                  nt_texture_filter_t mag_filter, nt_texture_wrap_t wrap_u, nt_texture_wrap_t wrap_v, uint32_t transcode_target) {
    (void)base_width;
    (void)base_height;

    nt_basisu_format_t target = (nt_basisu_format_t)transcode_target;
    bool is_compressed = (target != NT_BASISU_FORMAT_RGBA32);
    uint32_t gl_internal = nt_basisu_gl_internal_format(target);
    uint32_t bpb = nt_basisu_bytes_per_block(target);

    GLuint tex;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, (GLint)map_texture_filter(min_filter));
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, (GLint)map_texture_filter(mag_filter));
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, (GLint)map_texture_wrap(wrap_u));
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, (GLint)map_texture_wrap(wrap_v));

    for (uint32_t level = 0; level < level_count; level++) {
        uint32_t lw = 0;
        uint32_t lh = 0;
        uint32_t total_blocks = 0;
        if (!nt_basisu_get_level_desc(basis_data, basis_size, level, &lw, &lh, &total_blocks)) {
            glDeleteTextures(1, &tex);
            return 0;
        }

        uint32_t output_size;
        if (is_compressed) {
            output_size = total_blocks * bpb;
        } else {
            /* RGBA32 fallback: 4 bytes per pixel */
            output_size = lw * lh * 4;
            total_blocks = lw * lh; /* for transcode_level: output_blocks = pixel count */
        }

        /* Allocate transcode output buffer (heap -- not hot path, only at load time) */
        uint8_t *output = (uint8_t *)malloc(output_size);
        if (!output) {
            glDeleteTextures(1, &tex);
            return 0;
        }

        if (!nt_basisu_transcode_level(basis_data, basis_size, level, output, total_blocks, target)) {
            free(output);
            glDeleteTextures(1, &tex);
            return 0;
        }

        if (is_compressed) {
            glCompressedTexImage2D(GL_TEXTURE_2D, (GLint)level, (GLenum)gl_internal, (GLsizei)lw, (GLsizei)lh, 0, (GLsizei)output_size, output);
        } else {
            /* RGBA32 fallback: regular upload */
            glTexImage2D(GL_TEXTURE_2D, (GLint)level, GL_RGBA8, (GLsizei)lw, (GLsizei)lh, 0, GL_RGBA, GL_UNSIGNED_BYTE, output);
        }

        free(output);
    }

    /* Find free texture slot (same pattern as existing create_texture) */
    uint32_t slot = 0;
    for (uint32_t i = 1; i <= s_init_desc.max_textures; i++) {
        if (s_texture_gl[i] == 0) {
            slot = i;
            break;
        }
    }
    if (slot == 0) {
        glDeleteTextures(1, &tex);
        return 0;
    }
    s_texture_gl[slot] = tex;

    /* Invalidate cache */
    uint32_t active_slot_c = s_gl_cache.active_texture - GL_TEXTURE0;
    if (active_slot_c < NT_GFX_MAX_TEXTURE_SLOTS) {
        s_gl_cache.bound_textures[active_slot_c] = 0;
    }

    return slot;
}

void nt_gfx_backend_destroy_texture(uint32_t backend_handle) {
    if (backend_handle == 0 || backend_handle > s_init_desc.max_textures) {
        return;
    }
    GLuint tex = s_texture_gl[backend_handle];
    if (tex) {
        /* Invalidate cache entries referencing this GL name */
        for (uint32_t i = 0; i < NT_GFX_MAX_TEXTURE_SLOTS; i++) {
            if (s_gl_cache.bound_textures[i] == tex) {
                s_gl_cache.bound_textures[i] = 0;
            }
        }
        glDeleteTextures(1, &tex);
    }
    s_texture_gl[backend_handle] = 0;
}

void nt_gfx_backend_bind_texture(uint32_t backend_handle, uint32_t slot) {
    if (backend_handle == 0 || backend_handle > s_init_desc.max_textures) {
        return;
    }
    GLuint tex = s_texture_gl[backend_handle];
    if (s_gl_cache.bound_textures[slot] == tex) {
        return; /* already bound to this slot */
    }
    GLenum unit = GL_TEXTURE0 + slot;
    if (s_gl_cache.active_texture != unit) {
        glActiveTexture(unit);
        s_gl_cache.active_texture = unit;
    }
    glBindTexture(GL_TEXTURE_2D, tex);
    s_gl_cache.bound_textures[slot] = tex;
}

void nt_gfx_backend_draw_instanced(uint32_t first_vertex, uint32_t num_vertices, uint32_t instance_count) {
    glDrawArraysInstanced(GL_TRIANGLES, (GLint)first_vertex, (GLsizei)num_vertices, (GLsizei)instance_count);
}

void nt_gfx_backend_draw_indexed_instanced(uint32_t first_index, uint32_t num_indices, uint32_t instance_count, uint8_t index_type) {
    GLenum gl_type = (index_type == 2) ? GL_UNSIGNED_INT : GL_UNSIGNED_SHORT;
    uint32_t stride = (index_type == 2) ? sizeof(uint32_t) : sizeof(uint16_t);
    glDrawElementsInstanced(GL_TRIANGLES, (GLsizei)num_indices, gl_type,
                            (void *)(uintptr_t)(first_index * stride), // NOLINT(performance-no-int-to-ptr)
                            (GLsizei)instance_count);
}

/* ---- Context loss recovery ---- */

bool nt_gfx_backend_recreate_all_resources(void) {
    /* Destroy old context and create a fresh one. */
    nt_gfx_gl_ctx_destroy();
    if (!nt_gfx_gl_ctx_create(&s_init_desc)) {
        return false;
    }

    /* Zero out all backend-side arrays -- old GL names are invalid. */
    if (s_pipelines) {
        memset(s_pipelines, 0, (s_init_desc.max_pipelines + 1) * sizeof(nt_gfx_gl_pipeline_t));
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
    s_bound_program = 0;
    s_bound_pipeline_slot = 0;
    nt_gfx_gl_cache_reset();
    return true;
}
