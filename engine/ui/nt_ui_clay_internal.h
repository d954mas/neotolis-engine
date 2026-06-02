#ifndef NT_UI_CLAY_INTERNAL_H
#define NT_UI_CLAY_INTERNAL_H

/* Exclusive CLAY_IMPLEMENTATION TU. Owns Clay private types + statics; the rest
 * of the engine uses Clay's public API. The inspector emit body lives here
 * (~30 Clay private symbols); nt_ui_inspector.c forwards through the extern
 * prototype below. */

#include <stdbool.h>
#include <stdint.h>

#include "clay.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Forward decl avoids dragging nt_ui_internal.h into every consumer. */
typedef struct nt_ui_context nt_ui_context_t;

// #region clay module-level access
/* Thin forwarders for Clay__* statics that live inside CLAY_IMPLEMENTATION. */
void nt_ui_clay_priv_set_measure_text_cb(Clay_Dimensions (*cb)(Clay_StringSlice, Clay_TextElementConfig *, void *));
int32_t nt_ui_clay_priv_default_max_element_count(void);
int32_t nt_ui_clay_priv_default_max_measure_text_word_cache_count(void);
/* Open/configure wrappers — widgets need to inject code (e.g. widget_register)
 * between open and configure, which the CLAY({...}) macro doesn't allow. */
void nt_ui_clay_priv_open_element(void);
void nt_ui_clay_priv_configure_open_element(Clay_ElementDeclaration decl);
void nt_ui_clay_priv_close_element(void);
// #endregion

// #region clay_context primitive accessors
/* Each returns a primitive so callers never deref Clay_Context directly. */
int32_t nt_ui_clay_priv_layout_elements_length(Clay_Context *clay);
float nt_ui_clay_priv_pointer_x(Clay_Context *clay);
float nt_ui_clay_priv_pointer_y(Clay_Context *clay);
/* 1 if state == PRESSED or PRESSED_THIS_FRAME, else 0. */
int nt_ui_clay_priv_pointer_pressed(Clay_Context *clay);
// #endregion

// #region in-frame ctx getter
/* Storage lives in nt_ui.c (set/cleared by begin/end); read here. */
nt_ui_context_t *nt_ui_internal_get_inframe_ctx(void);
// #endregion

// #region inspector emit-layout entry
/* Asserts ctx != NULL, ctx->in_frame, ctx->clay != NULL. */
void nt_ui_internal_emit_inspector_layout_extern(nt_ui_context_t *ctx);
// #endregion

#ifdef __cplusplus
}
#endif

#endif /* NT_UI_CLAY_INTERNAL_H */
