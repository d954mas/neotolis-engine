#include "graphics/gl/nt_gfx_gl_calls.h"
#include "graphics/gl/nt_gfx_gl_ctx.h"
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include "core/nt_assert.h"
#include "log/nt_log.h"

static void copy_device_string(char *out, size_t cap, GLenum name) {
    const char *text = (const char *)NT_GL(glGetString, name);
    size_t n = 0;
    while (text != NULL && text[n] != '\0' && n < cap - 1U) {
        out[n] = (char)(text[n] == '\r' || text[n] == '\n' ? ' ' : text[n]);
        n++;
    }
    out[n] = '\0';
}

bool nt_gfx_gl_ctx_create(const nt_gfx_desc_t *desc) {
    (void)desc;
    /* Window + GL context already created by nt_window_init().
       Load GL function pointers via glad. */
    if (!gladLoadGL(glfwGetProcAddress)) {
        NT_LOG_ERROR("GL-load: failed requested=3.3-core");
        return false;
    }
    copy_device_string(g_nt_gfx_device_info.vendor, sizeof(g_nt_gfx_device_info.vendor), GL_VENDOR);
    copy_device_string(g_nt_gfx_device_info.renderer, sizeof(g_nt_gfx_device_info.renderer), GL_RENDERER);
    copy_device_string(g_nt_gfx_device_info.api_version, sizeof(g_nt_gfx_device_info.api_version), GL_VERSION);
    return true;
}

void nt_gfx_gl_ctx_destroy(void) { /* GL context destroyed with GLFW window in nt_window_shutdown() */ }

/* Desktop GL contexts do not suffer context loss like WebGL */
bool nt_gfx_gl_ctx_take_loss(void) { return false; }

bool nt_gfx_gl_ctx_query_lost(void) { return false; }

static uint32_t gl_limit(GLenum name) {
    GLint value = 0;
    NT_GL(glGetIntegerv, name, &value);
    return (uint32_t)value;
}

nt_gfx_gpu_caps_t nt_gfx_gl_ctx_detect_gpu_caps(void) {
    nt_gfx_gpu_caps_t caps = {0};
    caps.has_bc7 = GLAD_GL_ARB_texture_compression_bptc != 0;
    caps.has_astc = GLAD_GL_KHR_texture_compression_astc_ldr != 0;
    caps.has_etc2 = GLAD_GL_ARB_ES3_compatibility != 0;
    caps.has_parallel_shader_compile = GLAD_GL_KHR_parallel_shader_compile != 0 || GLAD_GL_ARB_parallel_shader_compile != 0;

    /* BC7 is core in GL 4.2+, ETC2 in GL 4.3+ — available without extensions */
    GLint major = 0;
    GLint minor = 0;
    NT_GL(glGetIntegerv, GL_MAJOR_VERSION, &major);
    NT_GL(glGetIntegerv, GL_MINOR_VERSION, &minor);
    int gl_ver = (major * 10) + minor;
    if (gl_ver >= 42) {
        caps.has_bc7 = true;
    }
    if (gl_ver >= 43) {
        /* Core, but desktop drivers commonly decompress it on upload (NVIDIA does; Intel Broadwell+ is native): ETC1S blobs can then cost RGBA8 VRAM. */
        caps.has_etc2 = true;
    }
    /* Float colour attachments are core in GL 3.0+ — no extension needed */
    caps.has_float_render_target = gl_ver >= 30;
    caps.has_float_texture_linear = gl_ver >= 30;

    caps.max_texture_size = gl_limit(GL_MAX_TEXTURE_SIZE);
    caps.uniform_buffer_offset_alignment = gl_limit(GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT);
    caps.max_varying_components = gl_limit(GL_MAX_VARYING_COMPONENTS);
    caps.max_vertex_uniform_components = gl_limit(GL_MAX_VERTEX_UNIFORM_COMPONENTS);
    caps.max_fragment_uniform_components = gl_limit(GL_MAX_FRAGMENT_UNIFORM_COMPONENTS);
    caps.max_vertex_uniform_blocks = gl_limit(GL_MAX_VERTEX_UNIFORM_BLOCKS);
    caps.max_fragment_uniform_blocks = gl_limit(GL_MAX_FRAGMENT_UNIFORM_BLOCKS);
    caps.max_uniform_block_size = gl_limit(GL_MAX_UNIFORM_BLOCK_SIZE);

    return caps;
}

#if NT_GFX_GPU_TIMING_ENABLED
bool nt_gfx_gl_ctx_enable_timer_query(void) {
    /* GL_TIME_ELAPSED + glBeginQuery were promoted to core in GL 3.3, so on
     * desktop with a 3.3+ context the entry points are already loaded by
     * glad and need no extension activation. */
    return GLAD_GL_VERSION_3_3 != 0 || GLAD_GL_ARB_timer_query != 0;
}

bool nt_gfx_gl_ctx_enable_debug_groups(void) {
    /* glPushDebugGroup / glPopDebugGroup ship via the KHR_debug extension
     * (core in GL 4.3+, but our glad config only generates up to 3.3+ext).
     * KHR_debug is widely supported by modern desktop drivers; if absent
     * we no-op the labeling (segments still work, just unlabeled). */
    return GLAD_GL_KHR_debug != 0;
}

#endif

#if NT_GFX_NATIVE_GL_DEBUG
/* GLAD_API_PTR matches GLDEBUGPROC's calling convention (__stdcall on Windows); a plain
   function pointer would mismatch the stack on the driver's callback. */
static void GLAD_API_PTR nt_gl_debug_cb(GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei length, const GLchar *message, const void *user) {
    (void)source;
    (void)length;
    (void)user;
    NT_LOG_ERROR("GL debug type=0x%04X id=%u sev=0x%04X: %s", type, id, severity, message);
    NT_ASSERT(type != GL_DEBUG_TYPE_ERROR && "GL error — see log above");
}
#endif

bool nt_gfx_gl_ctx_enable_debug_callback(void) {
#if NT_GFX_NATIVE_GL_DEBUG
    if (GLAD_GL_KHR_debug == 0) {
        return false; /* driver lacks KHR_debug — silently no-op, mirrors enable_debug_groups. */
    }
    NT_GL(glEnable, GL_DEBUG_OUTPUT);
    NT_GL(glEnable, GL_DEBUG_OUTPUT_SYNCHRONOUS); /* deliver on the offending call's stack so a breakpoint lands right. */
    NT_GL(glDebugMessageCallback, nt_gl_debug_cb, NULL);
    /* Mute notification-severity chatter (buffer-mapping hints etc.) at the driver, not per-callback. */
    NT_GL(glDebugMessageControl, GL_DONT_CARE, GL_DONT_CARE, GL_DEBUG_SEVERITY_NOTIFICATION, 0, NULL, GL_FALSE);
    return true;
#else
    return false;
#endif
}
