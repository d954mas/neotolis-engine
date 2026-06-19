/* Phase 66 — walker → custom-material binding for radial emits (INT-66-06).
 *
 * The phase's one genuine open integration risk: how a custom-material radial
 * emit reaches the UI walker. The walker binds ONE ctx->sprite_material per pass
 * and (before this plan) had no per-element material override on the image path.
 *
 * Route A (prototype): a CUSTOM-command handler binds the radial material and
 * runs the plan-01 custom-attr emit. It proves the renderer hook end-to-end
 * through the walker, but CUSTOM is a hard barrier (is_segmentable false) so it
 * flushes per radial — it does NOT meet the D-66-07 thousands-in-one-draw scale
 * target. Superseded by Route B below.
 *
 * Route B (shipped): an optional material handle on nt_ui_image_payload_t; the
 * walker image dispatch binds it via set_material (auto-flush on .id change) when
 * it differs from the bound base. N radials sharing ONE radial material batch to
 * one flush + one draw (D-66-07).
 *
 * The widget math/ABI for nt_ui_radial / nt_ui_radial_image lands in 66-03/04. */

#include <stdalign.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "atlas/nt_atlas.h"
#include "clay.h"
#include "graphics/nt_gfx.h"
#include "hash/nt_hash.h"
#include "material/nt_material.h"
#include "math/nt_math.h"
#include "nt_pack_format.h"
#include "renderers/nt_sprite_renderer.h"
#include "resource/nt_resource.h"
#include "test_helpers/ui_walker_fixture.h"
#include "ui/nt_ui.h"
#include "ui/nt_ui_internal.h"
#include "unity.h"

alignas(NT_UI_ARENA_ALIGN) static uint8_t s_arena[NT_UI_TEST_ARENA_SIZE];
static ui_walker_fixture_t s_fx;

#define MAX_TEST_CMDS 64
static Clay_RenderCommand s_test_cmds[MAX_TEST_CMDS];

/* One FLOAT4 a_radial block (matches the plan-01 v1 payload). */
static const float k_radial_attrs[4] = {0.25F, 1.75F, 0.5F, 1.0F};

static uint32_t s_vpack_counter;

/* Radial material: shares the fixture's vs/fs role but declares a_radial @ loc 4
 * so build_sprite_layout produces the extended 36 B stride (distinct pipeline). */
static nt_material_t make_radial_material(void) {
    nt_shader_t vs = nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_VERTEX, .source = "void main(){}", .label = "radial_vs"});
    nt_shader_t fs = nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_FRAGMENT, .source = "void main(){}", .label = "radial_fs"});

    char pack_name[64];
    char vs_name[64];
    char fs_name[64];
    (void)snprintf(pack_name, sizeof pack_name, "radial_mat_pack_%u", s_vpack_counter);
    (void)snprintf(vs_name, sizeof vs_name, "radial_test_vs_%u", s_vpack_counter);
    (void)snprintf(fs_name, sizeof fs_name, "radial_test_fs_%u", s_vpack_counter);
    s_vpack_counter++;

    const nt_hash32_t pid = nt_hash32_str(pack_name);
    const nt_hash64_t vs_rid = nt_hash64_str(vs_name);
    const nt_hash64_t fs_rid = nt_hash64_str(fs_name);

    TEST_ASSERT_EQUAL(NT_OK, nt_resource_create_pack(pid, 0));
    TEST_ASSERT_EQUAL(NT_OK, nt_resource_register(pid, vs_rid, NT_ASSET_SHADER_CODE, vs.id));
    TEST_ASSERT_EQUAL(NT_OK, nt_resource_register(pid, fs_rid, NT_ASSET_SHADER_CODE, fs.id));
    const nt_resource_t vs_res = nt_resource_request(vs_rid, NT_ASSET_SHADER_CODE);
    const nt_resource_t fs_res = nt_resource_request(fs_rid, NT_ASSET_SHADER_CODE);
    nt_resource_step();

    nt_material_create_desc_t desc;
    memset(&desc, 0, sizeof desc);
    desc.vs = vs_res;
    desc.fs = fs_res;
    desc.depth_test = false;
    desc.depth_write = false;
    desc.cull_mode = NT_CULL_NONE;
    desc.color_mode = NT_COLOR_MODE_NONE;
    desc.attr_map[0].stream_name = "a_radial";
    desc.attr_map[0].location = 4;
    desc.attr_map_count = 1;
    desc.label = "radial_test_material";

    const nt_material_t mat = nt_material_create(&desc);
    nt_material_step();
    return mat;
}

void setUp(void) {
    s_vpack_counter = 0;
    memset(s_test_cmds, 0, sizeof s_test_cmds);
    ui_walker_fixture_init(&s_fx, s_arena, sizeof s_arena, UI_WALKER_FX_BIND_ALL);
}

void tearDown(void) { ui_walker_fixture_shutdown(&s_fx); }

static void inject_frozen_cmds(int32_t count) {
    s_fx.ctx->frozen_cmds.internalArray = s_test_cmds;
    s_fx.ctx->frozen_cmds.length = count;
    s_fx.ctx->frozen_cmds.capacity = MAX_TEST_CMDS;
}

/* ---- Route A: CUSTOM handler binds the radial material + emits ---- */

typedef struct {
    nt_resource_t atlas;
    uint32_t region_index;
    nt_material_t radial_mat;
    int calls;
} route_a_ctx_t;

static void route_a_handler(const nt_ui_custom_frame_t *frame, void *userdata) {
    (void)frame;
    route_a_ctx_t *rc = (route_a_ctx_t *)userdata;
    rc->calls++;

    /* Bind the radial material (different fs + extended layout than the base
     * ctx->sprite_material), set the per-widget block, emit a quad. */
    nt_sprite_renderer_set_material(rc->radial_mat);
    nt_sprite_renderer_set_custom_attrs(k_radial_attrs, (uint8_t)sizeof k_radial_attrs);

    const float positions[4][2] = {{0.0F, 0.0F}, {1.0F, 0.0F}, {1.0F, 1.0F}, {0.0F, 1.0F}};
    const uint16_t idx[6] = {0, 1, 2, 0, 2, 3};
    nt_sprite_renderer_emit_geometry(rc->atlas, rc->region_index, positions, 4, idx, 6, NT_MATH_MAT4_IDENTITY, 0xFFFFFFFFU);
}

/* Route A proves the renderer hook end-to-end through the walker: a radial
 * CUSTOM element binds the radial material and produces one extended-stride emit
 * with the per-vertex a_radial block baked in. */
static void test_route_a_custom_binds_radial_material(void) {
    route_a_ctx_t rc = {.atlas = s_fx.atlas.handle, .region_index = s_fx.atlas.white_region_idx, .radial_mat = make_radial_material(), .calls = 0};
    nt_ui_set_custom_handler(s_fx.ctx, route_a_handler, &rc);

    static nt_ui_custom_data_t cd;
    cd = (nt_ui_custom_data_t){.type = NT_UI_CUSTOM_TYPE_GAME, .data = NULL};
    Clay_RenderCommand *c = &s_test_cmds[0];
    c->commandType = CLAY_RENDER_COMMAND_TYPE_CUSTOM;
    c->boundingBox = (Clay_BoundingBox){.x = 10, .y = 10, .width = 32, .height = 32};
    c->renderData.custom.customData = &cd;
    inject_frozen_cmds(1);

    nt_ui_target_t target = {.viewport = {0, 0, 800, 600}};
    nt_ui_walk(s_fx.ctx, &target);

    TEST_ASSERT_EQUAL_INT(1, rc.calls);

    /* Extended-stride emit occurred: the a_radial block is baked into every
     * vertex (renderer-hook end-to-end proof). */
    TEST_ASSERT_EQUAL_UINT32(4U, nt_sprite_renderer_test_last_emit_vertex_count());
    for (uint32_t v = 0; v < 4U; v++) {
        float out[4] = {0};
        nt_sprite_renderer_test_last_emit_radial(v, out, 4);
        for (uint8_t k = 0; k < 4; k++) {
            TEST_ASSERT_TRUE_MESSAGE(out[k] == k_radial_attrs[k], "Route A: a_radial block not baked per-vertex");
        }
    }
}

/* Route A is a HARD BARRIER: CUSTOM is not segmentable, so emit_custom flushes
 * before AND the radial emit's own boundary flushes — N radials via CUSTOM scale
 * the draw-call count linearly. This is the documented prototype-only limitation
 * that fails D-66-07; contrast test_route_b_*_batches below. */
static void test_route_a_custom_does_not_batch(void) {
    route_a_ctx_t rc = {.atlas = s_fx.atlas.handle, .region_index = s_fx.atlas.white_region_idx, .radial_mat = make_radial_material(), .calls = 0};
    nt_ui_set_custom_handler(s_fx.ctx, route_a_handler, &rc);

    static nt_ui_custom_data_t cd[4];
    const int n = 4;
    for (int i = 0; i < n; i++) {
        cd[i] = (nt_ui_custom_data_t){.type = NT_UI_CUSTOM_TYPE_GAME, .data = NULL};
        Clay_RenderCommand *c = &s_test_cmds[i];
        c->commandType = CLAY_RENDER_COMMAND_TYPE_CUSTOM;
        c->boundingBox = (Clay_BoundingBox){.x = (float)(i * 40), .y = 10, .width = 32, .height = 32};
        c->renderData.custom.customData = &cd[i];
    }
    inject_frozen_cmds(n);

    nt_ui_target_t target = {.viewport = {0, 0, 800, 600}};
    nt_ui_walk(s_fx.ctx, &target);

    TEST_ASSERT_EQUAL_INT(n, rc.calls);
    /* Each CUSTOM is its own draw — linear in N, NOT batched (Pitfall 3). */
    TEST_ASSERT_EQUAL_UINT32((uint32_t)n, nt_ui_get_last_walk_draw_calls(s_fx.ctx));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_route_a_custom_binds_radial_material);
    RUN_TEST(test_route_a_custom_does_not_batch);
    return UNITY_END();
}
