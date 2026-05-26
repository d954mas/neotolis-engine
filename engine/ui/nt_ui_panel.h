#ifndef NT_UI_PANEL_H
#define NT_UI_PANEL_H

/* Panel and group container widgets. Both push_transform + push_opacity on
 * begin and pop both on end. Panel has an IMAGE background; group is invisible.
 * transform may be NULL (= identity). */

#include <stdint.h>

#include "clay.h"
#include "ui/nt_ui.h"       /* nt_ui_element_data_t, nt_ui_transform_t */
#include "ui/nt_ui_image.h" /* nt_ui_image_style_t */

typedef struct nt_ui_context nt_ui_context_t;

/* Panel: image background container + transform/opacity inheritance.
 * Internally: push_transform + push_opacity + Clay IMAGE container.
 * transform may be NULL (= identity). style uses nt_ui_image_style_t. */
void nt_ui_panel_begin(nt_ui_context_t *ctx, const nt_ui_element_data_t *data, nt_resource_t atlas, uint32_t region_index, const nt_ui_image_style_t *style, const nt_ui_transform_t *transform,
                       float opacity);
void nt_ui_panel_end(nt_ui_context_t *ctx);

/* Group: invisible container for transform/opacity inheritance WITHOUT image.
 * Internally: push_transform + push_opacity + Clay container (no IMAGE).
 * transform may be NULL (= identity). */
void nt_ui_group_begin(nt_ui_context_t *ctx, const nt_ui_element_data_t *data, const nt_ui_transform_t *transform, float opacity);
void nt_ui_group_end(nt_ui_context_t *ctx);

#endif /* NT_UI_PANEL_H */
