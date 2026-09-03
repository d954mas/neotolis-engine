#include "test_helpers/ui_walker_fixture.h"
#include "test_helpers/nt_gfx_fake.h"

/* Empty TU when NT_TEST_ACCESS undefined (helper compiled into non-UI binaries). */
#ifdef NT_TEST_ACCESS

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "atlas/nt_atlas.h"
#include "clay.h"
#include "core/nt_assert.h"
#include "font/nt_font.h"
#include "graphics/nt_gfx.h"
#include "hash/nt_hash.h"
#include "material/nt_material.h"
#include "memory/nt_mem_scratch.h"
#include "nt_pack_format.h"
#include "renderers/nt_sprite_renderer.h"
#include "renderers/nt_text_renderer.h"
#include "resource/nt_resource.h"
#include "ui/nt_ui_internal.h"
#include "unity.h"

static nt_material_t make_material(bool with_page_sampler) {
    nt_material_create_desc_t desc;
    memset(&desc, 0, sizeof desc);
    desc.program = with_page_sampler ? nt_gfx_fake_make_program((const char *const[]){"u_texture"}, 1) : nt_gfx_fake_make_program((const char *const[]){"u_curve_texture", "u_band_texture"}, 2);
    /* Programs the engine links later (shape renderer) must not inherit this template. */
    nt_gfx_fake_set_samplers(NULL, 0);
    desc.depth_test = false;
    desc.depth_write = false;
    desc.cull_mode = NT_CULL_NONE;
    desc.color_mode = NT_COLOR_MODE_NONE;
    /* Sprite materials name the atlas page's sampler; text materials declare nothing --
     * the font textures are the text renderer's own binds. */
    if (with_page_sampler) {
        desc.textures[0].name = "u_texture";
        desc.texture_count = 1;
    }
    desc.label = "walker_test_material";

    const nt_material_t mat = nt_material_create(&desc);
    nt_material_step();
    return mat;
}

void ui_walker_fixture_init(ui_walker_fixture_t *fx, void *arena, size_t arena_size, ui_walker_fx_bind_t bind) {
    NT_ASSERT(fx != NULL);
    NT_ASSERT(arena != NULL);
    memset(fx, 0, sizeof *fx);

    nt_hash_init(&(nt_hash_desc_t){0});
    nt_mem_scratch_init((size_t)64U * 1024U); /* NT_UI_DATA_LAYER / NT_UI_DATA_FULL allocate here. */
    nt_gfx_init(
        &(nt_gfx_desc_t){.max_shaders = 32, .max_programs = 16, .max_pipelines = 16, .max_buffers = 64, .max_textures = 32, .max_meshes = 16, .max_vertex_inputs = 16, .max_render_targets = 16});
    nt_resource_init(&(nt_resource_desc_t){0});
    nt_atlas_init();
    nt_font_init(&(nt_font_desc_t){.max_fonts = 16}); /* rich multi-face tests create >4 distinct stub fonts */
    nt_material_init(&(nt_material_desc_t){.max_materials = 32});

    /* Open a frame/pass so sprite/text renderers can draw_indexed without
     * tripping the stub gfx backend's "no active pass" guard (mirrors the
     * test_nt_sprite_renderer setUp). */
    nt_gfx_begin_frame();
    nt_gfx_begin_pass(&(nt_pass_desc_t){.clear_depth = 1.0F});

    nt_sprite_renderer_init(&(nt_sprite_renderer_desc_t){.max_pipelines = 4});
    nt_text_renderer_init();
    nt_ui_module_init();

    /* nt_debug_overlay is NOT init'd here -- nt_ui_walk does not depend on it.
     * Tests that need nt_debug_overlay (e.g. test_nt_ui_stats verifying the
     * metrics-bridge pattern) init/shutdown it themselves around the
     * fixture calls. */

    fx->atlas = minimal_ui_atlas_create();
    fx->sprite_material = make_material(true);
    fx->text_material = make_material(false);

    /* Stub font: valid pool slot, no resource attached. nt_font_valid() is
     * true so walker's contract assert passes, but units_per_em stays 0 so
     * nt_text_renderer_draw_n early-returns before any glyph work. */
    fx->stub_font = nt_font_create(&(nt_font_create_desc_t){
        .curve_texture_width = 64,
        .curve_texture_height = 64,
        .band_texture_height = 16,
        .band_count = 4,
        .measure_cache_size = 0,
    });

    const nt_ui_create_desc_t desc = nt_ui_create_desc_defaults();
    fx->ctx = nt_ui_create_context(arena, arena_size, &desc);
    TEST_ASSERT_NOT_NULL(fx->ctx);
    nt_ui_set_font(fx->ctx, 0U, fx->stub_font);

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
        /* If a Unity TEST_ASSERT longjmp'd out of a frame, the ctx is mid-frame
         * and destroy_context would trap. Close the frame defensively so tearDown
         * surfaces the real failure (Unity output) instead of a SIGILL on cleanup. */
        if (fx->ctx->in_frame) {
            fx->ctx->in_frame = false;
            Clay_SetCurrentContext(NULL);
        }
        nt_ui_destroy_context(fx->ctx);
        fx->ctx = NULL;
    }
    if (nt_font_valid(fx->stub_font)) {
        nt_font_destroy(fx->stub_font);
    }
    minimal_ui_atlas_destroy(&fx->atlas);

    nt_ui_module_shutdown();
    nt_sprite_renderer_shutdown();
    nt_text_renderer_shutdown();
    nt_gfx_end_pass();
    nt_gfx_end_frame();

    nt_material_shutdown();
    nt_font_shutdown();
    nt_atlas_test_reset();
    nt_resource_shutdown();
    nt_gfx_shutdown();
    nt_mem_scratch_shutdown();
    nt_hash_shutdown();
}

#endif /* NT_TEST_ACCESS */
