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

/* Inspector descriptors. */
extern const nt_ui_widget_def_t NT_UI_PANEL_DEF;
extern const nt_ui_widget_def_t NT_UI_GROUP_DEF;

/* Image-background container. `decl` optional (NULL = FIT default).
 * Engine OWNS .image / .backgroundColor / .userData — caller leaves them NULL. */
void nt_ui_panel_begin(nt_ui_context_t *ctx, const nt_ui_element_data_t *data, nt_resource_t atlas, uint32_t region_index, const nt_ui_image_style_t *style, const Clay_ElementDeclaration *decl);
void nt_ui_panel_end(nt_ui_context_t *ctx);

/* Invisible container (no image). Engine OWNS .custom (anchor) + .userData. */
void nt_ui_group_begin(nt_ui_context_t *ctx, const nt_ui_element_data_t *data, const Clay_ElementDeclaration *decl);
void nt_ui_group_end(nt_ui_context_t *ctx);

#endif /* NT_UI_PANEL_H */
