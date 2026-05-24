#ifndef NT_UI_SCALE_H
#define NT_UI_SCALE_H

/* nt_ui_scale: maps reference resolution to framebuffer for adaptive UI.
 * Pure math; caller recomputes per frame and feeds ortho + begin + target. */

#include "input/nt_input.h"
#include "ui/nt_ui.h" /* nt_ui_target_t */

typedef enum {
    NT_UI_SCALE_STRETCH,   /* logical = ref; scale_x/y differ -- aspect distorted */
    NT_UI_SCALE_LETTERBOX, /* fit inside, black bars; logical = ref */
    NT_UI_SCALE_CROP,      /* fit outside, edges clipped; logical = ref */
    NT_UI_SCALE_EXPAND,    /* fit inside; logical grows past ref to fill window */
} nt_ui_scale_mode_t;

typedef struct {
    float ref_w;
    float ref_h;
    nt_ui_scale_mode_t mode;
} nt_ui_scale_desc_t;

typedef struct {
    float logical_w; /* feed to glm_ortho + nt_ui_begin + target.viewport */
    float logical_h;
    float scale_x; /* physical / logical (scale_x == scale_y unless STRETCH) */
    float scale_y;
    float offset_x; /* letterbox margin in physical px; 0 for non-letterbox */
    float offset_y;
    float fb_w; /* physical framebuffer dims -- echoed back for target build */
    float fb_h;
} nt_ui_scale_t;

/* Ortho projection bounds (Y-up world). Feed directly to glm_ortho. */
typedef struct {
    float left;
    float right;
    float bottom;
    float top;
} nt_ui_scale_ortho_t;

nt_ui_scale_t nt_ui_compute_scale(const nt_ui_scale_desc_t *desc, float fb_w, float fb_h);

/* Physical pointer -> logical. dx/dy/wheel/buttons pass through unchanged. */
nt_pointer_t nt_ui_scale_apply_pointer(const nt_ui_scale_t *s, nt_pointer_t physical);

/* Ortho bounds mapping logical Clay-space onto the full fb (including margins). */
nt_ui_scale_ortho_t nt_ui_scale_ortho(const nt_ui_scale_t *s);

nt_ui_target_t nt_ui_scale_make_target(const nt_ui_scale_t *s);

#endif /* NT_UI_SCALE_H */
