#ifndef NT_UI_RICH_FX_H
#define NT_UI_RICH_FX_H

/* Per-ATOM rich-text effects (FX-67-01/02). An effect fn runs at EMIT, once per solved atom
 * (glyph AND image AND object -- D-67-17, per-atom, NOT #184's per-glyph/TEXT-only framing),
 * and is VISUAL-ONLY: it shifts/tints/scales/hides the atom but never changes the solver's
 * layout (line/box stay put, D-67-19). The animation clock is passed IN BY THE GAME (`time`);
 * there is no global frame clock (RESEARCH Pitfall 4).
 *
 * The stock catalog (wave/shake/rainbow/pulse/fade_in) is a STARTING set the game registers
 * piecemeal by name into the tagset (nt_ui_rich_tagset_register_effect) -- NO register_defaults
 * bundle (D-67-12). Per-effect tuning is compile-time constants (NOT tag params, RESEARCH A5);
 * a game that needs tunable amplitude registers its OWN fn. */

#include <stdbool.h>
#include <stdint.h>

#include "ui/nt_ui_rich_text.h" /* nt_rich_atom_kind_t */

/* The effect transform applied to one atom at emit. offset shifts the quad / draw() xy;
 * color multiplies the glyph tint / image a_tint; scale scales about the atom center;
 * visible==false skips the atom emit (fade_in / typewriter-per-glyph). */
typedef struct {
    float offset_x; /*  0: px shift of the atom quad / draw() x */
    float offset_y; /*  4: px shift of the atom quad / draw() y */
    float color[4]; /*  8: RGBA multiply against the atom's base color (text tint / image a_tint) */
    float scale;    /* 24: scale about the atom center (1 = identity) */
    bool visible;   /* 28: false -> skip the atom emit */
} nt_ui_rich_fx_result_t;
_Static_assert(sizeof(nt_ui_rich_fx_result_t) == 32, "nt_ui_rich_fx_result_t stable ABI (6 float + 1 bool + pad)");

/* Per-atom effect callback. base_xy/base_wh/base_color are the solver's resolved atom box +
 * color (read-only inputs); time is the game-owned clock (seconds); hovered is true only for
 * the atoms of the currently-hovered link (link hover gates effects, D-67-24). The fn returns
 * the visual-only transform; it MUST NOT mutate layout. */
typedef nt_ui_rich_fx_result_t (*nt_ui_rich_fx_fn)(uint32_t atom_idx, nt_rich_atom_kind_t kind, const float base_xy[2], const float base_wh[2], const float base_color[4], float time, bool hovered);

/* The identity result: no shift, no tint change, no scale, visible. Use as the base a stock fn
 * mutates so a future field addition stays forward-compatible (no bare {0}, alpha 0 != opaque). */
nt_ui_rich_fx_result_t nt_ui_rich_fx_identity(const float base_color[4]);

/* ---- Stock catalog (D-67-20). Per-effect constants are compile-time (RESEARCH A5). ---- */
/* offset.y = AMP * sin(time*SPEED + atom_idx*PHASE) -- a per-atom phase-shifted vertical wave. */
nt_ui_rich_fx_result_t nt_ui_rich_fx_wave(uint32_t atom_idx, nt_rich_atom_kind_t kind, const float base_xy[2], const float base_wh[2], const float base_color[4], float time, bool hovered);
/* offset.xy = AMP * (hash(atom_idx, floor(time*RATE)) - 0.5) -- a per-atom deterministic jitter. */
nt_ui_rich_fx_result_t nt_ui_rich_fx_shake(uint32_t atom_idx, nt_rich_atom_kind_t kind, const float base_xy[2], const float base_wh[2], const float base_color[4], float time, bool hovered);
/* color = hsv((atom_idx*PHASE + time*SPEED) mod 1, 1, 1) -- a per-atom hue cycle (alpha kept). */
nt_ui_rich_fx_result_t nt_ui_rich_fx_rainbow(uint32_t atom_idx, nt_rich_atom_kind_t kind, const float base_xy[2], const float base_wh[2], const float base_color[4], float time, bool hovered);
/* scale = 1 + AMP * sin(time*SPEED) -- a uniform breathing pulse about each atom's center. */
nt_ui_rich_fx_result_t nt_ui_rich_fx_pulse(uint32_t atom_idx, nt_rich_atom_kind_t kind, const float base_xy[2], const float base_wh[2], const float base_color[4], float time, bool hovered);
/* color.a *= clamp((time - atom_idx*STAGGER) / DUR, 0, 1) -- a staggered per-atom fade-in;
 * alpha 0 -> visible=false so the atom is skipped entirely until its window opens. */
nt_ui_rich_fx_result_t nt_ui_rich_fx_fade_in(uint32_t atom_idx, nt_rich_atom_kind_t kind, const float base_xy[2], const float base_wh[2], const float base_color[4], float time, bool hovered);

/* Resolve a stock effect_id (1-based catalog index; 0 = none) to its fn, or NULL if out of range.
 * The composed style carries the effect_id; the emit path maps it to a fn through this table so a
 * game that registered a stock effect by name gets the matching curve. */
nt_ui_rich_fx_fn nt_ui_rich_fx_stock(uint8_t effect_id);

/* ---- Stock catalog ids (the effect_id the style carries; register the matching fn by name). ---- */
#define NT_UI_RICH_FX_ID_WAVE 1U
#define NT_UI_RICH_FX_ID_SHAKE 2U
#define NT_UI_RICH_FX_ID_RAINBOW 3U
#define NT_UI_RICH_FX_ID_PULSE 4U
#define NT_UI_RICH_FX_ID_FADE_IN 5U

#endif /* NT_UI_RICH_FX_H */
