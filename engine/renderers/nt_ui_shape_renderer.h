#ifndef NT_UI_SHAPE_RENDERER_H
#define NT_UI_SHAPE_RENDERER_H

#include "core/nt_types.h"
#include "material/nt_material.h"

#ifndef NT_UI_SHAPE_RENDERER_MAX_PIPELINES
#define NT_UI_SHAPE_RENDERER_MAX_PIPELINES 8
#endif

#ifndef NT_UI_SHAPE_RENDERER_MAX_DRAW_CMDS
#define NT_UI_SHAPE_RENDERER_MAX_DRAW_CMDS 64
#endif

#define NT_UI_SHAPE_MODE_BOX 1U
#define NT_UI_SHAPE_MODE_RADIAL 2U
#define NT_UI_SHAPE_MODE_SHADOW 3U

/* One analytic UI quad. The vertex shader places local point p at
 * origin + p.x * axis_x + p.y * axis_y for p in [-pad, size + pad] layout pixels.
 * Attribute locations follow the field order (0..9). */
typedef struct {
    float origin[3];
    float width;
    float axis_x[3];
    float height;
    float axis_y[3];
    float pad;
    float geometry[4]; /* BOX radii tl,tr,br,bl; RADIAL start, end, inner radius */
    float widths[4];   /* BOX border l,t,r,b; SHADOW spread, softness, reach */
    float user[4];     /* Read only by game shaders. */
    uint8_t color[4];  /* Fill or shadow RGB; alpha is the inherited opacity. */
    uint8_t endpoint[4];
    uint8_t border[4];
    uint8_t control[4]; /* Fill alpha, mode, gradient, flags. */
} nt_ui_shape_instance_t;
_Static_assert(sizeof(nt_ui_shape_instance_t) == 112, "UI shape instance is 112 bytes");

/* max_instances sizes the CPU staging and the instance buffer; a fuller frame flushes early. */
nt_result_t nt_ui_shape_renderer_init(uint32_t max_instances);
void nt_ui_shape_renderer_shutdown(void);
/* Drops staged instances and cached pipelines, then rebuilds the GPU buffer. */
nt_result_t nt_ui_shape_renderer_restore_gpu(void);

/* The material supplies program, blend, depth, params and textures; it declares no vertex layout.
 * Frame UBOs (view_proj) are bound by the game, as for sprites. */
void nt_ui_shape_renderer_set_material(nt_material_t mat);
/* Copies the instance into the open command; call set_material first. */
void nt_ui_shape_renderer_emit(const nt_ui_shape_instance_t *instance);
/* Uploads staged instances once and issues one instanced draw per command. */
void nt_ui_shape_renderer_flush(void);

#ifdef NT_TEST_ACCESS
/* Every emit since the last reset, across flushes (first 256 kept), and the instanced draws issued. */
void nt_ui_shape_renderer_test_reset(void);
uint32_t nt_ui_shape_renderer_test_emit_count(void);
const nt_ui_shape_instance_t *nt_ui_shape_renderer_test_emitted(uint32_t index);
uint32_t nt_ui_shape_renderer_test_draw_count(void);
#endif

#endif
