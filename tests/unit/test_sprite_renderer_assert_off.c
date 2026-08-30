/* Drives the sprite renderer's pipeline-cache capacity guard with
 * NT_ASSERT_MODE=0, the one configuration where the assert above it vanishes.
 * Nothing else exercises that guard: no build sets OFF, so without this the
 * branch is unreachable code. OFF is not a supported runtime mode; this only
 * pins that the guard does not depend on an assert to hold the bound.
 *
 * max_pipelines is the HARDCAP on purpose. entries[] is sized to the hardcap,
 * so any smaller cap lands the unguarded write inside the array and would prove
 * nothing about memory safety -- only at the hardcap does the next entry run
 * off the end. */
#undef NT_ASSERT_MODE
#define NT_ASSERT_MODE 0

#include <stdint.h>

#include "graphics/nt_gfx.h"
#include "hash/nt_hash.h"
#include "material/nt_material.h"
#include "renderers/nt_sprite_renderer.h"
#include "resource/nt_resource.h"
#include "unity.h"

#define CAP NT_SPRITE_RENDERER_MAX_PIPELINES_HARDCAP

void setUp(void) {
    nt_hash_init(&(nt_hash_desc_t){0});
    /* Two stages plus one program per material, one material per cache entry,
     * and one more of each to drive the overflow. */
    nt_gfx_init(&(nt_gfx_desc_t){.max_shaders = (uint16_t)((CAP + 1) * 2),
                                 .max_programs = (uint16_t)(CAP + 1),
                                 .max_pipelines = (uint16_t)(CAP + 1),
                                 .max_buffers = 64,
                                 .max_textures = 32,
                                 .max_meshes = 16,
                                 .max_render_targets = 16});
    nt_resource_init(&(nt_resource_desc_t){0});
    nt_material_init(&(nt_material_desc_t){.max_materials = (uint16_t)(CAP + 1)});
}

void tearDown(void) {
    nt_sprite_renderer_shutdown();
    nt_material_shutdown();
    nt_resource_shutdown();
    nt_gfx_shutdown();
    nt_hash_shutdown();
}

/* Each material gets its own program, so each needs its own cache entry. */
static nt_material_t make_material(void) {
    nt_shader_t vs = nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_VERTEX, .source = "void main(){}"});
    nt_shader_t fs = nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_FRAGMENT, .source = "void main(){}"});
    nt_material_t mat = nt_material_create(&(nt_material_create_desc_t){
        .program = nt_gfx_make_program(vs, fs),
        .cull_mode = NT_CULL_NONE,
        .label = "assert_off",
    });
    nt_material_step();
    return mat;
}

/* At the hardcap the entry after the last one would land past entries[], on
 * count and the buffer handles that follow it, so a clobbered count is the
 * observable. */
void test_cache_overflow_does_not_write_past_the_array(void) {
    nt_sprite_renderer_desc_t desc = nt_sprite_renderer_desc_defaults();
    desc.max_pipelines = CAP;
    TEST_ASSERT_EQUAL(NT_OK, nt_sprite_renderer_init(&desc));

    for (uint32_t i = 0; i < CAP; i++) {
        nt_sprite_renderer_set_material(make_material());
    }
    TEST_ASSERT_EQUAL_UINT32(CAP, nt_sprite_renderer_test_pipeline_cache_count());

    /* One past capacity: the guard must refuse rather than write. */
    nt_sprite_renderer_set_material(make_material());

    TEST_ASSERT_EQUAL_UINT32(CAP, nt_sprite_renderer_test_pipeline_cache_count());
    /* The renderer is still usable: a material already in the cache still binds. */
    TEST_ASSERT_EQUAL_UINT32(0, nt_sprite_renderer_test_vertex_count());
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_cache_overflow_does_not_write_past_the_array);
    return UNITY_END();
}
