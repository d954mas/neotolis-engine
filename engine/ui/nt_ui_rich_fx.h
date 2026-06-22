#ifndef NT_UI_RICH_FX_H
#define NT_UI_RICH_FX_H

/* Per-ATOM rich-text effects. An effect fn runs at EMIT, once per solved atom
 * (glyph AND image AND object -- per-atom), and is VISUAL-ONLY: it shifts/tints/scales/hides
 * the atom but never changes the solver's layout (line/box stay put). The animation clock is
 * passed IN BY THE GAME (`time`); there is no global frame clock.
 *
 * The stock catalog (wave/shake/rainbow/pulse/fade_in) is a STARTING set the game registers
 * piecemeal by name into the tagset (nt_ui_rich_tagset_register_effect) -- NO register_defaults
 * bundle. The catalog constants are DEFAULTS: a stock effect is tunable at runtime via
 * nt_ui_rich_fx_params_t (amp/speed) passed through nt_ui_rich_push_effect_ex or the
 * `<fx=name amp=.. speed=..>` markup; a field <=0 keeps the default. A game needing a curve the
 * params can't express still registers its OWN fn. */

#include <stdbool.h>
#include <stdint.h>

/* nt_ui_rich_fx_result_t + nt_ui_rich_fx_fn + nt_rich_atom_kind_t are declared in nt_ui_rich_text.h
 * (so the builder's push_effect_fn signature avoids a text.h <-> fx.h include cycle). The ABSOLUTE
 * tint contract: identity returns base_color verbatim, rainbow REPLACES rgb, fade_in scales the base
 * alpha -- the emit path uses it directly (never out = base * color). */
#include "ui/nt_ui_rich_text.h"

/* The identity result: no shift, no tint change, no scale, visible. Use as the base a stock fn
 * mutates so a future field addition stays forward-compatible (no bare {0}, alpha 0 != opaque). */
nt_ui_rich_fx_result_t nt_ui_rich_fx_identity(const float base_color[4]);

/* ---- Stock catalog. Constants are DEFAULTS; user_data (nt_ui_rich_fx_params_t) tunes amp/speed. ---- */
/* offset.y = AMP * sin(time*SPEED + atom_idx*PHASE) -- a per-atom phase-shifted vertical wave. */
nt_ui_rich_fx_result_t nt_ui_rich_fx_wave(uint32_t atom_idx, nt_rich_atom_kind_t kind, const float base_xy[2], const float base_wh[2], const float base_color[4], float time, bool hovered,
                                          void *user_data);
/* offset.xy = AMP * (hash(atom_idx, floor(time*RATE)) - 0.5) -- a per-atom deterministic jitter. */
nt_ui_rich_fx_result_t nt_ui_rich_fx_shake(uint32_t atom_idx, nt_rich_atom_kind_t kind, const float base_xy[2], const float base_wh[2], const float base_color[4], float time, bool hovered,
                                           void *user_data);
/* color = hsv((atom_idx*PHASE + time*SPEED) mod 1, 1, 1) -- a per-atom hue cycle (alpha kept). */
nt_ui_rich_fx_result_t nt_ui_rich_fx_rainbow(uint32_t atom_idx, nt_rich_atom_kind_t kind, const float base_xy[2], const float base_wh[2], const float base_color[4], float time, bool hovered,
                                             void *user_data);
/* scale = 1 + AMP * sin(time*SPEED) -- a uniform breathing pulse about each atom's center. */
nt_ui_rich_fx_result_t nt_ui_rich_fx_pulse(uint32_t atom_idx, nt_rich_atom_kind_t kind, const float base_xy[2], const float base_wh[2], const float base_color[4], float time, bool hovered,
                                           void *user_data);
/* color.a *= clamp((time - atom_idx*STAGGER) / DUR, 0, 1) -- a staggered per-atom fade-in;
 * alpha 0 -> visible=false so the atom is skipped entirely until its window opens. */
nt_ui_rich_fx_result_t nt_ui_rich_fx_fade_in(uint32_t atom_idx, nt_rich_atom_kind_t kind, const float base_xy[2], const float base_wh[2], const float base_color[4], float time, bool hovered,
                                             void *user_data);

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

/* ---- Custom (game-supplied) effects ----
 * A game registers its OWN fn (push_effect_fn / tagset register_effect_fn). The (fn,user_data)
 * pair lives in a per-block fixed-cap table captured at build (NOT in the 48B style, NOT in the
 * tagset -- which is not present at emit); the style's uint8 effect_id carries a CUSTOM
 * index instead of a stock id when effect_id >= NT_UI_RICH_FX_CUSTOM_BASE. The emit path resolves
 * a custom id against the solved-state table, a stock id against nt_ui_rich_fx_stock. The stored
 * user_data is passed back to the custom fn at emit (NULL for stock) -- one fn, parameterized per
 * registration. */
#define NT_UI_RICH_FX_CUSTOM_BASE 128U /* effect_id >= this -> index (id - BASE) into the per-block custom table */

/* True when a composed-style effect_id names a custom (game-supplied) fn, not a stock id. */
static inline bool nt_ui_rich_fx_id_is_custom(uint8_t effect_id) { return effect_id >= NT_UI_RICH_FX_CUSTOM_BASE; }

#endif /* NT_UI_RICH_FX_H */
