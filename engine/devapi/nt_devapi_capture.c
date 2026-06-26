#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "core/nt_assert.h"
#include "devapi/nt_devapi_base64.h"
#include "devapi/nt_devapi_internal.h"
#include "fpng/nt_fpng.h"
#include "graphics/nt_gfx.h"
#include "window/nt_window.h"

/* Capture command group (capture.frame / capture.region): the FIRST deferred command that returns
   DATA. Each handler validates its bot params (NEVER asserts) then defers with
   a producer; the producer runs at the GL-valid pre-swap seam and chains readback -> RGB strip
   -> optional box-average ½/¼ -> fpng -> base64 -> {width,height,format:"png",data}.
   Compiles out entirely when NT_DEVAPI_GROUP_CAPTURE is absent (zero release delta). */

#ifdef NT_DEVAPI_GROUP_CAPTURE

/* Bounded readback buffer: a region capped at the framebuffer keeps w*h*4 small, but the static cap
   is the DoS backstop. 4096x4096 rgba8 = 64 MiB — generous for any real demo FB; -D overridable. */
#ifndef NT_DEVAPI_CAPTURE_MAX_PIXELS
#define NT_DEVAPI_CAPTURE_MAX_PIXELS (4096ULL * 4096ULL)
#endif

/* The producer sizes rgba (w*h*4), the PNG cap (rgb*1.5 + 1024 ~= w*h*6), AND the base64 expansion
   (b64_need = png_len*4/3, which at the worst-case png_cap is w*h*6 + ~1368) in uint32. Bound by *8 so
   the LARGEST product (the base64 need, not just the PNG cap) stays free of wraparound for ANY -D-raised
   cap — the uint32 size math is then provably safe end-to-end. */
_Static_assert((uint64_t)NT_DEVAPI_CAPTURE_MAX_PIXELS * 8U <= UINT32_MAX, "NT_DEVAPI_CAPTURE_MAX_PIXELS too large: the base64 size expansion (~w*h*8) would overflow the uint32 producer size math");

/* NT_DEVAPI_CAPTURE_MAX_INFLIGHT (default 4) is defined in the public capture header so a host can -D it
   and the unit test can assert the exact cap. It bounds the per-seam encode burst + held base64 payloads
   a flooding client can trigger, independent of the shared deferred queue (NT_DEVAPI_MAX_DEFERRED). */

static void set_bad_params(nt_devapi_error *err, const char *message) {
    err->code = NT_DEVAPI_ERR_BAD_PARAMS;
    err->message = message;
}

/* Producer params: the captured rect + scale divisor, owned by the slot (freed at the seam). */
typedef struct capture_ctx {
    uint32_t x;
    uint32_t y;
    uint32_t w;
    uint32_t h;
    uint32_t factor; /* scale divisor: 1 (full) / 2 (½) / 4 (¼). */
} capture_ctx;

// #region seam producer
/* Fused alpha-strip + box-average: rgba8 src -> RGB dst, dst dims = w/factor x h/factor.
   factor==1 is a plain strip (1x1 box). Integer-only, no heap. */
void nt_devapi_capture_strip_and_box(const uint8_t *src, uint32_t w, uint32_t h, uint32_t factor, uint8_t *dst) {
    uint32_t ow = w / factor;
    uint32_t oh = h / factor;
    uint32_t area = factor * factor;
    for (uint32_t oy = 0; oy < oh; oy++) {
        for (uint32_t ox = 0; ox < ow; ox++) {
            uint32_t r = 0;
            uint32_t g = 0;
            uint32_t b = 0;
            for (uint32_t dy = 0; dy < factor; dy++) {
                for (uint32_t dx = 0; dx < factor; dx++) {
                    size_t soff = (((size_t)((oy * factor) + dy) * w) + ((ox * factor) + dx)) * 4U;
                    const uint8_t *p = src + soff;
                    r += p[0];
                    g += p[1];
                    b += p[2];
                }
            }
            size_t doff = ((size_t)(oy * ow) + ox) * 3U;
            uint8_t *q = dst + doff;
            q[0] = (uint8_t)(r / area);
            q[1] = (uint8_t)(g / area);
            q[2] = (uint8_t)(b / area);
        }
    }
}

/* Build the {width,height,format:"png",data:<base64>} payload from the encoded PNG. Owns nothing
   on failure (returns NULL -> the DATA slot yields the distinguishable capture_failed error). */
static cJSON *build_payload(const uint8_t *png, uint32_t png_len, uint32_t out_w, uint32_t out_h) {
    uint32_t b64_need = ((png_len + 2U) / 3U) * 4U;
    char *b64 = (char *)malloc((size_t)b64_need + 1U); /* +1 for the NUL the JSON string needs. */
    if (b64 == NULL) {
        return NULL;
    }
    uint32_t b64_len = nt_devapi_base64_encode(png, png_len, b64, b64_need);
    if (b64_len == 0U) {
        free(b64);
        return NULL;
    }
    b64[b64_len] = '\0';

    cJSON *payload = cJSON_CreateObject();
    if (payload == NULL) {
        free(b64);
        return NULL;
    }
    devapi_add_number(payload, "width", (double)out_w);
    devapi_add_number(payload, "height", (double)out_h);
    devapi_add_string(payload, "format", "png");
    devapi_add_string(payload, "data", b64);
    free(b64);
    return payload;
}

/* The pre-swap seam producer: readback -> strip+box -> fpng -> base64 -> payload. Runs once
   per slot at the GL-valid seam; failure at any bounded stage returns NULL (no assert on the path
   that bot params can reach). */
static cJSON *capture_produce(void *vctx) {
    const capture_ctx *c = (const capture_ctx *)vctx;
    uint32_t out_w = c->w / c->factor;
    uint32_t out_h = c->h / c->factor;
    if (out_w == 0U || out_h == 0U) {
        return NULL; /* defensive only: the handlers reject non-dividing scale synchronously (bad_params). */
    }

    uint32_t rgba_len = c->w * c->h * 4U;
    uint8_t *rgba = (uint8_t *)malloc((size_t)rgba_len);
    if (rgba == NULL) {
        return NULL;
    }
    if (!nt_gfx_read_pixels((int)c->x, (int)c->y, (int)c->w, (int)c->h, rgba, rgba_len)) {
        free(rgba);
        return NULL;
    }

    uint32_t rgb_len = out_w * out_h * 3U;
    uint8_t *rgb = (uint8_t *)malloc((size_t)rgb_len);
    if (rgb == NULL) {
        free(rgba);
        return NULL;
    }
    nt_devapi_capture_strip_and_box(rgba, c->w, c->h, c->factor, rgb);
    free(rgba);

    /* PNG is bounded by the raw RGB size + a small header overhead in the worst (uncompressible) case;
       size the encode buffer generously and let fpng's own cap-check fail-early if it overruns. */
    uint32_t png_cap = rgb_len + (rgb_len / 2U) + 1024U;
    uint8_t *png = (uint8_t *)malloc((size_t)png_cap);
    if (png == NULL) {
        free(rgb);
        return NULL;
    }
    uint32_t png_len = nt_fpng_encode_rgb(rgb, out_w, out_h, png, png_cap);
    free(rgb);
    if (png_len == 0U) {
        free(png);
        return NULL;
    }

    cJSON *payload = build_payload(png, png_len, out_w, out_h);
    free(png);
    return payload;
}

static void capture_ctx_free(void *ctx) { free(ctx); }
// #endregion

// #region handlers
/* Parse an optional scale param to a divisor factor in {1,2,4}. Absent -> 1. Anything else (non-number,
   non-integer, not {1,2,4}) -> bad_params (NEVER asserts on bot input). */
static bool parse_scale(const cJSON *params, uint32_t *factor, nt_devapi_error *err, const char *who) {
    *factor = 1U;
    const cJSON *js = cJSON_GetObjectItemCaseSensitive(params, "scale");
    if (js == NULL || cJSON_IsNull(js)) {
        return true;
    }
    if (!cJSON_IsNumber(js)) {
        set_bad_params(err, who);
        return false;
    }
    double v = js->valuedouble;
    if (v == 1.0) {
        *factor = 1U;
    } else if (v == 2.0) {
        *factor = 2U;
    } else if (v == 4.0) {
        *factor = 4U;
    } else {
        set_bad_params(err, who);
        return false;
    }
    return true;
}

/* Defer with a producer carrying the captured rect + factor. `gl_y` is the GL bottom-left y of the
   rect (top-left y converted by the caller); the row-flip in nt_gfx_read_pixels then yields a
   top-left sub-rect matching the documented contract. Capture resolves after ~1 render. */
static bool defer_capture(uint32_t x, uint32_t gl_y, uint32_t w, uint32_t h, uint32_t factor, nt_devapi_error *err) {
    if (!nt_devapi_capture_seam_armed()) {
        /* The host advertises capture but never drives the pre-swap seam (e.g. devapi_host, which inits
           no GL) — a deferred slot would never resolve, hanging the client. Reject synchronously with a
           distinct code instead. A capture host arms the seam at startup, so this never trips there. */
        err->code = NT_DEVAPI_ERR_CAPTURE_UNAVAILABLE;
        err->message = "capture not available on this host (it does not drive the pre-swap capture seam)";
        return false;
    }
    if (nt_devapi_deferred_data_inflight() >= NT_DEVAPI_CAPTURE_MAX_INFLIGHT) {
        set_bad_params(err, "capture: too many captures in flight — drain pending results before requesting more");
        return false;
    }
    capture_ctx *ctx = (capture_ctx *)malloc(sizeof(capture_ctx));
    NT_ASSERT(ctx != NULL); /* OOM is a host-side fault, not bot input. */
    ctx->x = x;
    ctx->y = gl_y;
    ctx->w = w;
    ctx->h = h;
    ctx->factor = factor;
    return nt_devapi_defer_current_with_result(1, capture_produce, ctx, capture_ctx_free);
}

/* capture.frame {scale?}: defers a full-framebuffer capture (x=0,y=0,w=fb_width,h=fb_height). */
static bool cmd_capture_frame(const cJSON *params, cJSON *result, nt_devapi_error *err, void *ud) {
    (void)result;
    (void)ud;
    uint32_t factor = 1U;
    if (!parse_scale(params, &factor, err, "capture.frame: scale must be one of {1, 2, 4}")) {
        return false;
    }
    uint32_t fb_w = g_nt_window.fb_width;
    uint32_t fb_h = g_nt_window.fb_height;
    if (fb_w == 0U || fb_h == 0U) {
        set_bad_params(err, "capture.frame: framebuffer has zero size");
        return false;
    }
    if ((uint64_t)fb_w * (uint64_t)fb_h > NT_DEVAPI_CAPTURE_MAX_PIXELS) {
        set_bad_params(err, "capture.frame: framebuffer exceeds the capture pixel cap");
        return false;
    }
    if (fb_w % factor != 0U || fb_h % factor != 0U) {
        set_bad_params(err, "capture.frame: scale must evenly divide the framebuffer dimensions");
        return false;
    }
    return defer_capture(0U, 0U, fb_w, fb_h, factor, err);
}

/* capture.region {x,y,w,h,scale?}: validates the rect against the framebuffer (overflow-safe) then
   defers a sub-rect capture; scale applies after readback (orthogonal to region). */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static bool cmd_capture_region(const cJSON *params, cJSON *result, nt_devapi_error *err, void *ud) {
    (void)result;
    (void)ud;
    uint32_t x = 0;
    uint32_t y = 0;
    uint32_t w = 0;
    uint32_t h = 0;
    const char *bad = "capture.region: x/y must be non-negative integers, w/h positive integers";
    if (!nt_devapi_parse_u32_param_exact(cJSON_GetObjectItemCaseSensitive(params, "x"), UINT32_MAX, err, set_bad_params, bad, &x) ||
        !nt_devapi_parse_u32_param_exact(cJSON_GetObjectItemCaseSensitive(params, "y"), UINT32_MAX, err, set_bad_params, bad, &y) ||
        !nt_devapi_parse_u32_param_exact(cJSON_GetObjectItemCaseSensitive(params, "w"), UINT32_MAX, err, set_bad_params, bad, &w) ||
        !nt_devapi_parse_u32_param_exact(cJSON_GetObjectItemCaseSensitive(params, "h"), UINT32_MAX, err, set_bad_params, bad, &h)) {
        return false;
    }
    if (w == 0U || h == 0U) {
        set_bad_params(err, "capture.region: w and h must be > 0");
        return false;
    }
    uint32_t factor = 1U;
    if (!parse_scale(params, &factor, err, "capture.region: scale must be one of {1, 2, 4}")) {
        return false;
    }
    uint32_t fb_w = g_nt_window.fb_width;
    uint32_t fb_h = g_nt_window.fb_height;
    /* Overflow-safe bounds: x + w may wrap, so compare via the headroom (x <= fb_w && w <= fb_w - x). */
    if (x > fb_w || w > fb_w - x || y > fb_h || h > fb_h - y) {
        set_bad_params(err, "capture.region: rect out of framebuffer bounds");
        return false;
    }
    if ((uint64_t)w * (uint64_t)h > NT_DEVAPI_CAPTURE_MAX_PIXELS) {
        set_bad_params(err, "capture.region: rect exceeds the capture pixel cap");
        return false;
    }
    if (w % factor != 0U || h % factor != 0U) {
        set_bad_params(err, "capture.region: scale must evenly divide w and h");
        return false;
    }
    /* The docstring contract is top-left origin; glReadPixels reads bottom-left. Convert the rect's
       top-left y to a GL bottom-left y (h<=fb_h-y already proven) so the row-flip yields a top-left
       sub-rect. Full-frame (capture.frame) is unchanged: fb_h-(0+fb_h)==0. */
    uint32_t gl_y = fb_h - (y + h);
    return defer_capture(x, gl_y, w, h, factor, err);
}
// #endregion

static const nt_devapi_command_desc k_capture_cmds[] = {
    {
        .method = "capture.frame",
        .group = "capture",
        .summary = "capture the full framebuffer at the next pre-swap as a PNG (base64); optional scale {1,2,4} box-average downscale",
        .params_shape = "{scale?:number}",
        .result_shape = "{width:number,height:number,format:string,data:string}",
        .frame_behavior = "deferred",
        .side_effects = "reads framebuffer at next pre-swap",
    },
    {
        .method = "capture.region",
        .group = "capture",
        .summary = "capture a framebuffer sub-rect {x,y,w,h} at the next pre-swap as a PNG (base64); optional scale {1,2,4} downscale (no rect resampling)",
        .params_shape = "{x:number,y:number,w:number,h:number,scale?:number}",
        .result_shape = "{width:number,height:number,format:string,data:string}",
        .frame_behavior = "deferred",
        .side_effects = "reads framebuffer at next pre-swap",
    },
};

static const nt_devapi_handler_fn k_capture_handlers[] = {
    cmd_capture_frame,
    cmd_capture_region,
};
_Static_assert(sizeof(k_capture_cmds) / sizeof(k_capture_cmds[0]) == sizeof(k_capture_handlers) / sizeof(k_capture_handlers[0]), "capture: descriptor/handler arrays must have equal length");

void nt_devapi_register_capture(void) {
    /* fpng needs a one-time CPU-feature detect before any encode; do it here so the encoder dependency
       is the capture group's own concern, not something the game host must know to call. Idempotent. */
    nt_fpng_init();
    /* Engine-internal dup is a build-time bug -> assert NT_OK. Capture first: NT_ASSERT compiles
       out under NT_ASSERT_MODE=0, so the call must not live inside the macro. */
    int n = (int)(sizeof(k_capture_cmds) / sizeof(k_capture_cmds[0]));
    for (int i = 0; i < n; i++) {
        nt_result_t rr = nt_devapi_register(&k_capture_cmds[i], k_capture_handlers[i], NULL);
        NT_ASSERT(rr == NT_OK);
        (void)rr;
    }
}

#endif /* NT_DEVAPI_GROUP_CAPTURE */
