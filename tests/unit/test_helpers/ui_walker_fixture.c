#include "test_helpers/ui_walker_fixture.h"

/* The fixture body is only compiled into test binaries that opt into
 * nt_ui (NT_UI_TEST_ACCESS). Other test binaries -- notably
 * test_nt_sprite_renderer_emit_region, which lives in the same helper
 * sources list but does NOT link nt_ui -- get an empty TU and avoid
 * the unresolved nt_ui_create_context / nt_ui_set_* symbols. */
#ifdef NT_UI_TEST_ACCESS

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "atlas/nt_atlas.h"
#include "core/nt_assert.h"
#include "font/nt_font.h"
#include "graphics/nt_gfx.h"
#include "hash/nt_hash.h"
#include "material/nt_material.h"
#include "nt_pack_format.h"
#include "renderers/nt_sprite_renderer.h"
#include "renderers/nt_text_renderer.h"
#include "resource/nt_resource.h"
#include "unity.h"

/* Per-binary counter so multiple ui_walker_fixture_make_material() calls
 * inside the same test process do not collide on virtual-pack ids. */
static uint32_t s_vpack_counter;

nt_material_t ui_walker_fixture_make_material(void) {
    nt_shader_t vs = nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_VERTEX, .source = "void main(){}", .label = "walker_vs"});
    nt_shader_t fs = nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_FRAGMENT, .source = "void main(){}", .label = "walker_fs"});

    char pack_name[64];
    char vs_name[64];
    char fs_name[64];
    (void)snprintf(pack_name, sizeof pack_name, "walker_mat_pack_%u", s_vpack_counter);
    (void)snprintf(vs_name, sizeof vs_name, "walker_vs_%u", s_vpack_counter);
    (void)snprintf(fs_name, sizeof fs_name, "walker_fs_%u", s_vpack_counter);
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
    desc.label = "walker_test_material";

    const nt_material_t mat = nt_material_create(&desc);
    nt_material_step();
    return mat;
}

void ui_walker_fixture_init(ui_walker_fixture_t *fx, void *arena, size_t arena_size, ui_walker_fx_bind_t bind) {
    NT_ASSERT(fx != NULL);
    NT_ASSERT(arena != NULL);
    memset(fx, 0, sizeof *fx);
    s_vpack_counter = 0;

    nt_hash_init(&(nt_hash_desc_t){0});
    nt_gfx_init(&(nt_gfx_desc_t){.max_shaders = 32, .max_pipelines = 16, .max_buffers = 64, .max_textures = 32, .max_meshes = 16});
    nt_resource_init(&(nt_resource_desc_t){0});
    nt_atlas_init();
    nt_font_init(&(nt_font_desc_t){.max_fonts = 4});
    nt_material_init(&(nt_material_desc_t){.max_materials = 32});

    /* Open a frame/pass so sprite/text renderers can draw_indexed without
     * tripping the stub gfx backend's "no active pass" guard (mirrors the
     * test_nt_sprite_renderer setUp). */
    nt_gfx_begin_frame();
    nt_gfx_begin_pass(&(nt_pass_desc_t){.clear_depth = 1.0F});

    nt_sprite_renderer_init(&(nt_sprite_renderer_desc_t){.max_pipelines = 4});
    nt_text_renderer_init();

    /* nt_stats is NOT init'd here -- nt_ui_walk does not depend on it.
     * Tests that need nt_stats (e.g. test_nt_ui_stats verifying the
     * metrics-bridge pattern) init/shutdown it themselves around the
     * fixture calls. */

    fx->atlas = minimal_ui_atlas_create();
    fx->sprite_material = ui_walker_fixture_make_material();
    fx->text_material = ui_walker_fixture_make_material();

    fx->ctx = nt_ui_create_context(arena, arena_size);
    TEST_ASSERT_NOT_NULL(fx->ctx);

    if ((bind & UI_WALKER_FX_BIND_ATLAS) != 0U) {
        nt_ui_set_atlas_white_region(fx->ctx, fx->atlas.handle, fx->atlas.white_region_idx);
    }
    if ((bind & UI_WALKER_FX_BIND_SPRITE_MATERIAL) != 0U) {
        nt_ui_set_sprite_material(fx->ctx, fx->sprite_material);
    }
    if ((bind & UI_WALKER_FX_BIND_TEXT_MATERIAL) != 0U) {
        nt_ui_set_text_material(fx->ctx, fx->text_material);
    }
    nt_ui_set_custom_handler(fx->ctx, NULL, NULL);
}

void ui_walker_fixture_shutdown(ui_walker_fixture_t *fx) {
    NT_ASSERT(fx != NULL);
    if (fx->ctx != NULL) {
        nt_ui_destroy_context(fx->ctx);
        fx->ctx = NULL;
    }
    minimal_ui_atlas_destroy(&fx->atlas);

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

#endif /* NT_UI_TEST_ACCESS */
