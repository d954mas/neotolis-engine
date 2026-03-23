#include "graphics/gl/nt_gfx_gl_ctx.h"
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <glad/gl.h>

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
    /* ETC2 is core in GL 4.3+; for GL 3.3, check GL_ARB_ES3_compatibility */
    caps.has_etc2 = GLAD_GL_ARB_ES3_compatibility != 0;
    return caps;
}
