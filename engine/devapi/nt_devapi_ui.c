#include <stdint.h>
#include <string.h>

#include "core/nt_assert.h"
#include "devapi/nt_devapi_internal.h"
#include "input/nt_input.h"          /* Plan 05's ui.click/drag/scroll write surface (harmless now). */
#include "input/nt_input_internal.h" /* inject decls live in the INTERNAL header (CORRECTION-3). */
#include "ui/nt_ui.h"                /* nt_ui_probe_collect + the POD node + nt_ui_id. */
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

void nt_devapi_ui_reset(void) {
    /* B-strict disconnect: drop devapi-owned state. The host re-registers its contexts on the next
       init->register cycle, so the name table is devapi-owned and cleared here. */
    for (uint32_t i = 0; i < NT_DEVAPI_UI_CONTEXT_MAX; i++) {
        s_ui_ctx[i].name = NULL;
        s_ui_ctx[i].ctx = NULL;
    }
    s_ui_ctx_count = 0;
}
// #endregion

static void set_bad_params(nt_devapi_error *err, const char *message) {
    err->code = NT_DEVAPI_ERR_BAD_PARAMS;
    err->message = message;
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
};

static const nt_devapi_handler_fn k_ui_handlers[] = {
    cmd_ui_tree,
    cmd_ui_element,
    cmd_ui_contexts,
};
_Static_assert(sizeof(k_ui_cmds) / sizeof(k_ui_cmds[0]) == sizeof(k_ui_handlers) / sizeof(k_ui_handlers[0]), "ui: descriptor/handler arrays must have equal length");

void nt_devapi_register_ui(void) {
    nt_devapi_ui_reset(); /* fresh name table each init->register. */
    nt_devapi_register_reset(nt_devapi_ui_reset);
    /* The tick hook + s_sched scheduler arrive in Plan 05 with the ui.click/drag/scroll writes. */
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
