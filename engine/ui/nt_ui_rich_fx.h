#ifndef NT_UI_RICH_FX_H
#define NT_UI_RICH_FX_H

/* Per-ATOM rich-text effects. An effect fn runs at EMIT, once per solved atom, VISUAL-ONLY (never
 * touches layout). The clock is game-passed (`time`); no global frame clock. The stock catalog
 * constants are DEFAULTS, tunable via nt_ui_rich_fx_params_t (amp/speed; a field <=0 keeps the
 * default); a game needing a curve params can't express registers its OWN fn. */

#include <stdbool.h>
#include <stdint.h>

/* nt_ui_rich_fx_result_t + nt_ui_rich_fx_fn + nt_rich_atom_kind_t are declared in nt_ui_rich_text.h
 * (so the builder's push_effect_fn signature avoids a text.h <-> fx.h include cycle). The ABSOLUTE
 * tint contract: identity returns base_color verbatim, rainbow REPLACES rgb, fade_in scales the base
 * alpha -- the emit path uses it directly (never out = base * color). */
#include "ui/nt_ui_rich_text.h"

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
/* offset.y = -AMP * |sin(time*SPEED + atom_idx*PHASE)| -- an always-upward sharp-bottom hop. */
nt_ui_rich_fx_result_t nt_ui_rich_fx_bounce(uint32_t atom_idx, nt_rich_atom_kind_t kind, const float base_xy[2], const float base_wh[2], const float base_color[4], float time, bool hovered,
                                            void *user_data);
/* color.rgb = base + (1-base) * AMP*(0.5+0.5*sin(time*SPEED)) -- a brightness pulse toward white (alpha kept). */
nt_ui_rich_fx_result_t nt_ui_rich_fx_glow(uint32_t atom_idx, nt_rich_atom_kind_t kind, const float base_xy[2], const float base_wh[2], const float base_color[4], float time, bool hovered,
                                          void *user_data);
/* offset.x = AMP * sin(time*SPEED + atom_idx*PHASE) -- a per-atom phase-shifted horizontal sway. */
nt_ui_rich_fx_result_t nt_ui_rich_fx_sway(uint32_t atom_idx, nt_rich_atom_kind_t kind, const float base_xy[2], const float base_wh[2], const float base_color[4], float time, bool hovered,
                                          void *user_data);

#endif /* NT_UI_RICH_FX_H */
