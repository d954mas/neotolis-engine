/* tests/unit/test_nt_ui_walker_custom.c -- Plan 52-04
 *
 * Covers WALK-05 / D-52-09: CUSTOM command -> registered handler called
 * with (cmd, userdata); NULL handler is a silent skip.
 */

#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* clang-format off */
#include "atlas/nt_atlas.h"
#include "clay.h"
#include "core/nt_assert.h"
#include "font/nt_font.h"
#include "graphics/nt_gfx.h"
#include "hash/nt_hash.h"
#include "input/nt_input.h"
#include "material/nt_material.h"
#include "nt_pack_format.h"
#include "renderers/nt_sprite_renderer.h"
#include "renderers/nt_text_renderer.h"
#include "resource/nt_resource.h"
#include "test_helpers/nt_assert_trap.h"
#include "test_helpers/ui_atlas.h"
#include "ui/nt_ui.h"
#include "ui/nt_ui_internal.h"
#include "unity.h"
/* clang-format on */

static uint64_t s_arena[NT_UI_DEFAULT_ARENA_SIZE / 8u];
static minimal_ui_atlas_t s_atlas;
static nt_material_t s_sprite_material;
static nt_material_t s_text_material;
static nt_ui_context_t *s_ctx;
static uint32_t s_vpack_counter;

#define MAX_TEST_CMDS 4
static Clay_RenderCommand s_test_cmds[MAX_TEST_CMDS];

/* Custom-handler observers. */
static int s_custom_calls;
static const void *s_custom_received_cmd;
static void *s_custom_received_user;

static void test_custom_handler(const void *clay_cmd, void *userdata) {
    s_custom_calls++;
    s_custom_received_cmd = clay_cmd;
    s_custom_received_user = userdata;
}

static nt_material_t make_test_material(void) {
    nt_shader_t vs = nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_VERTEX, .source = "void main(){}", .label = "walker_vs"});
    nt_shader_t fs = nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_FRAGMENT, .source = "void main(){}", .label = "walker_fs"});

    char pack_name[64];
    char vs_name[64];
    char fs_name[64];
    (void)snprintf(pack_name, sizeof pack_name, "walker_mat_pack_%u", s_vpack_counter);
    (void)snprintf(vs_name, sizeof vs_name, "walker_vs_%u", s_vpack_counter);
    (void)snprintf(fs_name, sizeof fs_name, "walker_fs_%u", s_vpack_counter);
    s_vpack_counter++;

    nt_hash32_t pid = nt_hash32_str(pack_name);
    nt_hash64_t vs_rid = nt_hash64_str(vs_name);
    nt_hash64_t fs_rid = nt_hash64_str(fs_name);

    TEST_ASSERT_EQUAL(NT_OK, nt_resource_create_pack(pid, 0));
    TEST_ASSERT_EQUAL(NT_OK, nt_resource_register(pid, vs_rid, NT_ASSET_SHADER_CODE, vs.id));
    TEST_ASSERT_EQUAL(NT_OK, nt_resource_register(pid, fs_rid, NT_ASSET_SHADER_CODE, fs.id));

    nt_resource_t vs_res = nt_resource_request(vs_rid, NT_ASSET_SHADER_CODE);
    nt_resource_t fs_res = nt_resource_request(fs_rid, NT_ASSET_SHADER_CODE);
    nt_resource_step();

    nt_material_create_desc_t desc;
    memset(&desc, 0, sizeof desc);
    desc.vs = vs_res;
    desc.fs = fs_res;
    desc.depth_test = false;
    desc.depth_write = false;
    desc.cull_mode = NT_CULL_NONE;
    desc.color_mode = NT_COLOR_MODE_NONE;
    desc.label = "walker_test_material";

    nt_material_t mat = nt_material_create(&desc);
    nt_material_step();
    return mat;
}

void setUp(void) {
    nt_test_assert_install();
    s_vpack_counter = 0;
    s_custom_calls = 0;
    s_custom_received_cmd = NULL;
    s_custom_received_user = NULL;
    memset(s_test_cmds, 0, sizeof s_test_cmds);

    nt_hash_init(&(nt_hash_desc_t){0});
    nt_gfx_init(&(nt_gfx_desc_t){.max_shaders = 32, .max_pipelines = 16, .max_buffers = 64, .max_textures = 32, .max_meshes = 16});
    nt_resource_init(&(nt_resource_desc_t){0});
    nt_atlas_init();
    nt_font_init(&(nt_font_desc_t){.max_fonts = 4});
    nt_material_init(&(nt_material_desc_t){.max_materials = 32});

    nt_gfx_begin_frame();
    nt_gfx_begin_pass(&(nt_pass_desc_t){.clear_depth = 1.0F});

    nt_sprite_renderer_init(&(nt_sprite_renderer_desc_t){.max_pipelines = 4});
    nt_text_renderer_init();

    s_atlas = minimal_ui_atlas_create();
    s_sprite_material = make_test_material();
    s_text_material = make_test_material();

    nt_ui_set_atlas_white_region(s_atlas.handle, s_atlas.white_region_idx);
    nt_ui_set_sprite_material(s_sprite_material);
    nt_ui_set_text_material(s_text_material);
    nt_ui_set_custom_handler(NULL, NULL);

    s_ctx = nt_ui_create_context(s_arena, sizeof s_arena);
    TEST_ASSERT_NOT_NULL(s_ctx);
}

void tearDown(void) {
    if (s_ctx != NULL) {
        nt_ui_destroy_context(s_ctx);
        s_ctx = NULL;
    }
    nt_ui_test_reset_walker_globals();
    minimal_ui_atlas_destroy(&s_atlas);
    nt_sprite_renderer_shutdown();
    nt_text_renderer_shutdown();
    nt_gfx_end_pass();
    nt_gfx_end_frame();
    nt_material_shutdown();
    nt_font_shutdown();
    nt_atlas_test_reset();
    nt_resource_shutdown();
    nt_gfx_shutdown();
    nt_hash_shutdown();
}

static void inject_frozen_cmds(int32_t count) {
    s_ctx->frozen_cmds.internalArray = s_test_cmds;
    s_ctx->frozen_cmds.length = count;
    s_ctx->frozen_cmds.capacity = MAX_TEST_CMDS;
}

/* WALK-05: registered handler is called with (clay_cmd, userdata). */
static void test_custom_handler_invoked(void) {
    int sentinel = 42;
    nt_ui_set_custom_handler(test_custom_handler, &sentinel);

    Clay_RenderCommand *c = &s_test_cmds[0];
    c->commandType = CLAY_RENDER_COMMAND_TYPE_CUSTOM;
    c->boundingBox = (Clay_BoundingBox){.x = 5, .y = 5, .width = 50, .height = 50};
    c->renderData.custom.backgroundColor = (Clay_Color){0};
    c->renderData.custom.customData = NULL;
    inject_frozen_cmds(1);

    nt_ui_target_t target = {.viewport = {0, 0, 800, 600}};
    nt_ui_walk(s_ctx, &target);

    TEST_ASSERT_EQUAL_INT(1, s_custom_calls);
    /* D-52-09 Option A: handler receives clay_cmd as const void * (opaque),
     * which is the same pointer that's in our cmds array slot 0. */
    TEST_ASSERT_EQUAL_PTR(c, s_custom_received_cmd);
    TEST_ASSERT_EQUAL_PTR(&sentinel, s_custom_received_user);
}

/* WALK-05 / D-52-09: NULL handler = silent skip (no crash, no warning). */
static void test_null_custom_handler_silent_skip(void) {
    nt_ui_set_custom_handler(NULL, NULL);

    Clay_RenderCommand *c = &s_test_cmds[0];
    c->commandType = CLAY_RENDER_COMMAND_TYPE_CUSTOM;
    c->boundingBox = (Clay_BoundingBox){.x = 0, .y = 0, .width = 10, .height = 10};
    inject_frozen_cmds(1);

    nt_ui_target_t target = {.viewport = {0, 0, 800, 600}};
    /* Must NOT crash. */
    nt_ui_walk(s_ctx, &target);

    TEST_ASSERT_EQUAL_INT(0, s_custom_calls);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_custom_handler_invoked);
    RUN_TEST(test_null_custom_handler_silent_skip);
    return UNITY_END();
}
