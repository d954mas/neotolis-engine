/* Stock rich-text effect catalog (FX-67-02). Each fn is a pure, deterministic curve over
 * (atom_idx, time, hovered) -- no global state, no allocation -- so the same time yields the
 * same transform headless (time==0 is each effect's t=0 value). Per-effect tuning is the
 * compile-time constants below (RESEARCH A5); a game that needs other amplitudes registers
 * its own fn (the catalog is a starting set, D-67-12). */

#include "ui/nt_ui_rich_fx.h"

#include <math.h>

/* Per-effect constants (compile-time; NOT tag params). Tuned for 16px text at ~60fps. */
#define RICH_FX_WAVE_AMP 3.0F   /* px vertical amplitude */
#define RICH_FX_WAVE_SPEED 6.0F /* rad/sec */
#define RICH_FX_WAVE_PHASE 0.5F /* rad/atom */

#define RICH_FX_SHAKE_AMP 2.0F   /* px jitter amplitude */
#define RICH_FX_SHAKE_RATE 30.0F /* jitter steps/sec (quantizes time so it snaps, not drifts) */

#define RICH_FX_RAINBOW_PHASE 0.07F /* hue turns/atom */
#define RICH_FX_RAINBOW_SPEED 0.30F /* hue turns/sec */

#define RICH_FX_PULSE_AMP 0.15F  /* scale amplitude (1 +/- AMP) */
#define RICH_FX_PULSE_SPEED 4.0F /* rad/sec */

#define RICH_FX_FADE_STAGGER 0.05F /* sec/atom start offset */
#define RICH_FX_FADE_DUR 0.30F     /* sec per-atom fade duration */

static float rich_fx_clamp01(float v) {
    if (v < 0.0F) {
        return 0.0F;
    }
    if (v > 1.0F) {
        return 1.0F;
    }
    return v;
}

nt_ui_rich_fx_result_t nt_ui_rich_fx_identity(const float base_color[4]) {
    nt_ui_rich_fx_result_t r;
    r.offset_x = 0.0F;
    r.offset_y = 0.0F;
    r.color[0] = base_color[0];
    r.color[1] = base_color[1];
    r.color[2] = base_color[2];
    r.color[3] = base_color[3];
    r.scale = 1.0F;
    r.visible = true;
    return r;
}

/* A cheap deterministic [0,1) hash of two integers (xorshift-mix; no table). */
static float rich_fx_hash01(uint32_t a, uint32_t b) {
    uint32_t h = (a * 0x9E3779B1U) + (b * 0x85EBCA77U);
    h ^= h >> 15;
    h *= 0x2C1B3C6DU;
    h ^= h >> 12;
    return (float)(h & 0xFFFFFFU) / (float)0x1000000U; /* 24-bit mantissa -> [0,1) */
}

/* HSV (h in [0,1) turns, s=v=1) -> RGB. Keeps the incoming alpha. */
static void rich_fx_hue_rgb(float h, float out_rgb[3]) {
    const float hp = (h - floorf(h)) * 6.0F; /* [0,6) */
    const float x = 1.0F - fabsf(fmodf(hp, 2.0F) - 1.0F);
    float r = 0.0F;
    float g = 0.0F;
    float b = 0.0F;
    if (hp < 1.0F) {
        r = 1.0F;
        g = x;
    } else if (hp < 2.0F) {
        r = x;
        g = 1.0F;
    } else if (hp < 3.0F) {
        g = 1.0F;
        b = x;
    } else if (hp < 4.0F) {
        g = x;
        b = 1.0F;
    } else if (hp < 5.0F) {
        r = x;
        b = 1.0F;
    } else {
        r = 1.0F;
        b = x;
    }
    out_rgb[0] = r;
    out_rgb[1] = g;
    out_rgb[2] = b;
}

nt_ui_rich_fx_result_t nt_ui_rich_fx_wave(uint32_t atom_idx, nt_rich_atom_kind_t kind, const float base_xy[2], const float base_wh[2], const float base_color[4], float time, bool hovered) {
    (void)kind;
    (void)base_xy;
    (void)base_wh;
    (void)hovered;
    nt_ui_rich_fx_result_t r = nt_ui_rich_fx_identity(base_color);
    r.offset_y = RICH_FX_WAVE_AMP * sinf((time * RICH_FX_WAVE_SPEED) + ((float)atom_idx * RICH_FX_WAVE_PHASE));
    return r;
}

nt_ui_rich_fx_result_t nt_ui_rich_fx_shake(uint32_t atom_idx, nt_rich_atom_kind_t kind, const float base_xy[2], const float base_wh[2], const float base_color[4], float time, bool hovered) {
    (void)kind;
    (void)base_xy;
    (void)base_wh;
    (void)hovered;
    nt_ui_rich_fx_result_t r = nt_ui_rich_fx_identity(base_color);
    /* via signed intermediate: float->unsigned is UB for negative time (game-owned, reachable) */
    const uint32_t step = (uint32_t)(int32_t)floorf(time * RICH_FX_SHAKE_RATE); /* quantize so it snaps per step */
    r.offset_x = RICH_FX_SHAKE_AMP * (rich_fx_hash01(atom_idx, step) - 0.5F) * 2.0F;
    r.offset_y = RICH_FX_SHAKE_AMP * (rich_fx_hash01(atom_idx, step + 0x1000U) - 0.5F) * 2.0F;
    return r;
}

nt_ui_rich_fx_result_t nt_ui_rich_fx_rainbow(uint32_t atom_idx, nt_rich_atom_kind_t kind, const float base_xy[2], const float base_wh[2], const float base_color[4], float time, bool hovered) {
    (void)kind;
    (void)base_xy;
    (void)base_wh;
    (void)hovered;
    nt_ui_rich_fx_result_t r = nt_ui_rich_fx_identity(base_color);
    const float hue = ((float)atom_idx * RICH_FX_RAINBOW_PHASE) + (time * RICH_FX_RAINBOW_SPEED);
    float rgb[3];
    rich_fx_hue_rgb(hue, rgb);
    r.color[0] = rgb[0];
    r.color[1] = rgb[1];
    r.color[2] = rgb[2];
    /* keep base alpha (r.color[3] already = base_color[3]) */
    return r;
}

nt_ui_rich_fx_result_t nt_ui_rich_fx_pulse(uint32_t atom_idx, nt_rich_atom_kind_t kind, const float base_xy[2], const float base_wh[2], const float base_color[4], float time, bool hovered) {
    (void)atom_idx;
    (void)kind;
    (void)base_xy;
    (void)base_wh;
    (void)hovered;
    nt_ui_rich_fx_result_t r = nt_ui_rich_fx_identity(base_color);
    r.scale = 1.0F + (RICH_FX_PULSE_AMP * sinf(time * RICH_FX_PULSE_SPEED));
    return r;
}

nt_ui_rich_fx_result_t nt_ui_rich_fx_fade_in(uint32_t atom_idx, nt_rich_atom_kind_t kind, const float base_xy[2], const float base_wh[2], const float base_color[4], float time, bool hovered) {
    (void)kind;
    (void)base_xy;
    (void)base_wh;
    (void)hovered;
    nt_ui_rich_fx_result_t r = nt_ui_rich_fx_identity(base_color);
    const float a = rich_fx_clamp01((time - ((float)atom_idx * RICH_FX_FADE_STAGGER)) / RICH_FX_FADE_DUR);
    r.color[3] = base_color[3] * a;
    r.visible = (a > 0.0F); /* fully transparent -> skip the atom emit */
    return r;
}

nt_ui_rich_fx_fn nt_ui_rich_fx_stock(uint8_t effect_id) {
    switch (effect_id) {
    case NT_UI_RICH_FX_ID_WAVE:
        return nt_ui_rich_fx_wave;
    case NT_UI_RICH_FX_ID_SHAKE:
        return nt_ui_rich_fx_shake;
    case NT_UI_RICH_FX_ID_RAINBOW:
        return nt_ui_rich_fx_rainbow;
    case NT_UI_RICH_FX_ID_PULSE:
        return nt_ui_rich_fx_pulse;
    case NT_UI_RICH_FX_ID_FADE_IN:
        return nt_ui_rich_fx_fade_in;
    default:
        return NULL; /* 0 = none, or an unregistered id -> no effect */
    }
}
