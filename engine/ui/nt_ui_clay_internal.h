#ifndef NT_UI_CLAY_INTERNAL_H
#define NT_UI_CLAY_INTERNAL_H

/* Phase 56 ext: nt_ui_clay_internal -- exclusive TU that defines
 * CLAY_IMPLEMENTATION. Owns Clay's static globals + private-typed bodies
 * (Clay_Context fields, Clay__GetHashMapItem, Clay__ElementIsOffscreen, the
 * verbatim Clay-debug-view port, etc.) so the rest of the engine can stay on
 * Clay's PUBLIC API. nt_ui.c (walker, hit-test, push/pop_transform, begin/end,
 * interaction CQS) and nt_ui_inspector.c (public toggle API + post-walk overlay)
 * both include <clay.h> via the public header path; neither defines
 * CLAY_IMPLEMENTATION.
 *
 * Architecture note: the verbatim Clay-debug-view emit body (cdv_* helpers +
 * nt_ui_internal_emit_inspector_layout) lives in nt_ui_clay_internal.c rather
 * than nt_ui_inspector.c -- the body touches ~30 Clay private types and
 * file-scope statics (Clay_LayoutElement, Clay_LayoutElementHashMapItem,
 * Clay_Context fields, Clay__GetHashMapItem, Clay__ElementIsOffscreen,
 * Clay__DebugView_ScrollViewItemLayoutConfig, etc.). Wrapping each access in
 * a thin forwarder would mean ~40 wrappers AND a deeply invasive rewrite of
 * the verbatim port that risks breaking its byte-for-byte parity with Clay's
 * Clay__RenderDebugView (clay.h:3392-3800). Keeping it next to
 * CLAY_IMPLEMENTATION preserves the verbatim shape, makes "anything that
 * needs Clay privates lives in ONE TU" the rule, and leaves nt_ui_inspector.c
 * as a clean public API + Clay-agnostic overlay surface. nt_ui_inspector.c
 * forwards to nt_ui_internal_emit_inspector_layout via the prototype below. */

#include <stdbool.h>
#include <stdint.h>

/* Public Clay surface. nt_ui_clay_internal.c additionally defines
 * CLAY_IMPLEMENTATION before re-including clay.h to pull in the private
 * surface. */
#include "clay.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Forward decl for nt_ui_context_t -- avoids dragging nt_ui_internal.h into
 * every consumer of this header. Concrete struct stays in nt_ui_internal.h. */
typedef struct nt_ui_context nt_ui_context_t;

// #region clay module-level access (used by nt_ui.c)
/* Clay__MeasureText / Clay__defaultMax* live inside CLAY_IMPLEMENTATION, so
 * nt_ui.c (which no longer defines CLAY_IMPLEMENTATION) reaches them via
 * these thin forwarders. Each is a single-line read/write of the underlying
 * static. Module init/shutdown + min-arena-size + create_context use these
 * to thread the text-measure callback and the default element-count when
 * sizing Clay's arena. */
void nt_ui_clay_priv_set_measure_text_cb(Clay_Dimensions (*cb)(Clay_StringSlice, Clay_TextElementConfig *, void *));
int32_t nt_ui_clay_priv_default_max_element_count(void);
int32_t nt_ui_clay_priv_default_max_measure_text_word_cache_count(void);
// #endregion

// #region clay_context primitive accessors (used by nt_ui.c marker emit + test_access)
/* Clay_Context fields used outside CLAY_IMPLEMENTATION. Each returns a
 * primitive so the caller never needs to deref Clay_Context directly. */
int32_t nt_ui_clay_priv_layout_elements_length(Clay_Context *clay);
float nt_ui_clay_priv_pointer_x(Clay_Context *clay);
float nt_ui_clay_priv_pointer_y(Clay_Context *clay);
/* 1 if state == PRESSED or PRESSED_THIS_FRAME, else 0. */
int nt_ui_clay_priv_pointer_pressed(Clay_Context *clay);
// #endregion

// #region in-frame ctx getter (used by clay_internal.c only; nt_ui.c owns the storage)
/* The module-global "currently in-frame ctx" lives in nt_ui.c (set/cleared by
 * nt_ui_begin/nt_ui_end). clay_internal.c's nt_ui_internal_current_open_element_id
 * and nt_ui_internal_last_emitted_element_id read it via this getter. */
nt_ui_context_t *nt_ui_internal_get_inframe_ctx(void);
// #endregion

// #region inspector emit-layout entry (called by nt_ui_inspector_emit_layout forwarder)
/* The verbatim Clay debug-view emit body lives in nt_ui_clay_internal.c (it
 * touches Clay private types). nt_ui_inspector.c forwards to this prototype.
 * Asserts ctx != NULL, ctx->in_frame, ctx->clay != NULL. */
void nt_ui_internal_emit_inspector_layout_extern(nt_ui_context_t *ctx);
// #endregion

#ifdef __cplusplus
}
#endif

#endif /* NT_UI_CLAY_INTERNAL_H */
