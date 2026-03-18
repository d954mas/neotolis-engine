#include "graphics/nt_gfx.h"
#include "graphics/nt_gfx_internal.h"
#include "nt_mesh_format.h"
#include "nt_shader_format.h"
#include "nt_texture_format.h"
#include "unity.h"

#include <string.h>

/* 4x4 RGBA8 test pixel data (64 bytes) */
static const uint8_t s_test_pixels_4x4[4 * 4 * 4] = {
    255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 255, 255, 0, 255, 255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 255, 255, 0, 255,
    255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 255, 255, 0, 255, 255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 255, 255, 0, 255,
};

void setUp(void) { nt_gfx_init(&(nt_gfx_desc_t){.max_shaders = 8, .max_pipelines = 4, .max_buffers = 8, .max_textures = 8}); }

void tearDown(void) { nt_gfx_shutdown(); }

/* ---- Pool: alloc returns nonzero ---- */

void test_gfx_pool_alloc_returns_nonzero(void) {
    nt_gfx_pool_t pool;
    nt_gfx_pool_init(&pool, 4);
    uint32_t id = nt_gfx_pool_alloc(&pool);
    TEST_ASSERT_NOT_EQUAL_UINT32(0, id);
    nt_gfx_pool_shutdown(&pool);
}

/* ---- Pool: two allocs return different ids ---- */

void test_gfx_pool_alloc_unique(void) {
    nt_gfx_pool_t pool;
    nt_gfx_pool_init(&pool, 4);
    uint32_t a = nt_gfx_pool_alloc(&pool);
    uint32_t b = nt_gfx_pool_alloc(&pool);
    TEST_ASSERT_NOT_EQUAL_UINT32(a, b);
    nt_gfx_pool_shutdown(&pool);
}

/* ---- Pool: free then realloc gives new generation ---- */

void test_gfx_pool_free_and_realloc(void) {
    nt_gfx_pool_t pool;
    nt_gfx_pool_init(&pool, 4);
    uint32_t first = nt_gfx_pool_alloc(&pool);
    nt_gfx_pool_free(&pool, first);
    uint32_t second = nt_gfx_pool_alloc(&pool);
    /* Same slot index, different generation -> different id */
    TEST_ASSERT_NOT_EQUAL_UINT32(first, second);
    /* Same slot index */
    TEST_ASSERT_EQUAL_UINT32(nt_gfx_pool_slot_index(first), nt_gfx_pool_slot_index(second));
    nt_gfx_pool_shutdown(&pool);
}

/* ---- Pool: valid accepts live handle ---- */

void test_gfx_pool_valid_accepts_live(void) {
    nt_gfx_pool_t pool;
    nt_gfx_pool_init(&pool, 4);
    uint32_t id = nt_gfx_pool_alloc(&pool);
    TEST_ASSERT_TRUE(nt_gfx_pool_valid(&pool, id));
    nt_gfx_pool_shutdown(&pool);
}

/* ---- Pool: valid rejects zero ---- */

void test_gfx_pool_valid_rejects_zero(void) {
    nt_gfx_pool_t pool;
    nt_gfx_pool_init(&pool, 4);
    TEST_ASSERT_FALSE(nt_gfx_pool_valid(&pool, 0));
    nt_gfx_pool_shutdown(&pool);
}

/* ---- Pool: valid rejects stale handle ---- */

void test_gfx_pool_valid_rejects_stale(void) {
    nt_gfx_pool_t pool;
    nt_gfx_pool_init(&pool, 4);
    uint32_t id = nt_gfx_pool_alloc(&pool);
    nt_gfx_pool_free(&pool, id);
    TEST_ASSERT_FALSE(nt_gfx_pool_valid(&pool, id));
    nt_gfx_pool_shutdown(&pool);
}

/* ---- Pool: full pool returns zero ---- */

void test_gfx_pool_full_returns_zero(void) {
    nt_gfx_pool_t pool;
    nt_gfx_pool_init(&pool, 2);
    nt_gfx_pool_alloc(&pool);
    nt_gfx_pool_alloc(&pool);
    uint32_t third = nt_gfx_pool_alloc(&pool);
    TEST_ASSERT_EQUAL_UINT32(0, third);
    nt_gfx_pool_shutdown(&pool);
}

/* ---- Pool: slot_index extracts correctly ---- */

void test_gfx_slot_index_extracts_correctly(void) {
    /* Construct a known handle: generation=3, slot=5 */
    uint32_t id = (3U << NT_GFX_SLOT_SHIFT) | 5U;
    TEST_ASSERT_EQUAL_UINT32(5, nt_gfx_pool_slot_index(id));
}

/* ---- High-level: init/shutdown transitions initialized flag ---- */

void test_gfx_init_shutdown(void) {
    /* setUp already called nt_gfx_init, so initialized should be true */
    TEST_ASSERT_TRUE(g_nt_gfx.initialized);
    nt_gfx_shutdown();
    TEST_ASSERT_FALSE(g_nt_gfx.initialized);
    /* Re-init for tearDown */
    nt_gfx_init(&(nt_gfx_desc_t){.max_shaders = 8, .max_pipelines = 4, .max_buffers = 8, .max_textures = 8});
}

/* ---- High-level: make/destroy shader ---- */

void test_gfx_make_destroy_shader(void) {
    nt_shader_t shd = nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_VERTEX, .source = "test"});
    TEST_ASSERT_NOT_EQUAL_UINT32(0, shd.id);
    nt_gfx_destroy_shader(shd);
    /* After destroy, making a new one should still work */
    nt_shader_t shd2 = nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_FRAGMENT, .source = "test2"});
    TEST_ASSERT_NOT_EQUAL_UINT32(0, shd2.id);
}

/* ---- High-level: make/destroy buffer ---- */

void test_gfx_make_destroy_buffer(void) {
    nt_buffer_t buf = nt_gfx_make_buffer(&(nt_buffer_desc_t){.type = NT_BUFFER_VERTEX, .size = 64});
    TEST_ASSERT_NOT_EQUAL_UINT32(0, buf.id);
    nt_gfx_destroy_buffer(buf);
}

/* ---- High-level: nt_gfx_desc_defaults provides usable config ---- */

void test_gfx_defaults_applied(void) {
    /* Shutdown current, re-init with defaults */
    nt_gfx_shutdown();
    nt_gfx_desc_t defaults = nt_gfx_desc_defaults();
    nt_gfx_init(&defaults);
    TEST_ASSERT_TRUE(g_nt_gfx.initialized);

    /* Verify we can allocate more than 4 shaders (proves defaults > test setUp) */
    nt_shader_t shaders[10];
    for (int i = 0; i < 10; i++) {
        shaders[i] = nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_VERTEX, .source = "v"});
        TEST_ASSERT_NOT_EQUAL_UINT32(0, shaders[i].id);
    }

    /* Re-init for tearDown */
    nt_gfx_shutdown();
    nt_gfx_init(&(nt_gfx_desc_t){.max_shaders = 8, .max_pipelines = 4, .max_buffers = 8, .max_textures = 8});
}

/* ---- Pipeline: create with valid shaders, destroy ---- */

void test_gfx_make_destroy_pipeline(void) {
    nt_shader_t vs = nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_VERTEX, .source = "v"});
    nt_shader_t fs = nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_FRAGMENT, .source = "f"});
    TEST_ASSERT_NOT_EQUAL_UINT32(0, vs.id);
    TEST_ASSERT_NOT_EQUAL_UINT32(0, fs.id);

    nt_pipeline_t pip = nt_gfx_make_pipeline(&(nt_pipeline_desc_t){
        .vertex_shader = vs,
        .fragment_shader = fs,
        .layout = {.attr_count = 1, .stride = 12, .attrs = {{.location = 0, .format = NT_FORMAT_FLOAT3}}},
    });
    TEST_ASSERT_NOT_EQUAL_UINT32(0, pip.id);
    nt_gfx_destroy_pipeline(pip);
}

/* ---- Pipeline: survives shader destroy ---- */

void test_gfx_pipeline_survives_shader_destroy(void) {
    nt_shader_t vs = nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_VERTEX, .source = "v"});
    nt_shader_t fs = nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_FRAGMENT, .source = "f"});
    nt_pipeline_t pip = nt_gfx_make_pipeline(&(nt_pipeline_desc_t){
        .vertex_shader = vs,
        .fragment_shader = fs,
        .layout = {.attr_count = 1, .stride = 12, .attrs = {{.location = 0, .format = NT_FORMAT_FLOAT3}}},
    });
    TEST_ASSERT_NOT_EQUAL_UINT32(0, pip.id);

    /* Destroy shaders — pipeline should still be bindable */
    nt_gfx_destroy_shader(vs);
    nt_gfx_destroy_shader(fs);

    nt_gfx_begin_frame();
    nt_gfx_begin_pass(&(nt_pass_desc_t){.clear_depth = 1.0F});
    nt_gfx_bind_pipeline(pip);
    nt_gfx_end_pass();
    nt_gfx_end_frame();

    nt_gfx_destroy_pipeline(pip);
}

/* ---- State machine: valid frame cycle ---- */

void test_gfx_state_machine_valid_cycle(void) {
    nt_gfx_begin_frame();
    nt_gfx_begin_pass(&(nt_pass_desc_t){.clear_depth = 1.0F});
    nt_gfx_end_pass();
    nt_gfx_end_frame();
    /* Reaching here without crash means state machine accepted the sequence */
}

/* ---- Double destroy: shader ---- */

void test_gfx_double_destroy_shader(void) {
    nt_shader_t shd = nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_VERTEX, .source = "test"});
    nt_gfx_destroy_shader(shd);
    /* Second destroy on stale handle — must be no-op */
    nt_gfx_destroy_shader(shd);
}

/* ---- Double destroy: buffer ---- */

void test_gfx_double_destroy_buffer(void) {
    nt_buffer_t buf = nt_gfx_make_buffer(&(nt_buffer_desc_t){.type = NT_BUFFER_VERTEX, .size = 64});
    nt_gfx_destroy_buffer(buf);
    nt_gfx_destroy_buffer(buf);
}

/* ---- Pipeline: rejected with invalid shaders ---- */

void test_gfx_pipeline_rejects_invalid_shaders(void) {
    nt_pipeline_t pip = nt_gfx_make_pipeline(&(nt_pipeline_desc_t){
        .vertex_shader = {.id = 0},
        .fragment_shader = {.id = 0},
    });
    TEST_ASSERT_EQUAL_UINT32(0, pip.id);
}

/* ---- Texture: make with valid data ---- */

void test_gfx_make_texture_valid(void) {
    nt_texture_t tex = nt_gfx_make_texture(&(nt_texture_desc_t){
        .width = 4,
        .height = 4,
        .data = s_test_pixels_4x4,
    });
    TEST_ASSERT_NOT_EQUAL_UINT32(0, tex.id);
    nt_gfx_destroy_texture(tex);
}

/* ---- Texture: NULL desc ---- */

void test_gfx_make_texture_null_desc(void) {
    nt_texture_t tex = nt_gfx_make_texture(NULL);
    TEST_ASSERT_EQUAL_UINT32(0, tex.id);
}

/* ---- Texture: NULL data ---- */

void test_gfx_make_texture_null_data(void) {
    nt_texture_t tex = nt_gfx_make_texture(&(nt_texture_desc_t){
        .width = 4,
        .height = 4,
        .data = NULL,
    });
    TEST_ASSERT_EQUAL_UINT32(0, tex.id);
}

/* ---- Texture: zero width ---- */

void test_gfx_make_texture_zero_width(void) {
    nt_texture_t tex = nt_gfx_make_texture(&(nt_texture_desc_t){
        .width = 0,
        .height = 4,
        .data = s_test_pixels_4x4,
    });
    TEST_ASSERT_EQUAL_UINT32(0, tex.id);
}

/* ---- Texture: zero height ---- */

void test_gfx_make_texture_zero_height(void) {
    nt_texture_t tex = nt_gfx_make_texture(&(nt_texture_desc_t){
        .width = 4,
        .height = 0,
        .data = s_test_pixels_4x4,
    });
    TEST_ASSERT_EQUAL_UINT32(0, tex.id);
}

/* ---- Texture: NPOT dimensions accepted ---- */

void test_gfx_make_texture_npot(void) {
    /* WebGL 2 / GL 3.3 fully support NPOT textures */
    nt_texture_t tex = nt_gfx_make_texture(&(nt_texture_desc_t){
        .width = 3,
        .height = 5,
        .data = s_test_pixels_4x4,
    });
    TEST_ASSERT_NOT_EQUAL_UINT32(0, tex.id);
    nt_gfx_destroy_texture(tex);
}

/* ---- Texture: mag_filter clamped from mipmap variant ---- */

void test_gfx_make_texture_mag_filter_clamped(void) {
    nt_texture_t tex = nt_gfx_make_texture(&(nt_texture_desc_t){
        .width = 4,
        .height = 4,
        .data = s_test_pixels_4x4,
        .mag_filter = NT_FILTER_LINEAR_MIPMAP_LINEAR,
    });
    TEST_ASSERT_NOT_EQUAL_UINT32(0, tex.id);
    nt_gfx_destroy_texture(tex);
}

/* ---- Texture: gen_mipmaps with mipmap min_filter ---- */

void test_gfx_make_texture_gen_mipmaps(void) {
    nt_texture_t tex = nt_gfx_make_texture(&(nt_texture_desc_t){
        .width = 4,
        .height = 4,
        .data = s_test_pixels_4x4,
        .min_filter = NT_FILTER_LINEAR_MIPMAP_LINEAR,
        .gen_mipmaps = true,
    });
    TEST_ASSERT_NOT_EQUAL_UINT32(0, tex.id);
    nt_gfx_destroy_texture(tex);
}

/* ---- Texture: mipmap min_filter clamped when gen_mipmaps=false ---- */

void test_gfx_make_texture_mipmap_filter_no_mipmaps(void) {
    /* Without gen_mipmaps, mipmap min_filter would create incomplete texture.
       Engine must clamp to non-mipmap variant and still return valid handle. */
    nt_texture_t tex = nt_gfx_make_texture(&(nt_texture_desc_t){
        .width = 4,
        .height = 4,
        .data = s_test_pixels_4x4,
        .min_filter = NT_FILTER_LINEAR_MIPMAP_LINEAR,
        .gen_mipmaps = false,
    });
    TEST_ASSERT_NOT_EQUAL_UINT32(0, tex.id);
    nt_gfx_destroy_texture(tex);
}

/* ---- Texture: bind with valid handle ---- */

void test_gfx_bind_texture_valid(void) {
    nt_texture_t tex = nt_gfx_make_texture(&(nt_texture_desc_t){
        .width = 4,
        .height = 4,
        .data = s_test_pixels_4x4,
    });
    TEST_ASSERT_NOT_EQUAL_UINT32(0, tex.id);
    nt_gfx_bind_texture(tex, 0);
    nt_gfx_destroy_texture(tex);
}

/* ---- Texture: bind with invalid handle is no-op ---- */

void test_gfx_bind_texture_invalid(void) {
    nt_texture_t tex = {.id = 0};
    nt_gfx_bind_texture(tex, 0); /* must not crash */
}

/* ---- Texture: destroy and reuse slot ---- */

void test_gfx_destroy_texture_and_reuse(void) {
    nt_texture_t first = nt_gfx_make_texture(&(nt_texture_desc_t){
        .width = 4,
        .height = 4,
        .data = s_test_pixels_4x4,
    });
    uint32_t first_slot = first.id & 0xFFFF;
    nt_gfx_destroy_texture(first);
    nt_texture_t second = nt_gfx_make_texture(&(nt_texture_desc_t){
        .width = 4,
        .height = 4,
        .data = s_test_pixels_4x4,
    });
    uint32_t second_slot = second.id & 0xFFFF;
    TEST_ASSERT_EQUAL_UINT32(first_slot, second_slot);
    TEST_ASSERT_NOT_EQUAL_UINT32(first.id, second.id);
    nt_gfx_destroy_texture(second);
}

/* ---- Texture: double destroy is safe ---- */

void test_gfx_double_destroy_texture(void) {
    nt_texture_t tex = nt_gfx_make_texture(&(nt_texture_desc_t){
        .width = 4,
        .height = 4,
        .data = s_test_pixels_4x4,
    });
    nt_gfx_destroy_texture(tex);
    nt_gfx_destroy_texture(tex); /* must be safe no-op */
}

/* ---- Texture: pool exhaustion ---- */

void test_gfx_texture_pool_full(void) {
    nt_texture_t textures[8];
    for (int i = 0; i < 8; i++) {
        textures[i] = nt_gfx_make_texture(&(nt_texture_desc_t){
            .width = 4,
            .height = 4,
            .data = s_test_pixels_4x4,
        });
        TEST_ASSERT_NOT_EQUAL_UINT32(0, textures[i].id);
    }
    nt_texture_t overflow = nt_gfx_make_texture(&(nt_texture_desc_t){
        .width = 4,
        .height = 4,
        .data = s_test_pixels_4x4,
    });
    TEST_ASSERT_EQUAL_UINT32(0, overflow.id);
    for (int i = 0; i < 8; i++) {
        nt_gfx_destroy_texture(textures[i]);
    }
}

/* ---- Activator: texture valid blob ---- */

void test_activate_texture_valid_blob(void) {
    uint32_t w = 2;
    uint32_t h = 2;
    uint32_t pixel_size = w * h * 4;
    uint32_t blob_size = (uint32_t)sizeof(NtTextureAssetHeader) + pixel_size;
    uint8_t blob[sizeof(NtTextureAssetHeader) + 16];
    NtTextureAssetHeader *hdr = (NtTextureAssetHeader *)blob;
    memset(blob, 0, sizeof(blob));
    hdr->magic = NT_TEXTURE_MAGIC;
    hdr->version = NT_TEXTURE_VERSION;
    hdr->format = NT_TEXTURE_FORMAT_RGBA8;
    hdr->width = w;
    hdr->height = h;
    hdr->mip_count = 1;
    /* Fill pixel data with non-zero */
    memset(blob + sizeof(NtTextureAssetHeader), 0xFF, pixel_size);
    uint32_t handle = nt_gfx_activate_texture(blob, blob_size);
    TEST_ASSERT_NOT_EQUAL_UINT32(0, handle);
    nt_gfx_deactivate_texture(handle);
}

/* ---- Activator: texture bad magic ---- */

void test_activate_texture_bad_magic(void) {
    uint8_t blob[sizeof(NtTextureAssetHeader) + 16];
    NtTextureAssetHeader *hdr = (NtTextureAssetHeader *)blob;
    memset(blob, 0, sizeof(blob));
    hdr->magic = 0xDEADBEEF;
    hdr->width = 2;
    hdr->height = 2;
    hdr->mip_count = 1;
    uint32_t handle = nt_gfx_activate_texture(blob, sizeof(blob));
    TEST_ASSERT_EQUAL_UINT32(0, handle);
}

/* ---- Activator: texture too small ---- */

void test_activate_texture_too_small(void) {
    uint8_t blob[4]; /* smaller than header */
    memset(blob, 0, sizeof(blob));
    uint32_t handle = nt_gfx_activate_texture(blob, sizeof(blob));
    TEST_ASSERT_EQUAL_UINT32(0, handle);
}

/* ---- Activator: mesh valid blob ---- */

void test_activate_mesh_valid_blob(void) {
    /* Build: header + 1 stream desc + 12 bytes vertex data + 6 bytes index data */
    uint32_t streams_size = (uint32_t)sizeof(NtStreamDesc);
    uint32_t vdata_size = 12; /* 1 vertex, 3 floats */
    uint32_t idata_size = 6;  /* 3 uint16 indices */
    uint32_t blob_size = (uint32_t)sizeof(NtMeshAssetHeader) + streams_size + vdata_size + idata_size;
    uint8_t blob[sizeof(NtMeshAssetHeader) + sizeof(NtStreamDesc) + 12 + 6];
    memset(blob, 0, sizeof(blob));

    NtMeshAssetHeader *hdr = (NtMeshAssetHeader *)blob;
    hdr->magic = NT_MESH_MAGIC;
    hdr->version = NT_MESH_VERSION;
    hdr->stream_count = 1;
    hdr->index_type = 1; /* uint16 */
    hdr->vertex_count = 1;
    hdr->index_count = 3;
    hdr->vertex_data_size = vdata_size;
    hdr->index_data_size = idata_size;

    NtStreamDesc *sd = (NtStreamDesc *)(blob + sizeof(NtMeshAssetHeader));
    sd->name_hash = 0x12345678;
    sd->type = NT_STREAM_FLOAT32;
    sd->count = 3;

    uint32_t handle = nt_gfx_activate_mesh(blob, blob_size);
    TEST_ASSERT_NOT_EQUAL_UINT32(0, handle);
    nt_gfx_deactivate_mesh(handle);
}

/* ---- Activator: mesh bad magic ---- */

void test_activate_mesh_bad_magic(void) {
    uint8_t blob[sizeof(NtMeshAssetHeader)];
    memset(blob, 0, sizeof(blob));
    NtMeshAssetHeader *hdr = (NtMeshAssetHeader *)blob;
    hdr->magic = 0xDEADBEEF;
    uint32_t handle = nt_gfx_activate_mesh(blob, sizeof(blob));
    TEST_ASSERT_EQUAL_UINT32(0, handle);
}

/* ---- Activator: shader valid blob ---- */

void test_activate_shader_valid_blob(void) {
    const char *source = "void main() {}\0";
    uint32_t code_size = (uint32_t)strlen(source) + 1;
    uint32_t blob_size = (uint32_t)sizeof(NtShaderCodeHeader) + code_size;
    uint8_t blob[sizeof(NtShaderCodeHeader) + 32];
    memset(blob, 0, sizeof(blob));

    NtShaderCodeHeader *hdr = (NtShaderCodeHeader *)blob;
    hdr->magic = NT_SHADER_CODE_MAGIC;
    hdr->version = NT_SHADER_CODE_VERSION;
    hdr->stage = NT_SHADER_STAGE_VERTEX;
    hdr->code_size = code_size;
    memcpy(blob + sizeof(NtShaderCodeHeader), source, code_size);

    uint32_t handle = nt_gfx_activate_shader(blob, blob_size);
    TEST_ASSERT_NOT_EQUAL_UINT32(0, handle);
    nt_gfx_deactivate_shader(handle);
}

/* ---- Activator: shader bad magic ---- */

void test_activate_shader_bad_magic(void) {
    uint8_t blob[sizeof(NtShaderCodeHeader)];
    memset(blob, 0, sizeof(blob));
    NtShaderCodeHeader *hdr = (NtShaderCodeHeader *)blob;
    hdr->magic = 0xDEADBEEF;
    uint32_t handle = nt_gfx_activate_shader(blob, sizeof(blob));
    TEST_ASSERT_EQUAL_UINT32(0, handle);
}

/* ---- Deactivate mesh clears table ---- */

void test_deactivate_mesh_clears_table(void) {
    uint32_t streams_size = (uint32_t)sizeof(NtStreamDesc);
    uint32_t vdata_size = 12;
    uint32_t idata_size = 6;
    uint32_t blob_size = (uint32_t)sizeof(NtMeshAssetHeader) + streams_size + vdata_size + idata_size;
    uint8_t blob[sizeof(NtMeshAssetHeader) + sizeof(NtStreamDesc) + 12 + 6];
    memset(blob, 0, sizeof(blob));

    NtMeshAssetHeader *hdr = (NtMeshAssetHeader *)blob;
    hdr->magic = NT_MESH_MAGIC;
    hdr->version = NT_MESH_VERSION;
    hdr->stream_count = 1;
    hdr->index_type = 1;
    hdr->vertex_count = 3;
    hdr->index_count = 3;
    hdr->vertex_data_size = vdata_size;
    hdr->index_data_size = idata_size;

    NtStreamDesc *sd = (NtStreamDesc *)(blob + sizeof(NtMeshAssetHeader));
    sd->type = NT_STREAM_FLOAT32;
    sd->count = 3;

    uint32_t handle = nt_gfx_activate_mesh(blob, blob_size);
    TEST_ASSERT_NOT_NULL(nt_gfx_get_mesh_info(handle));
    nt_gfx_deactivate_mesh(handle);
    TEST_ASSERT_NULL(nt_gfx_get_mesh_info(handle));
}

/* ---- Mesh info fields ---- */

void test_mesh_info_fields(void) {
    uint32_t streams_size = (uint32_t)sizeof(NtStreamDesc);
    uint32_t vdata_size = 36; /* 3 vertices * 3 floats * 4 bytes */
    uint32_t idata_size = 6;  /* 3 uint16 indices */
    uint32_t blob_size = (uint32_t)sizeof(NtMeshAssetHeader) + streams_size + vdata_size + idata_size;
    uint8_t blob[sizeof(NtMeshAssetHeader) + sizeof(NtStreamDesc) + 36 + 6];
    memset(blob, 0, sizeof(blob));

    NtMeshAssetHeader *hdr = (NtMeshAssetHeader *)blob;
    hdr->magic = NT_MESH_MAGIC;
    hdr->version = NT_MESH_VERSION;
    hdr->stream_count = 1;
    hdr->index_type = 1;
    hdr->vertex_count = 3;
    hdr->index_count = 3;
    hdr->vertex_data_size = vdata_size;
    hdr->index_data_size = idata_size;

    NtStreamDesc *sd = (NtStreamDesc *)(blob + sizeof(NtMeshAssetHeader));
    sd->type = NT_STREAM_FLOAT32;
    sd->count = 3;

    uint32_t handle = nt_gfx_activate_mesh(blob, blob_size);
    const nt_gfx_mesh_info_t *info = nt_gfx_get_mesh_info(handle);
    TEST_ASSERT_NOT_NULL(info);
    TEST_ASSERT_EQUAL_UINT32(3, info->vertex_count);
    TEST_ASSERT_EQUAL_UINT32(3, info->index_count);
    TEST_ASSERT_EQUAL_UINT8(1, info->stream_count);
    TEST_ASSERT_EQUAL_UINT8(1, info->index_type);
    TEST_ASSERT_NOT_EQUAL_UINT32(0, info->vbo.id);
    TEST_ASSERT_NOT_EQUAL_UINT32(0, info->ibo.id);
    nt_gfx_deactivate_mesh(handle);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_gfx_pool_alloc_returns_nonzero);
    RUN_TEST(test_gfx_pool_alloc_unique);
    RUN_TEST(test_gfx_pool_free_and_realloc);
    RUN_TEST(test_gfx_pool_valid_accepts_live);
    RUN_TEST(test_gfx_pool_valid_rejects_zero);
    RUN_TEST(test_gfx_pool_valid_rejects_stale);
    RUN_TEST(test_gfx_pool_full_returns_zero);
    RUN_TEST(test_gfx_slot_index_extracts_correctly);
    RUN_TEST(test_gfx_init_shutdown);
    RUN_TEST(test_gfx_make_destroy_shader);
    RUN_TEST(test_gfx_make_destroy_buffer);
    RUN_TEST(test_gfx_defaults_applied);
    RUN_TEST(test_gfx_make_destroy_pipeline);
    RUN_TEST(test_gfx_pipeline_survives_shader_destroy);
    RUN_TEST(test_gfx_state_machine_valid_cycle);
    RUN_TEST(test_gfx_double_destroy_shader);
    RUN_TEST(test_gfx_double_destroy_buffer);
    RUN_TEST(test_gfx_pipeline_rejects_invalid_shaders);
    /* Texture tests */
    RUN_TEST(test_gfx_make_texture_valid);
    RUN_TEST(test_gfx_make_texture_null_desc);
    RUN_TEST(test_gfx_make_texture_null_data);
    RUN_TEST(test_gfx_make_texture_zero_width);
    RUN_TEST(test_gfx_make_texture_zero_height);
    RUN_TEST(test_gfx_make_texture_npot);
    RUN_TEST(test_gfx_make_texture_mag_filter_clamped);
    RUN_TEST(test_gfx_make_texture_gen_mipmaps);
    RUN_TEST(test_gfx_make_texture_mipmap_filter_no_mipmaps);
    RUN_TEST(test_gfx_bind_texture_valid);
    RUN_TEST(test_gfx_bind_texture_invalid);
    RUN_TEST(test_gfx_destroy_texture_and_reuse);
    RUN_TEST(test_gfx_double_destroy_texture);
    RUN_TEST(test_gfx_texture_pool_full);
    /* Activator tests */
    RUN_TEST(test_activate_texture_valid_blob);
    RUN_TEST(test_activate_texture_bad_magic);
    RUN_TEST(test_activate_texture_too_small);
    RUN_TEST(test_activate_mesh_valid_blob);
    RUN_TEST(test_activate_mesh_bad_magic);
    RUN_TEST(test_activate_shader_valid_blob);
    RUN_TEST(test_activate_shader_bad_magic);
    RUN_TEST(test_deactivate_mesh_clears_table);
    RUN_TEST(test_mesh_info_fields);
    return UNITY_END();
}
