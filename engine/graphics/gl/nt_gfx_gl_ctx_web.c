#include "graphics/gl/nt_gfx_gl_ctx.h"

#include <emscripten.h>
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

/* Detect GPU compressed texture extensions via JavaScript.
 * gl.getExtension() both checks AND enables the extension.
 * Bit 0 = ASTC, Bit 1 = BC7/BPTC, Bit 2 = ETC2 */
// clang-format off
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wextra-semi"
EM_JS(int, nt_gfx_js_detect_gpu_caps, (void), {
    var gl = GL.currentContext ? GL.currentContext.GLctx : null;
    if (!gl) return 0;
    var caps = 0;
    if (gl.getExtension('WEBGL_compressed_texture_astc')) caps |= 1;
    if (gl.getExtension('EXT_texture_compression_bptc')) caps |= 2;
    if (gl.getExtension('WEBGL_compressed_texture_etc')) caps |= 4;
    return caps;
});
#pragma clang diagnostic pop
// clang-format on

nt_gfx_gpu_caps_t nt_gfx_gl_ctx_detect_gpu_caps(void) {
    int bits = nt_gfx_js_detect_gpu_caps();
    nt_gfx_gpu_caps_t caps = {0};
    caps.has_astc = (bits & 1) != 0;
    caps.has_bc7 = (bits & 2) != 0;
    caps.has_etc2 = (bits & 4) != 0;
    return caps;
}
