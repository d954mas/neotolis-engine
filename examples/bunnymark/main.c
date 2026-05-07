/*
 * Bunnymark Demo -- Neotolis Engine
 *
 * Interactive sprite stress test mirroring britzl/defold-bunnymark
 * update_native_position_velocity. Click/tap spawns 500 bunnies; hold spawns
 * 50/frame; arrow up/down add/remove 100 (shift = 1000).
 *
 * Physics constants live in bunny_physics.h (pure C, stdint-only). The renderer
 * pipeline is: nt_atlas + nt_sprite_comp + nt_sprite_renderer with a
 * game-shipped sprite.vert / sprite.frag.
 *
 * Build packs first:  build_bunnymark_packs build/examples/bunnymark
 *
 * Coordinate convention: bottom-left = (0, 0), Y up. Ortho VP matches.
 */

#include "app/nt_app.h"
#include "atlas/nt_atlas.h"
#include "core/nt_assert.h"
#include "core/nt_core.h"
#include "core/nt_platform.h"
#include "drawable_comp/nt_drawable_comp.h"
#include "entity/nt_entity.h"
#include "font/nt_font.h"
#include "fs/nt_fs.h"
#include "graphics/nt_gfx.h"
#include "hash/nt_hash.h"
#include "http/nt_http.h"
#include "input/nt_input.h"
#include "log/nt_log.h"
#include "material/nt_material.h"
#include "material_comp/nt_material_comp.h"
#include "render/nt_render_defs.h"
#include "renderers/nt_sprite_renderer.h"
#include "renderers/nt_text_renderer.h"
#include "resource/nt_resource.h"
#include "sprite_comp/nt_sprite_comp.h"
#include "stats/nt_stats.h"
#include "time/nt_time.h"
#include "transform_comp/nt_transform_comp.h"
#include "window/nt_window.h"

#include "bunny_physics.h"
#include "math/nt_math.h"
#include "nt_pack_format.h"

#include "bunnymark_assets.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#ifdef NT_PLATFORM_WEB
#include "platform/web/nt_platform_web.h"
#endif

/* ---- Demo limits ----
 *
 * BUNNY_MAX is bounded by uint16_t entity/component storage. Keep spare slots
 * below 65535 for non-bunny demo entities and future overlays. */
#define BUNNY_MAX 60000

#define BUNNY_INITIAL_COUNT 500
#define BUNNY_CLICK_SPAWN_COUNT 500
#define BUNNY_HOLD_SPAWN_RATE 50 /* per-frame on hold */
#define BUNNY_BULK_ADD 100       /* arrow up/down */
#define BUNNY_BULK_ADD_BIG 1000  /* shift + arrow up/down */

/* ---- State ---- */

static nt_buffer_t s_frame_ubo;

static nt_hash32_t s_pack_id;
static nt_hash32_t s_hd_pack_id;

static nt_resource_t s_atlas_handle;
static nt_resource_t s_vs_handle;
static nt_resource_t s_fs_handle;

/* HD/SD toggle state — demo starts in SD.
 *
 * BUNNYMARK_HD_AVAILABLE is set by examples/bunnymark/CMakeLists.txt when the
 * raw/hd/ directory exists. When absent, s_hd_available stays false and the
 * H key + tap-zone toggle log a warning instead of crashing.
 *
 * The toggle works by mounting/unmounting the HD pack at higher priority than
 * the SD pack. Atlas merge re-maps regions in place so all live
 * SpriteComponent.region_index values stay valid across the toggle. */
#ifdef BUNNYMARK_HD_AVAILABLE
static bool s_hd_available = true;
#else
static bool s_hd_available = false;
#endif
static bool s_hd_active = false;
static bool s_hd_load_started = false;

static nt_material_t s_sprite_material;

/* Stats overlay — separate material/font from sprites. */
static nt_material_t s_text_material;
static nt_font_t s_overlay_font;

/* Demo-level BunnyComponent payload. Engine rendering still goes through
 * entity + transform/material/drawable/sprite components per bunny. */
static nt_bunny_t s_bunnies[BUNNY_MAX];
static nt_entity_t s_entities[BUNNY_MAX];
static uint32_t s_bunny_count;
static bool s_initial_spawned;

static uint16_t s_variant_region_idx[5]; /* resolved at startup once */

static nt_render_item_t s_items[BUNNY_MAX];

static nt_bunny_rng_t s_rng = {.state = 0x9E3779B97F4A7C15ULL}; /* non-zero seed */

static bool s_pack_dumped;
static bool s_atlas_resolved;

/* ---- Helpers ---- */

static uint32_t s_canvas_w(void) { return g_nt_window.fb_width > 0 ? g_nt_window.fb_width : 800; }
static uint32_t s_canvas_h(void) { return g_nt_window.fb_height > 0 ? g_nt_window.fb_height : 600; }

// #region HD/SD toggle
/* Tap-zone geometry: ~120x40 px in the top-right corner. Coordinates are in
 * canvas (window) pixels with y top-down (raw pointer space). The tap_zone_hit
 * test runs against the unflipped pointer, BEFORE the y-flip that converts to
 * world space — and BEFORE the world-space spawn block. */
#define TAP_ZONE_W 120.0F
#define TAP_ZONE_H 40.0F

static bool tap_zone_hit(float px, float py, float w, float h) {
    /* Top-right rect in window-pixel coordinates (y top-down). */
    (void)h;
    return (px >= w - TAP_ZONE_W) && (px <= w) && (py >= 0.0F) && (py <= TAP_ZONE_H);
}

/* Mount or unmount the HD pack at higher priority than SD. Atlas merge re-maps
 * regions in place — region_index values stay stable across the toggle, so
 * live bunnies don't need set_region re-binding. */
static void toggle_atlas_quality(void) {
    if (!s_hd_available) {
        nt_log_warn("Bunnymark: HD pack not available — toggle is a no-op (drop 5 PNGs in examples/bunnymark/raw/hd/ and re-run cmake configure to enable)");
        return;
    }
    if (!s_hd_load_started) {
        /* First activation: mount the HD pack at a higher priority than SD
         * (SD priority is 100; HD = 200) and kick off the auto-load. */
        nt_result_t mount_r = nt_resource_mount(s_hd_pack_id, 200);
        if (mount_r != NT_OK) {
            nt_log_warn("Bunnymark: HD pack mount failed (result=%d) — toggle is a no-op", (int)mount_r);
            s_hd_available = false;
            return;
        }
#ifdef NT_CDN_URL
        nt_resource_load_auto(s_hd_pack_id, NT_CDN_URL "/bunnymark/bunnymark_hd.ntpack");
#else
        nt_resource_load_auto(s_hd_pack_id, "assets/bunnymark_hd.ntpack");
#endif
        s_hd_load_started = true;
        s_hd_active = true;
        nt_log_info("Bunnymark: atlas toggled HD (HD pack mounted at priority 200)");
    } else {
        if (s_hd_active) {
            /* Lower priority below SD instead of unmounting, avoiding reload. */
            nt_resource_set_priority(s_hd_pack_id, 50);
            s_hd_active = false;
            nt_log_info("Bunnymark: atlas toggled SD (HD pack priority lowered to 50)");
        } else {
            /* Raise priority above SD. */
            nt_resource_set_priority(s_hd_pack_id, 200);
            s_hd_active = true;
            nt_log_info("Bunnymark: atlas toggled HD (HD pack priority raised to 200)");
        }
    }
}
// #endregion

static void resolve_atlas_regions(void) {
    if (s_atlas_resolved || !nt_resource_is_ready(s_atlas_handle)) {
        return;
    }
    /* Region names match builder output: "bunny_red.png", etc. (codegen header
     * shows ASSET_ATLAS_REGION_BUNNIES_BUNNY_RED_PNG = hash of "bunnies/bunny_red.png").
     * The atlas indexes regions by region-name hash within the atlas — that's
     * the basename of the source file (with extension). */
    static const uint64_t names[5] = {
        0x83DBC7E6A787C8D3ULL, /* nt_hash64_str("bunnies/bunny_red.png") */
        0xED252533A158CD02ULL, /* "bunnies/bunny_green.png" */
        0xED791D4914243C90ULL, /* "bunnies/bunny_blue.png" */
        0xCCC0131AD05DE339ULL, /* "bunnies/bunny_yellow.png" */
        0x3CB0925D85DCF606ULL, /* "bunnies/bunny_purple.png" */
    };
    for (int i = 0; i < 5; i++) {
        uint32_t ridx = nt_atlas_find_region(s_atlas_handle, names[i]);
        NT_ASSERT(ridx != NT_ATLAS_INVALID_REGION && "bunny region not found in atlas");
        s_variant_region_idx[i] = (uint16_t)ridx;
    }
    s_atlas_resolved = true;
    nt_log_info("Bunnymark: atlas resolved, 5 region indices cached");
}

// #region spawn
static void init_bunny_entity(uint32_t i) {
    s_entities[i] = nt_entity_create();
    NT_ASSERT(s_entities[i].id != 0 && "Bunnymark entity pool exhausted");

    nt_transform_comp_add(s_entities[i]);
    nt_material_comp_add(s_entities[i]);
    nt_drawable_comp_add(s_entities[i]);
    nt_sprite_comp_add(s_entities[i]);

    *nt_material_comp_handle(s_entities[i]) = s_sprite_material;
    nt_drawable_comp_set_color(s_entities[i], 1.0F, 1.0F, 1.0F, 1.0F);

    float *scale = nt_transform_comp_scale(s_entities[i]);
    scale[0] = 1.0F;
    scale[1] = 1.0F;
    scale[2] = 1.0F;
}

static void spawn_one_defold(void) {
    if (s_bunny_count >= BUNNY_MAX) {
        return; /* hard cap */
    }
    if (!s_atlas_resolved) {
        return;
    }
    uint32_t i = s_bunny_count++;
    init_bunny_entity(i);
    nt_bunny_init_defold(&s_bunnies[i], (float)s_canvas_w(), &s_rng);
    /* Region picked per spawn from the cached 5. */
    nt_sprite_comp_set_region(s_entities[i], s_atlas_handle, s_variant_region_idx[s_bunnies[i].variant]);
    *nt_transform_comp_dirty(s_entities[i]) = true;
}

static void spawn_n_defold(uint32_t n) {
    for (uint32_t i = 0; i < n; i++) {
        if (s_bunny_count >= BUNNY_MAX) {
            break;
        }
        spawn_one_defold();
    }
}
// #endregion

/* ---- Frame callback ---- */

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void frame(void) {
    nt_stats_frame_begin();
    nt_window_poll();
    nt_input_poll();

#ifndef NT_PLATFORM_WEB
    if (nt_input_key_is_pressed(NT_KEY_ESCAPE)) {
        nt_app_quit();
    }
#endif

    nt_resource_step();
    nt_material_step();
    nt_font_step();
    nt_sprite_comp_sync_resources();

    /* Dump pack contents once, when ready. */
    if (!s_pack_dumped && nt_resource_pack_state(s_pack_id) == NT_PACK_STATE_READY) {
        nt_log_info("======== BUNNYMARK PACK READY ========");
        nt_resource_dump_pack(s_pack_id);
        s_pack_dumped = true;
    }

    /* Atlas region indices resolve once — picks variant per spawn. */
    resolve_atlas_regions();
    if (s_atlas_resolved && !s_initial_spawned) {
        spawn_n_defold(BUNNY_INITIAL_COUNT);
        s_initial_spawned = true;
    }

    // #region input dispatch
    /* g_nt_input.pointers[0] unifies mouse + touch — using the pointer's button
     * state (not nt_input_mouse_*) avoids double-spawn on iOS where touch +
     * mouse events coexist. */
    const nt_pointer_t *p = &g_nt_input.pointers[0];
    float w = (float)s_canvas_w();
    float h = (float)s_canvas_h();
    bool consumed = false;
    /* Tap-zone hit-test runs FIRST against the raw pointer (window pixels,
     * y top-down) — before the y-flip and the world-space spawn — so a click
     * in the top-right "Quality" rect toggles HD/SD without also spawning a
     * bunny at that location. */
    if (p->buttons[NT_BUTTON_LEFT].is_pressed && tap_zone_hit(p->x, p->y, w, h)) {
        toggle_atlas_quality();
        consumed = true;
    }
    /* H key always toggles regardless of pointer state. */
    if (nt_input_key_is_pressed(NT_KEY_H)) {
        toggle_atlas_quality();
    }
    /* T toggles GPU timer queries. Shows current support state in the log. */
    if (nt_input_key_is_pressed(NT_KEY_T)) {
        static bool s_gpu_timing_on = true;
        s_gpu_timing_on = !s_gpu_timing_on;
        nt_gfx_set_gpu_timing_enabled(s_gpu_timing_on);
        nt_log_info("Bunnymark: GPU timing %s (supported=%d)", s_gpu_timing_on ? "ON" : "OFF", (int)nt_gfx_is_gpu_timing_supported());
    }
    if (!consumed) {
        if (p->buttons[NT_BUTTON_LEFT].is_pressed) {
            spawn_n_defold(BUNNY_CLICK_SPAWN_COUNT);
        }
        if (p->buttons[NT_BUTTON_LEFT].is_down) {
            for (int i = 0; i < BUNNY_HOLD_SPAWN_RATE; i++) {
                spawn_one_defold();
            }
        }
    }
    /* Bulk add/remove via arrow keys — the input enum has no
     * NT_KEY_PLUS / NT_KEY_MINUS, so we use arrow up/down. README documents
     * the substitution. */
    bool shift = nt_input_key_is_down(NT_KEY_LSHIFT) || nt_input_key_is_down(NT_KEY_RSHIFT);
    if (nt_input_key_is_pressed(NT_KEY_ARROW_UP)) {
        spawn_n_defold(shift ? BUNNY_BULK_ADD_BIG : BUNNY_BULK_ADD);
    }
    if (nt_input_key_is_pressed(NT_KEY_ARROW_DOWN)) {
        uint32_t n = shift ? BUNNY_BULK_ADD_BIG : BUNNY_BULK_ADD;
        if (n > s_bunny_count) {
            n = s_bunny_count;
        }
        for (uint32_t i = 0; i < n; i++) {
            s_bunny_count--;
            nt_entity_destroy(s_entities[s_bunny_count]);
            s_entities[s_bunny_count] = NT_ENTITY_INVALID;
        }
    }
    // #endregion

    // #region physics step
    const float dt = g_nt_app.dt;
    for (uint32_t i = 0; i < s_bunny_count; i++) {
        nt_bunny_step(&s_bunnies[i], dt);
        float *pos = nt_transform_comp_position(s_entities[i]);
        pos[0] = s_bunnies[i].x;
        pos[1] = s_bunnies[i].y;
        pos[2] = 0.0F;
        *nt_transform_comp_dirty(s_entities[i]) = true;
    }
    nt_transform_comp_update();
    // #endregion

    // #region frame uniforms (ortho VP via cglm)
    mat4 view_m;
    mat4 proj_m;
    mat4 vp;
    glm_mat4_identity(view_m);
    glm_ortho(0.0F, w, 0.0F, h, -1.0F, 1.0F, proj_m);
    glm_mat4_mul(proj_m, view_m, vp);

    nt_frame_uniforms_t uniforms = {0};
    memcpy(uniforms.view_proj, vp, 64);
    memcpy(uniforms.view, view_m, 64);
    memcpy(uniforms.proj, proj_m, 64);
    uniforms.time[0] = (float)nt_time_now();
    uniforms.time[1] = g_nt_app.dt;
    uniforms.resolution[0] = w;
    uniforms.resolution[1] = h;
    uniforms.resolution[2] = 1.0F / w;
    uniforms.resolution[3] = 1.0F / h;
    uniforms.near_far[0] = -1.0F;
    uniforms.near_far[1] = 1.0F;
    // #endregion

    /* ---- Render ---- */
    const nt_material_info_t *mat_info = nt_material_get_info(s_sprite_material);
    bool can_render = s_atlas_resolved && mat_info && mat_info->ready && s_bunny_count > 0;

    nt_gfx_begin_frame();
    /* nt_stats reads frame total via segment named "frame" by convention. */
    static nt_hash32_t s_frame_seg;
    if (s_frame_seg.value == 0) {
        s_frame_seg = nt_hash32_str("frame");
    }
    nt_gfx_begin_segment(s_frame_seg);

    if (g_nt_gfx.context_restored) {
        /* WebGL context loss recovery. mat_info / s_overlay_font handles
         * captured above are stale (nt_material_step / nt_font_step ran
         * before begin_frame detected the restore). Invalidate resources
         * so the next frame's *_step calls re-resolve, recreate game-owned
         * GPU buffers, and restore both renderers. Skip rendering this
         * frame — it's safer than driving pipelines with stale handles. */
        nt_resource_invalidate(NT_ASSET_SHADER_CODE);
        nt_resource_invalidate(NT_ASSET_TEXTURE);
        nt_resource_invalidate(NT_ASSET_FONT);
        nt_gfx_destroy_buffer(s_frame_ubo); /* free pool slot before reuse */
        s_frame_ubo = nt_gfx_make_buffer(&(nt_buffer_desc_t){
            .type = NT_BUFFER_UNIFORM,
            .usage = NT_USAGE_DYNAMIC,
            .size = sizeof(nt_frame_uniforms_t),
            .label = "frame_uniforms",
        });
        nt_sprite_renderer_restore_gpu();
        nt_text_renderer_restore_gpu();
        can_render = false;
    }

    nt_gfx_begin_pass(&(nt_pass_desc_t){.clear_color = {0.1F, 0.1F, 0.15F, 1.0F}, .clear_depth = 1.0F});

    if (can_render) {
        nt_gfx_update_buffer(s_frame_ubo, &uniforms, sizeof(uniforms));
        nt_gfx_bind_uniform_buffer(s_frame_ubo, 0);

        // #region build draw list
        for (uint32_t i = 0; i < s_bunny_count; i++) {
            s_items[i].sort_key = 0; /* unsorted in Bunnymark; renderer ignores */
            s_items[i].entity = s_entities[i].id;
            /* Coarse compatibility hint. The sprite renderer validates the
             * actual atlas page while emitting and splits draw commands when a
             * run crosses page textures, so the game no longer does per-bunny
             * atlas/resource lookups just to build this key. */
            s_items[i].batch_key = s_sprite_material.id;
        }
        nt_sprite_renderer_draw_list(s_items, s_bunny_count);
        // #endregion
    }

    // #region stats overlay (on-screen HUD)
    /* Top-left corner anchor in world coords (y-up, bottom-left origin).
     * Text renders below the model translation point by line; size is the
     * em-height in world units, which == pixels here since ortho is 1:1.
     * Skipped on context_restored frames — text material's resolved shader
     * handles are stale until next frame's nt_material_step / nt_font_step. */
    if (!g_nt_gfx.context_restored) {
        const float overlay_size = 22.0F;
        mat4 overlay_model;
        glm_mat4_identity(overlay_model);
        glm_translate(overlay_model, (vec3){10.0F, h - overlay_size - 4.0F, 0.0F});
        const float white[4] = {1.0F, 1.0F, 1.0F, 1.0F};

        char overlay[768];
        uint32_t written = nt_stats_format_lines(overlay, sizeof(overlay));
        if (written < sizeof(overlay)) {
            (void)snprintf(overlay + written, sizeof(overlay) - written,
                           "\nControls:\n"
                           "Click/tap: +%d\n"
                           "Hold: +%d/frame\n"
                           "Up/Down: +/-%d\n"
                           "Shift+Up/Down: +/-%d\n"
                           "H/top-right: SD/HD\n"
#ifndef NT_PLATFORM_WEB
                           "Esc: quit\n",
#else
                           "Esc: native quit only\n",
#endif
                           BUNNY_CLICK_SPAWN_COUNT, BUNNY_HOLD_SPAWN_RATE, BUNNY_BULK_ADD, BUNNY_BULK_ADD_BIG);
        }
        nt_text_renderer_set_material(s_text_material);
        nt_text_renderer_set_font(s_overlay_font);
        nt_text_renderer_draw(overlay, (const float *)overlay_model, overlay_size, white);
        nt_text_renderer_flush();
    }
    // #endregion

    nt_gfx_end_pass();
    nt_gfx_end_segment();
    nt_gfx_end_frame();

    nt_stats_count("bunnies", (uint64_t)s_bunny_count);
    nt_stats_count("atlas_quality", s_hd_active ? 1ULL : 0ULL);
    nt_stats_frame_end();

    nt_window_swap_buffers();
}

/* ---- Main ---- */

int main(void) {
    nt_engine_config_t config = {0};
    config.app_name = "bunnymark_demo";
    config.version = 1;

    nt_result_t result = nt_engine_init(&config);
    if (result != NT_OK) {
        return 1;
    }

    /* Pattern 7 init order: window → input → gfx + globals UBO register →
     * I/O (http/fs/hash/resource) → activator registrations → component
     * subsystems → renderer modules → mount packs → run frame loop. */
    g_nt_window.width = 800;
    g_nt_window.height = 600;
    nt_window_init();
    nt_input_init();

    nt_gfx_desc_t gfx_desc = nt_gfx_desc_defaults();
    nt_gfx_init(&gfx_desc);
    nt_gfx_register_global_block("Globals", 0);

    nt_http_init();
    nt_fs_init();
    nt_hash_init(&(nt_hash_desc_t){0});
    nt_resource_init(&(nt_resource_desc_t){0});

    /* Activators */
    nt_resource_set_activator(NT_ASSET_TEXTURE, nt_gfx_activate_texture, nt_gfx_deactivate_texture);
    nt_resource_set_activator(NT_ASSET_SHADER_CODE, nt_gfx_activate_shader, nt_gfx_deactivate_shader);
    nt_atlas_init();

    /* Components — capacity sized for full bunny pool. */
    nt_entity_init(&(nt_entity_desc_t){.max_entities = BUNNY_MAX + 8});
    nt_transform_comp_init(&(nt_transform_comp_desc_t){.capacity = BUNNY_MAX + 8});
    nt_material_comp_init(&(nt_material_comp_desc_t){.capacity = BUNNY_MAX + 8});
    nt_drawable_comp_init(&(nt_drawable_comp_desc_t){.capacity = BUNNY_MAX + 8});
    nt_sprite_comp_init(&(nt_sprite_comp_desc_t){.capacity = BUNNY_MAX + 8});

    nt_material_init(&(nt_material_desc_t){.max_materials = 4});
    nt_font_init(&(nt_font_desc_t){.max_fonts = 2});

    nt_sprite_renderer_desc_t sr_desc = nt_sprite_renderer_desc_defaults();
    nt_sprite_renderer_init(&sr_desc);
    nt_text_renderer_init();

    /* Console throughput log every 60 frames (FPS, CPU/GPU ms, draws, bunnies,
     * atlas quality) plus on-screen stats/controls overlay. */
    nt_stats_desc_t stats_desc = nt_stats_desc_defaults();
    nt_stats_init(&stats_desc);

    /* Frame rate cap removed: native engine loop runs uncapped (target_dt=0.0F).
     * dt-scaled physics already produces the same trajectories at any FPS. */
    g_nt_app.target_dt = 0.0F;

    /* Frame UBO */
    s_frame_ubo = nt_gfx_make_buffer(&(nt_buffer_desc_t){
        .type = NT_BUFFER_UNIFORM,
        .usage = NT_USAGE_DYNAMIC,
        .size = sizeof(nt_frame_uniforms_t),
        .label = "frame_uniforms",
    });

    /* Mount + load the SD pack. SD is the base layer (priority 100); HD will
     * stack on top at priority 200 when the user toggles it on — demo starts
     * in SD with HD lazy-mounted on first toggle. */
    s_pack_id = nt_hash32_str("bunnymark_sd");
    nt_resource_mount(s_pack_id, 100);
#ifdef NT_CDN_URL
    nt_resource_load_auto(s_pack_id, NT_CDN_URL "/bunnymark/bunnymark_sd.ntpack");
#else
    nt_resource_load_auto(s_pack_id, "assets/bunnymark_sd.ntpack");
#endif

    /* HD pack id is reserved up front; the actual mount/load happens lazily on
     * first toggle. */
    s_hd_pack_id = nt_hash32_str("bunnymark_hd");

    /* Resource handles */
    s_vs_handle = nt_resource_request(ASSET_SHADER_ASSETS_SHADERS_SPRITE_VERT, NT_ASSET_SHADER_CODE);
    s_fs_handle = nt_resource_request(ASSET_SHADER_ASSETS_SHADERS_SPRITE_FRAG, NT_ASSET_SHADER_CODE);
    s_atlas_handle = nt_resource_request(ASSET_ATLAS_BUNNIES, NT_ASSET_ATLAS);
    nt_resource_t atlas_tex_handle = nt_resource_request(ASSET_TEXTURE_BUNNIES_TEX0, NT_ASSET_TEXTURE);

    /* Material — premultiplied-alpha blend, depth off. */
    s_sprite_material = nt_material_create(&(nt_material_create_desc_t){
        .vs = s_vs_handle,
        .fs = s_fs_handle,
        .textures = {{.name = "u_texture", .resource = atlas_tex_handle}},
        .texture_count = 1,
        .blend_mode = NT_BLEND_MODE_ALPHA,
        .depth_test = false,
        .depth_write = false,
        .cull_mode = NT_CULL_NONE,
        .label = "sprite",
    });

    /* Stats overlay material (Slug shader) + Latin font for FPS / draws / bunnies HUD. */
    nt_resource_t slug_vs = nt_resource_request(ASSET_SHADER_ASSETS_SHADERS_SLUG_TEXT_VERT, NT_ASSET_SHADER_CODE);
    nt_resource_t slug_fs = nt_resource_request(ASSET_SHADER_ASSETS_SHADERS_SLUG_TEXT_FRAG, NT_ASSET_SHADER_CODE);
    s_text_material = nt_material_create(&(nt_material_create_desc_t){
        .vs = slug_vs,
        .fs = slug_fs,
        .blend_mode = NT_BLEND_MODE_ALPHA,
        .depth_test = false,
        .depth_write = false,
        .cull_mode = NT_CULL_NONE,
        .label = "stats_overlay",
    });
    s_overlay_font = nt_font_create(&(nt_font_create_desc_t){
        .curve_texture_width = 1024,
        .curve_texture_height = 512,
        .band_texture_height = 256,
        .band_count = 8,
    });
    nt_resource_t overlay_font_res = nt_resource_request(ASSET_FONT_BUNNYMARK_FONT_OVERLAY, NT_ASSET_FONT);
    nt_font_add(s_overlay_font, overlay_font_res);

    nt_resource_set_activate_time_budget(0);

#ifdef NT_PLATFORM_WEB
    nt_platform_web_loading_complete();
#endif

    /* Log test conditions at startup. Schema:
     *   "Bunnymark conditions: viewport=WxH sprite_size=~26x37 px blend=premultiplied atlas=SD|HD pages=N initial=I click=C hold_rate=R bunny_max=M hd_available=0|1 gpu=..."
     * GPU detection is browser/driver-side and the engine doesn't yet expose a
     * caps query — gpu=unknown until that ships (documented in README). */
    const char *atlas_q = s_hd_active ? "HD" : "SD";
    const char *blend_str = "premultiplied";
    const char *gpu_str = "unknown";
    nt_log_info("Bunnymark conditions: viewport=%ux%u sprite_size=~26x37 px blend=%s atlas=%s pages=1 initial=%d click=%d hold_rate=%d bunny_max=%u hd_available=%d gpu=%s", (unsigned)s_canvas_w(),
                (unsigned)s_canvas_h(), blend_str, atlas_q, BUNNY_INITIAL_COUNT, BUNNY_CLICK_SPAWN_COUNT, BUNNY_HOLD_SPAWN_RATE, (unsigned)BUNNY_MAX, s_hd_available ? 1 : 0, gpu_str);

    nt_app_run(frame);

#ifndef NT_PLATFORM_WEB
    nt_stats_shutdown();
    nt_text_renderer_shutdown();
    nt_font_destroy(s_overlay_font);
    nt_font_shutdown();
    nt_sprite_renderer_shutdown();
    nt_sprite_comp_shutdown();
    nt_drawable_comp_shutdown();
    nt_material_comp_shutdown();
    nt_transform_comp_shutdown();
    nt_entity_shutdown();
    nt_material_destroy(s_sprite_material);
    nt_material_destroy(s_text_material);
    nt_material_shutdown();
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
