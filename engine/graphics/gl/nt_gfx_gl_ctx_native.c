#include "graphics/gl/nt_gfx_gl_ctx.h"
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <glad/gl.h>

/* Unconditional: nt_assert.h pulls core/nt_platform.h, which DEFINES NT_DEBUG — the guards
   below see it only if it is defined first. nt_log.h gives NT_LOG_ERROR. */
#include "core/nt_assert.h"
#include "log/nt_log.h"

bool nt_gfx_gl_ctx_create(const nt_gfx_desc_t *desc) {
    (void)desc;
    /* Window + GL context already created by nt_window_init().
       Load GL function pointers via glad. */
    if (!gladLoadGL(glfwGetProcAddress)) {
        return false;
    }
    return true;
}

void nt_gfx_gl_ctx_destroy(void) { /* GL context destroyed with GLFW window in nt_window_shutdown() */ }

bool nt_gfx_gl_ctx_is_lost(void) {
    /* Desktop GL contexts do not suffer context loss like WebGL */
    return false;
}

nt_gfx_gpu_caps_t nt_gfx_gl_ctx_detect_gpu_caps(void) {
    nt_gfx_gpu_caps_t caps = {0};
    caps.has_bc7 = GLAD_GL_ARB_texture_compression_bptc != 0;
    caps.has_astc = GLAD_GL_KHR_texture_compression_astc_ldr != 0;
    caps.has_etc2 = GLAD_GL_ARB_ES3_compatibility != 0;

    /* BC7 is core in GL 4.2+, ETC2 in GL 4.3+ — available without extensions */
    GLint major = 0;
    GLint minor = 0;
    glGetIntegerv(GL_MAJOR_VERSION, &major);
    glGetIntegerv(GL_MINOR_VERSION, &minor);
    int gl_ver = (major * 10) + minor;
    if (gl_ver >= 42) {
        caps.has_bc7 = true;
    }
    if (gl_ver >= 43) {
        caps.has_etc2 = true;
    }
    /* Float colour attachments are core in GL 3.0+; the engine already requires 3.3. */
    caps.has_float_render_target = gl_ver >= 30;

    GLint max_tex_size = 0;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &max_tex_size);
    caps.max_texture_size = (uint32_t)max_tex_size;

    return caps;
}

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

#ifdef NT_DEBUG
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
#ifdef NT_DEBUG
    if (GLAD_GL_KHR_debug == 0) {
        return false; /* driver lacks KHR_debug — silently no-op, mirrors enable_debug_groups. */
    }
    glEnable(GL_DEBUG_OUTPUT);
    glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS); /* deliver on the offending call's stack so a breakpoint lands right. */
    glDebugMessageCallback(nt_gl_debug_cb, NULL);
    /* Mute notification-severity chatter (buffer-mapping hints etc.) at the driver, not per-callback. */
    glDebugMessageControl(GL_DONT_CARE, GL_DONT_CARE, GL_DEBUG_SEVERITY_NOTIFICATION, 0, NULL, GL_FALSE);
    return true;
#else
    return false;
#endif
}
