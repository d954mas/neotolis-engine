/* ui_3d_demo — walkable space with a central rotating primitive and two world-mounted UI
 * panels (PASS 2). Left wall hosts the SHAPE panel (CUBE / SPHERE / CAPSULE); right wall hosts
 * the SPEED panel (STOP / SLOW / MEDIUM / FAST). Same controls remain on keyboard for parity.
 * PASS 3 adds the 2D-vs-3D input bench.
 *
 * Controls:
 *   WASD            walk along yaw (no collisions per scope)
 *   RMB-drag        mouselook (yaw + pitch)
 *   Q / E           yaw keys (alternate to mouse)
 *   LMB             click world-space panel buttons (raycast through cursor)
 *   R               reset player + shape rotation
 *   1 / 2 / 3       shape  (cube / sphere / capsule)  — same as left panel
 *   4 / 5 / 6 / 7   speed  (stop / slow / medium / fast) — same as right panel
 *   F1              toggle debug overlay (player pose + nt_stats)
 *   Esc             quit (native) */

// #region includes
#include "app/nt_app.h"
#include "atlas/nt_atlas.h"
#include "core/nt_assert.h"
#include "core/nt_core.h"
#include "core/nt_platform.h"
#include "font/nt_font.h"
#include "fs/nt_fs.h"
#include "graphics/nt_gfx.h"
#include "hash/nt_hash.h"
#include "http/nt_http.h"
#include "input/nt_input.h"
#include "log/nt_log.h"
#include "material/nt_material.h"
#include "math/nt_math.h"
#include "memory/nt_mem_scratch.h"
#include "render/nt_render_defs.h"
#include "renderers/nt_shape_renderer.h"
#include "renderers/nt_sprite_renderer.h"
#include "renderers/nt_text_renderer.h"
#include "resource/nt_resource.h"
#include "stats/nt_stats.h"
#include "time/nt_time.h"
#include "ui/nt_ui.h"
#include "ui/nt_ui_button.h"
#include "ui/nt_ui_inspector.h"
#include "ui/nt_ui_label.h"
#include "window/nt_window.h"

#include "nt_pack_format.h"
#include "ui_buttons_demo_assets.h"

#include "clay.h"
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#ifdef NT_PLATFORM_WEB
#include "platform/web/nt_platform_web.h"
#endif
// #endregion

// #region constants
#define ROOM_W 20.0F
#define ROOM_H 10.0F
#define ROOM_D 20.0F
#define GRID_STEP 1.0F

#define MOVE_SPEED 5.0F
#define ROT_SPEED 1.8F
#define MOUSE_SENS 0.005F
#define PITCH_LIMIT 1.2F
#define FOV_DEG 65.0F
#define EYE_HEIGHT 1.7F

#define SCRATCH_ARENA_SIZE ((size_t)256 * 1024)
#define UI_ARENA_SIZE ((size_t)4 * 1024 * 1024)

#define HUD_SIZE 16.0F
#define HUD_TITLE_SIZE 20.0F

#define LAYER_PANEL 1
#define LAYER_LABEL 2

/* Panel layout in Clay-pixel space. Both panels share size so make_wall_xform's pivot math
 * (panel_w/2, panel_h/2) lines up with each one's actual Clay bbox center. */
#define PANEL_W 320
#define PANEL_H 440 /* fits title + 4 buttons + paddings */
#define PANEL_TITLE_H 48
#define BTN_W 280
#define BTN_H 64
#define PANEL_SCALE 0.012F

enum { SHAPE_CUBE = 0, SHAPE_SPHERE, SHAPE_CAPSULE, SHAPE_COUNT };
enum { SPEED_STOP = 0, SPEED_SLOW, SPEED_MED, SPEED_FAST, SPEED_COUNT };
// #endregion

// #region tables
/* clang-format off */
static const float s_speed_table[SPEED_COUNT] = { 0.0F, 0.4F, 1.2F, 3.5F };
static const float s_shape_colors[SHAPE_COUNT][4] = {
    {0.25F, 0.85F, 0.95F, 1.0F},
    {0.95F, 0.45F, 1.00F, 1.0F},
    {1.00F, 0.75F, 0.20F, 1.0F},
};
/* clang-format on */
static const char *const s_shape_labels[SHAPE_COUNT] = {"CUBE", "SPHERE", "CAPSULE"};
static const char *const s_speed_labels[SPEED_COUNT] = {"STOP", "SLOW", "MEDIUM", "FAST"};
// #endregion

// #region state
static float s_player_pos[3] = {0.0F, EYE_HEIGHT, 7.0F};
static float s_player_yaw; /* yaw=0: face -Z toward front wall panels (no X-mirror) */
static float s_player_pitch;

static int s_shape_kind = SHAPE_CUBE;
static int s_speed_kind = SPEED_SLOW;
static float s_shape_yaw;

static bool s_debug_overlay;

/* Resources. */
static nt_hash32_t s_pack_id;
static nt_resource_t s_sprite_vs_handle;
static nt_resource_t s_sprite_fs_handle;
static nt_resource_t s_text_vs_handle;
static nt_resource_t s_text_fs_handle;
static nt_resource_t s_atlas_handle;
static nt_resource_t s_atlas_tex_handle;
static nt_resource_t s_font_resource;
static nt_material_t s_sprite_material;
static nt_material_t s_text_material;
static nt_font_t s_font;
static bool s_atlas_bound;
static bool s_font_bound;
static nt_buffer_t s_frame_ubo;

/* UI. */
static nt_ui_context_t *s_ctx;
_Alignas(16) static uint8_t s_ui_arena[UI_ARENA_SIZE];
static uint32_t s_white_region_idx;
static uint32_t s_button_blue_idx;
static uint32_t s_button_green_idx;
static nt_ui_button_style_t s_btn_idle;
static nt_ui_button_style_t s_btn_active; /* highlighted for current selection */
static const Clay_ElementDeclaration s_btn_decl = {
    .layout =
        {
            .sizing = {CLAY_SIZING_FIXED(BTN_W), CLAY_SIZING_FIXED(BTN_H)},
            .padding = CLAY_PADDING_ALL(8),
            .childAlignment = {CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER},
        },
};
static nt_ui_label_style_t s_panel_title_style = {
    .font_id = 0,
    .font_size = 40,
    .color = {255.0F, 240.0F, 180.0F, 255.0F},
    .align = CLAY_TEXT_ALIGN_CENTER,
};
static nt_ui_label_style_t s_btn_label_style = {
    .font_id = 0,
    .font_size = 34,
    .color = {245.0F, 245.0F, 250.0F, 255.0F},
    .align = CLAY_TEXT_ALIGN_CENTER,
};

/* Stable widget ids. */
static uint32_t s_id_shape_btn[SHAPE_COUNT];
static uint32_t s_id_speed_btn[SPEED_COUNT];
static bool s_ids_ready;
// #endregion

// #region helpers
static void player_reset(void) {
    s_player_pos[0] = 0.0F;
    s_player_pos[1] = EYE_HEIGHT;
    s_player_pos[2] = 7.0F;
    s_player_yaw = 0.0F;
    s_player_pitch = 0.0F;
    s_shape_yaw = 0.0F;
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void player_update(float dt) {
    if (nt_input_mouse_is_down(NT_BUTTON_RIGHT)) {
        s_player_yaw += g_nt_input.pointers[0].dx * MOUSE_SENS;
        s_player_pitch -= g_nt_input.pointers[0].dy * MOUSE_SENS;
    }
    if (nt_input_key_is_down(NT_KEY_Q)) {
        s_player_yaw -= ROT_SPEED * dt;
    }
    if (nt_input_key_is_down(NT_KEY_E)) {
        s_player_yaw += ROT_SPEED * dt;
    }
    if (s_player_pitch > PITCH_LIMIT) {
        s_player_pitch = PITCH_LIMIT;
    }
    if (s_player_pitch < -PITCH_LIMIT) {
        s_player_pitch = -PITCH_LIMIT;
    }

    const float cy = cosf(s_player_yaw);
    const float sy = sinf(s_player_yaw);
    const float fwd_x = sy;
    const float fwd_z = -cy;
    const float right_x = cy;
    const float right_z = sy;

    float mx = 0.0F;
    float mz = 0.0F;
    if (nt_input_key_is_down(NT_KEY_W)) {
        mx += fwd_x;
        mz += fwd_z;
    }
    if (nt_input_key_is_down(NT_KEY_S)) {
        mx -= fwd_x;
        mz -= fwd_z;
    }
    if (nt_input_key_is_down(NT_KEY_A)) {
        mx -= right_x;
        mz -= right_z;
    }
    if (nt_input_key_is_down(NT_KEY_D)) {
        mx += right_x;
        mz += right_z;
    }
    const float len = sqrtf((mx * mx) + (mz * mz));
    if (len > 0.0001F) {
        mx /= len;
        mz /= len;
        const float step = MOVE_SPEED * dt;
        s_player_pos[0] += mx * step;
        s_player_pos[2] += mz * step;
    }
    const float inset = 0.5F;
    const float hw = (ROOM_W * 0.5F) - inset;
    const float hd = (ROOM_D * 0.5F) - inset;
    if (s_player_pos[0] < -hw) {
        s_player_pos[0] = -hw;
    }
    if (s_player_pos[0] > hw) {
        s_player_pos[0] = hw;
    }
    if (s_player_pos[2] < -hd) {
        s_player_pos[2] = -hd;
    }
    if (s_player_pos[2] > hd) {
        s_player_pos[2] = hd;
    }
}

static void compute_perspective_vp(mat4 out_vp, float aspect) {
    const float cy = cosf(s_player_yaw);
    const float sy = sinf(s_player_yaw);
    const float cp = cosf(s_player_pitch);
    const float sp = sinf(s_player_pitch);
    vec3 eye = {s_player_pos[0], s_player_pos[1], s_player_pos[2]};
    vec3 fwd = {sy * cp, sp, -cy * cp};
    vec3 center = {eye[0] + fwd[0], eye[1] + fwd[1], eye[2] + fwd[2]};
    vec3 up = {0.0F, 1.0F, 0.0F};
    mat4 view;
    mat4 proj;
    glm_lookat(eye, center, up, view);
    glm_perspective(glm_rad(FOV_DEG), aspect, 0.1F, 100.0F, proj);
    glm_mat4_mul(proj, view, out_vp);
}
// #endregion

// #region drawing
static void draw_room(void) {
    const float hw = ROOM_W * 0.5F;
    const float hd = ROOM_D * 0.5F;
    const float floor_col[4] = {0.13F, 0.13F, 0.16F, 1.0F};
    const float ceil_col[4] = {0.10F, 0.10F, 0.16F, 1.0F};
    const float wall_col[4] = {0.20F, 0.18F, 0.16F, 1.0F};
    const float grid_col[4] = {0.30F, 0.30F, 0.36F, 1.0F};
    const float floor_pos[3] = {0.0F, 0.0F, 0.0F};
    const float floor_sz[2] = {ROOM_W, ROOM_D};
    const float floor_rot[4] = {0.7071068F, 0.0F, 0.0F, 0.7071068F};
    const float ceil_pos[3] = {0.0F, ROOM_H, 0.0F};
    nt_shape_renderer_rect_rot(floor_pos, floor_sz, floor_rot, floor_col);
    nt_shape_renderer_rect_rot(ceil_pos, floor_sz, floor_rot, ceil_col);

    const int nx = (int)(ROOM_W / GRID_STEP) + 1;
    const int nz = (int)(ROOM_D / GRID_STEP) + 1;
    for (int ix = 0; ix < nx; ++ix) {
        const float x = -hw + ((float)ix * GRID_STEP);
        const float a[3] = {x, 0.002F, -hd};
        const float b[3] = {x, 0.002F, hd};
        nt_shape_renderer_line(a, b, grid_col);
    }
    for (int iz = 0; iz < nz; ++iz) {
        const float z = -hd + ((float)iz * GRID_STEP);
        const float a[3] = {-hw, 0.002F, z};
        const float b[3] = {hw, 0.002F, z};
        nt_shape_renderer_line(a, b, grid_col);
    }

    const float wall_sz_fb[2] = {ROOM_W, ROOM_H};
    const float front_pos[3] = {0.0F, ROOM_H * 0.5F, -hd};
    nt_shape_renderer_rect(front_pos, wall_sz_fb, wall_col);
    const float back_pos[3] = {0.0F, ROOM_H * 0.5F, hd};
    nt_shape_renderer_rect(back_pos, wall_sz_fb, wall_col);

    const float side_rot[4] = {0.0F, 0.7071068F, 0.0F, 0.7071068F};
    const float wall_sz_lr[2] = {ROOM_D, ROOM_H};
    const float left_pos[3] = {-hw, ROOM_H * 0.5F, 0.0F};
    nt_shape_renderer_rect_rot(left_pos, wall_sz_lr, side_rot, wall_col);
    const float right_pos[3] = {hw, ROOM_H * 0.5F, 0.0F};
    nt_shape_renderer_rect_rot(right_pos, wall_sz_lr, side_rot, wall_col);
}

static void draw_shape(void) {
    const float pos[3] = {0.0F, ROOM_H * 0.5F, 0.0F};
    versor q;
    vec3 axis = {0.0F, 1.0F, 0.0F};
    glm_quatv(q, s_shape_yaw, axis);
    const float rot[4] = {q[0], q[1], q[2], q[3]};
    const float *col = s_shape_colors[s_shape_kind];
    const float wire_col[4] = {0.0F, 0.0F, 0.0F, 1.0F};
    switch (s_shape_kind) {
    case SHAPE_CUBE: {
        const float sz[3] = {1.6F, 1.6F, 1.6F};
        nt_shape_renderer_cube_rot(pos, sz, rot, col);
        nt_shape_renderer_cube_wire_rot(pos, sz, rot, wire_col);
        break;
    }
    case SHAPE_SPHERE:
        nt_shape_renderer_sphere_rot(pos, 1.0F, rot, col);
        nt_shape_renderer_sphere_wire_rot(pos, 1.0F, rot, wire_col);
        break;
    case SHAPE_CAPSULE:
        nt_shape_renderer_capsule_rot(pos, 0.5F, 2.0F, rot, col);
        nt_shape_renderer_capsule_wire_rot(pos, 0.5F, 2.0F, rot, wire_col);
        break;
    default:
        break;
    }
}
// #endregion

// #region resource binding
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void try_bind_resources(void) {
    if (!s_atlas_bound && nt_resource_is_ready(s_atlas_handle)) {
        s_white_region_idx = nt_atlas_find_region(s_atlas_handle, ASSET_ATLAS_REGION_UI_BUTTONS_DEMO_ATLAS__WHITE.value);
        NT_ASSERT(s_white_region_idx != NT_ATLAS_INVALID_REGION);
        s_button_blue_idx = nt_atlas_find_region(s_atlas_handle, ASSET_ATLAS_REGION_UI_BUTTONS_DEMO_ATLAS_BUTTON_BLUE.value);
        NT_ASSERT(s_button_blue_idx != NT_ATLAS_INVALID_REGION);
        s_button_green_idx = nt_atlas_find_region(s_atlas_handle, ASSET_ATLAS_REGION_UI_BUTTONS_DEMO_ATLAS_BUTTON_GREEN.value);
        NT_ASSERT(s_button_green_idx != NT_ATLAS_INVALID_REGION);
        nt_ui_set_atlas_white_region(s_ctx, s_atlas_handle, s_white_region_idx);

        /* IDLE: blue everywhere with a small hover-press ease. */
        s_btn_idle = (nt_ui_button_style_t){
            .idle = {.atlas = s_atlas_handle, .bg_region = s_button_blue_idx, .bg_tint = 0xFFFFFFFFU, .scale = 1.0F, .opacity = 1.0F},
            .hover = {.bg_region = s_button_blue_idx, .bg_tint = 0xFFFFFFFFU, .scale = 1.04F, .opacity = 1.0F},
            .pressed = {.bg_region = s_button_blue_idx, .bg_tint = 0xFFFFFFFFU, .scale = 0.96F, .opacity = 1.0F},
            .disabled = {.bg_region = s_button_blue_idx, .bg_tint = 0xFFFFFFFFU, .scale = 1.0F, .opacity = 0.4F},
            .transition_speed = 8.0F,
            .slice9_scale = 1.0F,
        };
        /* ACTIVE: green (currently-selected) — same animation but different art. */
        s_btn_active = s_btn_idle;
        s_btn_active.idle.bg_region = s_button_green_idx;
        s_btn_active.hover.bg_region = s_button_green_idx;
        s_btn_active.pressed.bg_region = s_button_green_idx;
        s_btn_active.disabled.bg_region = s_button_green_idx;

        s_atlas_bound = true;
        nt_log_info("ui_3d_demo: atlas bound");
    }
    if (!s_font_bound && nt_resource_is_ready(s_font_resource)) {
        nt_font_add(s_font, s_font_resource);
        nt_ui_set_font(s_ctx, 0U, s_font);
        s_font_bound = true;
        nt_log_info("ui_3d_demo: font bound");
    }
}
// #endregion

// #region UI panels
/* Wall-mount XFORM: panel local Clay-bbox center (cx,cy,0) maps to world `wx,wy,wz`.
 * scale_y is negative — that's the only way to flip Clay Y-down → world Y-up without also
 * mirroring X (rotation_z=π) or flipping the panel normal (rotation_x=π). Engine asserts
 * accept non-zero scale (positive or negative); the negative scale is the documented 3D path. */
static nt_ui_transform_t make_wall_xform(float wx, float wy, float wz, float yaw, float panel_w_px, float panel_h_px) {
    nt_ui_transform_t t = nt_ui_transform_defaults();
    const float cx = panel_w_px * 0.5F;
    const float cy = panel_h_px * 0.5F;
    t.offset_x = wx - cx;
    t.offset_y = wy - cy;
    t.offset_z = wz;
    t.rotation_y = yaw;
    t.scale_x = PANEL_SCALE;
    t.scale_y = -PANEL_SCALE;
    t.scale_z = PANEL_SCALE;
    return t;
}

static void ensure_ids(void) {
    if (s_ids_ready) {
        return;
    }
    s_id_shape_btn[SHAPE_CUBE] = nt_ui_id("btn_shape_cube");
    s_id_shape_btn[SHAPE_SPHERE] = nt_ui_id("btn_shape_sphere");
    s_id_shape_btn[SHAPE_CAPSULE] = nt_ui_id("btn_shape_capsule");
    s_id_speed_btn[SPEED_STOP] = nt_ui_id("btn_speed_stop");
    s_id_speed_btn[SPEED_SLOW] = nt_ui_id("btn_speed_slow");
    s_id_speed_btn[SPEED_MED] = nt_ui_id("btn_speed_med");
    s_id_speed_btn[SPEED_FAST] = nt_ui_id("btn_speed_fast");
    s_ids_ready = true;
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void declare_panels(void) {
    ensure_ids();
    /* Both panels mount on the FRONT wall (-Z) side-by-side. The default panel normal is +Z;
     * with no rotation_y it points at the player (at +Z looking -Z). Y-flip is the negative
     * scale_y in make_wall_xform, so the glyphs read upright AND the panel reads top-to-bottom. */
    const float hd = ROOM_D * 0.5F;
    const float wall_y = 2.8F;
    const float wall_z = -hd + 0.05F;
    const nt_ui_transform_t xform_left = make_wall_xform(-4.5F, wall_y, wall_z, 0.0F, (float)PANEL_W, (float)PANEL_H);
    const nt_ui_transform_t xform_right = make_wall_xform(4.5F, wall_y, wall_z, 0.0F, (float)PANEL_W, (float)PANEL_H);

    /* Root: full-fb invisible group anchoring the floating panels. */
    CLAY({.id = CLAY_ID("ui3d-root"), .layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0)}}}) {

        /* ===== Panel A: SHAPE — floating so its Clay bbox starts at (0,0). Without floating
         * Clay would slide the second panel right (cx=480), the XFORM would offset to
         * wx+320, and only the first panel would land on the wall. */
        CLAY({.id = CLAY_ID("panel-shape"),
              .userData = (void *)NT_UI_DATA_XFORM(LAYER_PANEL, &xform_left, 1.0F),
              .floating = {.attachTo = CLAY_ATTACH_TO_PARENT, .attachPoints = {.element = CLAY_ATTACH_POINT_LEFT_TOP, .parent = CLAY_ATTACH_POINT_LEFT_TOP}},
              .layout = {.sizing = {CLAY_SIZING_FIXED(PANEL_W), CLAY_SIZING_FIXED(PANEL_H)},
                         .padding = CLAY_PADDING_ALL(12),
                         .layoutDirection = CLAY_TOP_TO_BOTTOM,
                         .childGap = 10,
                         .childAlignment = {CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_TOP}},
              .backgroundColor = {30.0F, 32.0F, 42.0F, 230.0F}}) {

            CLAY({.layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(PANEL_TITLE_H)}, .childAlignment = {CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER}}}) {
                nt_ui_label(s_ctx, NT_UI_DATA_LAYER(LAYER_LABEL), "SHAPE", &s_panel_title_style);
            }
            for (int i = 0; i < SHAPE_COUNT; ++i) {
                const nt_ui_button_style_t *style = (i == s_shape_kind) ? &s_btn_active : &s_btn_idle;
                nt_ui_button_begin(s_ctx, NT_UI_DATA_LAYER(LAYER_PANEL), s_id_shape_btn[i], style, &s_btn_decl, true);
                nt_ui_label(s_ctx, NT_UI_DATA_LAYER(LAYER_LABEL), s_shape_labels[i], &s_btn_label_style);
                if (nt_ui_button_end(s_ctx)) {
                    s_shape_kind = i;
                }
            }
        }

        /* ===== Panel B: SPEED — also floating to keep bbox center at (PANEL_W/2, PANEL_H/2). */
        CLAY({.id = CLAY_ID("panel-speed"),
              .userData = (void *)NT_UI_DATA_XFORM(LAYER_PANEL, &xform_right, 1.0F),
              .floating = {.attachTo = CLAY_ATTACH_TO_PARENT, .attachPoints = {.element = CLAY_ATTACH_POINT_LEFT_TOP, .parent = CLAY_ATTACH_POINT_LEFT_TOP}},
              .layout = {.sizing = {CLAY_SIZING_FIXED(PANEL_W), CLAY_SIZING_FIXED(PANEL_H)},
                         .padding = CLAY_PADDING_ALL(12),
                         .layoutDirection = CLAY_TOP_TO_BOTTOM,
                         .childGap = 10,
                         .childAlignment = {CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_TOP}},
              .backgroundColor = {30.0F, 32.0F, 42.0F, 230.0F}}) {

            CLAY({.layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(PANEL_TITLE_H)}, .childAlignment = {CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER}}}) {
                nt_ui_label(s_ctx, NT_UI_DATA_LAYER(LAYER_LABEL), "SPEED", &s_panel_title_style);
            }
            for (int i = 0; i < SPEED_COUNT; ++i) {
                const nt_ui_button_style_t *style = (i == s_speed_kind) ? &s_btn_active : &s_btn_idle;
                nt_ui_button_begin(s_ctx, NT_UI_DATA_LAYER(LAYER_PANEL), s_id_speed_btn[i], style, &s_btn_decl, true);
                nt_ui_label(s_ctx, NT_UI_DATA_LAYER(LAYER_LABEL), s_speed_labels[i], &s_btn_label_style);
                if (nt_ui_button_end(s_ctx)) {
                    s_speed_kind = i;
                }
            }
        }
    }
}
// #endregion

// #region hud
static void draw_hud_block(const char *text, float x, float y, float size, const float color[4]) {
    mat4 model;
    glm_mat4_identity(model);
    glm_translate(model, (vec3){x, y, 0.0F});
    nt_text_renderer_draw(text, (const float *)model, size, color, 0.0F, 0.0F);
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void draw_hud(float fb_w, float fb_h) {
    if (!s_font_bound) {
        return;
    }
    nt_text_renderer_set_material(s_text_material);
    nt_text_renderer_set_font(s_font);

    const float white[4] = {0.95F, 0.95F, 0.98F, 1.0F};
    const float accent[4] = {1.00F, 0.85F, 0.30F, 1.0F};
    const float dim[4] = {0.75F, 0.78F, 0.82F, 1.0F};

    const float left_x = 12.0F;
    float y = fb_h - HUD_TITLE_SIZE - 4.0F;
    draw_hud_block("UI 3D DEMO  (PASS 2)", left_x, y, HUD_TITLE_SIZE, accent);
    y -= HUD_TITLE_SIZE + 6.0F;

    const char *lines[] = {
        "WASD          walk",
        "RMB drag      mouselook",
        "Q / E         yaw left / right",
        "LMB           click world panel",
        "R             reset position",
        "1 / 2 / 3     shape  (cube / sphere / capsule)",
        "4 / 5 / 6 / 7 speed  (stop / slow / medium / fast)",
        "F1            debug overlay (pos / fps)",
        "F2            UI inspector (sidebar; WIP in 3D)",
        "Esc           quit",
    };
    for (size_t i = 0; i < sizeof(lines) / sizeof(lines[0]); ++i) {
        draw_hud_block(lines[i], left_x, y, HUD_SIZE, white);
        y -= HUD_SIZE + 2.0F;
    }

    /* Top-right: FPS + current shape + speed status. */
    const float right_x = fb_w - 360.0F;
    float ry = fb_h - HUD_SIZE - 6.0F;
    char fps_line[64];
    (void)snprintf(fps_line, sizeof fps_line, "fps: %5.1f   cpu: %.2f ms", (double)nt_stats_get_fps(), (double)nt_stats_get_cpu_ms());
    draw_hud_block(fps_line, right_x, ry, HUD_SIZE, white);
    ry -= HUD_SIZE + 2.0F;

    char status[64];
    (void)snprintf(status, sizeof status, "shape: %-7s   speed: %s", s_shape_labels[s_shape_kind], s_speed_labels[s_speed_kind]);
    draw_hud_block(status, right_x, ry, HUD_SIZE, accent);

    if (s_debug_overlay) {
        /* Anchor the debug block ABOVE the bottom; nt_stats is ~6 lines so allow ~110 px. */
        const float dbg_y_top = 130.0F;
        char dbg[256];
        const float yaw_deg = s_player_yaw * 57.29578F;
        const float pitch_deg = s_player_pitch * 57.29578F;
        (void)snprintf(dbg, sizeof dbg, "pos: (%+.2f, %+.2f, %+.2f)   yaw: %+.1f°   pitch: %+.1f°", (double)s_player_pos[0], (double)s_player_pos[1], (double)s_player_pos[2], (double)yaw_deg,
                       (double)pitch_deg);
        draw_hud_block(dbg, left_x, dbg_y_top, HUD_SIZE, dim);
        mat4 stats_model;
        glm_mat4_identity(stats_model);
        glm_translate(stats_model, (vec3){left_x, dbg_y_top - HUD_SIZE - 6.0F, 0.0F});
        const float stats_color[4] = {0.8F, 0.9F, 0.8F, 1.0F};
        nt_stats_draw(s_text_material, s_font, (const float *)stats_model, HUD_SIZE - 2.0F, stats_color);
    }

    nt_text_renderer_flush();
}
// #endregion

// #region frame
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void frame(void) {
    nt_stats_frame_begin();
    nt_window_poll();
    nt_input_poll();
    nt_mem_scratch_reset();

    const float dt = g_nt_app.dt;

#ifndef NT_PLATFORM_WEB
    if (nt_input_key_is_pressed(NT_KEY_ESCAPE)) {
        nt_app_quit();
    }
#endif
    if (nt_input_key_is_pressed(NT_KEY_R)) {
        player_reset();
    }
    if (nt_input_key_is_pressed(NT_KEY_F1)) {
        s_debug_overlay = !s_debug_overlay;
    }
    if (nt_input_key_is_pressed(NT_KEY_F2)) {
        nt_ui_inspector_set_active(s_ctx, !nt_ui_inspector_is_active(s_ctx));
    }
    if (nt_input_key_is_pressed(NT_KEY_1)) {
        s_shape_kind = SHAPE_CUBE;
    }
    if (nt_input_key_is_pressed(NT_KEY_2)) {
        s_shape_kind = SHAPE_SPHERE;
    }
    if (nt_input_key_is_pressed(NT_KEY_3)) {
        s_shape_kind = SHAPE_CAPSULE;
    }
    if (nt_input_key_is_pressed(NT_KEY_4)) {
        s_speed_kind = SPEED_STOP;
    }
    if (nt_input_key_is_pressed(NT_KEY_5)) {
        s_speed_kind = SPEED_SLOW;
    }
    if (nt_input_key_is_pressed(NT_KEY_6)) {
        s_speed_kind = SPEED_MED;
    }
    if (nt_input_key_is_pressed(NT_KEY_7)) {
        s_speed_kind = SPEED_FAST;
    }

    player_update(dt);
    s_shape_yaw += s_speed_table[s_speed_kind] * dt;

    nt_resource_step();
    nt_material_step();
    try_bind_resources();

    const float fb_w = (float)(g_nt_window.fb_width > 0 ? g_nt_window.fb_width : 800);
    const float fb_h = (float)(g_nt_window.fb_height > 0 ? g_nt_window.fb_height : 600);
    const float aspect = (fb_h > 0.0F) ? (fb_w / fb_h) : 1.0F;

    mat4 vp_3d;
    compute_perspective_vp(vp_3d, aspect);
    mat4 view_m;
    mat4 proj_m;
    mat4 vp_ortho;
    glm_mat4_identity(view_m);
    glm_ortho(0.0F, fb_w, 0.0F, fb_h, -1.0F, 1.0F, proj_m);
    glm_mat4_mul(proj_m, view_m, vp_ortho);

    nt_frame_uniforms_t uniforms_3d = {0};
    memcpy(uniforms_3d.view_proj, vp_3d, 64);
    uniforms_3d.time[0] = (float)nt_time_now();
    uniforms_3d.time[1] = dt;
    uniforms_3d.resolution[0] = fb_w;
    uniforms_3d.resolution[1] = fb_h;
    uniforms_3d.resolution[2] = (fb_w > 0.0F) ? (1.0F / fb_w) : 0.0F;
    uniforms_3d.resolution[3] = (fb_h > 0.0F) ? (1.0F / fb_h) : 0.0F;
    uniforms_3d.near_far[0] = 0.1F;
    uniforms_3d.near_far[1] = 100.0F;

    nt_frame_uniforms_t uniforms_2d = uniforms_3d;
    memcpy(uniforms_2d.view_proj, vp_ortho, 64);
    memcpy(uniforms_2d.view, view_m, 64);
    memcpy(uniforms_2d.proj, proj_m, 64);
    uniforms_2d.near_far[0] = -1.0F;
    uniforms_2d.near_far[1] = 1.0F;

    nt_gfx_begin_frame();
    nt_gfx_begin_segment("frame");
    if (g_nt_gfx.context_restored) {
        nt_resource_invalidate(NT_ASSET_SHADER_CODE);
        nt_resource_invalidate(NT_ASSET_TEXTURE);
        nt_resource_invalidate(NT_ASSET_FONT);
        nt_gfx_destroy_buffer(s_frame_ubo);
        s_frame_ubo = nt_gfx_make_buffer(&(nt_buffer_desc_t){
            .type = NT_BUFFER_UNIFORM,
            .usage = NT_USAGE_DYNAMIC,
            .size = sizeof(nt_frame_uniforms_t),
            .label = "frame_uniforms",
        });
        nt_shape_renderer_restore_gpu();
        nt_sprite_renderer_restore_gpu();
        nt_text_renderer_restore_gpu();
        s_atlas_bound = false;
        s_font_bound = false;
    }

    nt_gfx_begin_pass(&(nt_pass_desc_t){.clear_color = {0.06F, 0.07F, 0.10F, 1.0F}, .clear_depth = 1.0F});
    nt_font_step();

    /* 3D pass: shape_renderer drives its own VP. */
    const float cam_pos[3] = {s_player_pos[0], s_player_pos[1], s_player_pos[2]};
    nt_shape_renderer_set_vp((const float *)vp_3d);
    nt_shape_renderer_set_cam_pos(cam_pos);
    nt_shape_renderer_set_depth(true);
    draw_room();
    draw_shape();
    nt_shape_renderer_flush();

    /* UI: needs perspective VP in frame_uniforms for sprite/text material shaders. */
    const nt_material_info_t *sprite_info = nt_material_get_info(s_sprite_material);
    const nt_material_info_t *text_info = nt_material_get_info(s_text_material);
    const bool ui_can_render = s_atlas_bound && s_font_bound && sprite_info && sprite_info->ready && text_info && text_info->ready;

    if (ui_can_render) {
        nt_gfx_update_buffer(s_frame_ubo, &uniforms_3d, sizeof uniforms_3d);
        nt_gfx_bind_uniform_buffer(s_frame_ubo, 0);

        const nt_pointer_t mouse_phys = g_nt_input.pointers[0];
        nt_ui_begin(s_ctx, fb_w, fb_h, dt, &mouse_phys, 1);
        nt_ui_set_view_proj(s_ctx, (const float *)vp_3d);
        declare_panels();
        nt_ui_end(s_ctx);

        const nt_ui_target_t target = {.viewport = {0.0F, 0.0F, fb_w, fb_h}};
        nt_ui_walk(s_ctx, &target);
        nt_ui_inspector_overlay_draw(s_ctx, &target, s_font, 16.0F);
        /* Flush UI draws under VP_3D BEFORE switching uniforms; otherwise labels emitted
         * by ui_walk get rasterized with the next pass's ortho matrix and vanish. */
        nt_sprite_renderer_flush();
        nt_text_renderer_flush();

        /* DEBUG: standalone text directly in world space at (0, 4, 0). If this shows up,
         * text material + font + VP_3D plumbing work — panel labels then disappear due to a
         * walker emit issue. If this is also invisible, the bug is below nt_ui. */
        if (s_font_bound) {
            mat4 debug_model;
            glm_mat4_identity(debug_model);
            glm_translate(debug_model, (vec3){-2.5F, 4.0F, 0.0F});
            const float yellow[4] = {1.0F, 1.0F, 0.2F, 1.0F};
            nt_text_renderer_set_material(s_text_material);
            nt_text_renderer_set_font(s_font);
            nt_text_renderer_draw("HELLO 3D WORLD", (const float *)debug_model, 0.6F, yellow, 0.0F, 0.0F);
            nt_text_renderer_flush();
        }
    }

    /* HUD: ortho VP. */
    if (text_info && text_info->ready) {
        nt_gfx_update_buffer(s_frame_ubo, &uniforms_2d, sizeof uniforms_2d);
        nt_gfx_bind_uniform_buffer(s_frame_ubo, 0);
        draw_hud(fb_w, fb_h);
    }

    nt_gfx_end_pass();
    nt_gfx_end_segment();
    nt_gfx_end_frame();
    nt_stats_frame_end();
    nt_window_swap_buffers();
}
// #endregion

// #region main
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;

    nt_engine_config_t config = {0};
    config.app_name = "ui_3d_demo";
    config.version = 1;
    if (nt_engine_init(&config) != NT_OK) {
        return 1;
    }

    g_nt_window.width = 1280;
    g_nt_window.height = 800;
    nt_window_init();
    nt_input_init();

    nt_gfx_desc_t gfx_desc = nt_gfx_desc_defaults();
    gfx_desc.depth = true;
    nt_gfx_init(&gfx_desc);
    nt_gfx_register_global_block("Globals", 0);

    nt_http_init();
    nt_fs_init();
    nt_hash_init(&(nt_hash_desc_t){0});
    nt_resource_init(&(nt_resource_desc_t){0});
    nt_mem_scratch_init(SCRATCH_ARENA_SIZE);

    nt_resource_set_activator(NT_ASSET_TEXTURE, nt_gfx_activate_texture, nt_gfx_deactivate_texture);
    nt_resource_set_activator(NT_ASSET_SHADER_CODE, nt_gfx_activate_shader, nt_gfx_deactivate_shader);
    nt_atlas_init();
    nt_material_init(&(nt_material_desc_t){.max_materials = 4});
    nt_font_init(&(nt_font_desc_t){.max_fonts = 2});

    nt_shape_renderer_init();
    nt_sprite_renderer_desc_t sr_desc = nt_sprite_renderer_desc_defaults();
    nt_sprite_renderer_init(&sr_desc);
    nt_text_renderer_init();

    nt_ui_module_init();
    nt_ui_create_desc_t ui_desc = nt_ui_create_desc_defaults();
    ui_desc.use_raycast_input = true;
    nt_log_info("ui_3d_demo: UI arena %zu KB, min required %zu KB", sizeof s_ui_arena / 1024U, nt_ui_min_arena_size(&ui_desc) / 1024U);
    s_ctx = nt_ui_create_context(s_ui_arena, sizeof s_ui_arena, &ui_desc);
    NT_ASSERT(s_ctx != NULL && "ui_3d_demo: failed to create UI context");

    g_nt_app.target_dt = 0.0F;

    s_frame_ubo = nt_gfx_make_buffer(&(nt_buffer_desc_t){
        .type = NT_BUFFER_UNIFORM,
        .usage = NT_USAGE_DYNAMIC,
        .size = sizeof(nt_frame_uniforms_t),
        .label = "frame_uniforms",
    });

    s_pack_id = nt_hash32_str("ui_buttons_demo");
    nt_resource_mount(s_pack_id, 100);
#ifdef NT_CDN_URL
    nt_resource_load_auto(s_pack_id, NT_CDN_URL "/ui_buttons_demo/ui_buttons_demo.ntpack");
#else
    nt_resource_load_auto(s_pack_id, "assets/ui_buttons_demo.ntpack");
#endif

    s_sprite_vs_handle = nt_resource_request(ASSET_SHADER_ASSETS_SHADERS_SPRITE_VERT, NT_ASSET_SHADER_CODE);
    s_sprite_fs_handle = nt_resource_request(ASSET_SHADER_ASSETS_SHADERS_SPRITE_FRAG, NT_ASSET_SHADER_CODE);
    s_text_vs_handle = nt_resource_request(ASSET_SHADER_ASSETS_SHADERS_SLUG_TEXT_VERT, NT_ASSET_SHADER_CODE);
    s_text_fs_handle = nt_resource_request(ASSET_SHADER_ASSETS_SHADERS_SLUG_TEXT_FRAG, NT_ASSET_SHADER_CODE);
    s_atlas_handle = nt_resource_request(ASSET_ATLAS_UI_BUTTONS_DEMO_ATLAS, NT_ASSET_ATLAS);
    s_atlas_tex_handle = nt_resource_request(ASSET_TEXTURE_UI_BUTTONS_DEMO_ATLAS_TEX0, NT_ASSET_TEXTURE);
    s_font_resource = nt_resource_request(ASSET_FONT_UI_BUTTONS_DEMO_FONT, NT_ASSET_FONT);

    s_sprite_material = nt_material_create(&(nt_material_create_desc_t){
        .vs = s_sprite_vs_handle,
        .fs = s_sprite_fs_handle,
        .textures = {{.name = "u_texture", .resource = s_atlas_tex_handle}},
        .texture_count = 1,
        .blend_mode = NT_BLEND_MODE_ALPHA,
        .depth_test = false,
        .depth_write = false,
        .cull_mode = NT_CULL_NONE,
        .label = "ui_3d_demo_sprite",
    });
    s_text_material = nt_material_create(&(nt_material_create_desc_t){
        .vs = s_text_vs_handle,
        .fs = s_text_fs_handle,
        .blend_mode = NT_BLEND_MODE_ALPHA,
        .depth_test = false,
        .depth_write = false,
        .cull_mode = NT_CULL_NONE,
        .label = "ui_3d_demo_text",
    });

    nt_ui_set_sprite_material(s_ctx, s_sprite_material);
    nt_ui_set_text_material(s_ctx, s_text_material);

    s_font = nt_font_create(&(nt_font_create_desc_t){
        .curve_texture_width = 1024,
        .curve_texture_height = 512,
        .band_texture_height = 256,
        .band_count = 8,
        .measure_cache_size = 256,
    });

    nt_resource_set_activate_time_budget(0);
    nt_stats_desc_t stats_desc = nt_stats_desc_defaults();
    nt_stats_init(&stats_desc);

#ifdef NT_PLATFORM_WEB
    nt_platform_web_loading_complete();
#endif

    nt_log_info("ui_3d_demo: PASS 2 — walk + look + click world panels (SHAPE left, SPEED right). Esc quit.");

    nt_app_run(frame);

#ifndef NT_PLATFORM_WEB
    nt_ui_destroy_context(s_ctx);
    nt_ui_module_shutdown();
    nt_text_renderer_shutdown();
    nt_sprite_renderer_shutdown();
    nt_shape_renderer_shutdown();
    nt_font_destroy(s_font);
    nt_font_shutdown();
    nt_material_destroy(s_sprite_material);
    nt_material_destroy(s_text_material);
    nt_material_shutdown();
    nt_stats_shutdown();
    nt_mem_scratch_shutdown();
    nt_resource_shutdown();
    nt_fs_shutdown();
    nt_http_shutdown();
    nt_hash_shutdown();
    nt_gfx_destroy_buffer(s_frame_ubo);
    nt_gfx_shutdown();
    nt_input_shutdown();
    nt_window_shutdown();
    nt_engine_shutdown();
#endif
    return 0;
}
// #endregion
