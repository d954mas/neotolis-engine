#include "graphics/gl/nt_gfx_gl_calls.h"
#include "graphics/gl/nt_gfx_gl_ctx.h"

#include <emscripten.h>
#include <emscripten/html5.h>
#include <emscripten/html5_webgl.h>

/* The cap-probe EM_JS bodies below reach the Emscripten GL registry (GL.currentContext.GLctx). GL lives
 * in library_webgl.js, force-linked by this TU's own emscripten_webgl_* C calls -- so it survives today.
 * EM_JS_DEPS makes that implicit dependency explicit + Closure-kept, so moving context creation out of
 * this file can't silently strip GL in release. */
EM_JS_DEPS(nt_gfx_gl_ctx_web, "$GL,$UTF8ToString")

static EMSCRIPTEN_WEBGL_CONTEXT_HANDLE s_gl_context;
static bool s_loss_pending; /* set by a lost event until begin_frame acknowledges it */
static bool s_lost;

static bool on_context_lost(int event_type, const void *reserved, void *user_data) {
    (void)event_type;
    (void)reserved;
    (void)user_data;
    s_loss_pending = true;
    s_lost = true;
    return true; /* preventDefault: the browser restores only a context whose loss was handled */
}

static bool on_context_restored(int event_type, const void *reserved, void *user_data) {
    (void)event_type;
    (void)reserved;
    (void)user_data;
    s_lost = false;
    return true;
}

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
    /* Capability probes enable only the extensions this build uses. */
    attrs.enableExtensionsByDefault = false;

    s_gl_context = emscripten_webgl_create_context("#canvas", &attrs);
    if (s_gl_context <= 0) {
        return false;
    }
    emscripten_webgl_make_context_current(s_gl_context);
    s_loss_pending = false;
    s_lost = false;
    emscripten_set_webglcontextlost_callback("#canvas", NULL, false, on_context_lost);
    emscripten_set_webglcontextrestored_callback("#canvas", NULL, false, on_context_restored);
    return true;
}

void nt_gfx_gl_ctx_destroy(void) {
    emscripten_set_webglcontextlost_callback("#canvas", NULL, false, NULL);
    emscripten_set_webglcontextrestored_callback("#canvas", NULL, false, NULL);
    if (s_gl_context > 0) {
        emscripten_webgl_destroy_context(s_gl_context);
        s_gl_context = 0;
    }
}

bool nt_gfx_gl_ctx_is_lost(void) { return s_gl_context <= 0 || s_loss_pending || s_lost; }

void nt_gfx_gl_ctx_ack_loss(void) { s_loss_pending = false; }

bool nt_gfx_gl_ctx_query_lost(void) {
    if (s_gl_context > 0 && emscripten_is_webgl_context_lost(s_gl_context) != 0) {
        s_loss_pending = true;
        s_lost = true;
    }
    return nt_gfx_gl_ctx_is_lost();
}

/* gl.getExtension() both checks AND enables the extension. The C wrapper counts
 * and records the JS call, which the GL funnel cannot see. */
// clang-format off
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wextra-semi"
EM_JS(int, nt_gfx_js_get_extension, (const char *name), {
    var gl = GL.currentContext ? GL.currentContext.GLctx : null;
    return gl && gl.getExtension(UTF8ToString(name)) ? 1 : 0;
});
#pragma clang diagnostic pop
// clang-format on

static bool get_extension(const char *name) {
    NT_GL_ISSUED(getExtension, name);
    return nt_gfx_js_get_extension(name) != 0;
}

nt_gfx_gpu_caps_t nt_gfx_gl_ctx_detect_gpu_caps(void) {
    nt_gfx_gpu_caps_t caps = {0};
    caps.has_astc = get_extension("WEBGL_compressed_texture_astc");
    caps.has_bc7 = get_extension("EXT_texture_compression_bptc");
    caps.has_etc2 = get_extension("WEBGL_compressed_texture_etc");
    caps.has_float_render_target = get_extension("EXT_color_buffer_float");
    caps.has_float_texture_linear = get_extension("OES_texture_float_linear");
    GLint max_texture_size = 0;
    NT_GL(glGetIntegerv, GL_MAX_TEXTURE_SIZE, &max_texture_size);
    caps.max_texture_size = (uint32_t)max_texture_size;
    return caps;
}

#if NT_GFX_GPU_TIMING_ENABLED
/* Enable EXT_disjoint_timer_query_webgl2; getExtension activates its constants for this context. */
bool nt_gfx_gl_ctx_enable_timer_query(void) { return get_extension("EXT_disjoint_timer_query_webgl2"); }

/* Convert the JS number in C; the SDK's u64 heap writer overflows under SAFE_HEAP. */
// clang-format off
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wextra-semi"
EM_JS(double, nt_gfx_gl_ctx_query_result, (uint32_t query), {
    return GL.currentContext.GLctx.getQueryParameter(GL.queries[query], 0x8866 /* GL_QUERY_RESULT */);
});
#pragma clang diagnostic pop
// clang-format on

/* WebGL2 has no KHR_debug equivalent in the spec; Spector.js intercepts at
 * the JS call level and wouldn't see glPushDebugGroup anyway. Return false
 * — segment labeling becomes a no-op on web. */
bool nt_gfx_gl_ctx_enable_debug_groups(void) { return false; }

#endif

/* WebGL2 has no KHR_debug / glDebugMessageCallback — GL errors surface via the browser console
 * (always on) and Emscripten GL_ASSERTIONS in debug builds. No-op here. */
bool nt_gfx_gl_ctx_enable_debug_callback(void) { return false; }
