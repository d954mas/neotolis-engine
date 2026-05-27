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
