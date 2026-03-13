#include "core/nt_assert.h"
#include "log/nt_log.h"
#include "window/nt_window.h"

/* clang-format off */
#include <glad/gl.h>   /* Must precede glfw3.h -- glad replaces system GL header */
#include <GLFW/glfw3.h>
/* clang-format on */

/* ---- Global GLFW window handle (used by nt_app_native.c, nt_input_native.c, nt_gfx_gl_ctx_native.c) ---- */
GLFWwindow *g_nt_glfw_window = NULL;

/* ---- Pre-fullscreen size for restore ---- */
static int s_windowed_x;
static int s_windowed_y;
static int s_windowed_w;
static int s_windowed_h;

/* ---- Callbacks ---- */

static void fb_size_callback(GLFWwindow *window, int width, int height) {
    (void)window;
    float xscale = 1.0F;
    float yscale = 1.0F;
    glfwGetWindowContentScale(g_nt_glfw_window, &xscale, &yscale);

    g_nt_window.fb_width = (uint32_t)width;
    g_nt_window.fb_height = (uint32_t)height;
    g_nt_window.width = (xscale > 0.0F) ? (uint32_t)((float)width / xscale) : (uint32_t)width;
    g_nt_window.height = (yscale > 0.0F) ? (uint32_t)((float)height / yscale) : (uint32_t)height;
    g_nt_window.dpr = xscale;
}

/* ---- Lifecycle ---- */

void nt_window_init(void) {
    NT_ASSERT(g_nt_window.width > 0 && "g_nt_window.width must be set before nt_window_init()");
    NT_ASSERT(g_nt_window.height > 0 && "g_nt_window.height must be set before nt_window_init()");

    if (!glfwInit()) {
        nt_log_error("GLFW: glfwInit() failed");
        return;
    }

    /* GL 3.3 Core context */
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    /* Always resizable by default (CONTEXT.md decision) */
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

    const char *title = g_nt_window.title ? g_nt_window.title : "Neotolis";

    g_nt_glfw_window = glfwCreateWindow((int)g_nt_window.width, (int)g_nt_window.height, title, NULL, NULL);
    NT_ASSERT(g_nt_glfw_window && "GLFW: glfwCreateWindow() failed");

    glfwMakeContextCurrent(g_nt_glfw_window);

    /* Register framebuffer resize callback */
    glfwSetFramebufferSizeCallback(g_nt_glfw_window, fb_size_callback);

    /* Query initial framebuffer size and content scale */
    int fb_w = 0;
    int fb_h = 0;
    glfwGetFramebufferSize(g_nt_glfw_window, &fb_w, &fb_h);

    float xscale = 1.0F;
    float yscale = 1.0F;
    glfwGetWindowContentScale(g_nt_glfw_window, &xscale, &yscale);

    g_nt_window.fb_width = (uint32_t)fb_w;
    g_nt_window.fb_height = (uint32_t)fb_h;
    g_nt_window.dpr = xscale;

    /* Save initial windowed position/size for fullscreen restore */
    glfwGetWindowPos(g_nt_glfw_window, &s_windowed_x, &s_windowed_y);
    s_windowed_w = (int)g_nt_window.width;
    s_windowed_h = (int)g_nt_window.height;
}

void nt_window_poll(void) { /* No-op on desktop: resize handled via callback */ }

void nt_window_shutdown(void) {
    if (g_nt_glfw_window) {
        glfwDestroyWindow(g_nt_glfw_window);
        g_nt_glfw_window = NULL;
    }
    glfwTerminate();
}

void nt_window_set_fullscreen(bool fullscreen) {
    if (!g_nt_glfw_window) {
        return;
    }

    if (fullscreen) {
        /* Save current windowed position/size */
        glfwGetWindowPos(g_nt_glfw_window, &s_windowed_x, &s_windowed_y);
        glfwGetWindowSize(g_nt_glfw_window, &s_windowed_w, &s_windowed_h);

        GLFWmonitor *monitor = glfwGetPrimaryMonitor();
        const GLFWvidmode *mode = glfwGetVideoMode(monitor);
        glfwSetWindowMonitor(g_nt_glfw_window, monitor, 0, 0, mode->width, mode->height, mode->refreshRate);
    } else {
        /* Restore windowed mode */
        glfwSetWindowMonitor(g_nt_glfw_window, NULL, s_windowed_x, s_windowed_y, s_windowed_w, s_windowed_h, 0);
    }
}
