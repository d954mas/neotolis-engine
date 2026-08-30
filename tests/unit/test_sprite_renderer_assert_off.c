/* Drives the sprite renderer's pipeline-cache capacity guard with
 * NT_ASSERT_MODE=0, the one configuration where the assert above it vanishes.
 * Nothing else exercises that guard: no build sets OFF, so without this the
 * branch is unreachable code. OFF is not a supported runtime mode; this only
 * pins that the guard does not depend on an assert to hold the bound. */
#undef NT_ASSERT_MODE
#define NT_ASSERT_MODE 0

#include <stdint.h>
#include <string.h>

#include "graphics/nt_gfx.h"
#include "hash/nt_hash.h"
#include "material/nt_material.h"
#include "renderers/nt_sprite_renderer.h"
#include "resource/nt_resource.h"
#include "unity.h"

void setUp(void) {
    nt_hash_init(&(nt_hash_desc_t){0});
    nt_gfx_init(&(nt_gfx_desc_t){.max_shaders = 32, .max_programs = 16, .max_pipelines = 16, .max_buffers = 64, .max_textures = 32, .max_meshes = 16, .max_render_targets = 16});
    nt_resource_init(&(nt_resource_desc_t){0});
    nt_material_init(&(nt_material_desc_t){.max_materials = 16});
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

/* The entry after the last one would land on pipeline_count and the buffer
 * handles that follow the array, so a clobbered count is the observable. */
void test_cache_overflow_does_not_write_past_the_array(void) {
    nt_sprite_renderer_desc_t desc = nt_sprite_renderer_desc_defaults();
    desc.max_pipelines = 2;
    TEST_ASSERT_EQUAL(NT_OK, nt_sprite_renderer_init(&desc));

    nt_sprite_renderer_set_material(make_material());
    nt_sprite_renderer_set_material(make_material());
    TEST_ASSERT_EQUAL_UINT32(2, nt_sprite_renderer_test_pipeline_cache_count());

    /* One past capacity: the guard must refuse rather than write. */
    nt_sprite_renderer_set_material(make_material());

    TEST_ASSERT_EQUAL_UINT32(2, nt_sprite_renderer_test_pipeline_cache_count());
    /* The renderer is still usable: a material already in the cache still binds. */
    TEST_ASSERT_EQUAL_UINT32(0, nt_sprite_renderer_test_vertex_count());
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_cache_overflow_does_not_write_past_the_array);
    return UNITY_END();
}
