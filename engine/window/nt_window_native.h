#ifndef NT_WINDOW_NATIVE_H
#define NT_WINDOW_NATIVE_H

/* Internal header for native platform backends.
   Provides the GLFW window handle owned by nt_window_native.c.
   Game code should NOT include this -- use nt_window.h instead. */

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

extern GLFWwindow *g_nt_glfw_window;

#endif /* NT_WINDOW_NATIVE_H */
