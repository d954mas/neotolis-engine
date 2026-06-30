#ifndef NT_UI_SLIDER_H
#define NT_UI_SLIDER_H

/* Interactive slider (float + int): track + fill + thumb, each a per-state
 * nt_atlas_region_ref_t with inherit-from-idle and no-art skip. Value lives in the GAME
 * (float* / int*); the engine stores only the eased visual (value_t) and the press-scoped
 * drag offset. Press ON the thumb grabs with offset (relative drag); press on the track
 * jumps to the click point. Thumb tracks the value 1:1 during drag; a game-driven value
 * change eases via value_speed. Thumb screen position is exposed for game drag-bubbles. */

#include <stdbool.h>
#include <stdint.h>

#include "atlas/nt_atlas.h" /* nt_atlas_region_ref_t */
#include "clay.h"
#include "resource/nt_resource.h"
#include "ui/nt_ui.h"      /* nt_ui_element_data_t */
#include "ui/nt_ui_fill.h" /* fill mode + direction */

typedef struct nt_ui_context nt_ui_context_t;

extern const nt_ui_widget_def_t NT_UI_SLIDER_DEF;

/* 4-state index: idle/hover/pressed/disabled (inherit-from-idle). */
enum { NT_UI_SLIDER_IDLE = 0, NT_UI_SLIDER_HOVER, NT_UI_SLIDER_PRESSED, NT_UI_SLIDER_DISABLED };

/* One cell of the 4-state grid. track/fill/thumb are atomic nt_atlas_region_ref_t:
 *   non-idle atlas.id==0 -> inherit the idle cell's WHOLE ref;
 *   idle terminal atlas.id==0 -> NO ART (that part skips its IMAGE). */
typedef struct {
    nt_atlas_region_ref_t track, fill, thumb;
    uint32_t track_tint, fill_tint, thumb_tint; /* 0xFFFFFFFF = no tint */
    float opacity;                              /* whole-widget, INHERITS to children */
} nt_ui_slider_cell_t;
_Static_assert(sizeof(nt_ui_slider_cell_t) == 64, "nt_ui_slider_cell_t stable ABI (3x16 ref + 3 tint + 1 float)");

/* Drag AXIS. orientation is the source of truth; fill_direction is the anchor WITHIN
 * the axis (VERTICAL -> BOTTOM_UP/TOP_DOWN, HORIZONTAL -> LTR only — RTL is rejected, as
 * its fill would right-anchor while drag/thumb/thumb_pos stay LTR). */
typedef enum { NT_UI_SLIDER_HORIZONTAL = 0, NT_UI_SLIDER_VERTICAL } nt_ui_slider_orientation_t;

typedef struct {
    nt_ui_slider_cell_t states[4];          /* idle/hover/pressed/disabled, inherit-from-idle */
    float track_w, track_h;                 /* track FIXED layout px; asserted > 0 */
    float thumb_w, thumb_h;                 /* thumb FIXED layout px; asserted finite >= 0 */
    nt_ui_fill_mode_t fill_mode;            /* STRETCH (slice9) | CROP */
    nt_ui_fill_direction_t fill_direction;  /* anchor within axis; LTR default (vertical: BOTTOM_UP) */
    nt_ui_slider_orientation_t orientation; /* the AXIS; HORIZONTAL default */
    float state_speed;                      /* hover/press/disabled ease (nt_ui_anim state group) */
    float value_speed;                      /* GAME-driven preset ease (0 = instant); 1:1 during drag */
    /* Touch-target inflation (button parity). The thumb-overhang pad auto-grows along the
     * CROSS axis: horizontal grows top/bottom by (thumb_h-track_h)/2, vertical grows left/right
     * by (thumb_w-track_w)/2 — so the overhang is always clickable even at zero pad. */
    int16_t hit_padding_lrtb[4];
} nt_ui_slider_style_t;
_Static_assert(sizeof(nt_ui_slider_style_t) == 304, "nt_ui_slider_style_t stable ABI (296 + 4B orientation, 8-byte aligned)");

/* Thumb screen position for game-drawn drag-bubbles. found=false if the
 * slider was not declared the immediately preceding frame. */
typedef struct {
    float x, y;
    bool found;
} nt_ui_slider_thumb_t;

/* Returns true the frame the (quantized) value changed. value lives in the game.
 * step quantizes onto the min+k*step grid (float: 0 = continuous; int always quantizes,
 * step <= 0 falls back to 1). min != max required. Engine owns .id/.clip/.userData on the decl; data must NOT set
 * HAS_TRANSFORM/HAS_OPACITY. enabled=false short-circuits interaction + dims.
 *
 * Out-of-range *value: clamped to [min,max] AND written back (returns true that
 * frame) so game memory matches what's drawn (Unity property-clamp semantics).
 *
 * Commit-on-release: live drags return true every frame. To act only when the
 * user lets go (e.g. apply a setting once), poll the release edge instead:
 *   nt_ui_slider_int(...);  // ignore the live return
 *   if (nt_ui_query_interaction(ctx, id).released_now) commit(value); */
bool nt_ui_slider_float(nt_ui_context_t *ctx, const nt_ui_element_data_t *data, uint8_t label_layer, uint32_t id, const char *label, float *value, float min, float max, float step,
                        nt_ui_slider_style_t *style, const Clay_ElementDeclaration *decl, bool enabled);

bool nt_ui_slider_int(nt_ui_context_t *ctx, const nt_ui_element_data_t *data, uint8_t label_layer, uint32_t id, const char *label, int *value, int min, int max, int step, nt_ui_slider_style_t *style,
                      const Clay_ElementDeclaration *decl, bool enabled);

/* Thumb screen pos for game drag-bubbles. Reads the prev-frame track bbox + the last
 * value fraction. {0,0,false} if the slider was not declared last frame. */
nt_ui_slider_thumb_t nt_ui_slider_thumb_pos(const nt_ui_context_t *ctx, uint32_t id);

/* Valid baseline: every cell opacity = 1, no tint, sensible sizes/speeds, STRETCH/LTR/HORIZONTAL.
 * Caller supplies art (track/fill/thumb refs). Avoids the zero-init trap. */
nt_ui_slider_style_t nt_ui_slider_style_defaults(void);

#endif /* NT_UI_SLIDER_H */
