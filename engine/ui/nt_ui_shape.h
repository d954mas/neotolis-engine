#ifndef NT_UI_SHAPE_H
#define NT_UI_SHAPE_H

#include "clay.h"
#include "ui/nt_ui.h"

/* Typed tail shared by the shape shader and the mode-zero sprite uber shader. */
typedef struct {
    float layout[4]; /* Width, height, affine padding/projective scale, center X. */
    float geometry[4];
    float widths[4];
    float center_y;
    uint8_t endpoint[4];
    uint8_t border[4];
    uint8_t control[4]; /* Fill alpha, mode, gradient, empty-interior/projective flags. */
} nt_ui_shape_attrs_t;

typedef struct {
    float position[3];
    uint16_t texcoord[2];
    uint8_t color[4];
    nt_ui_shape_attrs_t attrs;
} nt_ui_shape_vertex_t;

_Static_assert(sizeof(nt_ui_shape_attrs_t) == 64, "shape tail is 64 bytes");
_Static_assert(sizeof(nt_ui_shape_vertex_t) == 84, "shape vertex is 84 bytes");
/* Material vertex_layout for shape materials (dedicated shape and sprite uber shaders). */
extern const nt_vertex_layout_t NT_UI_SHAPE_VERTEX_LAYOUT;

typedef enum { NT_UI_SHAPE_BOX = 1, NT_UI_SHAPE_RADIAL = 2 } nt_ui_shape_kind_t;
typedef enum { NT_UI_SHAPE_SOLID = 0, NT_UI_SHAPE_HORIZONTAL = 1, NT_UI_SHAPE_VERTICAL = 2 } nt_ui_shape_gradient_t;

typedef struct {
    float top_left, top_right, bottom_right, bottom_left; /* Finite, nonnegative layout pixels. */
} nt_ui_shape_radii_t;

typedef struct {
    float left, top, right, bottom;
} nt_ui_shape_border_widths_t;

typedef struct {
    uint32_t color0, color1, border_color;     /* Straight 0xAABBGGRR; zero is transparent. */
    nt_ui_shape_border_widths_t border_widths; /* Inside; finite, nonnegative layout pixels. */
    nt_ui_shape_gradient_t gradient;
} nt_ui_shape_paint_t;

typedef struct {
    uint32_t color;                             /* Drawn with the shape material; alpha zero disables. */
    float offset_x, offset_y, spread, softness; /* Layout pixels; finite-support shadow. */
} nt_ui_shape_shadow_t;

typedef struct {
    nt_material_t material;
    nt_ui_shape_kind_t kind;
    union {
        nt_ui_shape_radii_t box;
        struct {
            float angle_start, angle_end; /* Finite radians in local UI space: 0 right, +pi/2 down, clockwise+. */
            float inner_radius_norm;      /* [0, 1); 0 is a disc. */
        } radial;
    };
    nt_ui_shape_paint_t paint;
    nt_ui_shape_shadow_t shadow; /* BOX only; alpha zero disables. */
} nt_ui_shape_style_t;

/* Opaque white BOX; the game must assign a shape material before use. */
static inline nt_ui_shape_style_t nt_ui_shape_style_defaults(void) { return (nt_ui_shape_style_t){.kind = NT_UI_SHAPE_BOX, .paint = {.color0 = 0xFFFFFFFFU, .color1 = 0xFFFFFFFFU}}; }

extern const nt_ui_widget_def_t NT_UI_SHAPE_DEF;

/* Requires an active frame on non-NULL ctx and a non-NULL style, copied into frame
 * scratch until nt_mem_scratch_reset. The game keeps materials/programs alive for every walk.
 * data may be NULL; otherwise it is borrowed through nt_ui_end and all walks of this frame.
 * decl may be NULL; when present it is copied during the call and controls layout, clip and
 * floating. id/image/background/userData are owned here. begin/end enclose child declarations. */
void nt_ui_shape(nt_ui_context_t *ctx, const nt_ui_element_data_t *data, const nt_ui_shape_style_t *style, const Clay_ElementDeclaration *decl);
void nt_ui_shape_begin(nt_ui_context_t *ctx, const nt_ui_element_data_t *data, const nt_ui_shape_style_t *style, const Clay_ElementDeclaration *decl);
void nt_ui_shape_end(nt_ui_context_t *ctx);

#endif
