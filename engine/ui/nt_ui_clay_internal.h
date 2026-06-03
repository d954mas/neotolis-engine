#ifndef NT_UI_CLAY_INTERNAL_H
#define NT_UI_CLAY_INTERNAL_H

/* Owns CLAY_IMPLEMENTATION; everywhere else uses Clay's public API.
 * Inspector emit body lives here too (touches Clay private symbols). */

#include <stdbool.h>
#include <stdint.h>

#include "clay.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct nt_ui_context nt_ui_context_t;

// #region clay module-level access
void nt_ui_clay_priv_set_measure_text_cb(Clay_Dimensions (*cb)(Clay_StringSlice, Clay_TextElementConfig *, void *));
int32_t nt_ui_clay_priv_default_max_element_count(void);
int32_t nt_ui_clay_priv_default_max_measure_text_word_cache_count(void);
/* Widgets need to inject code (e.g. widget_register) between open and configure. */
void nt_ui_clay_priv_open_element(void);
void nt_ui_clay_priv_configure_open_element(Clay_ElementDeclaration decl);
void nt_ui_clay_priv_close_element(void);
// #endregion

// #region clay_context primitive accessors
int32_t nt_ui_clay_priv_layout_elements_length(Clay_Context *clay);
float nt_ui_clay_priv_pointer_x(Clay_Context *clay);
float nt_ui_clay_priv_pointer_y(Clay_Context *clay);
/* 1 if state == PRESSED or PRESSED_THIS_FRAME. */
int nt_ui_clay_priv_pointer_pressed(Clay_Context *clay);

/* Slot index is stable across frames — safe as an index into ctx->hit_baked. */
int32_t nt_ui_clay_priv_hashmap_slot_for_id(Clay_Context *clay, uint32_t id);
/* May shift between frames as Clay reuses array slots — DO NOT cache. */
int32_t nt_ui_clay_priv_layout_index_for_id(Clay_Context *clay, uint32_t id);
/* Persistent hashmap survives BeginLayout's ephemeral reset. */
bool nt_ui_clay_priv_bbox_for_id(Clay_Context *clay, uint32_t id, float *x, float *y, float *w, float *h);
// #endregion

// #region in-frame ctx getter
nt_ui_context_t *nt_ui_internal_get_inframe_ctx(void);
// #endregion

// #region inspector emit-layout entry
void nt_ui_internal_emit_inspector_layout_extern(nt_ui_context_t *ctx);
// #endregion

#ifdef __cplusplus
}
#endif

#endif /* NT_UI_CLAY_INTERNAL_H */
