#include <math.h>
#include <stdint.h>
#include <string.h>

#include "core/nt_assert.h"
#include "devapi/nt_devapi_internal.h"
#include "input/nt_input.h"   /* nt_inject_kind_t + the reserved mouse id; ui delegates to the input scheduler. */
#include "ui/nt_ui.h"         /* nt_ui_probe_collect + the POD node + nt_ui_id + nt_ui_get_bbox. */
#include "window/nt_window.h" /* g_nt_window: the one coordinate-space metadata source (like core view). */

/* L2 veneer over the L1 probe: range-check bot input -> bad_params, never assert. The host
   registers UI contexts by name; the engine keeps NO global ctx registry. */

#ifdef NT_DEVAPI_GROUP_UI

// #region ui context name table
/* Host-registered name -> ctx* table. Trusted in-process host calls may assert; bot input
   that misses the table is always bad_params. */
#ifndef NT_DEVAPI_UI_CONTEXT_MAX
#define NT_DEVAPI_UI_CONTEXT_MAX 4 /* cap = discretion */
#endif

static struct {
    const char *name;
    nt_ui_context_t *ctx;
} s_ui_ctx[NT_DEVAPI_UI_CONTEXT_MAX];
static uint32_t s_ui_ctx_count;

/* The NT_ASSERT(x && "msg") guards inflate measured complexity past the 25 threshold; the body
   is a flat append + dup scan. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void nt_devapi_ui_register_context(const char *name, nt_ui_context_t *ctx) {
    NT_ASSERT(name != NULL && "ui: context name must be non-NULL");
    NT_ASSERT(ctx != NULL && "ui: context must be non-NULL");
    NT_ASSERT(s_ui_ctx_count < NT_DEVAPI_UI_CONTEXT_MAX && "ui: context table full (raise NT_DEVAPI_UI_CONTEXT_MAX)");
    for (uint32_t i = 0; i < s_ui_ctx_count; i++) {
        NT_ASSERT(strcmp(s_ui_ctx[i].name, name) != 0 && "ui: duplicate context name");
    }
    s_ui_ctx[s_ui_ctx_count].name = name;
    s_ui_ctx[s_ui_ctx_count].ctx = ctx;
    s_ui_ctx_count++;
}

// #endregion

static void set_bad_params(nt_devapi_error *err, const char *message) {
    err->code = NT_DEVAPI_ERR_BAD_PARAMS;
    err->message = message;
}

/* Clear the host-registered name->ctx* table. The HOST owns these registrations (registered ONCE at
   startup, like game.* commands) — so this is called only at init/register (before the host registers),
   NOT on a client disconnect, or every session after the first would see an empty table. */
static void clear_ui_ctx_table(void) {
    for (uint32_t i = 0; i < NT_DEVAPI_UI_CONTEXT_MAX; i++) {
        s_ui_ctx[i].name = NULL;
        s_ui_ctx[i].ctx = NULL;
    }
    s_ui_ctx_count = 0;
}

/* A bot float coordinate/delta: require a number whose double AND float-narrowed form are finite.
   A huge finite double (1e309 -> inf, or past FLT_MAX) would narrow to inf and poison hit-tests /
   deltas. Mirrors the input group's parse_finite_coord; never asserts on bot input. */
static bool parse_finite_coord(const cJSON *nj, const char *cmd, float *out, nt_devapi_error *err) {
    if (nj == NULL || !cJSON_IsNumber(nj)) {
        set_bad_params(err, cmd);
        return false;
    }
    double v = nj->valuedouble;
    float fv = (float)v;
    if (!isfinite(v) || !isfinite(fv)) {
        set_bad_params(err, cmd);
        return false;
    }
    *out = fv;
    return true;
}

/* Range-check a non-negative integral frame count against [0, UINT16_MAX]. Reads valuedouble so a
   fractional/negative value (2.9, -0.5) is REJECTED, not silently truncated (the DoS cap for
   ui.drag frames, mirrors the input group / time.step). */
static bool parse_frame_count(const cJSON *nj, const char *cmd, uint16_t *out, nt_devapi_error *err) {
    if (nj == NULL || !cJSON_IsNumber(nj)) {
        set_bad_params(err, cmd);
        return false;
    }
    double v = nj->valuedouble;
    if (v < 0.0 || v > (double)UINT16_MAX || v != (double)(uint16_t)v) {
        set_bad_params(err, cmd);
        return false;
    }
    *out = (uint16_t)v;
    return true;
}

/* Resolve the target ctx from an optional `ctx` string param. Present -> table lookup (miss =
   bad_params, NEVER assert on bot input). Absent -> the sole/first context. Empty table ->
   bad_params. Returns NULL on any failure (err is set). */
static nt_ui_context_t *resolve_ctx(const cJSON *params, nt_devapi_error *err) {
    if (s_ui_ctx_count == 0) {
        set_bad_params(err, "ui: no UI context registered");
        return NULL;
    }
    const cJSON *jc = cJSON_GetObjectItemCaseSensitive(params, "ctx");
    if (jc == NULL) {
        return s_ui_ctx[0].ctx; /* default = sole/first */
    }
    if (!cJSON_IsString(jc) || jc->valuestring == NULL) {
        set_bad_params(err, "ui: ctx must be a string");
        return NULL;
    }
    for (uint32_t i = 0; i < s_ui_ctx_count; i++) {
        if (strcmp(s_ui_ctx[i].name, jc->valuestring) == 0) {
            return s_ui_ctx[i].ctx;
        }
    }
    set_bad_params(err, "ui: unknown context");
    return NULL;
}

// #region ui reads
/* Bounded scratch for one collect. BSS (dev-only); ~1024 nodes * ~280B ~= 280KB — acceptable for a
   dev tool, and keeps the handler off the stack / out of the hot path. */
static nt_ui_probe_node_t s_probe_nodes[NT_UI_PROBE_MAX_NODES];

/* The top-level coordinate-space metadata: EXPLICITLY declares the one ui.* contract — Y-up,
   origin bottom-left — that BOTH the read (bounds) AND the write ({x,y}) speak. width/height are the
   ctx LAYOUT dims (the SAME space the bounds + the resolve_target Y-flip use), NOT raw g_nt_window.fb_*
   (those differ under nt_ui_scale, a latent mislabel). projection = 2D-affine vs 3D-raycast mode. */
static void add_meta(cJSON *result, const nt_ui_context_t *ctx) {
    float lw = 0.0F;
    float lh = 0.0F;
    nt_ui_context_layout_size(ctx, &lw, &lh);
    devapi_add_string(result, "space", "ui");
    devapi_add_string(result, "origin", "bottom-left");
    devapi_add_string(result, "y_axis", "up");
    devapi_add_number(result, "width", (double)lw);
    devapi_add_number(result, "height", (double)lh);
    devapi_add_number(result, "dpr", (double)g_nt_window.dpr);
    devapi_add_string(result, "projection", nt_ui_context_uses_raycast(ctx) ? "3d" : "2d");
    /* Informational device viewport rect, recovered from the converters (no engine getter): the layout
       origin + the (lw,lh) corner map to the device content rect, so a remote bot can map layout<->screen
       itself. Identity (unscaled) -> {0,0,lw,lh}. */
    const float origin_layout[2] = {0.0F, 0.0F};
    const float corner_layout[2] = {lw, lh};
    float origin_dev[2];
    float corner_dev[2];
    nt_ui_layout_to_screen(ctx, origin_layout, origin_dev);
    nt_ui_layout_to_screen(ctx, corner_layout, corner_dev);
    cJSON *vp = cJSON_AddObjectToObject(result, "viewport");
    NT_ASSERT(vp != NULL);
    devapi_add_number(vp, "x", (double)origin_dev[0]);
    devapi_add_number(vp, "y", (double)origin_dev[1]);
    devapi_add_number(vp, "w", (double)(corner_dev[0] - origin_dev[0]));
    devapi_add_number(vp, "h", (double)(corner_dev[1] - origin_dev[1]));
}

/* Serialize one probe node into a fresh JSON object. bounds = {x,y,w,h} in the declared ui space
   (Y-up, origin bottom-left). read==write: a bot may feed these bounds straight into ui.click({x,y})
   with no flip — the one Y-up->device flip lives in resolve_target. */
static cJSON *node_to_json(const nt_ui_probe_node_t *n) {
    cJSON *obj = cJSON_CreateObject();
    NT_ASSERT(obj != NULL); /* OOM traps fail-early (matches devapi_add_*). */
    devapi_add_number(obj, "id", (double)n->id);
    devapi_add_number(obj, "parent", (double)n->parent);
    devapi_add_string(obj, "role", n->role_name[0] != '\0' ? n->role_name : "");
    devapi_add_number(obj, "role_kind", (double)n->role);
    devapi_add_string(obj, "id_string", n->id_string);
    devapi_add_string(obj, "text", n->text);
    devapi_add_string(obj, "label", n->label);
    devapi_add_number(obj, "child_count", (double)n->child_count);
    devapi_add_bool(obj, "visible", n->visible);
    devapi_add_bool(obj, "enabled", n->enabled);
    cJSON *b = cJSON_AddObjectToObject(obj, "bounds");
    NT_ASSERT(b != NULL);
    devapi_add_number(b, "x", (double)n->bounds[0]);
    devapi_add_number(b, "y", (double)n->bounds[1]);
    devapi_add_number(b, "w", (double)n->bounds[2]);
    devapi_add_number(b, "h", (double)n->bounds[3]);
    return obj;
}

/* ui.tree: IMMEDIATE read of the last completed frame's tree. Emits ALL nodes incl.
   invisible/offscreen/disabled carrying their flags (the bot filters, not the engine). */
static bool cmd_ui_tree(const cJSON *params, cJSON *result, nt_devapi_error *err, void *ud) {
    (void)ud;
    nt_ui_context_t *ctx = resolve_ctx(params, err);
    if (ctx == NULL) {
        return false;
    }
    uint32_t count = 0;
    nt_ui_probe_collect(ctx, s_probe_nodes, NT_UI_PROBE_MAX_NODES, &count);
    add_meta(result, ctx);
    cJSON *arr = cJSON_AddArrayToObject(result, "nodes");
    NT_ASSERT(arr != NULL);
    for (uint32_t i = 0; i < count; i++) {
        cJSON_bool added = cJSON_AddItemToArray(arr, node_to_json(&s_probe_nodes[i]));
        NT_ASSERT(added);
        (void)added;
    }
    return true;
}

/* ui.element: one node resolved by developer string id. collect-then-select so the returned
   node keeps its parent/label/children consistent. Unknown/stale id -> bad_params, never assert. */
static bool cmd_ui_element(const cJSON *params, cJSON *result, nt_devapi_error *err, void *ud) {
    (void)ud;
    const cJSON *jid = cJSON_GetObjectItemCaseSensitive(params, "id");
    if (!cJSON_IsString(jid) || jid->valuestring == NULL) {
        set_bad_params(err, "ui.element: id must be a string");
        return false;
    }
    nt_ui_context_t *ctx = resolve_ctx(params, err);
    if (ctx == NULL) {
        return false;
    }
    uint32_t target = nt_ui_id(jid->valuestring);
    uint32_t count = 0;
    nt_ui_probe_collect(ctx, s_probe_nodes, NT_UI_PROBE_MAX_NODES, &count);
    for (uint32_t i = 0; i < count; i++) {
        if (s_probe_nodes[i].id == target) {
            add_meta(result, ctx);
            cJSON_bool added = cJSON_AddItemToObject(result, "node", node_to_json(&s_probe_nodes[i]));
            NT_ASSERT(added);
            (void)added;
            return true;
        }
    }
    set_bad_params(err, "ui.element: unknown or stale id");
    return false;
}

/* ui.contexts: the host-registered context names. */
static bool cmd_ui_contexts(const cJSON *params, cJSON *result, nt_devapi_error *err, void *ud) {
    (void)params;
    (void)err;
    (void)ud;
    cJSON *arr = cJSON_AddArrayToObject(result, "contexts");
    NT_ASSERT(arr != NULL);
    for (uint32_t i = 0; i < s_ui_ctx_count; i++) {
        cJSON *s = cJSON_CreateString(s_ui_ctx[i].name);
        NT_ASSERT(s != NULL);
        cJSON_bool added = cJSON_AddItemToArray(arr, s);
        NT_ASSERT(added);
        (void)added;
    }
    return true;
}
// #endregion

// #region ui writes
/* Drag-frame DoS cap: bounds the per-command move-point expansion (frames+2 schedule entries) so a
   single ui.drag can't blow the schedule. Derived from the INPUT scheduler's cap (ui delegates there)
   with room for DOWN+UP. */
#ifndef NT_DEVAPI_UI_DRAG_FRAMES_MAX
#define NT_DEVAPI_UI_DRAG_FRAMES_MAX (NT_DEVAPI_INPUT_SCHED_MAX - 2)
#endif
_Static_assert(NT_DEVAPI_UI_DRAG_FRAMES_MAX + 2 <= NT_DEVAPI_INPUT_SCHED_MAX, "ui.drag frames+2 must fit the input scheduler");

/* Resolve a target field to a DEVICE-space (Y-down, top-left) pixel center for the inject path:
   EITHER a string id (nt_ui_id -> nt_ui_get_bbox, miss/empty -> bad_params, never assert) OR an {x,y}
   object (isfinite-checked). The user-facing ui.* contract is Y-up (origin bottom-left), declared in
   ui.tree/element metadata; the ONE documented Y-up->Y-down flip lives HERE, then the resulting LAYOUT
   coord is mapped LAYOUT->DEVICE through the ctx viewport (nt_ui_layout_to_screen) so the injected
   pointer is a real device coord — the ctx converts it back to layout for hit-test on nt_ui_begin.
     - {x,y}: interpreted as Y-up in the declared space -> flipped (layout_y = layout_h - in_y) using the
       SAME layout height the metadata + probe bounds use -> layout->device.
     - string id: nt_ui_get_bbox returns Clay Y-down layout center; NO flip -> layout->device.
   For an unscaled ctx the viewport is identity so device==layout (the hud is byte-identical); for an
   nt_ui_scale ctx the viewport inverts the scale+letterbox so a scaled ui.click lands correctly.
   `key` names the field ("id"/"from"/"to") for the error messages. */
static bool resolve_target(const cJSON *params, const char *key, nt_ui_context_t *ctx, float *out_x, float *out_y, nt_devapi_error *err) {
    const cJSON *jt = cJSON_GetObjectItemCaseSensitive(params, key);
    if (cJSON_IsString(jt)) {
        if (jt->valuestring == NULL || jt->valuestring[0] == '\0') {
            set_bad_params(err, "ui: target id must be a non-empty string");
            return false;
        }
        uint32_t uid = nt_ui_id(jt->valuestring); /* pre-checked non-empty: get_bbox asserts id!=0. */
        nt_ui_bbox_t bb = nt_ui_get_bbox(ctx, uid);
        if (!bb.found) {
            set_bad_params(err, "ui: unknown or stale target id");
            return false;
        }
        const float layout[2] = {bb.x + (bb.width * 0.5F), bb.y + (bb.height * 0.5F)};
        float device[2];
        nt_ui_layout_to_screen(ctx, layout, device);
        *out_x = device[0];
        *out_y = device[1];
        return true;
    }
    if (cJSON_IsObject(jt)) {
        const cJSON *jx = cJSON_GetObjectItemCaseSensitive(jt, "x");
        const cJSON *jy = cJSON_GetObjectItemCaseSensitive(jt, "y");
        float in_x = 0.0F;
        float in_y = 0.0F;
        if (!parse_finite_coord(jx, "ui: target x must be a finite number", &in_x, err) || !parse_finite_coord(jy, "ui: target y must be a finite number", &in_y, err)) {
            return false;
        }
        /* The sole documented Y-up -> layout Y-down flip; same layout height as the probe bounds. */
        float lw = 0.0F;
        float lh = 0.0F;
        nt_ui_context_layout_size(ctx, &lw, &lh);
        const float layout[2] = {in_x, lh - in_y};
        float device[2];
        nt_ui_layout_to_screen(ctx, layout, device);
        *out_x = device[0];
        *out_y = device[1];
        return true;
    }
    set_bad_params(err, "ui: target must be a string id or an {x,y} object");
    return false;
}

/* ui.click: resolve id|{x,y} -> bbox center px -> DOWN@0 + UP@hold via the SAME inject path as
   input.click (reserved synthetic mouse id 0x10000000). Fire-and-forget; the bot advances with
   frame.wait/time.step to apply. Whole-or-nothing preflight: reserve both entries or reject (no DOWN
   without its UP). */
static bool cmd_ui_click(const cJSON *params, cJSON *result, nt_devapi_error *err, void *ud) {
    (void)ud;
    nt_ui_context_t *ctx = resolve_ctx(params, err);
    if (ctx == NULL) {
        return false;
    }
    float cx;
    float cy;
    if (!resolve_target(params, "id", ctx, &cx, &cy, err)) {
        return false;
    }
    uint16_t hold = 1U; /* default: 1-frame-held click (down@0 + up@1), matches input.click. */
    const cJSON *hj = cJSON_GetObjectItemCaseSensitive(params, "hold");
    if (hj != NULL) {
        if (!cJSON_IsNumber(hj)) {
            set_bad_params(err, "ui.click: hold must be a number");
            return false;
        }
        if (!parse_frame_count(hj, "ui.click: hold must be an integer in [0, 65535]", &hold, err)) {
            return false;
        }
    }
    if (!nt_devapi_input_sched_can_reserve(2U)) { /* whole-or-nothing preflight on the shared scheduler. */
        set_bad_params(err, "ui.click: inject schedule overflow");
        return false;
    }
    nt_devapi_input_sched_pointer(NT_INJECT_POINTER_DOWN, NT_INPUT_INJECT_POINTER_ID_BASE, cx, cy, 1.0F, (uint8_t)NT_POINTER_MOUSE, 1U, 0);
    nt_devapi_input_sched_pointer(NT_INJECT_POINTER_UP, NT_INPUT_INJECT_POINTER_ID_BASE, cx, cy, 0.0F, (uint8_t)NT_POINTER_MOUSE, 0, hold);
    devapi_add_number(result, "queued", 2.0);
    return true;
}

/* ui.scroll: resolve id|{x,y} center -> MOVE there + wheel(dx,dy) notches via the inject path
   (reuse the positioned input.wheel form). Whole-or-nothing: MOVE + wheel reserved together. */
static bool cmd_ui_scroll(const cJSON *params, cJSON *result, nt_devapi_error *err, void *ud) {
    (void)ud;
    nt_ui_context_t *ctx = resolve_ctx(params, err);
    if (ctx == NULL) {
        return false;
    }
    float cx;
    float cy;
    if (!resolve_target(params, "id", ctx, &cx, &cy, err)) {
        return false;
    }
    float dx = 0.0F;
    float dy = 0.0F;
    const cJSON *dxj = cJSON_GetObjectItemCaseSensitive(params, "dx");
    const cJSON *dyj = cJSON_GetObjectItemCaseSensitive(params, "dy");
    if (dxj != NULL && !parse_finite_coord(dxj, "ui.scroll: dx must be a finite number", &dx, err)) {
        return false;
    }
    if (dyj != NULL && !parse_finite_coord(dyj, "ui.scroll: dy must be a finite number", &dy, err)) {
        return false;
    }
    if (!nt_devapi_input_sched_can_reserve(2U)) { /* whole-or-nothing: MOVE to center + wheel together. */
        set_bad_params(err, "ui.scroll: inject schedule overflow");
        return false;
    }
    nt_devapi_input_sched_pointer(NT_INJECT_POINTER_MOVE, NT_INPUT_INJECT_POINTER_ID_BASE, cx, cy, 1.0F, (uint8_t)NT_POINTER_MOUSE, 0, 0);
    nt_devapi_input_sched_wheel(dx, dy, 0);
    devapi_add_number(result, "queued", 2.0);
    return true;
}

/* ui.drag: resolve `from` AND `to` (id|{x,y}) -> DOWN@from + `frames` explicit interpolated MOVE
   points (the handler expands them — NO engine-side interpolation owner) + UP@to. frames CAPPED
   (DoS guard); whole-or-nothing preflight reserves DOWN + frames moves + UP up front. */
static bool cmd_ui_drag(const cJSON *params, cJSON *result, nt_devapi_error *err, void *ud) {
    (void)ud;
    nt_ui_context_t *ctx = resolve_ctx(params, err);
    if (ctx == NULL) {
        return false;
    }
    float fx;
    float fy;
    float tx;
    float ty;
    if (!resolve_target(params, "from", ctx, &fx, &fy, err) || !resolve_target(params, "to", ctx, &tx, &ty, err)) {
        return false;
    }
    uint16_t frames = 8U; /* default: a short multi-frame drag. */
    const cJSON *frj = cJSON_GetObjectItemCaseSensitive(params, "frames");
    if (frj != NULL) {
        if (!cJSON_IsNumber(frj)) {
            set_bad_params(err, "ui.drag: frames must be a number");
            return false;
        }
        if (!parse_frame_count(frj, "ui.drag: frames must be an integer in [0, 65535]", &frames, err)) {
            return false;
        }
    }
    if (frames > NT_DEVAPI_UI_DRAG_FRAMES_MAX) {
        set_bad_params(err, "ui.drag: frames exceeds the per-command cap");
        return false;
    }
    /* DOWN@0 + `frames` interpolated MOVEs (one per frame, t=1..frames) + UP@frames. */
    uint32_t total = (uint32_t)frames + 2U;
    if (!nt_devapi_input_sched_can_reserve(total)) { /* whole-or-nothing preflight on the shared scheduler. */
        set_bad_params(err, "ui.drag: inject schedule overflow");
        return false;
    }
    const uint32_t id = NT_INPUT_INJECT_POINTER_ID_BASE;
    nt_devapi_input_sched_pointer(NT_INJECT_POINTER_DOWN, id, fx, fy, 1.0F, (uint8_t)NT_POINTER_MOUSE, 1U, 0);
    for (uint16_t i = 1; i <= frames; i++) {
        float t = (float)i / (float)frames; /* frames >= 1 here: a 0-frame drag emits no MOVE. */
        float mx = fx + ((tx - fx) * t);
        float my = fy + ((ty - fy) * t);
        nt_devapi_input_sched_pointer(NT_INJECT_POINTER_MOVE, id, mx, my, 1.0F, (uint8_t)NT_POINTER_MOUSE, 1U, i);
    }
    nt_devapi_input_sched_pointer(NT_INJECT_POINTER_UP, id, tx, ty, 0.0F, (uint8_t)NT_POINTER_MOUSE, 0, frames);
    devapi_add_number(result, "queued", (double)total);
    return true;
}
// #endregion

static const nt_devapi_command_desc k_ui_cmds[] = {
    {
        .method = "ui.tree",
        .group = "ui",
        .summary = "IMMEDIATE read of the last completed frame's UI tree (ALL nodes incl. invisible/disabled) + a {space,origin,y_axis,width,height,dpr,projection,viewport} block; bounds are Y-up "
                   "(origin bottom-left), same space ui.click({x,y}) takes",
        .params_shape = "{ctx?:string}",
        .result_shape = "{space:string,origin:string,y_axis:string,width:number,height:number,dpr:number,projection:string,viewport:{x,y,w,h},nodes:[{id,parent,role,id_string,text,label,child_count,"
                        "visible,enabled,bounds}]}",
        .frame_behavior = "any",
        .side_effects = "none",
    },
    {
        .method = "ui.element",
        .group = "ui",
        .summary = "one UI node resolved by developer string id (unknown/stale id -> bad_params); bounds Y-up (origin bottom-left)",
        .params_shape = "{id:string, ctx?:string}",
        .result_shape = "{space:string,origin:string,y_axis:string,width:number,height:number,dpr:number,projection:string,viewport:{x,y,w,h},node:{id,parent,role,id_string,text,label,child_count,"
                        "visible,enabled,bounds}}",
        .frame_behavior = "any",
        .side_effects = "none",
    },
    {
        .method = "ui.contexts",
        .group = "ui",
        .summary = "list the host-registered UI context names",
        .params_shape = "{}",
        .result_shape = "{contexts:[string]}",
        .frame_behavior = "any",
        .side_effects = "none",
    },
    {
        .method = "ui.click",
        .group = "ui",
        .summary = "resolve a developer string id (or Y-up {x,y}, origin bottom-left — same space as ui.tree bounds) -> center -> synthetic pointer down@0 + up@hold (fire-and-forget; advance a frame "
                   "to apply)",
        .params_shape = "{id:string|{x,y}, hold?:number, ctx?:string}",
        .result_shape = "{queued:number}",
        .frame_behavior = "any",
        .side_effects = "enqueues synthetic pointer down+up events",
    },
    {
        .method = "ui.scroll",
        .group = "ui",
        .summary = "resolve id (or Y-up {x,y}, origin bottom-left) -> element center -> synthetic move + wheel(dx,dy) notches (fire-and-forget)",
        .params_shape = "{id:string|{x,y}, dx?:number, dy?:number, ctx?:string}",
        .result_shape = "{queued:number}",
        .frame_behavior = "any",
        .side_effects = "enqueues a synthetic move + wheel event",
    },
    {
        .method = "ui.drag",
        .group = "ui",
        .summary = "resolve from/to (id|Y-up {x,y}, origin bottom-left) -> down@from + frames interpolated moves + up@to (explicit points, NO interpolation owner; fire-and-forget)",
        .params_shape = "{from:string|{x,y}, to:string|{x,y}, frames?:number, ctx?:string}",
        .result_shape = "{queued:number}",
        .frame_behavior = "any",
        .side_effects = "enqueues an ordered multi-frame synthetic drag",
    },
};

static const nt_devapi_handler_fn k_ui_handlers[] = {
    cmd_ui_tree, cmd_ui_element, cmd_ui_contexts, cmd_ui_click, cmd_ui_scroll, cmd_ui_drag,
};
_Static_assert(sizeof(k_ui_cmds) / sizeof(k_ui_cmds[0]) == sizeof(k_ui_handlers) / sizeof(k_ui_handlers[0]), "ui: descriptor/handler arrays must have equal length");

void nt_devapi_register_ui(void) {
    clear_ui_ctx_table(); /* fresh host table at init (the host re-registers its ctx right after). */
    /* No tick/reset hook: ui.click/drag/scroll delegate scheduling to the input group's single
       scheduler, drained by nt_devapi_input_update. The host-owned ctx table survives client
       disconnects, so there is no ui-owned transient state to reset. */
    /* Engine-internal dup is a build-time bug -> assert NT_OK. Capture first: NT_ASSERT compiles
       out under NT_ASSERT_MODE=0, so the call must not live inside the macro. */
    int n = (int)(sizeof(k_ui_cmds) / sizeof(k_ui_cmds[0]));
    for (int i = 0; i < n; i++) {
        nt_result_t rr = nt_devapi_register(&k_ui_cmds[i], k_ui_handlers[i], NULL);
        NT_ASSERT(rr == NT_OK);
        (void)rr;
    }
}

#endif /* NT_DEVAPI_GROUP_UI */
