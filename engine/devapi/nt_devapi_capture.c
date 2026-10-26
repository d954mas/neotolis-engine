#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#include "base64/nt_base64.h"
#include "core/nt_assert.h"
#include "devapi/nt_devapi_capture.h"
#include "devapi/nt_devapi_capture_internal.h"
#include "devapi/nt_devapi_internal.h"
#include "fpng/nt_fpng.h"
#include "graphics/nt_gfx.h"
#include "window/nt_window.h"

/* Capture command group (capture.frame / capture.region). Handlers validate bot params (never assert),
   then DEFER a producer that runs at the GL-valid pre-swap seam — that timing is why capture returns its
   data asynchronously. Compiles out entirely without NT_DEVAPI_GROUP_CAPTURE. */

#ifdef NT_DEVAPI_GROUP_CAPTURE

/* Bounded readback buffer: a region capped at the framebuffer keeps w*h*4 small, but the static cap
   is the DoS backstop. 4096x4096 rgba8 = 64 MiB — generous for any real demo FB; -D overridable. */
#ifndef NT_DEVAPI_CAPTURE_MAX_PIXELS
#define NT_DEVAPI_CAPTURE_MAX_PIXELS (4096ULL * 4096ULL)
#endif

/* The producer's largest uint32 product is the base64 need (~w*h*8, above the rgba and png-cap sizes).
   The *8 bound keeps it wraparound-free for any -D-raised cap, so the uint32 size math is provably safe
   end-to-end. */
_Static_assert((uint64_t)NT_DEVAPI_CAPTURE_MAX_PIXELS * 8U <= UINT32_MAX, "NT_DEVAPI_CAPTURE_MAX_PIXELS too large: the base64 size expansion (~w*h*8) would overflow the uint32 producer size math");

/* Capture wire error codes (group-owned; the generic core never names them). */
#define NT_DEVAPI_ERR_CAPTURE_FAILED "capture_failed"
#define NT_DEVAPI_ERR_CAPTURE_UNAVAILABLE "capture_unavailable"

/* "This host drives the pre-swap seam" — set via install_seam; reset each register_capture (fresh init
   starts unarmed). defer_capture reads it to reject rather than defer a slot that never resolves. NOT
   reset on client-reset, so a reconnecting client on a capture host stays armed. */
static bool s_seam_armed;

void nt_devapi_capture_arm(void) { s_seam_armed = true; }

/* The pending capture's rect + scale divisor: only one deferred reply is ever pending. */
typedef struct capture_ctx {
    uint32_t x;
    uint32_t y;
    uint32_t w;
    uint32_t h;
    uint32_t factor; /* scale divisor: 1 (full) / 2 (½) / 4 (¼). */
} capture_ctx;

static capture_ctx s_capture;

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

/* Fill {width,height,format:"png",data:<base64>} from the encoded PNG. */
static bool build_payload(cJSON *result, const uint8_t *png, uint32_t png_len, uint32_t out_w, uint32_t out_h) {
    uint32_t b64_need = ((png_len + 2U) / 3U) * 4U;
    char *b64 = (char *)malloc((size_t)b64_need + 1U); /* +1 for the NUL the JSON string needs. */
    if (b64 == NULL) {
        return false;
    }
    uint32_t b64_len = nt_base64_encode(png, png_len, b64, b64_need);
    if (b64_len == 0U) {
        free(b64);
        return false;
    }
    b64[b64_len] = '\0';

    devapi_add_number(result, "width", (double)out_w);
    devapi_add_number(result, "height", (double)out_h);
    devapi_add_string(result, "format", "png");
    devapi_add_string(result, "data", b64);
    free(b64);
    return true;
}

/* readback -> strip+box -> fpng -> base64 -> result. Failure at any bounded stage returns false (no
   assert on the path that bot params can reach). */
static bool capture_produce(const capture_ctx *c, cJSON *result) {
    uint32_t out_w = c->w / c->factor;
    uint32_t out_h = c->h / c->factor;
    if (out_w == 0U || out_h == 0U) {
        return false; /* defensive only: the handlers reject non-dividing scale synchronously (bad_params). */
    }

    uint32_t rgba_len = c->w * c->h * 4U;
    uint8_t *rgba = (uint8_t *)malloc((size_t)rgba_len);
    if (rgba == NULL) {
        return false;
    }
    if (!nt_gfx_read_pixels((nt_render_target_t){0}, (int)c->x, (int)c->y, (int)c->w, (int)c->h, rgba, rgba_len)) {
        free(rgba);
        return false;
    }

    uint32_t rgb_len = out_w * out_h * 3U;
    uint8_t *rgb = (uint8_t *)malloc((size_t)rgb_len);
    if (rgb == NULL) {
        free(rgba);
        return false;
    }
    nt_devapi_capture_strip_and_box(rgba, c->w, c->h, c->factor, rgb);
    free(rgba);

    /* PNG is bounded by the raw RGB size + a small header overhead in the worst (uncompressible) case;
       size the encode buffer generously and let fpng's own cap-check fail-early if it overruns. */
    uint32_t png_cap = rgb_len + (rgb_len / 2U) + 1024U;
    uint8_t *png = (uint8_t *)malloc((size_t)png_cap);
    if (png == NULL) {
        free(rgb);
        return false;
    }
    uint32_t png_len = nt_fpng_encode_rgb(rgb, out_w, out_h, png, png_cap);
    free(rgb);
    if (png_len == 0U) {
        free(png);
        return false;
    }

    bool built = build_payload(result, png, png_len, out_w, out_h);
    free(png);
    return built;
}

/* The deferred reply, run once at the GL-valid pre-swap seam. */
static bool capture_reply(const cJSON *params, cJSON *result, nt_devapi_error *err, void *ud) {
    (void)params;
    (void)ud;
    if (!capture_produce(&s_capture, result)) {
        return nt_devapi_fail(err, NT_DEVAPI_ERR_CAPTURE_FAILED, "capture failed (framebuffer readback or PNG encode error)");
    }
    return true;
}
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
        return nt_devapi_fail(err, NT_DEVAPI_ERR_BAD_PARAMS, who);
    }
    double v = js->valuedouble;
    if (v == 1.0) {
        *factor = 1U;
    } else if (v == 2.0) {
        *factor = 2U;
    } else if (v == 4.0) {
        *factor = 4U;
    } else {
        return nt_devapi_fail(err, NT_DEVAPI_ERR_BAD_PARAMS, who);
    }
    return true;
}

/* Defer a reply carrying the captured rect + factor. `gl_y` is the GL bottom-left y of the
   rect (top-left y converted by the caller); the row-flip in nt_gfx_read_pixels then yields a
   top-left sub-rect matching the documented contract. Capture resolves at the next render, sim frozen or not. */
static bool defer_capture(uint32_t x, uint32_t gl_y, uint32_t w, uint32_t h, uint32_t factor, nt_devapi_error *err) {
    if (!s_seam_armed) {
        /* The host advertises capture but never drives the pre-swap seam (e.g. devapi_host, which inits
           no GL) — a deferred slot would never resolve, hanging the client. Reject synchronously with a
           distinct code instead. A capture host arms the seam at startup, so this never trips there. */
        return nt_devapi_fail(err, NT_DEVAPI_ERR_CAPTURE_UNAVAILABLE, "capture not available on this host (it does not drive the pre-swap capture seam)");
    }
    s_capture = (capture_ctx){.x = x, .y = gl_y, .w = w, .h = h, .factor = factor};
    return nt_devapi_defer(NT_DEVAPI_GATE_PRESENT, 0.0, capture_reply, err);
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
        return nt_devapi_fail(err, NT_DEVAPI_ERR_BAD_PARAMS, "capture.frame: framebuffer has zero size");
    }
    if ((uint64_t)fb_w * (uint64_t)fb_h > NT_DEVAPI_CAPTURE_MAX_PIXELS) {
        return nt_devapi_fail(err, NT_DEVAPI_ERR_BAD_PARAMS, "capture.frame: framebuffer exceeds the capture pixel cap");
    }
    if (fb_w % factor != 0U || fb_h % factor != 0U) {
        return nt_devapi_fail(err, NT_DEVAPI_ERR_BAD_PARAMS, "capture.frame: scale must evenly divide the framebuffer dimensions");
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
    if (!nt_devapi_parse_u32_param_exact(cJSON_GetObjectItemCaseSensitive(params, "x"), UINT32_MAX, err, bad, &x) ||
        !nt_devapi_parse_u32_param_exact(cJSON_GetObjectItemCaseSensitive(params, "y"), UINT32_MAX, err, bad, &y) ||
        !nt_devapi_parse_u32_param_exact(cJSON_GetObjectItemCaseSensitive(params, "w"), UINT32_MAX, err, bad, &w) ||
        !nt_devapi_parse_u32_param_exact(cJSON_GetObjectItemCaseSensitive(params, "h"), UINT32_MAX, err, bad, &h)) {
        return false;
    }
    if (w == 0U || h == 0U) {
        return nt_devapi_fail(err, NT_DEVAPI_ERR_BAD_PARAMS, "capture.region: w and h must be > 0");
    }
    uint32_t factor = 1U;
    if (!parse_scale(params, &factor, err, "capture.region: scale must be one of {1, 2, 4}")) {
        return false;
    }
    uint32_t fb_w = g_nt_window.fb_width;
    uint32_t fb_h = g_nt_window.fb_height;
    /* Overflow-safe bounds: x + w may wrap, so compare via the headroom (x <= fb_w && w <= fb_w - x). */
    if (x > fb_w || w > fb_w - x || y > fb_h || h > fb_h - y) {
        return nt_devapi_fail(err, NT_DEVAPI_ERR_BAD_PARAMS, "capture.region: rect out of framebuffer bounds");
    }
    if ((uint64_t)w * (uint64_t)h > NT_DEVAPI_CAPTURE_MAX_PIXELS) {
        return nt_devapi_fail(err, NT_DEVAPI_ERR_BAD_PARAMS, "capture.region: rect exceeds the capture pixel cap");
    }
    if (w % factor != 0U || h % factor != 0U) {
        return nt_devapi_fail(err, NT_DEVAPI_ERR_BAD_PARAMS, "capture.region: scale must evenly divide w and h");
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

/* Public host-facing seam (the window pre-swap hook target): builds a pending capture reply at the
   GL-valid point. */
void nt_devapi_capture_on_pre_swap(void) { nt_devapi_run_pre_swap_reply(); }

void nt_devapi_register_capture(void) {
    s_seam_armed = false; /* fresh init starts unarmed; the host (re-)arms via install_seam. */
    /* fpng needs a one-time CPU-feature detect before any encode; do it here so the encoder dependency
       is the capture group's own concern, not something the game host must know to call. Idempotent. */
    nt_fpng_init();
    /* Engine-internal dup is a build-time bug -> assert NT_OK. Capture first: assert expressions must stay side-effect-free. */
    int n = (int)(sizeof(k_capture_cmds) / sizeof(k_capture_cmds[0]));
    for (int i = 0; i < n; i++) {
        nt_result_t rr = nt_devapi_register(&k_capture_cmds[i], k_capture_handlers[i], NULL);
        NT_ASSERT(rr == NT_OK);
        (void)rr;
    }
}

#endif /* NT_DEVAPI_GROUP_CAPTURE */
