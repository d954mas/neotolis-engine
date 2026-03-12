#include "graphics/gl/nt_gfx_gl_ctx.h"
#include <emscripten/html5_webgl.h>

static EMSCRIPTEN_WEBGL_CONTEXT_HANDLE s_gl_context;

bool nt_gfx_gl_ctx_create(const nt_gfx_desc_t *desc) {
    EmscriptenWebGLContextAttributes attrs;
    emscripten_webgl_init_context_attributes(&attrs);
    attrs.alpha = desc->alpha;
    attrs.depth = desc->depth;
    attrs.stencil = desc->stencil;
    attrs.antialias = desc->antialias;
    attrs.majorVersion = 2;
    attrs.minorVersion = 0;
    attrs.premultipliedAlpha = desc->premultiplied_alpha;
    attrs.preserveDrawingBuffer = false;

    s_gl_context = emscripten_webgl_create_context("#canvas", &attrs);
    if (s_gl_context <= 0) {
        return false;
    }
    emscripten_webgl_make_context_current(s_gl_context);
    return true;
}

void nt_gfx_gl_ctx_destroy(void) {
    if (s_gl_context > 0) {
        emscripten_webgl_destroy_context(s_gl_context);
        s_gl_context = 0;
    }
}

bool nt_gfx_gl_ctx_is_lost(void) { return s_gl_context <= 0 || emscripten_is_webgl_context_lost(s_gl_context) != 0; }
