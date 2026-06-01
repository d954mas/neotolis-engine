#ifndef NT_UI_PANEL_H
#define NT_UI_PANEL_H

/* Panel and group container widgets. Panel has an IMAGE background; group is
 * invisible. Game code uses explicit push_transform/push_opacity around these
 * when transforms are needed (explicit over implicit). */

#include <stdint.h>

#include "clay.h"
#include "ui/nt_ui.h"       /* nt_ui_element_data_t */
#include "ui/nt_ui_image.h" /* nt_ui_image_style_t */

typedef struct nt_ui_context nt_ui_context_t;

/* EXPERIMENTAL (Phase 56 ext): API surface may change in v1.9. Used by
 * ui_buttons_demo and inspector internals. Game code adopting this should
 * pin the engine version.
 *
 * Inspector descriptors: pill name "nt_panel" / "nt_group" + color shown in
 * the element tree. Engine widget defs use the "nt_" prefix to disambiguate
 * from Clay's own config-type pills on the same row. Game code may compare
 * pointer identity against these symbols. */
extern const nt_ui_widget_def_t NT_UI_PANEL_DEF;
extern const nt_ui_widget_def_t NT_UI_GROUP_DEF;

/* Panel: image background container.
 * Internally: Clay IMAGE container. No transform/opacity -- use explicit
 * push_transform/push_opacity around panel_begin/end when needed. */
void nt_ui_panel_begin(nt_ui_context_t *ctx, const nt_ui_element_data_t *data, nt_resource_t atlas, uint32_t region_index, const nt_ui_image_style_t *style);
void nt_ui_panel_end(nt_ui_context_t *ctx);

/* Group: invisible container WITHOUT image.
 * Internally: Clay container with transparent bg. No transform/opacity --
 * use explicit push_transform/push_opacity when needed. */
void nt_ui_group_begin(nt_ui_context_t *ctx, const nt_ui_element_data_t *data);
void nt_ui_group_end(nt_ui_context_t *ctx);

#endif /* NT_UI_PANEL_H */
