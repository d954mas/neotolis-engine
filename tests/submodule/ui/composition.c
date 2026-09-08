#include <math.h>
#include <stdio.h>

#include "ui/nt_ui.h"
#include "ui/nt_ui_label.h"

typedef void (*ui_walk_fn)(nt_ui_context_t *, const nt_ui_target_t *);
typedef void (*ui_label_fn)(nt_ui_context_t *, const nt_ui_element_data_t *, const char *, const nt_ui_label_style_t *);
static ui_walk_fn volatile s_walk = nt_ui_walk;
static ui_label_fn volatile s_label = nt_ui_label;
static volatile float s_checksum;

#if NT_COMPOSITION_RICH
#include "ui/nt_ui_rich_text.h"
#if NT_COMPOSITION_EFFECTS > 0
#include "ui/nt_ui_rich_fx.h"
#endif

typedef void (*rich_text_fn)(nt_ui_context_t *, uint32_t, const nt_ui_element_data_t *, const nt_ui_rich_style_t *, float, nt_rich_align_t, float, nt_ui_rich_result_t *);
typedef void (*rich_push_fn)(nt_ui_context_t *, nt_ui_rich_fx_fn, void *);
static rich_text_fn volatile s_rich_text = nt_ui_rich_text;
static rich_push_fn volatile s_rich_push = nt_ui_rich_push_effect_fn;

#if NT_COMPOSITION_EFFECTS == 0
static nt_ui_rich_fx_result_t game_effect(uint32_t atom_idx, nt_rich_atom_kind_t kind, const float base_xy[2], const float base_wh[2], const float base_color[4], float time, bool hovered,
                                          void *user_data) {
    (void)kind;
    (void)base_xy;
    (void)base_wh;
    (void)hovered;
    const float *magnitude = (const float *)user_data;
    nt_ui_rich_fx_result_t result = nt_ui_rich_fx_identity(base_color);
    result.offset_y = *magnitude * (time + (float)atom_idx);
    return result;
}
static nt_ui_rich_fx_fn volatile s_effects[] = {game_effect};
#elif NT_COMPOSITION_EFFECTS == 1
static nt_ui_rich_fx_fn volatile s_effects[] = {nt_ui_rich_fx_wave};
#elif NT_COMPOSITION_EFFECTS == 8
static nt_ui_rich_fx_fn volatile s_effects[] = {
    nt_ui_rich_fx_wave, nt_ui_rich_fx_shake, nt_ui_rich_fx_rainbow, nt_ui_rich_fx_pulse, nt_ui_rich_fx_fade_in, nt_ui_rich_fx_bounce, nt_ui_rich_fx_glow, nt_ui_rich_fx_sway,
};
#else
#error Unsupported composition effect count
#endif
#endif

int main(int argc, char **argv) {
    (void)argv;
    if (s_walk == NULL || s_label == NULL) {
        return 1;
    }
    float checksum = 1.0F;
#if NT_COMPOSITION_RICH
    if (s_rich_text == NULL || s_rich_push == NULL) {
        return 2;
    }
    const float xy[2] = {3.0F, 5.0F};
    const float wh[2] = {11.0F, 17.0F};
    const float color[4] = {0.25F, 0.5F, 0.75F, 1.0F};
    const float time = (float)argc * 0.125F;
#if NT_COMPOSITION_EFFECTS == 0
    float magnitude = 2.0F;
    void *user = &magnitude;
#else
    void *user = NULL;
#endif
    for (uint32_t i = 0; i < sizeof s_effects / sizeof s_effects[0]; i++) {
        const nt_ui_rich_fx_fn fn = s_effects[i];
        const nt_ui_rich_fx_result_t result = fn(i, NT_RICH_ATOM_TEXT, xy, wh, color, time, false, user);
        checksum += result.offset_x + result.offset_y + result.scale + result.color[0] + result.color[1] + result.color[2] + result.color[3] + (result.visible ? 1.0F : 0.0F);
    }
#else
    (void)argc;
#endif
    s_checksum = checksum;
    if (!isfinite(s_checksum)) {
        return 3;
    }
    printf("ui-composition %.9g\n", (double)s_checksum);
    return 0;
}
