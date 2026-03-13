#include "graphics/gl/nt_gfx_gl_ctx.h"

/* clang-format off */
#include <glad/gl.h>   /* Must precede glfw3.h -- glad replaces system GL header */
#include <GLFW/glfw3.h>
/* clang-format on */

/* GLFW window handle owned by nt_window_native.c */
extern GLFWwindow *g_nt_glfw_window;

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
