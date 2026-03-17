/* System headers before Unity to avoid noreturn / __declspec conflict on MSVC */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* clang-format off */
#include "nt_builder.h"
#include "nt_crc32.h"
#include "nt_mesh_format.h"
#include "nt_pack_format.h"
#include "nt_shader_format.h"
#include "nt_texture_format.h"
#include "unity.h"
/* clang-format on */

/* --- Temp directory for test output --- */

#ifdef _WIN32
#include <direct.h>
#define MKDIR(p) _mkdir(p)
#else
#include <sys/stat.h>
#define MKDIR(p) mkdir(p, 0755)
#endif

#define TMP_DIR "build/tests/tmp"

/* --- Test fixture helpers --- */

static void write_test_shader(const char *path, const char *source) {
    FILE *f = fopen(path, "wb");
    if (f) {
        (void)fwrite(source, 1, strlen(source), f);
        (void)fclose(f);
    }
}

/*
 * Write a minimal valid 2x2 RGBA PNG.
 * This is a hardcoded minimal PNG with 4 pixels: red, green, blue, white.
 */
static void write_test_png(const char *path) {
    /* clang-format off */
    static const uint8_t png_data[] = {
        /* PNG signature */
        0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A,
        /* IHDR chunk: width=2, height=2, bit_depth=8, color_type=6 (RGBA) */
        0x00, 0x00, 0x00, 0x0D, /* chunk length = 13 */
        0x49, 0x48, 0x44, 0x52, /* "IHDR" */
        0x00, 0x00, 0x00, 0x02, /* width = 2 */
        0x00, 0x00, 0x00, 0x02, /* height = 2 */
        0x08,                   /* bit depth = 8 */
        0x06,                   /* color type = 6 (RGBA) */
        0x00,                   /* compression = 0 */
        0x00,                   /* filter = 0 */
        0x00,                   /* interlace = 0 */
        0x72, 0xD3, 0x3E, 0x15, /* IHDR CRC32 */
        /* IDAT chunk: compressed pixel data */
        0x00, 0x00, 0x00, 0x1D, /* chunk length = 29 */
        0x49, 0x44, 0x41, 0x54, /* "IDAT" */
        /* zlib stream: CMF=0x78, FLG=0x01 */
        0x78, 0x01,
        /* deflate block (final, uncompressed) */
        0x01,                   /* BFINAL=1, BTYPE=00 (no compression) */
        0x12, 0x00,             /* LEN = 18 */
        0xED, 0xFF,             /* NLEN = ~18 */
        /* Row 0: filter=0, R,G,B,A, R,G,B,A */
        0x00,
        0xFF, 0x00, 0x00, 0xFF, /* red pixel */
        0x00, 0xFF, 0x00, 0xFF, /* green pixel */
        /* Row 1: filter=0, R,G,B,A, R,G,B,A */
        0x00,
        0x00, 0x00, 0xFF, 0xFF, /* blue pixel */
        0xFF, 0xFF, 0xFF, 0xFF, /* white pixel */
        /* zlib Adler-32 checksum */
        0x2B, 0x05, 0x0A, 0x02,
        0x27, 0x5E, 0x48, 0x3B, /* IDAT CRC32 */
        /* IEND chunk */
        0x00, 0x00, 0x00, 0x00, /* chunk length = 0 */
        0x49, 0x45, 0x4E, 0x44, /* "IEND" */
        0xAE, 0x42, 0x60, 0x82, /* IEND CRC32 */
    };
    /* clang-format on */

    FILE *f = fopen(path, "wb");
    if (f) {
        (void)fwrite(png_data, 1, sizeof(png_data), f);
        (void)fclose(f);
    }
}

/*
 * Write a minimal valid .glb (glTF binary 2.0) with 1 triangle.
 * 3 vertices (POSITION float32 x3), 3 uint16 indices.
 */
static void write_test_glb(const char *path) {
    /* JSON chunk content */
    const char *json_str = "{"
                           "\"asset\":{\"version\":\"2.0\"},"
                           "\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":0},\"indices\":1}]}],"
                           "\"accessors\":["
                           "{\"bufferView\":0,\"componentType\":5126,\"count\":3,\"type\":\"VEC3\","
                           "\"max\":[1.0,1.0,0.0],\"min\":[0.0,0.0,0.0]},"
                           "{\"bufferView\":1,\"componentType\":5123,\"count\":3,\"type\":\"SCALAR\"}"
                           "],"
                           "\"bufferViews\":["
                           "{\"buffer\":0,\"byteOffset\":0,\"byteLength\":36},"
                           "{\"buffer\":0,\"byteOffset\":36,\"byteLength\":6}"
                           "],"
                           "\"buffers\":[{\"byteLength\":44}]"
                           "}";

    uint32_t json_len = (uint32_t)strlen(json_str);
    /* Pad JSON to 4-byte alignment */
    uint32_t json_padded = (json_len + 3U) & ~3U;
    uint32_t json_padding = json_padded - json_len;

    /* Binary data: 3 position vec3 (36 bytes) + 3 uint16 indices (6 bytes) + 2 bytes pad = 44 bytes */
    float positions[] = {
        0.0F, 0.0F, 0.0F, /* v0 */
        1.0F, 0.0F, 0.0F, /* v1 */
        0.0F, 1.0F, 0.0F, /* v2 */
    };
    uint16_t indices[] = {0, 1, 2};
    uint16_t idx_pad = 0;

    uint32_t bin_data_size = (uint32_t)sizeof(positions) + (uint32_t)sizeof(indices) + (uint32_t)sizeof(idx_pad);
    uint32_t bin_padded = (bin_data_size + 3U) & ~3U;

    /* GLB header: 12 bytes */
    uint32_t glb_magic = 0x46546C67; /* "glTF" */
    uint32_t glb_version = 2;

    /* Chunk headers: 8 bytes each */
    uint32_t json_chunk_type = 0x4E4F534A; /* "JSON" */
    uint32_t bin_chunk_type = 0x004E4942;  /* "BIN\0" */

    uint32_t total_length = 12 + 8 + json_padded + 8 + bin_padded;

    FILE *f = fopen(path, "wb");
    if (!f) {
        return;
    }

    /* GLB header */
    (void)fwrite(&glb_magic, 4, 1, f);
    (void)fwrite(&glb_version, 4, 1, f);
    (void)fwrite(&total_length, 4, 1, f);

    /* JSON chunk */
    (void)fwrite(&json_padded, 4, 1, f);
    (void)fwrite(&json_chunk_type, 4, 1, f);
    (void)fwrite(json_str, 1, json_len, f);
    /* Pad JSON with spaces (per glTF spec) */
    for (uint32_t i = 0; i < json_padding; i++) {
        char space = ' ';
        (void)fwrite(&space, 1, 1, f);
    }

    /* BIN chunk */
    (void)fwrite(&bin_padded, 4, 1, f);
    (void)fwrite(&bin_chunk_type, 4, 1, f);
    (void)fwrite(positions, sizeof(positions), 1, f);
    (void)fwrite(indices, sizeof(indices), 1, f);
    (void)fwrite(&idx_pad, sizeof(idx_pad), 1, f);

    (void)fclose(f);
}

/* --- Unity setUp / tearDown --- */

void setUp(void) {
    MKDIR("build");
    MKDIR("build/tests");
    MKDIR(TMP_DIR);
}

void tearDown(void) {}

/* --- FNV-1a hash tests --- */

void test_hash_known_value(void) {
    uint32_t h = nt_builder_hash("test");
    TEST_ASSERT_NOT_EQUAL(0, h);
    /* Deterministic */
    TEST_ASSERT_EQUAL_UINT32(h, nt_builder_hash("test"));
}

void test_hash_path_normalization(void) {
    /* Backslash -> forward slash */
    uint32_t h1 = nt_builder_hash("meshes/cube.glb");
    uint32_t h2 = nt_builder_hash("meshes\\cube.glb");
    TEST_ASSERT_EQUAL_UINT32(h1, h2);

    /* ./ stripped */
    TEST_ASSERT_EQUAL_UINT32(nt_builder_hash("foo/bar.png"), nt_builder_hash("./foo/bar.png"));

    /* // collapsed */
    TEST_ASSERT_EQUAL_UINT32(nt_builder_hash("foo/bar.png"), nt_builder_hash("foo//bar.png"));

    /* ../ resolved */
    TEST_ASSERT_EQUAL_UINT32(nt_builder_hash("bar.png"), nt_builder_hash("foo/../bar.png"));

    /* Leading ../ preserved */
    TEST_ASSERT_NOT_EQUAL(nt_builder_hash("assets/mesh.glb"), nt_builder_hash("../assets/mesh.glb"));
}

void test_hash_different_strings_differ(void) {
    uint32_t h1 = nt_builder_hash("a");
    uint32_t h2 = nt_builder_hash("b");
    TEST_ASSERT_NOT_EQUAL(h1, h2);
}

/* --- Pack writer core tests --- */

void test_start_pack_returns_context(void) {
    NtBuilderContext *ctx = nt_builder_start_pack(TMP_DIR "/test_ctx.neopak");
    TEST_ASSERT_NOT_NULL(ctx);
    /* Finish immediately with no assets should return error */
    nt_build_result_t r = nt_builder_finish_pack(ctx);
    TEST_ASSERT_NOT_EQUAL(NT_BUILD_OK, r);
    nt_builder_free_pack(ctx);
}

/* --- Shader round-trip test --- */

void test_shader_round_trip(void) {
    const char *vert_path = TMP_DIR "/rt_test.vert";
    write_test_shader(vert_path, "precision mediump float;\n"
                                 "layout(location = 0) in vec3 a_pos;\n"
                                 "void main() { gl_Position = vec4(a_pos, 1.0); }\n");

    const char *pack_path = TMP_DIR "/shader_rt.neopak";
    NtBuilderContext *ctx = nt_builder_start_pack(pack_path);
    TEST_ASSERT_NOT_NULL(ctx);

    nt_build_result_t r = nt_builder_add_shader(ctx, vert_path, NT_BUILD_SHADER_VERTEX);
    TEST_ASSERT_EQUAL(NT_BUILD_OK, r);

    r = nt_builder_finish_pack(ctx);
    TEST_ASSERT_EQUAL(NT_BUILD_OK, r);
    nt_builder_free_pack(ctx);

    /* Read back and verify header */
    FILE *f = fopen(pack_path, "rb");
    TEST_ASSERT_NOT_NULL(f);

    NtPackHeader hdr;
    TEST_ASSERT_EQUAL(1, fread(&hdr, sizeof(hdr), 1, f));
    TEST_ASSERT_EQUAL_UINT32(NT_PACK_MAGIC, hdr.magic);
    TEST_ASSERT_EQUAL_UINT16(NT_PACK_VERSION, hdr.version);
    TEST_ASSERT_EQUAL_UINT16(1, hdr.asset_count);

    NtAssetEntry entry;
    TEST_ASSERT_EQUAL(1, fread(&entry, sizeof(entry), 1, f));
    TEST_ASSERT_EQUAL_UINT8(NT_ASSET_SHADER_CODE, entry.asset_type);
    TEST_ASSERT_EQUAL_UINT16(NT_SHADER_CODE_VERSION, entry.format_version);

    /* Verify CRC32 */
    (void)fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    (void)fseek(f, 0, SEEK_SET);
    uint8_t *buf = (uint8_t *)malloc((size_t)file_size);
    TEST_ASSERT_NOT_NULL(buf);
    (void)fread(buf, 1, (size_t)file_size, f);
    uint32_t data_size = (uint32_t)file_size - hdr.header_size;
    uint32_t computed_crc = nt_crc32(buf + hdr.header_size, data_size);
    TEST_ASSERT_EQUAL_HEX32(hdr.checksum, computed_crc);
    free(buf);

    (void)fclose(f);
}

/* --- Texture round-trip test --- */

void test_texture_round_trip(void) {
    const char *png_path = TMP_DIR "/rt_test_2x2.png";
    write_test_png(png_path);

    const char *pack_path = TMP_DIR "/texture_rt.neopak";
    NtBuilderContext *ctx = nt_builder_start_pack(pack_path);
    TEST_ASSERT_NOT_NULL(ctx);

    nt_build_result_t r = nt_builder_add_texture(ctx, png_path);
    TEST_ASSERT_EQUAL(NT_BUILD_OK, r);

    r = nt_builder_finish_pack(ctx);
    TEST_ASSERT_EQUAL(NT_BUILD_OK, r);
    nt_builder_free_pack(ctx);

    /* Read back and verify texture header */
    FILE *f = fopen(pack_path, "rb");
    TEST_ASSERT_NOT_NULL(f);

    NtPackHeader hdr;
    (void)fread(&hdr, sizeof(hdr), 1, f);
    NtAssetEntry entry;
    (void)fread(&entry, sizeof(entry), 1, f);
    TEST_ASSERT_EQUAL_UINT8(NT_ASSET_TEXTURE, entry.asset_type);

    (void)fseek(f, (long)entry.offset, SEEK_SET);
    NtTextureAssetHeader tex;
    (void)fread(&tex, sizeof(tex), 1, f);
    TEST_ASSERT_EQUAL_UINT32(NT_TEXTURE_MAGIC, tex.magic);
    TEST_ASSERT_EQUAL_UINT32(2, tex.width);
    TEST_ASSERT_EQUAL_UINT32(2, tex.height);
    TEST_ASSERT_EQUAL_UINT16(NT_TEXTURE_FORMAT_RGBA8, tex.format);

    (void)fclose(f);
}

/* --- Mesh round-trip test --- */

void test_mesh_round_trip(void) {
    const char *glb_path = TMP_DIR "/rt_triangle.glb";
    write_test_glb(glb_path);

    NtStreamLayout layout[] = {
        {"position", "POSITION", NT_STREAM_FLOAT32, 3, false},
    };

    const char *pack_path = TMP_DIR "/mesh_rt.neopak";
    NtBuilderContext *ctx = nt_builder_start_pack(pack_path);
    TEST_ASSERT_NOT_NULL(ctx);

    nt_build_result_t r = nt_builder_add_mesh(ctx, glb_path, layout, 1);
    TEST_ASSERT_EQUAL(NT_BUILD_OK, r);

    r = nt_builder_finish_pack(ctx);
    TEST_ASSERT_EQUAL(NT_BUILD_OK, r);
    nt_builder_free_pack(ctx);

    /* Read back and verify mesh header */
    FILE *f = fopen(pack_path, "rb");
    TEST_ASSERT_NOT_NULL(f);

    NtPackHeader hdr;
    (void)fread(&hdr, sizeof(hdr), 1, f);
    NtAssetEntry entry;
    (void)fread(&entry, sizeof(entry), 1, f);
    TEST_ASSERT_EQUAL_UINT8(NT_ASSET_MESH, entry.asset_type);

    (void)fseek(f, (long)entry.offset, SEEK_SET);
    NtMeshAssetHeader mesh;
    (void)fread(&mesh, sizeof(mesh), 1, f);
    TEST_ASSERT_EQUAL_UINT32(NT_MESH_MAGIC, mesh.magic);
    TEST_ASSERT_EQUAL_UINT8(1, mesh.stream_count);
    TEST_ASSERT_EQUAL_UINT32(3, mesh.vertex_count);
    TEST_ASSERT_EQUAL_UINT8(1, mesh.index_type); /* uint16 */
    TEST_ASSERT_EQUAL_UINT32(3, mesh.index_count);

    (void)fclose(f);
}

/* --- Validation error tests --- */

void test_missing_position_attribute_errors(void) {
    const char *glb_path = TMP_DIR "/rt_triangle_nopos.glb";
    write_test_glb(glb_path);

    NtStreamLayout layout[] = {
        {"normal", "NORMAL", NT_STREAM_FLOAT32, 3, false},
    };

    const char *pack_path = TMP_DIR "/no_pos.neopak";
    NtBuilderContext *ctx = nt_builder_start_pack(pack_path);
    TEST_ASSERT_NOT_NULL(ctx);

    /* add_mesh is deferred -- succeeds */
    nt_build_result_t r = nt_builder_add_mesh(ctx, glb_path, layout, 1);
    TEST_ASSERT_EQUAL(NT_BUILD_OK, r);

    /* finish_pack fails during import */
    r = nt_builder_finish_pack(ctx);
    TEST_ASSERT_NOT_EQUAL(NT_BUILD_OK, r);
    nt_builder_free_pack(ctx);
}

void test_duplicate_path_errors(void) {
    const char *vert_path = TMP_DIR "/dup.vert";
    write_test_shader(vert_path, "precision mediump float;\n"
                                 "void main() { gl_Position = vec4(0); }\n");

    const char *pack_path = TMP_DIR "/dup_test.neopak";
    NtBuilderContext *ctx = nt_builder_start_pack(pack_path);
    TEST_ASSERT_NOT_NULL(ctx);

    nt_build_result_t r = nt_builder_add_shader(ctx, vert_path, NT_BUILD_SHADER_VERTEX);
    TEST_ASSERT_EQUAL(NT_BUILD_OK, r);

    /* Second add with same path = duplicate error */
    r = nt_builder_add_shader(ctx, vert_path, NT_BUILD_SHADER_VERTEX);
    TEST_ASSERT_EQUAL(NT_BUILD_ERR_DUPLICATE, r);

    nt_builder_free_pack(ctx);
}

void test_force_add_replaces(void) {
    const char *vert_path = TMP_DIR "/force.vert";
    write_test_shader(vert_path, "precision mediump float;\n"
                                 "void main() { gl_Position = vec4(0); }\n");

    const char *pack_path = TMP_DIR "/force_test.neopak";
    NtBuilderContext *ctx = nt_builder_start_pack(pack_path);
    TEST_ASSERT_NOT_NULL(ctx);

    nt_build_result_t r = nt_builder_add_shader(ctx, vert_path, NT_BUILD_SHADER_VERTEX);
    TEST_ASSERT_EQUAL(NT_BUILD_OK, r);

    /* Force mode: replaces without error */
    nt_builder_set_force(ctx, true);
    r = nt_builder_add_shader(ctx, vert_path, NT_BUILD_SHADER_FRAGMENT);
    nt_builder_set_force(ctx, false);
    TEST_ASSERT_EQUAL(NT_BUILD_OK, r);

    r = nt_builder_finish_pack(ctx);
    TEST_ASSERT_EQUAL(NT_BUILD_OK, r);
    nt_builder_free_pack(ctx);

    /* Verify 1 asset, stage = FRAGMENT (replaced) */
    FILE *f = fopen(pack_path, "rb");
    TEST_ASSERT_NOT_NULL(f);
    NtPackHeader hdr;
    (void)fread(&hdr, sizeof(hdr), 1, f);
    TEST_ASSERT_EQUAL_UINT16(1, hdr.asset_count);

    NtAssetEntry entry;
    (void)fread(&entry, sizeof(entry), 1, f);
    (void)fseek(f, (long)entry.offset, SEEK_SET);
    NtShaderCodeHeader shdr;
    (void)fread(&shdr, sizeof(shdr), 1, f);
    TEST_ASSERT_EQUAL_UINT8(NT_SHADER_STAGE_FRAGMENT, shdr.stage);

    (void)fclose(f);
}

void test_empty_shader_errors(void) {
    const char *vert_path = TMP_DIR "/empty.vert";
    write_test_shader(vert_path, "");

    const char *pack_path = TMP_DIR "/empty_test.neopak";
    NtBuilderContext *ctx = nt_builder_start_pack(pack_path);
    TEST_ASSERT_NOT_NULL(ctx);

    /* add is deferred -- succeeds */
    nt_build_result_t r = nt_builder_add_shader(ctx, vert_path, NT_BUILD_SHADER_VERTEX);
    TEST_ASSERT_EQUAL(NT_BUILD_OK, r);

    /* finish_pack fails during import */
    r = nt_builder_finish_pack(ctx);
    TEST_ASSERT_NOT_EQUAL(NT_BUILD_OK, r);
    nt_builder_free_pack(ctx);
}

void test_shader_with_version_errors(void) {
    const char *vert_path = TMP_DIR "/hasversion.vert";
    write_test_shader(vert_path, "#version 300 es\n"
                                 "precision mediump float;\n"
                                 "void main() { gl_Position = vec4(0); }\n");

    const char *pack_path = TMP_DIR "/hasversion_test.neopak";
    NtBuilderContext *ctx = nt_builder_start_pack(pack_path);
    TEST_ASSERT_NOT_NULL(ctx);

    /* add is deferred -- succeeds */
    nt_build_result_t r = nt_builder_add_shader(ctx, vert_path, NT_BUILD_SHADER_VERTEX);
    TEST_ASSERT_EQUAL(NT_BUILD_OK, r);

    /* finish_pack fails during import */
    r = nt_builder_finish_pack(ctx);
    TEST_ASSERT_NOT_EQUAL(NT_BUILD_OK, r);
    nt_builder_free_pack(ctx);
}

/* --- Comment stripping test --- */

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void test_shader_comment_stripping(void) {
    const char *vert_path = TMP_DIR "/comments.vert";
    write_test_shader(vert_path, "// This is a line comment\n"
                                 "precision mediump float;\n"
                                 "/* This is a\n   block comment */\n"
                                 "void main() {\n"
                                 "    gl_Position = vec4(0.0); // inline comment\n"
                                 "}\n");

    const char *pack_path = TMP_DIR "/comments_test.neopak";
    NtBuilderContext *ctx = nt_builder_start_pack(pack_path);
    TEST_ASSERT_NOT_NULL(ctx);

    nt_build_result_t r = nt_builder_add_shader(ctx, vert_path, NT_BUILD_SHADER_VERTEX);
    TEST_ASSERT_EQUAL(NT_BUILD_OK, r);

    r = nt_builder_finish_pack(ctx);
    TEST_ASSERT_EQUAL(NT_BUILD_OK, r);
    nt_builder_free_pack(ctx);

    /* Read back shader source from pack */
    FILE *f = fopen(pack_path, "rb");
    TEST_ASSERT_NOT_NULL(f);

    NtPackHeader hdr;
    (void)fread(&hdr, sizeof(hdr), 1, f);
    NtAssetEntry entry;
    (void)fread(&entry, sizeof(entry), 1, f);
    (void)fseek(f, (long)entry.offset, SEEK_SET);
    NtShaderCodeHeader shdr;
    (void)fread(&shdr, sizeof(shdr), 1, f);

    char *source = (char *)malloc(shdr.code_size);
    TEST_ASSERT_NOT_NULL(source);
    (void)fread(source, 1, shdr.code_size, f);

    /* Verify comments are stripped */
    TEST_ASSERT_NULL(strstr(source, "line comment"));
    TEST_ASSERT_NULL(strstr(source, "block comment"));
    TEST_ASSERT_NULL(strstr(source, "inline comment"));

    /* Verify code is preserved */
    TEST_ASSERT_NOT_NULL(strstr(source, "void main"));

    free(source);
    (void)fclose(f);
}

/* --- Asset alignment test --- */

void test_asset_alignment(void) {
    const char *v_path = TMP_DIR "/align_v.vert";
    const char *f_path = TMP_DIR "/align_f.frag";
    write_test_shader(v_path, "precision mediump float;\n"
                              "void main() { gl_Position = vec4(0); }\n");
    write_test_shader(f_path, "precision mediump float;\n"
                              "out vec4 c;\n"
                              "void main() { c = vec4(1); }\n");

    const char *pack_path = TMP_DIR "/align_test.neopak";
    NtBuilderContext *ctx = nt_builder_start_pack(pack_path);
    TEST_ASSERT_NOT_NULL(ctx);

    nt_build_result_t r = nt_builder_add_shader(ctx, v_path, NT_BUILD_SHADER_VERTEX);
    TEST_ASSERT_EQUAL(NT_BUILD_OK, r);
    r = nt_builder_add_shader(ctx, f_path, NT_BUILD_SHADER_FRAGMENT);
    TEST_ASSERT_EQUAL(NT_BUILD_OK, r);

    r = nt_builder_finish_pack(ctx);
    TEST_ASSERT_EQUAL(NT_BUILD_OK, r);
    nt_builder_free_pack(ctx);

    /* Read back and verify alignment */
    FILE *fp = fopen(pack_path, "rb");
    TEST_ASSERT_NOT_NULL(fp);

    NtPackHeader hdr;
    (void)fread(&hdr, sizeof(hdr), 1, fp);

    for (uint16_t i = 0; i < hdr.asset_count; i++) {
        NtAssetEntry entry;
        (void)fread(&entry, sizeof(entry), 1, fp);
        TEST_ASSERT_EQUAL_UINT32(0, entry.offset % NT_PACK_ASSET_ALIGN);
    }

    (void)fclose(fp);
}

/* --- CRC32 verification test --- */

void test_crc32_verification(void) {
    const char *vert_path = TMP_DIR "/crc_test.vert";
    write_test_shader(vert_path, "precision mediump float;\n"
                                 "void main() { gl_Position = vec4(1); }\n");

    const char *pack_path = TMP_DIR "/crc_test.neopak";
    NtBuilderContext *ctx = nt_builder_start_pack(pack_path);
    TEST_ASSERT_NOT_NULL(ctx);

    nt_build_result_t r = nt_builder_add_shader(ctx, vert_path, NT_BUILD_SHADER_VERTEX);
    TEST_ASSERT_EQUAL(NT_BUILD_OK, r);
    r = nt_builder_finish_pack(ctx);
    TEST_ASSERT_EQUAL(NT_BUILD_OK, r);
    nt_builder_free_pack(ctx);

    /* Read entire file, compute CRC32 manually, compare */
    FILE *f = fopen(pack_path, "rb");
    TEST_ASSERT_NOT_NULL(f);

    (void)fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    (void)fseek(f, 0, SEEK_SET);

    uint8_t *buf = (uint8_t *)malloc((size_t)file_size);
    TEST_ASSERT_NOT_NULL(buf);
    (void)fread(buf, 1, (size_t)file_size, f);
    (void)fclose(f);

    NtPackHeader *hdr = (NtPackHeader *)buf;
    uint32_t data_region_size = (uint32_t)file_size - hdr->header_size;
    uint32_t computed = nt_crc32(buf + hdr->header_size, data_region_size);
    TEST_ASSERT_EQUAL_HEX32(hdr->checksum, computed);

    free(buf);
}

/* --- pack_dump tests --- */

void test_dump_valid_pack(void) {
    /* Build a valid pack first */
    const char *vert_path = TMP_DIR "/dump_test.vert";
    write_test_shader(vert_path, "precision mediump float;\n"
                                 "void main() { gl_Position = vec4(0); }\n");

    const char *pack_path = TMP_DIR "/dump_test.neopak";
    NtBuilderContext *ctx = nt_builder_start_pack(pack_path);
    nt_build_result_t r = nt_builder_add_shader(ctx, vert_path, NT_BUILD_SHADER_VERTEX);
    TEST_ASSERT_EQUAL(NT_BUILD_OK, r);
    r = nt_builder_finish_pack(ctx);
    TEST_ASSERT_EQUAL(NT_BUILD_OK, r);
    nt_builder_free_pack(ctx);

    /* Dump should succeed */
    r = nt_builder_dump_pack(pack_path);
    TEST_ASSERT_EQUAL(NT_BUILD_OK, r);
}

void test_dump_invalid_file_errors(void) {
    /* Write garbage data */
    const char *bad_path = TMP_DIR "/bad.neopak";
    FILE *f = fopen(bad_path, "wb");
    TEST_ASSERT_NOT_NULL(f);
    uint8_t garbage[] = {0xDE, 0xAD, 0xBE, 0xEF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    (void)fwrite(garbage, 1, sizeof(garbage), f);
    (void)fclose(f);

    nt_build_result_t r = nt_builder_dump_pack(bad_path);
    TEST_ASSERT_NOT_EQUAL(NT_BUILD_OK, r);
}

/* --- Multi-asset pack test --- */

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void test_multi_asset_pack(void) {
    const char *glb_path = TMP_DIR "/multi_tri.glb";
    const char *png_path = TMP_DIR "/multi_tex.png";
    const char *vert_path = TMP_DIR "/multi.vert";
    write_test_glb(glb_path);
    write_test_png(png_path);
    write_test_shader(vert_path, "precision mediump float;\n"
                                 "layout(location = 0) in vec3 a_pos;\n"
                                 "void main() { gl_Position = vec4(a_pos, 1.0); }\n");

    NtStreamLayout layout[] = {
        {"position", "POSITION", NT_STREAM_FLOAT32, 3, false},
    };

    const char *pack_path = TMP_DIR "/multi_test.neopak";
    NtBuilderContext *ctx = nt_builder_start_pack(pack_path);
    TEST_ASSERT_NOT_NULL(ctx);

    nt_build_result_t r = nt_builder_add_mesh(ctx, glb_path, layout, 1);
    TEST_ASSERT_EQUAL(NT_BUILD_OK, r);

    r = nt_builder_add_texture(ctx, png_path);
    TEST_ASSERT_EQUAL(NT_BUILD_OK, r);

    r = nt_builder_add_shader(ctx, vert_path, NT_BUILD_SHADER_VERTEX);
    TEST_ASSERT_EQUAL(NT_BUILD_OK, r);

    r = nt_builder_finish_pack(ctx);
    TEST_ASSERT_EQUAL(NT_BUILD_OK, r);
    nt_builder_free_pack(ctx);

    /* Read back and verify */
    FILE *f = fopen(pack_path, "rb");
    TEST_ASSERT_NOT_NULL(f);

    NtPackHeader hdr;
    (void)fread(&hdr, sizeof(hdr), 1, f);
    TEST_ASSERT_EQUAL_UINT32(NT_PACK_MAGIC, hdr.magic);
    TEST_ASSERT_EQUAL_UINT16(3, hdr.asset_count);

    /* Verify all 3 asset types present */
    bool has_mesh = false;
    bool has_texture = false;
    bool has_shader = false;
    for (uint16_t i = 0; i < hdr.asset_count; i++) {
        NtAssetEntry entry;
        (void)fread(&entry, sizeof(entry), 1, f);
        if (entry.asset_type == NT_ASSET_MESH) {
            has_mesh = true;
        }
        if (entry.asset_type == NT_ASSET_TEXTURE) {
            has_texture = true;
        }
        if (entry.asset_type == NT_ASSET_SHADER_CODE) {
            has_shader = true;
        }
        /* Verify alignment */
        TEST_ASSERT_EQUAL_UINT32(0, entry.offset % NT_PACK_ASSET_ALIGN);
    }
    TEST_ASSERT_TRUE(has_mesh);
    TEST_ASSERT_TRUE(has_texture);
    TEST_ASSERT_TRUE(has_shader);

    /* Verify CRC32 */
    (void)fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    (void)fseek(f, 0, SEEK_SET);
    uint8_t *buf = (uint8_t *)malloc((size_t)file_size);
    (void)fread(buf, 1, (size_t)file_size, f);
    uint32_t data_size = (uint32_t)file_size - hdr.header_size;
    uint32_t computed_crc = nt_crc32(buf + hdr.header_size, data_size);
    TEST_ASSERT_EQUAL_HEX32(hdr.checksum, computed_crc);
    free(buf);

    (void)fclose(f);
}

/* --- Shader stage test --- */

void test_shader_stage_correct(void) {
    const char *frag_path = TMP_DIR "/stage.frag";
    write_test_shader(frag_path, "precision mediump float;\n"
                                 "out vec4 c;\n"
                                 "void main() { c = vec4(1); }\n");

    const char *pack_path = TMP_DIR "/stage_test.neopak";
    NtBuilderContext *ctx = nt_builder_start_pack(pack_path);
    nt_build_result_t r = nt_builder_add_shader(ctx, frag_path, NT_BUILD_SHADER_FRAGMENT);
    TEST_ASSERT_EQUAL(NT_BUILD_OK, r);
    r = nt_builder_finish_pack(ctx);
    TEST_ASSERT_EQUAL(NT_BUILD_OK, r);
    nt_builder_free_pack(ctx);

    /* Read back and verify stage */
    FILE *f = fopen(pack_path, "rb");
    TEST_ASSERT_NOT_NULL(f);

    NtPackHeader hdr;
    (void)fread(&hdr, sizeof(hdr), 1, f);
    NtAssetEntry entry;
    (void)fread(&entry, sizeof(entry), 1, f);
    (void)fseek(f, (long)entry.offset, SEEK_SET);
    NtShaderCodeHeader shdr;
    (void)fread(&shdr, sizeof(shdr), 1, f);

    TEST_ASSERT_EQUAL_UINT32(NT_SHADER_CODE_MAGIC, shdr.magic);
    TEST_ASSERT_EQUAL_UINT8(NT_SHADER_STAGE_FRAGMENT, shdr.stage);
    TEST_ASSERT_TRUE(shdr.code_size > 0);

    (void)fclose(f);
}

/* --- Glob batch test using fixture shaders --- */

void test_glob_shaders(void) {
    /* Use the test fixtures directory which has .vert and .frag files */
    const char *pack_path = TMP_DIR "/glob_test.neopak";
    NtBuilderContext *ctx = nt_builder_start_pack(pack_path);
    TEST_ASSERT_NOT_NULL(ctx);

    nt_build_result_t r = nt_builder_add_shaders(ctx, "tests/fixtures/*.vert", NT_BUILD_SHADER_VERTEX);
    TEST_ASSERT_EQUAL(NT_BUILD_OK, r);

    r = nt_builder_add_shaders(ctx, "tests/fixtures/*.frag", NT_BUILD_SHADER_FRAGMENT);
    TEST_ASSERT_EQUAL(NT_BUILD_OK, r);

    r = nt_builder_finish_pack(ctx);
    TEST_ASSERT_EQUAL(NT_BUILD_OK, r);
    nt_builder_free_pack(ctx);

    /* Verify pack has 2 assets */
    FILE *f = fopen(pack_path, "rb");
    TEST_ASSERT_NOT_NULL(f);

    NtPackHeader hdr;
    (void)fread(&hdr, sizeof(hdr), 1, f);
    TEST_ASSERT_EQUAL_UINT16(2, hdr.asset_count);

    (void)fclose(f);
}

int main(void) {
    UNITY_BEGIN();

    /* FNV-1a hash tests */
    RUN_TEST(test_hash_known_value);
    RUN_TEST(test_hash_path_normalization);
    RUN_TEST(test_hash_different_strings_differ);

    /* Pack writer core */
    RUN_TEST(test_start_pack_returns_context);

    /* Round-trip tests */
    RUN_TEST(test_shader_round_trip);
    RUN_TEST(test_texture_round_trip);
    RUN_TEST(test_mesh_round_trip);

    /* Validation errors */
    RUN_TEST(test_missing_position_attribute_errors);
    RUN_TEST(test_duplicate_path_errors);
    RUN_TEST(test_force_add_replaces);
    RUN_TEST(test_empty_shader_errors);
    RUN_TEST(test_shader_with_version_errors);

    /* Comment stripping */
    RUN_TEST(test_shader_comment_stripping);

    /* Alignment and CRC32 */
    RUN_TEST(test_asset_alignment);
    RUN_TEST(test_crc32_verification);

    /* Dump utility */
    RUN_TEST(test_dump_valid_pack);
    RUN_TEST(test_dump_invalid_file_errors);

    /* Multi-asset and stage */
    RUN_TEST(test_multi_asset_pack);
    RUN_TEST(test_shader_stage_correct);

    /* Glob batch */
    RUN_TEST(test_glob_shaders);

    return UNITY_END();
}
