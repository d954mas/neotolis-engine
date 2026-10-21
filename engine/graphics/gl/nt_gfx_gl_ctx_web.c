#include "graphics/gl/nt_gfx_gl_calls.h"
#include "graphics/gl/nt_gfx_gl_ctx.h"
#include "log/nt_log.h"

#include <emscripten.h>
#include <emscripten/html5.h>
#include <emscripten/html5_webgl.h>
#include <string.h>

/* The cap-probe EM_JS bodies below reach the Emscripten GL registry (GL.currentContext.GLctx). GL lives
 * in library_webgl.js, force-linked by this TU's own emscripten_webgl_* C calls -- so it survives today.
 * EM_JS_DEPS makes that implicit dependency explicit + Closure-kept, so moving context creation out of
 * this file can't silently strip GL in release. */
EM_JS_DEPS(nt_gfx_gl_ctx_web, "$GL,$UTF8ToString,$stringToUTF8,$findCanvasEventTarget,emscripten_webgl_create_context")

static EMSCRIPTEN_WEBGL_CONTEXT_HANDLE s_gl_context;
static bool s_loss_pending; /* set by a lost event until begin_frame takes it */

static void capture_device_info(void);

/* The browser explains a refusal only through a creation-error event fired synchronously inside getContext. */
/* clang-format off */
EM_JS(EMSCRIPTEN_WEBGL_CONTEXT_HANDLE, nt_gfx_js_create_context, (const char *target, const EmscriptenWebGLContextAttributes *attrs, char *status, unsigned int cap), {
    var canvas = findCanvasEventTarget(target);
    var message = "";
    var on_error = function(event) { message = event.statusMessage; };
    canvas.addEventListener("webglcontextcreationerror", on_error);
    var context = _emscripten_webgl_create_context(target, attrs);
    canvas.removeEventListener("webglcontextcreationerror", on_error);
    if (!context) stringToUTF8(message.replace(/[\\x00\\r\\n]/g, " "), status, cap);
    return context;
})
/* clang-format on */

static bool on_context_lost(int event_type, const void *reserved, void *user_data) {
    (void)event_type;
    (void)reserved;
    (void)user_data;
    s_loss_pending = true;
    return true; /* preventDefault: the browser restores only a context whose loss was handled */
}

bool nt_gfx_gl_ctx_create(const nt_gfx_desc_t *desc) {
    /* nt_log_write cuts the whole line to NT_LOG_BUF_SIZE on a UTF-8 boundary. */
    char creation_status[NT_LOG_BUF_SIZE];
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

    s_gl_context = nt_gfx_js_create_context("#canvas", &attrs, creation_status, sizeof(creation_status));
    if (s_gl_context == 0) {
        NT_LOG_ERROR("context-create: failed WebGL=2.0 alpha=%d depth=%d stencil=%d antialias=%d premultiplied_alpha=%d preserve=0 browser_status=%s", (int)desc->alpha, (int)desc->depth,
                     (int)desc->stencil, (int)desc->antialias, (int)desc->premultiplied_alpha, creation_status[0] ? creation_status : "<empty>");
        return false;
    }
    emscripten_webgl_make_context_current(s_gl_context);
    s_loss_pending = false;
    emscripten_set_webglcontextlost_callback("#canvas", NULL, false, on_context_lost);
    capture_device_info();
    return true;
}

void nt_gfx_gl_ctx_destroy(void) {
    emscripten_set_webglcontextlost_callback("#canvas", NULL, false, NULL);
    if (s_gl_context > 0) {
        emscripten_webgl_destroy_context(s_gl_context);
        s_gl_context = 0;
    }
}

bool nt_gfx_gl_ctx_take_loss(void) {
    const bool loss = s_loss_pending;
    s_loss_pending = false;
    return loss;
}

bool nt_gfx_gl_ctx_query_lost(void) { return s_gl_context <= 0 || emscripten_is_webgl_context_lost(s_gl_context) != 0; }

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

/* clang-format off */
EM_JS(void, nt_gfx_js_device_string, (unsigned int name, char *out, unsigned int cap), {
    var text = GL.currentContext.GLctx.getParameter(name);
    stringToUTF8(text === null ? "" : text.replace(/[\\r\\n]/g, " "), out, cap);
})
/* clang-format on */

static void capture_device_info(void) {
    const bool unmasked = get_extension("WEBGL_debug_renderer_info");
    const unsigned int vendor = unmasked ? 0x9245U : GL_VENDOR;
    const unsigned int renderer = unmasked ? 0x9246U : GL_RENDERER;
    nt_gfx_device_info_t info = {0};
    NT_GL_ISSUED(glGetString, vendor);
    nt_gfx_js_device_string(vendor, info.vendor, sizeof(info.vendor));
    NT_GL_ISSUED(glGetString, renderer);
    nt_gfx_js_device_string(renderer, info.renderer, sizeof(info.renderer));
    NT_GL_ISSUED(glGetString, GL_VERSION);
    nt_gfx_js_device_string(GL_VERSION, info.api_version, sizeof(info.api_version));
    if (!nt_gfx_gl_ctx_query_lost()) {
        memcpy(g_nt_gfx_device_info.vendor, info.vendor, sizeof(info.vendor));
        memcpy(g_nt_gfx_device_info.renderer, info.renderer, sizeof(info.renderer));
        memcpy(g_nt_gfx_device_info.api_version, info.api_version, sizeof(info.api_version));
    }
}

static uint32_t gl_limit(GLenum name) {
    GLint value = 0;
    NT_GL(glGetIntegerv, name, &value);
    return (uint32_t)value;
}

nt_gfx_gpu_caps_t nt_gfx_gl_ctx_detect_gpu_caps(void) {
    nt_gfx_gpu_caps_t caps = {0};
    caps.has_astc = get_extension("WEBGL_compressed_texture_astc");
    caps.has_bc7 = get_extension("EXT_texture_compression_bptc");
    caps.has_etc2 = get_extension("WEBGL_compressed_texture_etc");
    caps.has_float_render_target = get_extension("EXT_color_buffer_float");
    caps.has_float_texture_linear = get_extension("OES_texture_float_linear");
    /* Without getExtension the COMPLETION_STATUS query is INVALID_ENUM and reads as never complete. */
    caps.has_parallel_shader_compile = get_extension("KHR_parallel_shader_compile");
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
