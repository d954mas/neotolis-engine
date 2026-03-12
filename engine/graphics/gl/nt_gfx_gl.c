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
#include "log/nt_log.h"
#include "window/nt_window.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* ---- GL headers ---- */

#include <GLES3/gl3.h>

/* WebGL 2 / GLES 3.0 uses glClearDepthf.  When desktop GL is added via glad,
   this will need a desktop variant (glClearDepth with double). */
#define nt_gl_clear_depth(d) glClearDepthf(d)

/* ---- Pipeline backend data ---- */

typedef struct {
    GLuint vao;
    GLuint program;
    bool depth_test;
    bool depth_write;
    GLenum depth_func;
    bool cull_face;
    bool blend;
    GLenum blend_src;
    GLenum blend_dst;
    bool polygon_offset;
    float po_factor;
    float po_units;
    /* Store layout for re-applying vertex attrib pointers on buffer bind */
    nt_vertex_layout_t layout;
} nt_gfx_gl_pipeline_t;

/* ---- File-scope state ---- */

static GLuint s_bound_program;         /* currently bound GL program (for uniforms) */
static uint32_t s_bound_pipeline_slot; /* currently bound pipeline index */

static nt_gfx_gl_pipeline_t *s_pipelines; /* pipeline data, indexed by slot */
static GLuint *s_buffer_gl;               /* GL buffer names, indexed by slot */
static GLenum *s_buffer_targets;          /* GL_ARRAY_BUFFER or GL_ELEMENT_ARRAY_BUFFER */

static nt_gfx_desc_t s_init_desc; /* resolved desc: defaults applied, used everywhere */

/* ---- GL state cache (skip redundant JS interop calls) ---- */

static struct {
    GLuint vao;
    GLuint program;
    bool depth_test;
    bool depth_write;
    GLenum depth_func;
    bool cull_face;
    bool blend;
    GLenum blend_src;
    GLenum blend_dst;
    bool polygon_offset;
    float po_factor;
    float po_units;
} s_gl_cache;

static void nt_gfx_gl_cache_reset(void) {
    s_gl_cache.vao = 0;
    s_gl_cache.program = 0;
    s_gl_cache.depth_test = false;
    s_gl_cache.depth_write = true;   /* GL default: depth write enabled */
    s_gl_cache.depth_func = GL_LESS; /* GL default */
    s_gl_cache.cull_face = false;
    s_gl_cache.blend = false;
    s_gl_cache.blend_src = GL_ONE;  /* GL default */
    s_gl_cache.blend_dst = GL_ZERO; /* GL default */
    s_gl_cache.polygon_offset = false;
    s_gl_cache.po_factor = 0.0F;
    s_gl_cache.po_units = 0.0F;
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
    case NT_FORMAT_UBYTE4N:
        *size = 4;
        *type = GL_UNSIGNED_BYTE;
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

    s_bound_program = 0;
    s_bound_pipeline_slot = 0;
    nt_gfx_gl_cache_reset();
    return true;
}

void nt_gfx_backend_shutdown(void) {
    free(s_pipelines);
    free(s_buffer_gl);
    free(s_buffer_targets);

    s_pipelines = NULL;
    s_buffer_gl = NULL;
    s_buffer_targets = NULL;

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

    /* Cull face */
    if (s_gl_cache.cull_face != pip->cull_face) {
        if (pip->cull_face) {
            glEnable(GL_CULL_FACE);
            glCullFace(GL_BACK);
        } else {
            glDisable(GL_CULL_FACE);
        }
        s_gl_cache.cull_face = pip->cull_face;
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
    GLint loc = glGetUniformLocation(s_bound_program, name);
    if (loc >= 0) {
        glUniformMatrix4fv(loc, 1, GL_FALSE, matrix);
    }
}

void nt_gfx_backend_set_uniform_vec4(const char *name, const float *vec) {
    GLint loc = glGetUniformLocation(s_bound_program, name);
    if (loc >= 0) {
        glUniform4fv(loc, 1, vec);
    }
}

void nt_gfx_backend_set_uniform_float(const char *name, float val) {
    GLint loc = glGetUniformLocation(s_bound_program, name);
    if (loc >= 0) {
        glUniform1f(loc, val);
    }
}

void nt_gfx_backend_set_uniform_int(const char *name, int val) {
    GLint loc = glGetUniformLocation(s_bound_program, name);
    if (loc >= 0) {
        glUniform1i(loc, val);
    }
}

/* ---- Draw calls ---- */

/* TODO: support uint32 indices (GL_UNSIGNED_INT) via nt_index_type_t in nt_buffer_desc_t */
void nt_gfx_backend_draw(uint32_t first_element, uint32_t num_elements, bool indexed) {
    if (indexed) {
        glDrawElements(GL_TRIANGLES, (GLsizei)num_elements, GL_UNSIGNED_SHORT,
                       (void *)(uintptr_t)(first_element * sizeof(uint16_t))); // NOLINT(performance-no-int-to-ptr)
    } else {
        glDrawArrays(GL_TRIANGLES, (GLint)first_element, (GLsizei)num_elements);
    }
}

/* ---- Resource management (shader / buffer / pipeline) ---- */

uint32_t nt_gfx_backend_create_shader(const nt_shader_desc_t *desc) {
    GLenum gl_type = (desc->type == NT_SHADER_VERTEX) ? GL_VERTEX_SHADER : GL_FRAGMENT_SHADER;
    GLuint shader = glCreateShader(gl_type);

    /* TODO(builder): remove prefix once builder packs per-platform shader variants.
       TODO(glad): add desktop "#version 330 core\n" variant when native GL is enabled. */
    const char *prefix = "#version 300 es\nprecision mediump float;\n";
    const char *sources[2] = {prefix, desc->source};
    glShaderSource(shader, 2, sources, NULL);

    glCompileShader(shader);
    /* Per MDN best practice: do NOT check GL_COMPILE_STATUS here.
     * Checking forces synchronous compilation.  Errors surface at link time. */
    return (uint32_t)shader;
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
        char info[512];
        glGetShaderInfoLog((GLuint)vs_backend, (GLsizei)sizeof(info), NULL, info);
        nt_log_error(info);
        glGetShaderInfoLog((GLuint)fs_backend, (GLsizei)sizeof(info), NULL, info);
        nt_log_error(info);
        glGetProgramInfoLog(program, (GLsizei)sizeof(info), NULL, info);
        nt_log_error(info);

        glDeleteProgram(program);
        return 0;
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
    glBindVertexArray(0);

    /* Store pipeline data */
    nt_gfx_gl_pipeline_t *pip = &s_pipelines[slot];
    pip->vao = vao;
    pip->program = program;
    pip->depth_test = desc->depth_test;
    pip->depth_write = desc->depth_write;
    pip->depth_func = map_depth_func(desc->depth_func);
    pip->cull_face = desc->cull_face;
    pip->blend = desc->blend;
    pip->blend_src = map_blend_factor(desc->blend_src);
    pip->blend_dst = map_blend_factor(desc->blend_dst);
    pip->polygon_offset = desc->polygon_offset;
    pip->po_factor = desc->polygon_offset_factor;
    pip->po_units = desc->polygon_offset_units;
    pip->layout = desc->layout;

    return slot;
}

void nt_gfx_backend_destroy_pipeline(uint32_t backend_handle) {
    if (backend_handle == 0 || backend_handle > s_init_desc.max_pipelines) {
        return;
    }
    nt_gfx_gl_pipeline_t *pip = &s_pipelines[backend_handle];
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
    GLenum target = (desc->type == NT_BUFFER_VERTEX) ? GL_ARRAY_BUFFER : GL_ELEMENT_ARRAY_BUFFER;
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
    s_bound_program = 0;
    s_bound_pipeline_slot = 0;
    nt_gfx_gl_cache_reset();
    return true;
}
