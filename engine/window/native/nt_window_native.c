#include "window/nt_window_native.h"
#include "log/nt_log.h"
#include "window/nt_window.h"

GLFWwindow *g_nt_glfw_window = NULL;

/* ---- Pre-fullscreen size for restore ---- */
static int s_windowed_x;
static int s_windowed_y;
static int s_windowed_w;
static int s_windowed_h;

/* ---- Helpers ---- */

static void sync_sizes(void) {
    int win_w = 0;
    int win_h = 0;
    glfwGetWindowSize(g_nt_glfw_window, &win_w, &win_h);

    int fb_w = 0;
    int fb_h = 0;
    glfwGetFramebufferSize(g_nt_glfw_window, &fb_w, &fb_h);

    /* Derive DPR from actual framebuffer/logical ratio, not content scale.
       Content scale may not reflect the real framebuffer size on Windows
       when the process lacks DPI awareness. */
    float device_dpr = (win_w > 0) ? (float)fb_w / (float)win_w : 1.0F;
    nt_window_apply_sizes((float)win_w, (float)win_h, device_dpr);
}

/* ---- Callbacks ---- */

static void fb_size_callback(GLFWwindow *window, int width, int height) {
    (void)window;
    (void)width;
    (void)height;
    sync_sizes();
}

/* ---- Lifecycle ---- */

void nt_window_init(void) {
    if (g_nt_window.width == 0 || g_nt_window.height == 0) {
        nt_log_error("g_nt_window width/height must be set before nt_window_init()");
        __builtin_trap();
    }

    if (!glfwInit()) {
        nt_log_error("GLFW: glfwInit() failed");
        __builtin_trap();
    }

    /* GL 3.3 Core context */
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_RESIZABLE, g_nt_window.resizable ? GLFW_TRUE : GLFW_FALSE);

    const char *title = g_nt_window.title ? g_nt_window.title : "Neotolis";

    g_nt_glfw_window = glfwCreateWindow((int)g_nt_window.width, (int)g_nt_window.height, title, NULL, NULL);
    if (!g_nt_glfw_window) {
        nt_log_error("GLFW: glfwCreateWindow() failed");
        glfwTerminate();
        __builtin_trap();
    }

    glfwMakeContextCurrent(g_nt_glfw_window);

    /* Register framebuffer resize callback */
    glfwSetFramebufferSizeCallback(g_nt_glfw_window, fb_size_callback);

    /* Query initial sizes via shared DPR logic (respects max_dpr) */
    sync_sizes();

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
