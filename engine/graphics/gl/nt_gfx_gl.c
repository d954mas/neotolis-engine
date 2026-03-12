/*
 * Unified OpenGL backend for nt_gfx.
 *
 * Covers WebGL 2 (via Emscripten / GLES 3.0) and OpenGL 3.3 Core (native
 * desktop).  The two APIs share >95% of their surface; the handful of
 * differences are isolated behind #ifdef NT_PLATFORM_WEB.
 *
 * Platform differences:
 *   - Headers:  GLES3/gl3.h + emscripten/html5_webgl.h  vs  glad/gl.h
 *   - Context create/destroy:  emscripten_webgl_*  vs  no-op (window layer)
 *   - Context loss detection:  emscripten_is_webgl_context_lost  vs  false
 *   - glClearDepthf (ES) vs glClearDepth (desktop)
 *
 * Everything else (shaders, buffers, VAOs, pipelines, uniforms, draw calls)
 * uses identical GL calls on both paths.
 */

#include "core/nt_platform.h"
#include "graphics/nt_gfx_internal.h"
#include "window/nt_window.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* ---- Platform-specific GL headers ---- */

#ifdef NT_PLATFORM_WEB
#include <GLES3/gl3.h>
#include <emscripten/html5_webgl.h>
#else
/*
 * Desktop GL 3.3 Core.
 *
 * A proper GL loader (e.g. glad) is required to resolve GL function pointers
 * on desktop.  For now the project does not ship one -- this header satisfies
 * the compiler so the file compiles on native, while all tests link the stub
 * backend (no GL dependency).  A glad single-header will be vendored when
 * desktop rendering is enabled.
 */
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif
#include <GL/gl.h>

/* GL 3.3 Core constants / typedefs that may be absent from legacy gl.h.
 * These are only needed to let the file *compile* on native; actual
 * rendering will go through glad once vendored. */
#ifndef GL_ARRAY_BUFFER
#define GL_ARRAY_BUFFER 0x8892
#endif
#ifndef GL_ELEMENT_ARRAY_BUFFER
#define GL_ELEMENT_ARRAY_BUFFER 0x8893
#endif
#ifndef GL_STATIC_DRAW
#define GL_STATIC_DRAW 0x88E4
#endif
#ifndef GL_DYNAMIC_DRAW
#define GL_DYNAMIC_DRAW 0x88E8
#endif
#ifndef GL_STREAM_DRAW
#define GL_STREAM_DRAW 0x88E0
#endif
#ifndef GL_FRAGMENT_SHADER
#define GL_FRAGMENT_SHADER 0x8B30
#endif
#ifndef GL_VERTEX_SHADER
#define GL_VERTEX_SHADER 0x8B31
#endif
#ifndef GL_LINK_STATUS
#define GL_LINK_STATUS 0x8B82
#endif
#ifndef GL_INFO_LOG_LENGTH
#define GL_INFO_LOG_LENGTH 0x8B84
#endif

typedef char GLchar;
typedef ptrdiff_t GLsizeiptr;

/* Function pointer declarations for GL 2.0+ / 3.3 Core entry points that
 * are not in the base gl.h shipped by Windows / Mesa.  When glad is
 * vendored these will be provided by the loader. */
#define NT_GL_STUB_DECL
#ifdef NT_GL_STUB_DECL

/* Shader / program */
static GLuint (*glCreateShader)(GLenum) = NULL;
static void (*glDeleteShader)(GLuint) = NULL;
static void (*glShaderSource)(GLuint, GLsizei, const GLchar *const *, const GLint *) = NULL;
static void (*glCompileShader)(GLuint) = NULL;
static void (*glGetShaderInfoLog)(GLuint, GLsizei, GLsizei *, GLchar *) = NULL;
static GLuint (*glCreateProgram)(void) = NULL;
static void (*glDeleteProgram)(GLuint) = NULL;
static void (*glAttachShader)(GLuint, GLuint) = NULL;
static void (*glLinkProgram)(GLuint) = NULL;
static void (*glGetProgramiv)(GLuint, GLenum, GLint *) = NULL;
static void (*glGetProgramInfoLog)(GLuint, GLsizei, GLsizei *, GLchar *) = NULL;
static void (*glUseProgram)(GLuint) = NULL;

/* Uniforms */
static GLint (*glGetUniformLocation)(GLuint, const GLchar *) = NULL;
static void (*glUniformMatrix4fv)(GLint, GLsizei, GLboolean, const GLfloat *) = NULL;
static void (*glUniform4fv)(GLint, GLsizei, const GLfloat *) = NULL;
static void (*glUniform1f)(GLint, GLfloat) = NULL;
static void (*glUniform1i)(GLint, GLint) = NULL;

/* Buffers */
static void (*glGenBuffers)(GLsizei, GLuint *) = NULL;
static void (*glDeleteBuffers)(GLsizei, const GLuint *) = NULL;
static void (*glBindBuffer)(GLenum, GLuint) = NULL;
static void (*glBufferData)(GLenum, GLsizeiptr, const void *, GLenum) = NULL;
static void (*glBufferSubData)(GLenum, GLsizeiptr, GLsizeiptr, const void *) = NULL;

/* VAO */
static void (*glGenVertexArrays)(GLsizei, GLuint *) = NULL;
static void (*glDeleteVertexArrays)(GLsizei, const GLuint *) = NULL;
static void (*glBindVertexArray)(GLuint) = NULL;
static void (*glEnableVertexAttribArray)(GLuint) = NULL;
static void (*glVertexAttribPointer)(GLuint, GLint, GLenum, GLboolean, GLsizei, const void *) = NULL;

#endif /* NT_GL_STUB_DECL */

/* Desktop GL uses glClearDepth (double) while GLES/WebGL uses glClearDepthf (float). */
#define nt_gl_clear_depth(d) glClearDepth((double)(d))

#endif /* !NT_PLATFORM_WEB */

/* ---- WebGL-specific alias ---- */

#ifdef NT_PLATFORM_WEB
#define nt_gl_clear_depth(d) glClearDepthf(d)
#endif

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
    /* Store layout for re-applying vertex attrib pointers on buffer bind */
    nt_vertex_layout_t layout;
} nt_gfx_gl_pipeline_t;

/* ---- File-scope state ---- */

#ifdef NT_PLATFORM_WEB
static EMSCRIPTEN_WEBGL_CONTEXT_HANDLE s_gl_context;
#endif

static GLuint s_bound_program;         /* currently bound GL program (for uniforms) */
static uint32_t s_bound_pipeline_slot; /* currently bound pipeline index */

static GLuint *s_shader_gl;               /* GL shader names, indexed by slot */
static nt_gfx_gl_pipeline_t *s_pipelines; /* pipeline data, indexed by slot */
static GLuint *s_buffer_gl;               /* GL buffer names, indexed by slot */
static GLenum *s_buffer_targets;          /* GL_ARRAY_BUFFER or GL_ELEMENT_ARRAY_BUFFER */

static uint32_t s_max_shaders;
static uint32_t s_max_pipelines;
static uint32_t s_max_buffers;
static uint32_t s_next_pipeline_slot;

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

/* ---- Platform context helpers ---- */

#ifdef NT_PLATFORM_WEB

static bool create_gl_context(void) {
    EmscriptenWebGLContextAttributes attrs;
    emscripten_webgl_init_context_attributes(&attrs);
    attrs.alpha = false;
    attrs.depth = true;
    attrs.stencil = true;
    attrs.antialias = false;
    attrs.majorVersion = 2;
    attrs.minorVersion = 0;
    attrs.premultipliedAlpha = true;
    attrs.preserveDrawingBuffer = false;

    s_gl_context = emscripten_webgl_create_context("#canvas", &attrs);
    if (s_gl_context <= 0) {
        /* Fatal: cannot proceed without a GL context. */
        emscripten_force_exit(1);
        return false;
    }
    emscripten_webgl_make_context_current(s_gl_context);
    return true;
}

static void destroy_gl_context(void) {
    if (s_gl_context > 0) {
        emscripten_webgl_destroy_context(s_gl_context);
        s_gl_context = 0;
    }
}

#else /* native desktop */

static bool create_gl_context(void) {
    /* On desktop the window layer (GLFW, SDL, etc.) creates the GL context.
     * The GL backend assumes a current context already exists.
     * When glad is vendored, gladLoadGL() will be called here. */
    return true;
}

static void destroy_gl_context(void) { /* No-op: window layer owns the context. */ }

#endif /* NT_PLATFORM_WEB */

/* ==== Backend interface implementation ==== */

void nt_gfx_backend_init(const nt_gfx_desc_t *desc) {
    if (!create_gl_context()) {
        return;
    }

    /* Initial GL state */
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);

    /* Allocate backend resource arrays (+1 because slots are 1-based) */
    s_max_shaders = (desc && desc->max_shaders) ? desc->max_shaders : 32;
    s_max_pipelines = (desc && desc->max_pipelines) ? desc->max_pipelines : 16;
    s_max_buffers = (desc && desc->max_buffers) ? desc->max_buffers : 128;

    s_shader_gl = (GLuint *)calloc(s_max_shaders + 1, sizeof(GLuint));
    s_pipelines = (nt_gfx_gl_pipeline_t *)calloc(s_max_pipelines + 1, sizeof(nt_gfx_gl_pipeline_t));
    s_buffer_gl = (GLuint *)calloc(s_max_buffers + 1, sizeof(GLuint));
    s_buffer_targets = (GLenum *)calloc(s_max_buffers + 1, sizeof(GLenum));

    s_next_pipeline_slot = 1;
    s_bound_program = 0;
    s_bound_pipeline_slot = 0;
}

void nt_gfx_backend_shutdown(void) {
    free(s_shader_gl);
    free(s_pipelines);
    free(s_buffer_gl);
    free(s_buffer_targets);

    s_shader_gl = NULL;
    s_pipelines = NULL;
    s_buffer_gl = NULL;
    s_buffer_targets = NULL;

    s_bound_program = 0;
    s_bound_pipeline_slot = 0;
    s_next_pipeline_slot = 0;

    destroy_gl_context();
}

bool nt_gfx_backend_is_context_lost(void) {
#ifdef NT_PLATFORM_WEB
    return emscripten_is_webgl_context_lost(s_gl_context) != 0;
#else
    return false;
#endif
}

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

void nt_gfx_backend_end_pass(void) {
    glBindVertexArray(0);
    s_bound_program = 0;
    s_bound_pipeline_slot = 0;
}

/* ---- Pipeline bind ---- */

void nt_gfx_backend_bind_pipeline(uint32_t backend_handle) {
    if (backend_handle == 0 || backend_handle > s_max_pipelines) {
        return;
    }
    nt_gfx_gl_pipeline_t *pip = &s_pipelines[backend_handle];

    glBindVertexArray(pip->vao);
    glUseProgram(pip->program);
    s_bound_program = pip->program;
    s_bound_pipeline_slot = backend_handle;

    /* Depth test */
    if (pip->depth_test) {
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(pip->depth_func);
    } else {
        glDisable(GL_DEPTH_TEST);
    }
    glDepthMask(pip->depth_write ? GL_TRUE : GL_FALSE);

    /* Cull face */
    if (pip->cull_face) {
        glEnable(GL_CULL_FACE);
        glCullFace(GL_BACK);
    } else {
        glDisable(GL_CULL_FACE);
    }

    /* Blend */
    if (pip->blend) {
        glEnable(GL_BLEND);
        glBlendFunc(pip->blend_src, pip->blend_dst);
    } else {
        glDisable(GL_BLEND);
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
    glShaderSource(shader, 1, &desc->source, NULL);
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
        char info[1024];
        /* Log vertex shader errors */
        glGetShaderInfoLog((GLuint)vs_backend, (GLsizei)sizeof(info), NULL, info);
        (void)info; /* TODO: pipe through nt_log */
        /* Log fragment shader errors */
        glGetShaderInfoLog((GLuint)fs_backend, (GLsizei)sizeof(info), NULL, info);
        (void)info;
        /* Log program link errors */
        glGetProgramInfoLog(program, (GLsizei)sizeof(info), NULL, info);
        (void)info;

        glDeleteProgram(program);
        return 0;
    }

    /* Find free pipeline slot */
    uint32_t slot = 0;
    for (uint32_t i = 1; i <= s_max_pipelines; i++) {
        if (s_pipelines[i].vao == 0) {
            slot = i;
            break;
        }
    }
    if (slot == 0) {
        glDeleteProgram(program);
        return 0; /* no free slots */
    }

    /* Create VAO with vertex layout */
    GLuint vao;
    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);
    for (uint8_t i = 0; i < desc->layout.attr_count; i++) {
        const nt_vertex_attr_t *attr = &desc->layout.attrs[i];
        GLint size;
        GLenum type;
        GLboolean normalized;
        get_format_params(attr->format, &size, &type, &normalized);
        glEnableVertexAttribArray(attr->location);
        glVertexAttribPointer(attr->location, size, type, normalized, (GLsizei)desc->layout.stride,
                              (void *)(uintptr_t)attr->offset); // NOLINT(performance-no-int-to-ptr)
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
    pip->layout = desc->layout;

    return slot;
}

void nt_gfx_backend_destroy_pipeline(uint32_t backend_handle) {
    if (backend_handle == 0 || backend_handle > s_max_pipelines) {
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
    glBindBuffer(target, 0);

    /* Store the target for correct bind in update_buffer */
    /* Use the GL buffer name as backend handle directly.
     * We also need to track which slot it maps to for s_buffer_targets.
     * Since the shared pool handles slot tracking, and we return the GL
     * name as the backend handle, we store the target in a parallel
     * array indexed by GL buffer name -- but GL names can be large.
     * Alternative: use the buffer pool slot approach like pipelines.
     * Simplest: store a target lookup in s_buffer_targets indexed by
     * a sequential counter. */

    /* Find free buffer slot */
    uint32_t slot = 0;
    for (uint32_t i = 1; i <= s_max_buffers; i++) {
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
    if (backend_handle == 0 || backend_handle > s_max_buffers) {
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
    if (backend_handle == 0 || backend_handle > s_max_buffers) {
        return;
    }
    GLuint buf = s_buffer_gl[backend_handle];
    GLenum target = s_buffer_targets[backend_handle];
    glBindBuffer(target, buf);
    glBufferSubData(target, 0, (GLsizeiptr)size, data);
    glBindBuffer(target, 0);
}

void nt_gfx_backend_bind_vertex_buffer(uint32_t backend_handle) {
    if (backend_handle == 0 || backend_handle > s_max_buffers) {
        return;
    }
    GLuint buf = s_buffer_gl[backend_handle];
    glBindBuffer(GL_ARRAY_BUFFER, buf);

    /* Re-apply vertex attribute pointers from the currently bound pipeline.
     * VAOs record the buffer binding per attribute, so when a different
     * buffer is bound we must re-set the pointers. */
    if (s_bound_pipeline_slot != 0 && s_bound_pipeline_slot <= s_max_pipelines) {
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
    if (backend_handle == 0 || backend_handle > s_max_buffers) {
        return;
    }
    GLuint buf = s_buffer_gl[backend_handle];
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, buf);
}

/* ---- Context loss recovery ---- */

void nt_gfx_backend_recreate_all_resources(void) {
#ifdef NT_PLATFORM_WEB
    /* Destroy old context and create a fresh one. */
    destroy_gl_context();
    create_gl_context();
#endif

    /* Re-enable initial GL state. */
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);

    /* Zero out all backend-side arrays -- old GL names are invalid. */
    if (s_shader_gl) {
        memset(s_shader_gl, 0, (s_max_shaders + 1) * sizeof(GLuint));
    }
    if (s_pipelines) {
        memset(s_pipelines, 0, (s_max_pipelines + 1) * sizeof(nt_gfx_gl_pipeline_t));
    }
    if (s_buffer_gl) {
        memset(s_buffer_gl, 0, (s_max_buffers + 1) * sizeof(GLuint));
    }
    if (s_buffer_targets) {
        memset(s_buffer_targets, 0, (s_max_buffers + 1) * sizeof(GLenum));
    }
    s_bound_program = 0;
    s_bound_pipeline_slot = 0;
}
