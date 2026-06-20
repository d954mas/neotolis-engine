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
    float offset_x; /* physical px margin; 0 for STRETCH/EXPAND; negative in CROP */
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

/* Physical pointer -> logical. x/y/dx/dy scaled; wheel/buttons pass through. Ctx-free, framework-
 * agnostic primitive — kept for callers that convert outside nt_ui. The ctx-owned path
 * (nt_ui_set_viewport + nt_ui_viewport_from_scale) is preferred for nt_ui apps. */
nt_pointer_t nt_ui_scale_apply_pointer(const nt_ui_scale_t *s, nt_pointer_t physical);

/* Bridges a scale to the nt_ui ctx viewport (device content rect): offset = margins, size = logical *
 * scale. Feed to nt_ui_set_viewport so the ctx converts the raw device pointer internally. */
nt_ui_viewport_t nt_ui_viewport_from_scale(const nt_ui_scale_t *s);

/* Ortho bounds mapping logical Clay-space onto the full fb (including margins). */
nt_ui_scale_ortho_t nt_ui_scale_ortho(const nt_ui_scale_t *s);

nt_ui_target_t nt_ui_scale_make_target(const nt_ui_scale_t *s);

/* Standard UI projection used by 2D demos: logical x=[0,w], y=[0,h] maps to
 * GL Y-up world. nt_ui_walk folds Clay Y-down into world_mat4 for 2D/screen passes. */
void nt_ui_make_screen_view_proj(float logical_w, float logical_h, float out_view_proj[16]);

#endif /* NT_UI_SCALE_H */
