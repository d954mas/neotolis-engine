#include <math.h>
#include <stdint.h>
#include <string.h>

#include "app/nt_app.h" /* g_nt_app.frame — the advance clock the ui schedule ticks on (like input). */
#include "core/nt_assert.h"
#include "devapi/nt_devapi_internal.h"
#include "input/nt_input.h"          /* ui.click/drag/scroll write surface: the Phase-66 inject path. */
#include "input/nt_input_internal.h" /* inject decls live in the INTERNAL header (CORRECTION-3). */
#include "ui/nt_ui.h"                /* nt_ui_probe_collect + the POD node + nt_ui_id + nt_ui_get_bbox. */
#include "window/nt_window.h"        /* g_nt_window: the one D-10 metadata source (like core view). */

/* L2 veneer over the L1 probe: range-check bot input -> bad_params, never assert. The host
   registers UI contexts by name (D-15); the engine keeps NO global ctx registry. */

#ifdef NT_DEVAPI_GROUP_UI

// #region ui context name table
/* Host-registered name -> ctx* table (D-15). Trusted in-process host calls may assert; bot input
   that misses the table is always bad_params. */
#ifndef NT_DEVAPI_UI_CONTEXT_MAX
#define NT_DEVAPI_UI_CONTEXT_MAX 4 /* cap = discretion */
#endif

static struct {
    const char *name;
    nt_ui_context_t *ctx;
} s_ui_ctx[NT_DEVAPI_UI_CONTEXT_MAX];
static uint32_t s_ui_ctx_count;

/* The NT_ASSERT(x && "msg") guards inflate the measured complexity past the 25 threshold; the body
   is a flat append + dup scan. Matches the input-group big-handler NOLINT convention. */
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

/* nt_devapi_ui_reset is defined after the schedule region (it clears both the name table AND the
   schedule statics); the decl in nt_devapi_internal.h covers the earlier register-call use. */
// #endregion

static void set_bad_params(nt_devapi_error *err, const char *message) {
    err->code = NT_DEVAPI_ERR_BAD_PARAMS;
    err->message = message;
}

// #region devapi ui schedule
/* The ui group owns its OWN frame scheduler, cloned from nt_devapi_input.c (per-group modularity —
   no cross-group link dep on the input group's statics). The ONLY ui-vs-input delta is a
   nt_ui_id->nt_ui_get_bbox resolve step in the handlers BEFORE these sched_* calls. ui.click/drag/
   scroll enqueue the SAME low-level inject events as raw input (D-12/D-13, bot==human). */

/* Links the two caps so a full schedule's release can never exceed the immediate inject buffer. */
_Static_assert(NT_DEVAPI_UI_SCHED_MAX <= NT_INPUT_INJECT_QUEUE_MAX, "ui schedule must never release more than the immediate buffer can hold");

typedef struct {
    uint16_t frames_remaining; /* sim-advance countdown; 0 = release on the next advancing tick */
    uint8_t kind;              /* nt_inject_kind_t */
    union {
        struct {
            uint32_t id;
            float x, y, pressure;
            uint8_t type;
            uint8_t buttons_mask;
        } pointer; /* down / move */
        struct {
            uint32_t id;
        } pointer_up;
        struct {
            float dx, dy;
        } wheel;
    } u;
} sched_entry_t;

static sched_entry_t s_sched[NT_DEVAPI_UI_SCHED_MAX];
static uint32_t s_sched_count;

/* Whole-or-nothing reserve: a multi-event command (click=2, drag=N+2, scroll<=2) gets all N slots or
   none, so a near-full schedule can never accept a DOWN and reject its UP (Phase-66 CR-01). */
static sched_entry_t *sched_reserve(uint32_t n) {
    if (n > NT_DEVAPI_UI_SCHED_MAX - s_sched_count) {
        return NULL;
    }
    sched_entry_t *first = &s_sched[s_sched_count];
    s_sched_count += n;
    return first;
}

static bool sched_can_reserve(uint32_t n) { return n <= NT_DEVAPI_UI_SCHED_MAX - s_sched_count; }

static bool sched_pointer(nt_inject_kind_t kind, uint32_t id, float x, float y, float pressure, uint8_t type, uint8_t buttons_mask, uint16_t at_frame) {
    sched_entry_t *e = sched_reserve(1);
    if (e == NULL) {
        return false;
    }
    e->frames_remaining = at_frame;
    e->kind = (uint8_t)kind;
    if (kind == NT_INJECT_POINTER_UP) {
        e->u.pointer_up.id = id;
    } else {
        e->u.pointer.id = id;
        e->u.pointer.x = x;
        e->u.pointer.y = y;
        e->u.pointer.pressure = pressure;
        e->u.pointer.type = type;
        e->u.pointer.buttons_mask = buttons_mask;
    }
    return true;
}

static bool sched_wheel(float dx, float dy, uint16_t at_frame) {
    sched_entry_t *e = sched_reserve(1);
    if (e == NULL) {
        return false;
    }
    e->frames_remaining = at_frame;
    e->kind = NT_INJECT_WHEEL;
    e->u.wheel.dx = dx;
    e->u.wheel.dy = dy;
    return true;
}

/* Release one due entry into the immediate inject buffer. false == engine invariant violation
   (capacity preflighted + _Static_assert), so the caller asserts. */
static bool sched_release_one(const sched_entry_t *e) {
    switch ((nt_inject_kind_t)e->kind) {
    case NT_INJECT_POINTER_DOWN:
    case NT_INJECT_POINTER_MOVE:
        return nt_input_inject_pointer((nt_inject_kind_t)e->kind, e->u.pointer.id, e->u.pointer.x, e->u.pointer.y, e->u.pointer.pressure, e->u.pointer.type, e->u.pointer.buttons_mask);
    case NT_INJECT_POINTER_UP:
        return nt_input_inject_pointer(NT_INJECT_POINTER_UP, e->u.pointer_up.id, 0.0F, 0.0F, 0.0F, 0, 0);
    case NT_INJECT_WHEEL:
        return nt_input_inject_wheel(e->u.wheel.dx, e->u.wheel.dy);
    case NT_INJECT_KEY:
    case NT_INJECT_POINTER_BUTTONS:
    case NT_INJECT_CHAR:
        break; /* the ui group never schedules these kinds. */
    }
    return false; /* unreachable: ui only enqueues pointer down/move/up + wheel. */
}

/* Release due entries in enqueue order (preserves bot event ordering); survivors decrement + compact. */
static void sched_tick(void) {
    uint32_t out = 0;
    for (uint32_t i = 0; i < s_sched_count; i++) {
        if (s_sched[i].frames_remaining == 0) {
            bool ok = sched_release_one(&s_sched[i]); /* due: release, do NOT carry forward. */
            NT_ASSERT(ok);                            /* buffer >= schedule by _Static_assert; a drop is a build bug. */
            (void)ok;
            continue;
        }
        s_sched[i].frames_remaining--;
        s_sched[out++] = s_sched[i]; /* survivor: decrement + compact down. */
    }
    s_sched_count = out;
}

/* Real sim-advance = g_nt_app.frame changed since last update. Seeded in reset so the first
   post-reset tick doesn't fire spuriously (identical to nt_devapi_input_update). */
static uint32_t s_last_frame;

void nt_devapi_ui_update(void) {
    bool advanced = (g_nt_app.frame != s_last_frame);
    s_last_frame = g_nt_app.frame;
    /* A frozen tick (pause / manual-idle) releases nothing: synthetic input holds until the sim
       advances, so an injected edge can't be missed by a stalled update. */
    if (advanced) {
        sched_tick();
    }
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

void nt_devapi_ui_reset(void) {
    /* The client-disconnect reset hook: drop ONLY devapi-owned TRANSIENT state (the pending schedule +
       advance clock). The host-registered context table is HOST-owned and survives reconnects — it is
       cleared at init (clear_ui_ctx_table in nt_devapi_register_ui), never here. Applied input is
       game-owned (bot==human) so it is NOT released either, mirroring nt_devapi_input_reset. */
    s_sched_count = 0;
    s_last_frame = g_nt_app.frame;
}
// #endregion

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
    double v = nj->valuedouble;
    if (v < 0.0 || v > (double)UINT16_MAX || v != (double)(uint16_t)v) {
        set_bad_params(err, cmd);
        return false;
    }
    *out = (uint16_t)v;
    return true;
}

/* Resolve the target ctx from an optional `ctx` string param. Present -> table lookup (miss =
   bad_params, NEVER assert on bot input, D-15). Absent -> the sole/first context. Empty table ->
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

/* The top-level D-10 coordinate-space metadata, sourced like core `view` (one canonical g_nt_window
   read, no ad-hoc dims elsewhere). projection reflects the ctx's 2D-affine vs 3D-raycast mode. */
static void add_meta(cJSON *result, const nt_ui_context_t *ctx) {
    devapi_add_string(result, "space", "framebuffer");
    devapi_add_number(result, "fb_width", g_nt_window.fb_width);
    devapi_add_number(result, "fb_height", g_nt_window.fb_height);
    devapi_add_number(result, "dpr", (double)g_nt_window.dpr);
    devapi_add_string(result, "projection", nt_ui_context_uses_raycast(ctx) ? "3d" : "2d");
}

/* Serialize one probe node into a fresh JSON object. bounds = {x,y,w,h} framebuffer px (Y-up, the
   L1 convention — any Phase-66 top-left flip is the WRITE-side concern, not this read). */
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

/* ui.tree: IMMEDIATE read (D-14) of the last completed frame's tree. Emits ALL nodes incl.
   invisible/offscreen/disabled carrying their flags (D-05 — the bot filters, not the engine). */
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

/* ui.element: one node resolved by developer string id (D-11). collect-then-select so the returned
   node keeps its parent/label/children consistent (Open-Q3). Unknown/stale id -> bad_params, never
   assert (D-11). */
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

/* ui.contexts: the host-registered context names (D-15). */
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
   single ui.drag can't blow the schedule. Sized under the schedule cap with room for DOWN+UP. */
#ifndef NT_DEVAPI_UI_DRAG_FRAMES_MAX
#define NT_DEVAPI_UI_DRAG_FRAMES_MAX (NT_DEVAPI_UI_SCHED_MAX - 2)
#endif
_Static_assert(NT_DEVAPI_UI_DRAG_FRAMES_MAX + 2 <= NT_DEVAPI_UI_SCHED_MAX, "ui.drag frames+2 must fit the schedule");

/* Resolve a target field to a framebuffer-pixel center. The field is EITHER a string id (D-11:
   nt_ui_id -> nt_ui_get_bbox, miss/empty -> bad_params, never assert) OR an {x,y} object (raw coords,
   isfinite-checked). The bbox center is in Clay layout space (Y-down, top-left) — the SAME space the
   real pointer device feeds nt_ui_begin, so it is fed to the inject path as-is (no Y-flip: get_bbox is
   NOT the GL-Y-up probe convention; both the read-back bbox here and the injected pointer share Clay's
   layout space, so they agree). `key` names the field ("id"/"from"/"to") for the error messages. */
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
        *out_x = bb.x + (bb.width * 0.5F);
        *out_y = bb.y + (bb.height * 0.5F);
        return true;
    }
    if (cJSON_IsObject(jt)) {
        const cJSON *jx = cJSON_GetObjectItemCaseSensitive(jt, "x");
        const cJSON *jy = cJSON_GetObjectItemCaseSensitive(jt, "y");
        if (!parse_finite_coord(jx, "ui: target x must be a finite number", out_x, err) || !parse_finite_coord(jy, "ui: target y must be a finite number", out_y, err)) {
            return false;
        }
        return true;
    }
    set_bad_params(err, "ui: target must be a string id or an {x,y} object");
    return false;
}

/* ui.click: resolve id|{x,y} -> bbox center px -> DOWN@0 + UP@hold via the SAME inject path as
   input.click (reserved synthetic mouse id 0x10000000). Fire-and-forget (D-14); the bot advances with
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
    if (!sched_can_reserve(2U)) { /* whole-or-nothing preflight (see sched_reserve). */
        set_bad_params(err, "ui.click: inject schedule overflow");
        return false;
    }
    sched_pointer(NT_INJECT_POINTER_DOWN, NT_INPUT_INJECT_POINTER_ID_BASE, cx, cy, 1.0F, (uint8_t)NT_POINTER_MOUSE, 1U, 0);
    sched_pointer(NT_INJECT_POINTER_UP, NT_INPUT_INJECT_POINTER_ID_BASE, cx, cy, 0.0F, (uint8_t)NT_POINTER_MOUSE, 0, hold);
    devapi_add_number(result, "queued", 2.0);
    return true;
}

/* ui.scroll: resolve id|{x,y} center -> MOVE there + wheel(dx,dy) notches via the Phase-66 inject
   path (reuse the positioned input.wheel form). Whole-or-nothing: MOVE + wheel reserved together. */
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
    if (!sched_can_reserve(2U)) { /* whole-or-nothing: MOVE to center + wheel together. */
        set_bad_params(err, "ui.scroll: inject schedule overflow");
        return false;
    }
    sched_pointer(NT_INJECT_POINTER_MOVE, NT_INPUT_INJECT_POINTER_ID_BASE, cx, cy, 1.0F, (uint8_t)NT_POINTER_MOUSE, 0, 0);
    sched_wheel(dx, dy, 0);
    devapi_add_number(result, "queued", 2.0);
    return true;
}

/* ui.drag: resolve `from` AND `to` (id|{x,y}) -> DOWN@from + `frames` explicit interpolated MOVE
   points (the handler expands them — NO engine-side interpolation owner, Phase-66 D-10) + UP@to.
   frames CAPPED (DoS guard); whole-or-nothing preflight reserves DOWN + frames moves + UP up front. */
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
    if (!sched_can_reserve(total)) { /* whole-or-nothing preflight (see sched_reserve). */
        set_bad_params(err, "ui.drag: inject schedule overflow");
        return false;
    }
    const uint32_t id = NT_INPUT_INJECT_POINTER_ID_BASE;
    sched_pointer(NT_INJECT_POINTER_DOWN, id, fx, fy, 1.0F, (uint8_t)NT_POINTER_MOUSE, 1U, 0);
    for (uint16_t i = 1; i <= frames; i++) {
        float t = (float)i / (float)frames; /* frames >= 1 here: a 0-frame drag emits no MOVE. */
        float mx = fx + ((tx - fx) * t);
        float my = fy + ((ty - fy) * t);
        sched_pointer(NT_INJECT_POINTER_MOVE, id, mx, my, 1.0F, (uint8_t)NT_POINTER_MOUSE, 1U, i);
    }
    sched_pointer(NT_INJECT_POINTER_UP, id, tx, ty, 0.0F, (uint8_t)NT_POINTER_MOUSE, 0, frames);
    devapi_add_number(result, "queued", (double)total);
    return true;
}
// #endregion

static const nt_devapi_command_desc k_ui_cmds[] = {
    {
        .method = "ui.tree",
        .group = "ui",
        .summary = "IMMEDIATE read of the last completed frame's UI tree (ALL nodes incl. invisible/disabled) + a {space,fb_width,fb_height,dpr,projection} block",
        .params_shape = "{ctx?:string}",
        .result_shape = "{space:string,fb_width:number,fb_height:number,dpr:number,projection:string,nodes:[{id,parent,role,id_string,text,label,child_count,visible,enabled,bounds}]}",
        .frame_behavior = "any",
        .side_effects = "none",
    },
    {
        .method = "ui.element",
        .group = "ui",
        .summary = "one UI node resolved by developer string id (unknown/stale id -> bad_params)",
        .params_shape = "{id:string, ctx?:string}",
        .result_shape = "{space:string,fb_width:number,fb_height:number,dpr:number,projection:string,node:{id,parent,role,id_string,text,label,child_count,visible,enabled,bounds}}",
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
        .summary = "resolve a developer string id (or {x,y}) -> bbox center px -> synthetic pointer down@0 + up@hold (fire-and-forget; advance a frame to apply)",
        .params_shape = "{id:string|{x,y}, hold?:number, ctx?:string}",
        .result_shape = "{queued:number}",
        .frame_behavior = "any",
        .side_effects = "enqueues synthetic pointer down+up events",
    },
    {
        .method = "ui.scroll",
        .group = "ui",
        .summary = "resolve id (or {x,y}) -> element center -> synthetic move + wheel(dx,dy) notches (fire-and-forget)",
        .params_shape = "{id:string|{x,y}, dx?:number, dy?:number, ctx?:string}",
        .result_shape = "{queued:number}",
        .frame_behavior = "any",
        .side_effects = "enqueues a synthetic move + wheel event",
    },
    {
        .method = "ui.drag",
        .group = "ui",
        .summary = "resolve from/to (id|{x,y}) -> down@from + frames interpolated moves + up@to (explicit points, NO interpolation owner; fire-and-forget)",
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
    nt_devapi_ui_reset(); /* fresh schedule + seeded advance clock. */
    /* The only core coupling point: core/net call these generic hooks, never the group symbols.
       The tick hook drives the ui group's OWN s_sched (ui.click/drag/scroll release on a real advance). */
    nt_devapi_register_tick(nt_devapi_ui_update);
    nt_devapi_register_reset(nt_devapi_ui_reset);
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
