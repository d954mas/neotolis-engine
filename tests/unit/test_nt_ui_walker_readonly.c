/* tests/unit/test_nt_ui_walker_readonly.c -- Plan 52-04
 *
 * Covers UI-06 (two walks against same ctx+target produce identical state)
 * and UI-07 (walker entry applies target->viewport via nt_gfx_set_viewport).
 * Three death-tests for pre-walk asserts (D-52-06 atlas, D-52-19 sprite +
 * text material). All death-tests use NT_TEST_EXPECT_ASSERT (Revision
 * Issue 3) -- no TEST_IGNORE fallback.
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

#define MAX_TEST_CMDS 8
static Clay_RenderCommand s_test_cmds[MAX_TEST_CMDS];

/* setUp variants -- the death-tests need a partially-initialized walker
 * (some setters omitted). The flag controls which setters setUp runs. */
typedef enum {
    SETUP_FULL = 0,
    SETUP_NO_ATLAS,
    SETUP_NO_SPRITE_MATERIAL,
    SETUP_NO_TEXT_MATERIAL,
} setup_mode_t;
static setup_mode_t s_setup_mode = SETUP_FULL;

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

    /* Per-test walker setter selection. Clears first so death-tests start
     * from a known-empty state. */
    nt_ui_test_reset_walker_globals();
    if (s_setup_mode != SETUP_NO_ATLAS) {
        nt_ui_set_atlas_white_region(s_atlas.handle, s_atlas.white_region_idx);
    }
    if (s_setup_mode != SETUP_NO_SPRITE_MATERIAL) {
        nt_ui_set_sprite_material(s_sprite_material);
    }
    if (s_setup_mode != SETUP_NO_TEXT_MATERIAL) {
        nt_ui_set_text_material(s_text_material);
    }
    nt_ui_set_custom_handler(NULL, NULL);

    s_ctx = nt_ui_create_context(s_arena, sizeof s_arena);
    TEST_ASSERT_NOT_NULL(s_ctx);

    /* Reset mode to FULL for the next test -- per-test setup applies a
     * sentinel BEFORE Unity runs setUp via the run_with_mode wrappers below. */
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
    s_setup_mode = SETUP_FULL;
}

static void inject_frozen_cmds(int32_t count) {
    s_ctx->frozen_cmds.internalArray = s_test_cmds;
    s_ctx->frozen_cmds.length = count;
    s_ctx->frozen_cmds.capacity = MAX_TEST_CMDS;
}

/* UI-06: two walks against same ctx+target produce identical probe state. */
static void test_second_walk_identical(void) {
    Clay_RenderCommand *c = &s_test_cmds[0];
    c->commandType = CLAY_RENDER_COMMAND_TYPE_RECTANGLE;
    c->boundingBox = (Clay_BoundingBox){.x = 10, .y = 20, .width = 30, .height = 40};
    c->renderData.rectangle.backgroundColor = (Clay_Color){.r = 200, .g = 200, .b = 200, .a = 255};
    inject_frozen_cmds(1);

    nt_ui_target_t target = {.viewport = {0, 0, 800, 600}};
    nt_ui_walk(s_ctx, &target);

    int vp1[4];
    nt_gfx_test_viewport_rect(vp1);
    const uint32_t elements1 = nt_ui_test_last_walk_element_count();
    /* Read but don't compare draw-call delta -- walking once already
     * incurs draw calls; a second walk will too, so the EXACT delta-
     * to-delta count is what we compare. */
    const uint32_t delta1 = nt_ui_test_last_walk_draw_call_delta();

    nt_ui_walk(s_ctx, &target);

    int vp2[4];
    nt_gfx_test_viewport_rect(vp2);
    const uint32_t elements2 = nt_ui_test_last_walk_element_count();
    const uint32_t delta2 = nt_ui_test_last_walk_draw_call_delta();

    TEST_ASSERT_EQUAL_INT_ARRAY(vp1, vp2, 4);
    TEST_ASSERT_EQUAL_UINT32(elements1, elements2);
    TEST_ASSERT_EQUAL_UINT32(delta1, delta2);
}

/* UI-07: walker entry applies target->viewport via nt_gfx_set_viewport. */
static void test_viewport_applied(void) {
    inject_frozen_cmds(0);

    nt_ui_target_t target = {.viewport = {100.0f, 200.0f, 640.0f, 480.0f}};
    nt_ui_walk(s_ctx, &target);

    int vp[4];
    nt_gfx_test_viewport_rect(vp);
    TEST_ASSERT_EQUAL_INT(100, vp[0]);
    TEST_ASSERT_EQUAL_INT(200, vp[1]);
    TEST_ASSERT_EQUAL_INT(640, vp[2]);
    TEST_ASSERT_EQUAL_INT(480, vp[3]);
}

/* D-52-06 death-test: walk without atlas set asserts. */
static void test_walk_without_atlas_asserts(void) {
    /* Mode flag was set before Unity invoked setUp -- the atlas setter
     * was skipped, so g_nt_ui_atlas.id == 0. */
    inject_frozen_cmds(0);
    nt_ui_target_t target = {.viewport = {0, 0, 800, 600}};
    NT_TEST_EXPECT_ASSERT(nt_ui_walk(s_ctx, &target));
}

/* D-52-19 / Revision Issue 1 death-test: walk without sprite material asserts. */
static void test_walk_without_sprite_material_asserts(void) {
    inject_frozen_cmds(0);
    nt_ui_target_t target = {.viewport = {0, 0, 800, 600}};
    NT_TEST_EXPECT_ASSERT(nt_ui_walk(s_ctx, &target));
}

/* D-52-19 death-test: walk without text material asserts. */
static void test_walk_without_text_material_asserts(void) {
    inject_frozen_cmds(0);
    nt_ui_target_t target = {.viewport = {0, 0, 800, 600}};
    NT_TEST_EXPECT_ASSERT(nt_ui_walk(s_ctx, &target));
}

/* Macro to set s_setup_mode BEFORE RUN_TEST invokes setUp -- Unity runs
 * setUp first, then the test function, so the mode flag must be written
 * by the caller of RUN_TEST, not from inside the test body. */
#define RUN_TEST_WITH_MODE(mode, fn)                                                                                                                                                                   \
    do {                                                                                                                                                                                               \
        s_setup_mode = (mode);                                                                                                                                                                         \
        RUN_TEST(fn);                                                                                                                                                                                  \
    } while (0)

int main(void) {
    UNITY_BEGIN();
    s_setup_mode = SETUP_FULL;
    RUN_TEST(test_second_walk_identical);
    s_setup_mode = SETUP_FULL;
    RUN_TEST(test_viewport_applied);
    /* Death-tests last so a partial-init setUp doesn't leak into the
     * happy-path runs above. tearDown resets s_setup_mode to FULL each
     * time, so each death-test re-arms its own mode here. */
    RUN_TEST_WITH_MODE(SETUP_NO_ATLAS, test_walk_without_atlas_asserts);
    RUN_TEST_WITH_MODE(SETUP_NO_SPRITE_MATERIAL, test_walk_without_sprite_material_asserts);
    RUN_TEST_WITH_MODE(SETUP_NO_TEXT_MATERIAL, test_walk_without_text_material_asserts);
    return UNITY_END();
}
