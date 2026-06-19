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

void nt_devapi_register_ui(void) {
    /* Filled in Task 2 (descriptors + handlers). The reset hook keeps the name table B-strict. */
    nt_devapi_ui_reset();
    /* resolve_ctx / set_bad_params are consumed by the Task-2 handlers; reference them so the
       Task-1 scaffold builds clean under -Werror=unused-function. */
    (void)resolve_ctx;
    (void)set_bad_params;
}

#endif /* NT_DEVAPI_GROUP_UI */
