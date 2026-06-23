/* Stock effect catalog: each fn is a pure deterministic curve over (atom_idx, time, hovered) -- no
 * global state, so the same time replays headless. user_data is an optional nt_ui_rich_fx_params_t
 * override (NULL -> every compile-time default; a field <=0 keeps that field's default). */

#include "ui/nt_ui_rich_fx.h"

#include <math.h>

/* Per-effect constants (compile-time DEFAULTS; overridable via nt_ui_rich_fx_params_t). Tuned for 16px text at ~60fps. */
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

#define RICH_FX_BOUNCE_AMP 6.0F   /* px hop height */
#define RICH_FX_BOUNCE_SPEED 6.0F /* rad/sec */
#define RICH_FX_BOUNCE_PHASE 0.5F /* rad/atom */

#define RICH_FX_GLOW_AMP 0.6F   /* max brighten fraction toward white */
#define RICH_FX_GLOW_SPEED 3.0F /* rad/sec */

#define RICH_FX_SWAY_AMP 4.0F   /* px horizontal amplitude */
#define RICH_FX_SWAY_SPEED 3.0F /* rad/sec */
#define RICH_FX_SWAY_PHASE 0.5F /* rad/atom */

static float rich_fx_clamp01(float v) {
    if (v < 0.0F) {
        return 0.0F;
    }
    if (v > 1.0F) {
        return 1.0F;
    }
    return v;
}

/* Resolve a tunable field: a stored value > 0 overrides; NULL params OR a field <= 0 keeps the
 * compile-time default (so the plain push_effect path -- user_data==NULL -- is byte-identical). */
static float rich_fx_amp(const nt_ui_rich_fx_params_t *p, float def) { return (p != NULL && p->amp > 0.0F) ? p->amp : def; }
static float rich_fx_speed(const nt_ui_rich_fx_params_t *p, float def) { return (p != NULL && p->speed > 0.0F) ? p->speed : def; }

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

nt_ui_rich_fx_result_t nt_ui_rich_fx_wave(uint32_t atom_idx, nt_rich_atom_kind_t kind, const float base_xy[2], const float base_wh[2], const float base_color[4], float time, bool hovered,
                                          void *user_data) {
    (void)kind;
    (void)base_xy;
    (void)base_wh;
    (void)hovered;
    /* params: amp = vertical px amplitude, speed = rad/s; phase stays a compile-time constant. */
    const nt_ui_rich_fx_params_t *p = (const nt_ui_rich_fx_params_t *)user_data;
    const float amp = rich_fx_amp(p, RICH_FX_WAVE_AMP);
    const float speed = rich_fx_speed(p, RICH_FX_WAVE_SPEED);
    nt_ui_rich_fx_result_t r = nt_ui_rich_fx_identity(base_color);
    r.offset_y = amp * sinf((time * speed) + ((float)atom_idx * RICH_FX_WAVE_PHASE));
    return r;
}

nt_ui_rich_fx_result_t nt_ui_rich_fx_shake(uint32_t atom_idx, nt_rich_atom_kind_t kind, const float base_xy[2], const float base_wh[2], const float base_color[4], float time, bool hovered,
                                           void *user_data) {
    (void)kind;
    (void)base_xy;
    (void)base_wh;
    (void)hovered;
    /* params: amp = jitter px amplitude, speed = jitter steps/sec (the time quantize rate). */
    const nt_ui_rich_fx_params_t *p = (const nt_ui_rich_fx_params_t *)user_data;
    const float amp = rich_fx_amp(p, RICH_FX_SHAKE_AMP);
    const float rate = rich_fx_speed(p, RICH_FX_SHAKE_RATE);
    nt_ui_rich_fx_result_t r = nt_ui_rich_fx_identity(base_color);
    /* via signed intermediate: float->unsigned is UB for negative time (game-owned, reachable) */
    const uint32_t step = (uint32_t)(int32_t)floorf(time * rate); /* quantize so it snaps per step */
    r.offset_x = amp * (rich_fx_hash01(atom_idx, step) - 0.5F) * 2.0F;
    r.offset_y = amp * (rich_fx_hash01(atom_idx, step + 0x1000U) - 0.5F) * 2.0F;
    return r;
}

nt_ui_rich_fx_result_t nt_ui_rich_fx_rainbow(uint32_t atom_idx, nt_rich_atom_kind_t kind, const float base_xy[2], const float base_wh[2], const float base_color[4], float time, bool hovered,
                                             void *user_data) {
    (void)kind;
    (void)base_xy;
    (void)base_wh;
    (void)hovered;
    /* params: speed = hue turns/sec; amp is unused (rainbow has no magnitude axis). */
    const nt_ui_rich_fx_params_t *p = (const nt_ui_rich_fx_params_t *)user_data;
    const float speed = rich_fx_speed(p, RICH_FX_RAINBOW_SPEED);
    nt_ui_rich_fx_result_t r = nt_ui_rich_fx_identity(base_color);
    const float hue = ((float)atom_idx * RICH_FX_RAINBOW_PHASE) + (time * speed);
    float rgb[3];
    rich_fx_hue_rgb(hue, rgb);
    r.color[0] = rgb[0];
    r.color[1] = rgb[1];
    r.color[2] = rgb[2];
    /* keep base alpha (r.color[3] already = base_color[3]) */
    return r;
}

nt_ui_rich_fx_result_t nt_ui_rich_fx_pulse(uint32_t atom_idx, nt_rich_atom_kind_t kind, const float base_xy[2], const float base_wh[2], const float base_color[4], float time, bool hovered,
                                           void *user_data) {
    (void)atom_idx;
    (void)kind;
    (void)base_xy;
    (void)base_wh;
    (void)hovered;
    /* params: amp = scale delta (1 +/- amp), speed = rad/s. */
    const nt_ui_rich_fx_params_t *p = (const nt_ui_rich_fx_params_t *)user_data;
    const float amp = rich_fx_amp(p, RICH_FX_PULSE_AMP);
    const float speed = rich_fx_speed(p, RICH_FX_PULSE_SPEED);
    nt_ui_rich_fx_result_t r = nt_ui_rich_fx_identity(base_color);
    r.scale = 1.0F + (amp * sinf(time * speed));
    return r;
}

nt_ui_rich_fx_result_t nt_ui_rich_fx_fade_in(uint32_t atom_idx, nt_rich_atom_kind_t kind, const float base_xy[2], const float base_wh[2], const float base_color[4], float time, bool hovered,
                                             void *user_data) {
    (void)kind;
    (void)base_xy;
    (void)base_wh;
    (void)hovered;
    /* params: speed = per-atom reveal rate (1/sec); amp unused. The DEFAULT path keeps the exact
     * original /DUR division (byte-identical, not the float-distinct *(1/DUR)); an override divides
     * by the equivalent 1/speed duration. */
    const nt_ui_rich_fx_params_t *p = (const nt_ui_rich_fx_params_t *)user_data;
    const float dur = (p != NULL && p->speed > 0.0F) ? (1.0F / p->speed) : RICH_FX_FADE_DUR;
    nt_ui_rich_fx_result_t r = nt_ui_rich_fx_identity(base_color);
    const float a = rich_fx_clamp01((time - ((float)atom_idx * RICH_FX_FADE_STAGGER)) / dur);
    r.color[3] = base_color[3] * a;
    r.visible = (a > 0.0F); /* fully transparent -> skip the atom emit */
    return r;
}

nt_ui_rich_fx_result_t nt_ui_rich_fx_bounce(uint32_t atom_idx, nt_rich_atom_kind_t kind, const float base_xy[2], const float base_wh[2], const float base_color[4], float time, bool hovered,
                                            void *user_data) {
    (void)kind;
    (void)base_xy;
    (void)base_wh;
    (void)hovered;
    /* params: amp = hop px height, speed = rad/s; phase stays a compile-time constant. */
    const nt_ui_rich_fx_params_t *p = (const nt_ui_rich_fx_params_t *)user_data;
    const float amp = rich_fx_amp(p, RICH_FX_BOUNCE_AMP);
    const float speed = rich_fx_speed(p, RICH_FX_BOUNCE_SPEED);
    nt_ui_rich_fx_result_t r = nt_ui_rich_fx_identity(base_color);
    /* abs(sin) -> always-upward sharp-bottom hop (y up = negative), distinct from wave's smooth swing. */
    r.offset_y = -amp * fabsf(sinf((time * speed) + ((float)atom_idx * RICH_FX_BOUNCE_PHASE)));
    return r;
}

nt_ui_rich_fx_result_t nt_ui_rich_fx_glow(uint32_t atom_idx, nt_rich_atom_kind_t kind, const float base_xy[2], const float base_wh[2], const float base_color[4], float time, bool hovered,
                                          void *user_data) {
    (void)atom_idx;
    (void)kind;
    (void)base_xy;
    (void)base_wh;
    (void)hovered;
    /* params: amp = max brighten fraction toward white, speed = rad/s. Visual-only (color). */
    const nt_ui_rich_fx_params_t *p = (const nt_ui_rich_fx_params_t *)user_data;
    const float amp = rich_fx_amp(p, RICH_FX_GLOW_AMP);
    const float speed = rich_fx_speed(p, RICH_FX_GLOW_SPEED);
    nt_ui_rich_fx_result_t r = nt_ui_rich_fx_identity(base_color);
    const float g = rich_fx_clamp01(amp * (0.5F + (0.5F * sinf(time * speed)))); /* clamp01: keep rgb bounded by white even when params amp > 1 */
    r.color[0] = base_color[0] + ((1.0F - base_color[0]) * g);
    r.color[1] = base_color[1] + ((1.0F - base_color[1]) * g);
    r.color[2] = base_color[2] + ((1.0F - base_color[2]) * g);
    /* keep base alpha (r.color[3] already = base_color[3]) */
    return r;
}

nt_ui_rich_fx_result_t nt_ui_rich_fx_sway(uint32_t atom_idx, nt_rich_atom_kind_t kind, const float base_xy[2], const float base_wh[2], const float base_color[4], float time, bool hovered,
                                          void *user_data) {
    (void)kind;
    (void)base_xy;
    (void)base_wh;
    (void)hovered;
    /* params: amp = horizontal px amplitude, speed = rad/s; phase stays a compile-time constant. */
    const nt_ui_rich_fx_params_t *p = (const nt_ui_rich_fx_params_t *)user_data;
    const float amp = rich_fx_amp(p, RICH_FX_SWAY_AMP);
    const float speed = rich_fx_speed(p, RICH_FX_SWAY_SPEED);
    nt_ui_rich_fx_result_t r = nt_ui_rich_fx_identity(base_color);
    r.offset_x = amp * sinf((time * speed) + ((float)atom_idx * RICH_FX_SWAY_PHASE));
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
    case NT_UI_RICH_FX_ID_BOUNCE:
        return nt_ui_rich_fx_bounce;
    case NT_UI_RICH_FX_ID_GLOW:
        return nt_ui_rich_fx_glow;
    case NT_UI_RICH_FX_ID_SWAY:
        return nt_ui_rich_fx_sway;
    default:
        return NULL; /* 0 = none, or an unregistered id -> no effect */
    }
}
