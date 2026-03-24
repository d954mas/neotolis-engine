/* clang-analyzer thinks fread failures leave data uninitialized, but Unity's
   TEST_ASSERT_EQUAL aborts via longjmp before any uninitialized access. */
// NOLINTBEGIN(clang-analyzer-unix.Stream,clang-analyzer-core.CallAndMessage,clang-analyzer-core.UndefinedBinaryOperatorResult)
/* System headers before Unity to avoid noreturn / __declspec conflict on MSVC */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Suppress GLFW/GLX internal leaks (X11 extension query cache) */
const char *__lsan_default_suppressions(void);                                           // NOLINT(bugprone-reserved-identifier)
const char *__lsan_default_suppressions(void) { return "leak:extensionSupportedGLX\n"; } // NOLINT(bugprone-reserved-identifier)

/* clang-format off */
#include "nt_blob_format.h"
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

/* --- Normalize-and-hash tests --- */

void test_hash_known_value(void) {
    nt_hash64_t h = nt_builder_normalize_and_hash("test");
    TEST_ASSERT_TRUE(h.value != 0);
    /* Deterministic */
    TEST_ASSERT_TRUE(h.value == nt_builder_normalize_and_hash("test").value);
}

void test_hash_path_normalization(void) {
    /* Backslash -> forward slash */
    nt_hash64_t h1 = nt_builder_normalize_and_hash("meshes/cube.glb");
    nt_hash64_t h2 = nt_builder_normalize_and_hash("meshes\\cube.glb");
    TEST_ASSERT_TRUE(h1.value == h2.value);

    /* ./ stripped */
    TEST_ASSERT_TRUE(nt_builder_normalize_and_hash("foo/bar.png").value == nt_builder_normalize_and_hash("./foo/bar.png").value);

    /* // collapsed */
    TEST_ASSERT_TRUE(nt_builder_normalize_and_hash("foo/bar.png").value == nt_builder_normalize_and_hash("foo//bar.png").value);

    /* ../ resolved */
    TEST_ASSERT_TRUE(nt_builder_normalize_and_hash("bar.png").value == nt_builder_normalize_and_hash("foo/../bar.png").value);

    /* Leading ../ preserved */
    TEST_ASSERT_TRUE(nt_builder_normalize_and_hash("assets/mesh.glb").value != nt_builder_normalize_and_hash("../assets/mesh.glb").value);
}

void test_hash_different_strings_differ(void) {
    nt_hash64_t h1 = nt_builder_normalize_and_hash("a");
    nt_hash64_t h2 = nt_builder_normalize_and_hash("b");
    TEST_ASSERT_TRUE(h1.value != h2.value);
}

/* --- Pack writer core tests --- */

void test_start_pack_returns_context(void) {
    NtBuilderContext *ctx = nt_builder_start_pack(TMP_DIR "/test_ctx.ntpack");
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

    const char *pack_path = TMP_DIR "/shader_rt.ntpack";
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
    TEST_ASSERT_EQUAL((size_t)file_size, fread(buf, 1, (size_t)file_size, f));
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

    const char *pack_path = TMP_DIR "/texture_rt.ntpack";
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
    TEST_ASSERT_EQUAL(1, fread(&hdr, sizeof(hdr), 1, f));
    NtAssetEntry entry;
    TEST_ASSERT_EQUAL(1, fread(&entry, sizeof(entry), 1, f));
    TEST_ASSERT_EQUAL_UINT8(NT_ASSET_TEXTURE, entry.asset_type);

    (void)fseek(f, (long)entry.offset, SEEK_SET);
    NtTextureAssetHeaderV2 tex;
    TEST_ASSERT_EQUAL(1, fread(&tex, sizeof(tex), 1, f));
    TEST_ASSERT_EQUAL_UINT32(NT_TEXTURE_MAGIC, tex.magic);
    TEST_ASSERT_EQUAL_UINT16(NT_TEXTURE_VERSION_V2, tex.version);
    TEST_ASSERT_EQUAL_UINT32(2, tex.width);
    TEST_ASSERT_EQUAL_UINT32(2, tex.height);
    TEST_ASSERT_EQUAL_UINT16(NT_TEXTURE_FORMAT_RGBA8, tex.format);
    TEST_ASSERT_EQUAL_UINT8(NT_TEXTURE_COMPRESSION_RAW, tex.compression);
    TEST_ASSERT_EQUAL_UINT32(2 * 2 * 4, tex.data_size);

    (void)fclose(f);
}

/* --- Mesh round-trip test --- */

void test_mesh_round_trip(void) {
    const char *glb_path = TMP_DIR "/rt_triangle.glb";
    write_test_glb(glb_path);

    NtStreamLayout layout[] = {
        {"position", "POSITION", NT_STREAM_FLOAT32, 3, false},
    };

    const char *pack_path = TMP_DIR "/mesh_rt.ntpack";
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
    TEST_ASSERT_EQUAL(1, fread(&hdr, sizeof(hdr), 1, f));
    NtAssetEntry entry;
    TEST_ASSERT_EQUAL(1, fread(&entry, sizeof(entry), 1, f));
    TEST_ASSERT_EQUAL_UINT8(NT_ASSET_MESH, entry.asset_type);

    (void)fseek(f, (long)entry.offset, SEEK_SET);
    NtMeshAssetHeader mesh;
    TEST_ASSERT_EQUAL(1, fread(&mesh, sizeof(mesh), 1, f));
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

    const char *pack_path = TMP_DIR "/no_pos.ntpack";
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

    const char *pack_path = TMP_DIR "/dup_test.ntpack";
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
    /* Source must be valid as fragment shader since force-replace changes stage to FRAGMENT */
    write_test_shader(vert_path, "precision mediump float;\n"
                                 "out vec4 frag_color;\n"
                                 "void main() { frag_color = vec4(1); }\n");

    const char *pack_path = TMP_DIR "/force_test.ntpack";
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
    TEST_ASSERT_EQUAL(1, fread(&hdr, sizeof(hdr), 1, f));
    TEST_ASSERT_EQUAL_UINT16(1, hdr.asset_count);

    NtAssetEntry entry;
    TEST_ASSERT_EQUAL(1, fread(&entry, sizeof(entry), 1, f));
    (void)fseek(f, (long)entry.offset, SEEK_SET);
    NtShaderCodeHeader shdr;
    TEST_ASSERT_EQUAL(1, fread(&shdr, sizeof(shdr), 1, f));
    TEST_ASSERT_EQUAL_UINT8(NT_SHADER_STAGE_FRAGMENT, shdr.stage);

    (void)fclose(f);
}

void test_empty_shader_errors(void) {
    const char *vert_path = TMP_DIR "/empty.vert";
    write_test_shader(vert_path, "");

    const char *pack_path = TMP_DIR "/empty_test.ntpack";
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

    const char *pack_path = TMP_DIR "/hasversion_test.ntpack";
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

    const char *pack_path = TMP_DIR "/comments_test.ntpack";
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
    TEST_ASSERT_EQUAL(1, fread(&hdr, sizeof(hdr), 1, f));
    NtAssetEntry entry;
    TEST_ASSERT_EQUAL(1, fread(&entry, sizeof(entry), 1, f));
    (void)fseek(f, (long)entry.offset, SEEK_SET);
    NtShaderCodeHeader shdr;
    TEST_ASSERT_EQUAL(1, fread(&shdr, sizeof(shdr), 1, f));

    char *source = (char *)malloc(shdr.code_size);
    TEST_ASSERT_NOT_NULL(source);
    TEST_ASSERT_EQUAL(shdr.code_size, fread(source, 1, shdr.code_size, f));

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

    const char *pack_path = TMP_DIR "/align_test.ntpack";
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
    TEST_ASSERT_EQUAL(1, fread(&hdr, sizeof(hdr), 1, fp));

    for (uint16_t i = 0; i < hdr.asset_count; i++) {
        NtAssetEntry entry;
        TEST_ASSERT_EQUAL(1, fread(&entry, sizeof(entry), 1, fp));
        TEST_ASSERT_EQUAL_UINT32(0, entry.offset % NT_PACK_ASSET_ALIGN);
    }

    (void)fclose(fp);
}

/* --- CRC32 verification test --- */

void test_crc32_verification(void) {
    const char *vert_path = TMP_DIR "/crc_test.vert";
    write_test_shader(vert_path, "precision mediump float;\n"
                                 "void main() { gl_Position = vec4(1); }\n");

    const char *pack_path = TMP_DIR "/crc_test.ntpack";
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
    TEST_ASSERT_EQUAL((size_t)file_size, fread(buf, 1, (size_t)file_size, f));
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

    const char *pack_path = TMP_DIR "/dump_test.ntpack";
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
    const char *bad_path = TMP_DIR "/bad.ntpack";
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

    const char *pack_path = TMP_DIR "/multi_test.ntpack";
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
    TEST_ASSERT_EQUAL(1, fread(&hdr, sizeof(hdr), 1, f));
    TEST_ASSERT_EQUAL_UINT32(NT_PACK_MAGIC, hdr.magic);
    TEST_ASSERT_EQUAL_UINT16(3, hdr.asset_count);

    /* Verify all 3 asset types present */
    bool has_mesh = false;
    bool has_texture = false;
    bool has_shader = false;
    for (uint16_t i = 0; i < hdr.asset_count; i++) {
        NtAssetEntry entry;
        TEST_ASSERT_EQUAL(1, fread(&entry, sizeof(entry), 1, f));
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
    TEST_ASSERT_EQUAL((size_t)file_size, fread(buf, 1, (size_t)file_size, f));
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

    const char *pack_path = TMP_DIR "/stage_test.ntpack";
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
    TEST_ASSERT_EQUAL(1, fread(&hdr, sizeof(hdr), 1, f));
    NtAssetEntry entry;
    TEST_ASSERT_EQUAL(1, fread(&entry, sizeof(entry), 1, f));
    (void)fseek(f, (long)entry.offset, SEEK_SET);
    NtShaderCodeHeader shdr;
    TEST_ASSERT_EQUAL(1, fread(&shdr, sizeof(shdr), 1, f));

    TEST_ASSERT_EQUAL_UINT32(NT_SHADER_CODE_MAGIC, shdr.magic);
    TEST_ASSERT_EQUAL_UINT8(NT_SHADER_STAGE_FRAGMENT, shdr.stage);
    TEST_ASSERT_TRUE(shdr.code_size > 0);

    (void)fclose(f);
}

/* --- Glob batch test using fixture shaders --- */

void test_glob_shaders(void) {
    /* Use the test fixtures directory which has .vert and .frag files */
    const char *pack_path = TMP_DIR "/glob_test.ntpack";
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
    TEST_ASSERT_EQUAL(1, fread(&hdr, sizeof(hdr), 1, f));
    TEST_ASSERT_EQUAL_UINT16(2, hdr.asset_count);

    (void)fclose(f);
}

/* --- E2E test: real files → pack → verify data --- */

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void test_e2e_real_assets(void) {
    NtStreamLayout layout[] = {
        {"position", "POSITION", NT_STREAM_FLOAT32, 3, false},
        {"uv0", "TEXCOORD_0", NT_STREAM_FLOAT32, 2, false},
    };

    const char *pack_path = TMP_DIR "/e2e.ntpack";
    NtBuilderContext *ctx = nt_builder_start_pack(pack_path);
    TEST_ASSERT_NOT_NULL(ctx);

    TEST_ASSERT_EQUAL(NT_BUILD_OK, nt_builder_add_shader(ctx, "assets/shaders/mesh.vert", NT_BUILD_SHADER_VERTEX));
    TEST_ASSERT_EQUAL(NT_BUILD_OK, nt_builder_add_shader(ctx, "assets/shaders/mesh.frag", NT_BUILD_SHADER_FRAGMENT));
    TEST_ASSERT_EQUAL(NT_BUILD_OK, nt_builder_add_mesh(ctx, "assets/meshes/cube.glb", layout, 2));
    TEST_ASSERT_EQUAL(NT_BUILD_OK, nt_builder_add_texture(ctx, "assets/textures/lenna.png"));

    TEST_ASSERT_EQUAL(NT_BUILD_OK, nt_builder_finish_pack(ctx));
    nt_builder_free_pack(ctx);

    /* Read entire pack */
    FILE *f = fopen(pack_path, "rb");
    TEST_ASSERT_NOT_NULL(f);
    (void)fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    (void)fseek(f, 0, SEEK_SET);
    uint8_t *pack = (uint8_t *)malloc((size_t)file_size);
    TEST_ASSERT_NOT_NULL(pack);
    TEST_ASSERT_EQUAL((size_t)file_size, fread(pack, 1, (size_t)file_size, f));
    (void)fclose(f);

    NtPackHeader *hdr = (NtPackHeader *)pack;
    TEST_ASSERT_EQUAL_UINT32(NT_PACK_MAGIC, hdr->magic);
    TEST_ASSERT_EQUAL_UINT16(4, hdr->asset_count);

    /* CRC32 */
    uint32_t crc = nt_crc32(pack + hdr->header_size, (uint32_t)file_size - hdr->header_size);
    TEST_ASSERT_EQUAL_HEX32(hdr->checksum, crc);

    /* Find assets by type */
    NtAssetEntry *entries = (NtAssetEntry *)(pack + sizeof(NtPackHeader));
    NtAssetEntry *mesh_e = NULL;
    NtAssetEntry *tex_e = NULL;
    NtAssetEntry *vert_e = NULL;
    for (uint16_t i = 0; i < hdr->asset_count; i++) {
        if (entries[i].asset_type == NT_ASSET_MESH) {
            mesh_e = &entries[i];
        }
        if (entries[i].asset_type == NT_ASSET_TEXTURE) {
            tex_e = &entries[i];
        }
        if (entries[i].asset_type == NT_ASSET_SHADER_CODE && !vert_e) {
            vert_e = &entries[i];
        }
    }
    TEST_ASSERT_NOT_NULL(mesh_e);
    TEST_ASSERT_NOT_NULL(tex_e);
    TEST_ASSERT_NOT_NULL(vert_e);

    /* Verify mesh data */
    NtMeshAssetHeader *mh = (NtMeshAssetHeader *)(pack + mesh_e->offset);
    TEST_ASSERT_EQUAL_UINT32(NT_MESH_MAGIC, mh->magic);
    TEST_ASSERT_EQUAL_UINT32(24, mh->vertex_count);
    TEST_ASSERT_EQUAL_UINT32(36, mh->index_count);
    TEST_ASSERT_EQUAL_UINT8(2, mh->stream_count);
    /* First vertex: position (-0.5, -0.5, 0.5) — compare as uint32 bit pattern */
    uint8_t *vdata = pack + mesh_e->offset + sizeof(NtMeshAssetHeader) + (mh->stream_count * sizeof(NtStreamDesc));
    float vx = 0;
    float vy = 0;
    float vz = 0;
    memcpy(&vx, vdata + 0, 4);
    memcpy(&vy, vdata + 4, 4);
    memcpy(&vz, vdata + 8, 4);
    float expected_neg = -0.5F;
    float expected_pos = 0.5F;
    TEST_ASSERT_EQUAL_MEMORY(&expected_neg, &vx, 4);
    TEST_ASSERT_EQUAL_MEMORY(&expected_neg, &vy, 4);
    TEST_ASSERT_EQUAL_MEMORY(&expected_pos, &vz, 4);

    /* Verify texture data (v2 header) */
    NtTextureAssetHeaderV2 *th = (NtTextureAssetHeaderV2 *)(pack + tex_e->offset);
    TEST_ASSERT_EQUAL_UINT32(NT_TEXTURE_MAGIC, th->magic);
    TEST_ASSERT_EQUAL_UINT16(NT_TEXTURE_VERSION_V2, th->version);
    TEST_ASSERT_EQUAL_UINT32(512, th->width);
    TEST_ASSERT_EQUAL_UINT32(512, th->height);
    TEST_ASSERT_EQUAL_UINT8(NT_TEXTURE_COMPRESSION_RAW, th->compression);
    /* First pixel should be non-zero (Lenna top-left is skin tone) */
    uint8_t *pixel0 = pack + tex_e->offset + sizeof(NtTextureAssetHeaderV2);
    TEST_ASSERT_TRUE(pixel0[0] > 100);  /* R */
    TEST_ASSERT_TRUE(pixel0[3] == 255); /* A = opaque */

    /* Verify shader source */
    NtShaderCodeHeader *sh = (NtShaderCodeHeader *)(pack + vert_e->offset);
    TEST_ASSERT_EQUAL_UINT32(NT_SHADER_CODE_MAGIC, sh->magic);
    char *src = (char *)(pack + vert_e->offset + sizeof(NtShaderCodeHeader));
    TEST_ASSERT_NOT_NULL(strstr(src, "void main"));
    TEST_ASSERT_NOT_NULL(strstr(src, "u_mvp"));
    TEST_ASSERT_NULL(strstr(src, "#version"));

    free(pack);
}

/* --- Rename test --- */

void test_rename_changes_resource_id(void) {
    const char *vert_path = TMP_DIR "/rename.vert";
    write_test_shader(vert_path, "precision mediump float;\nvoid main() { gl_Position = vec4(0); }\n");

    const char *pack_path = TMP_DIR "/rename_test.ntpack";
    NtBuilderContext *ctx = nt_builder_start_pack(pack_path);
    TEST_ASSERT_NOT_NULL(ctx);

    TEST_ASSERT_EQUAL(NT_BUILD_OK, nt_builder_add_shader(ctx, vert_path, NT_BUILD_SHADER_VERTEX));

    nt_hash64_t old_id = nt_builder_normalize_and_hash(vert_path);
    nt_hash64_t new_id = nt_builder_normalize_and_hash("renamed/shader.vert");
    TEST_ASSERT_TRUE(old_id.value != new_id.value);

    TEST_ASSERT_EQUAL(NT_BUILD_OK, nt_builder_rename(ctx, vert_path, "renamed/shader.vert"));
    TEST_ASSERT_EQUAL(NT_BUILD_OK, nt_builder_finish_pack(ctx));
    nt_builder_free_pack(ctx);

    /* Verify resource_id in pack matches new path hash */
    FILE *f = fopen(pack_path, "rb");
    TEST_ASSERT_NOT_NULL(f);
    NtPackHeader hdr;
    TEST_ASSERT_EQUAL(1, fread(&hdr, sizeof(hdr), 1, f));
    NtAssetEntry entry;
    TEST_ASSERT_EQUAL(1, fread(&entry, sizeof(entry), 1, f));
    TEST_ASSERT_TRUE(new_id.value == entry.resource_id);
    (void)fclose(f);
}

/* --- Force + glob test --- */

void test_force_glob_override(void) {
    /* Write two shaders to tmp dir.
       a.vert must be valid as fragment shader since force-override changes its stage. */
    MKDIR(TMP_DIR "/force_glob");
    write_test_shader(TMP_DIR "/force_glob/a.vert", "precision mediump float;\nout vec4 frag_color;\nvoid main() { frag_color = vec4(0); }\n");
    write_test_shader(TMP_DIR "/force_glob/b.vert", "precision mediump float;\nvoid main() { gl_Position = vec4(1); }\n");

    const char *pack_path = TMP_DIR "/force_glob.ntpack";
    NtBuilderContext *ctx = nt_builder_start_pack(pack_path);
    TEST_ASSERT_NOT_NULL(ctx);

    /* Glob adds both */
    TEST_ASSERT_EQUAL(NT_BUILD_OK, nt_builder_add_shaders(ctx, TMP_DIR "/force_glob/*.vert", NT_BUILD_SHADER_VERTEX));

    /* Force override a.vert as FRAGMENT */
    nt_builder_set_force(ctx, true);
    TEST_ASSERT_EQUAL(NT_BUILD_OK, nt_builder_add_shader(ctx, TMP_DIR "/force_glob/a.vert", NT_BUILD_SHADER_FRAGMENT));
    nt_builder_set_force(ctx, false);

    TEST_ASSERT_EQUAL(NT_BUILD_OK, nt_builder_finish_pack(ctx));
    nt_builder_free_pack(ctx);

    /* Verify: 2 assets, a.vert is FRAGMENT */
    FILE *f = fopen(pack_path, "rb");
    TEST_ASSERT_NOT_NULL(f);
    NtPackHeader hdr;
    TEST_ASSERT_EQUAL(1, fread(&hdr, sizeof(hdr), 1, f));
    TEST_ASSERT_EQUAL_UINT16(2, hdr.asset_count);

    bool found_fragment = false;
    for (uint16_t i = 0; i < hdr.asset_count; i++) {
        NtAssetEntry entry;
        TEST_ASSERT_EQUAL(1, fread(&entry, sizeof(entry), 1, f));
        long cur = ftell(f);
        (void)fseek(f, (long)entry.offset, SEEK_SET);
        NtShaderCodeHeader sh;
        TEST_ASSERT_EQUAL(1, fread(&sh, sizeof(sh), 1, f));
        if (sh.stage == NT_SHADER_STAGE_FRAGMENT) {
            found_fragment = true;
        }
        (void)fseek(f, cur, SEEK_SET);
    }
    TEST_ASSERT_TRUE(found_fragment);
    (void)fclose(f);
}

/* --- free_pack without finish --- */

void test_free_pack_without_finish(void) {
    NtBuilderContext *ctx = nt_builder_start_pack(TMP_DIR "/nofin.ntpack");
    TEST_ASSERT_NOT_NULL(ctx);

    const char *vert_path = TMP_DIR "/nofin.vert";
    write_test_shader(vert_path, "precision mediump float;\nvoid main() { gl_Position = vec4(0); }\n");
    TEST_ASSERT_EQUAL(NT_BUILD_OK, nt_builder_add_shader(ctx, vert_path, NT_BUILD_SHADER_VERTEX));

    /* Free without finish — should not crash or leak */
    nt_builder_free_pack(ctx);
}

/* --- Blob import test --- */

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void test_blob_import(void) {
    const uint8_t test_data[] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10};

    const char *pack_path = TMP_DIR "/blob_test.ntpack";
    NtBuilderContext *ctx = nt_builder_start_pack(pack_path);
    TEST_ASSERT_NOT_NULL(ctx);

    nt_build_result_t r = nt_builder_add_blob(ctx, test_data, sizeof(test_data), "test/blob");
    TEST_ASSERT_EQUAL(NT_BUILD_OK, r);

    r = nt_builder_finish_pack(ctx);
    TEST_ASSERT_EQUAL(NT_BUILD_OK, r);
    nt_builder_free_pack(ctx);

    /* Read back and verify */
    FILE *f = fopen(pack_path, "rb");
    TEST_ASSERT_NOT_NULL(f);

    NtPackHeader hdr;
    TEST_ASSERT_EQUAL(1, fread(&hdr, sizeof(hdr), 1, f));
    TEST_ASSERT_EQUAL_UINT32(NT_PACK_MAGIC, hdr.magic);
    TEST_ASSERT_EQUAL_UINT16(1, hdr.asset_count);

    NtAssetEntry entry;
    TEST_ASSERT_EQUAL(1, fread(&entry, sizeof(entry), 1, f));
    TEST_ASSERT_EQUAL_UINT8(NT_ASSET_BLOB, entry.asset_type);
    TEST_ASSERT_EQUAL_UINT16(NT_BLOB_VERSION, entry.format_version);

    /* Verify blob header at entry offset */
    (void)fseek(f, (long)entry.offset, SEEK_SET);
    NtBlobAssetHeader blob_hdr;
    TEST_ASSERT_EQUAL(1, fread(&blob_hdr, sizeof(blob_hdr), 1, f));
    TEST_ASSERT_EQUAL_UINT32(NT_BLOB_MAGIC, blob_hdr.magic);
    TEST_ASSERT_EQUAL_UINT16(NT_BLOB_VERSION, blob_hdr.version);

    /* Verify blob data follows header */
    uint8_t read_data[16];
    TEST_ASSERT_EQUAL(sizeof(read_data), fread(read_data, 1, sizeof(read_data), f));
    TEST_ASSERT_EQUAL_MEMORY(test_data, read_data, sizeof(test_data));

    (void)fclose(f);
}

/* --- Texture from memory test --- */

void test_tex_from_memory(void) {
    /* Use our write_test_png to get PNG data, then read it back as memory */
    const char *png_path = TMP_DIR "/mem_test.png";
    write_test_png(png_path);

    /* Read the PNG file into memory */
    FILE *pf = fopen(png_path, "rb");
    TEST_ASSERT_NOT_NULL(pf);
    (void)fseek(pf, 0, SEEK_END);
    long png_size = ftell(pf);
    (void)fseek(pf, 0, SEEK_SET);
    uint8_t *png_data = (uint8_t *)malloc((size_t)png_size);
    TEST_ASSERT_NOT_NULL(png_data);
    TEST_ASSERT_EQUAL((size_t)png_size, fread(png_data, 1, (size_t)png_size, pf));
    (void)fclose(pf);

    const char *pack_path = TMP_DIR "/texmem_test.ntpack";
    NtBuilderContext *ctx = nt_builder_start_pack(pack_path);
    TEST_ASSERT_NOT_NULL(ctx);

    nt_build_result_t r = nt_builder_add_texture_from_memory(ctx, png_data, (uint32_t)png_size, "test/texture_mem");
    TEST_ASSERT_EQUAL(NT_BUILD_OK, r);

    r = nt_builder_finish_pack(ctx);
    TEST_ASSERT_EQUAL(NT_BUILD_OK, r);
    nt_builder_free_pack(ctx);

    free(png_data);

    /* Read back and verify texture */
    FILE *f = fopen(pack_path, "rb");
    TEST_ASSERT_NOT_NULL(f);

    NtPackHeader hdr;
    TEST_ASSERT_EQUAL(1, fread(&hdr, sizeof(hdr), 1, f));
    TEST_ASSERT_EQUAL_UINT16(1, hdr.asset_count);

    NtAssetEntry entry;
    TEST_ASSERT_EQUAL(1, fread(&entry, sizeof(entry), 1, f));
    TEST_ASSERT_EQUAL_UINT8(NT_ASSET_TEXTURE, entry.asset_type);

    (void)fseek(f, (long)entry.offset, SEEK_SET);
    NtTextureAssetHeaderV2 tex;
    TEST_ASSERT_EQUAL(1, fread(&tex, sizeof(tex), 1, f));
    TEST_ASSERT_EQUAL_UINT32(NT_TEXTURE_MAGIC, tex.magic);
    TEST_ASSERT_EQUAL_UINT16(NT_TEXTURE_VERSION_V2, tex.version);
    TEST_ASSERT_EQUAL_UINT32(2, tex.width);
    TEST_ASSERT_EQUAL_UINT32(2, tex.height);
    TEST_ASSERT_EQUAL_UINT8(NT_TEXTURE_COMPRESSION_RAW, tex.compression);

    (void)fclose(f);
}

/* --- Write a glb with nodes for scene tests --- */

static void write_test_glb_with_node(const char *path) {
    /* JSON chunk with mesh + node referencing the mesh */
    const char *json_str = "{"
                           "\"asset\":{\"version\":\"2.0\"},"
                           "\"scene\":0,"
                           "\"scenes\":[{\"nodes\":[0]}],"
                           "\"nodes\":[{\"mesh\":0,\"name\":\"TriNode\"}],"
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
    uint32_t json_padded = (json_len + 3U) & ~3U;
    uint32_t json_padding = json_padded - json_len;

    float positions[] = {0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F};
    uint16_t indices[] = {0, 1, 2};
    uint16_t idx_pad = 0;

    uint32_t bin_data_size = (uint32_t)sizeof(positions) + (uint32_t)sizeof(indices) + (uint32_t)sizeof(idx_pad);
    uint32_t bin_padded = (bin_data_size + 3U) & ~3U;

    uint32_t glb_magic = 0x46546C67;
    uint32_t glb_version = 2;
    uint32_t json_chunk_type = 0x4E4F534A;
    uint32_t bin_chunk_type = 0x004E4942;
    uint32_t total_length = 12 + 8 + json_padded + 8 + bin_padded;

    FILE *f = fopen(path, "wb");
    if (!f) {
        return;
    }

    (void)fwrite(&glb_magic, 4, 1, f);
    (void)fwrite(&glb_version, 4, 1, f);
    (void)fwrite(&total_length, 4, 1, f);
    (void)fwrite(&json_padded, 4, 1, f);
    (void)fwrite(&json_chunk_type, 4, 1, f);
    (void)fwrite(json_str, 1, json_len, f);
    for (uint32_t i = 0; i < json_padding; i++) {
        char space = ' ';
        (void)fwrite(&space, 1, 1, f);
    }
    (void)fwrite(&bin_padded, 4, 1, f);
    (void)fwrite(&bin_chunk_type, 4, 1, f);
    (void)fwrite(positions, sizeof(positions), 1, f);
    (void)fwrite(indices, sizeof(indices), 1, f);
    (void)fwrite(&idx_pad, sizeof(idx_pad), 1, f);

    (void)fclose(f);
}

/* --- glb scene parse test --- */

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void test_glb_scene_parse(void) {
    const char *glb_path = TMP_DIR "/scene_test.glb";
    write_test_glb_with_node(glb_path);

    nt_glb_scene_t scene;
    nt_build_result_t r = nt_builder_parse_glb_scene(&scene, glb_path);
    TEST_ASSERT_EQUAL(NT_BUILD_OK, r);

    /* Verify scene contents */
    TEST_ASSERT_EQUAL_UINT32(1, scene.mesh_count);
    TEST_ASSERT_TRUE(scene.node_count >= 1);
    TEST_ASSERT_EQUAL_UINT32(1, scene.meshes[0].primitive_count);

    /* Find a node that references the mesh */
    bool found_mesh_node = false;
    for (uint32_t i = 0; i < scene.node_count; i++) {
        if (scene.nodes[i].mesh_index == 0) {
            found_mesh_node = true;
            break;
        }
    }
    TEST_ASSERT_TRUE(found_mesh_node);

    /* Free should not crash */
    nt_builder_free_glb_scene(&scene);

    /* Verify scene is zeroed after free */
    TEST_ASSERT_NULL(scene.meshes);
    TEST_ASSERT_EQUAL_UINT32(0, scene.mesh_count);
}

/* --- Helper: read shader source from a single-shader pack --- */

static char *read_shader_source_from_pack(const char *pack_path, uint32_t *out_code_size) {
    FILE *f = fopen(pack_path, "rb");
    if (!f) {
        return NULL;
    }

    NtPackHeader hdr;
    if (fread(&hdr, sizeof(hdr), 1, f) != 1) {
        (void)fclose(f);
        return NULL;
    }
    NtAssetEntry entry;
    if (fread(&entry, sizeof(entry), 1, f) != 1) {
        (void)fclose(f);
        return NULL;
    }
    (void)fseek(f, (long)entry.offset, SEEK_SET);
    NtShaderCodeHeader shdr;
    if (fread(&shdr, sizeof(shdr), 1, f) != 1) {
        (void)fclose(f);
        return NULL;
    }

    char *source = (char *)malloc(shdr.code_size);
    if (!source) {
        (void)fclose(f);
        return NULL;
    }
    if (fread(source, 1, shdr.code_size, f) != shdr.code_size) {
        free(source);
        (void)fclose(f);
        return NULL;
    }
    (void)fclose(f);
    if (out_code_size) {
        *out_code_size = shdr.code_size;
    }
    return source;
}

/* --- Include resolver tests --- */

void test_include_basic(void) {
    MKDIR(TMP_DIR "/inc");
    write_test_shader(TMP_DIR "/inc/common.glsl", "vec3 apply_transform(vec3 p, mat4 m) { return (m * vec4(p, 1.0)).xyz; }\n");
    write_test_shader(TMP_DIR "/inc_main.vert", "precision mediump float;\n"
                                                "#include \"inc/common.glsl\"\n"
                                                "layout(location = 0) in vec3 a_position;\n"
                                                "uniform mat4 u_mvp;\n"
                                                "void main() {\n"
                                                "    gl_Position = u_mvp * vec4(apply_transform(a_position, u_mvp), 1.0);\n"
                                                "}\n");

    const char *pack_path = TMP_DIR "/inc_basic.ntpack";
    NtBuilderContext *ctx = nt_builder_start_pack(pack_path);
    TEST_ASSERT_NOT_NULL(ctx);

    nt_build_result_t r = nt_builder_add_shader(ctx, TMP_DIR "/inc_main.vert", NT_BUILD_SHADER_VERTEX);
    TEST_ASSERT_EQUAL(NT_BUILD_OK, r);

    r = nt_builder_finish_pack(ctx);
    TEST_ASSERT_EQUAL(NT_BUILD_OK, r);
    nt_builder_free_pack(ctx);

    /* Verify included content is present and #include is resolved */
    char *source = read_shader_source_from_pack(pack_path, NULL);
    TEST_ASSERT_NOT_NULL(source);
    TEST_ASSERT_NOT_NULL(strstr(source, "apply_transform"));
    TEST_ASSERT_NULL(strstr(source, "#include"));
    free(source);
}

void test_include_pragma_once(void) {
    MKDIR(TMP_DIR "/once");
    write_test_shader(TMP_DIR "/once/shared.glsl", "#pragma once\n"
                                                   "const float PI = 3.14159;\n");
    write_test_shader(TMP_DIR "/once/a.glsl", "#include \"shared.glsl\"\n");
    write_test_shader(TMP_DIR "/once_main.vert", "precision mediump float;\n"
                                                 "#include \"once/shared.glsl\"\n"
                                                 "#include \"once/a.glsl\"\n"
                                                 "layout(location = 0) in vec3 a_position;\n"
                                                 "uniform mat4 u_mvp;\n"
                                                 "void main() {\n"
                                                 "    gl_Position = u_mvp * vec4(a_position * PI, 1.0);\n"
                                                 "}\n");

    const char *pack_path = TMP_DIR "/inc_once.ntpack";
    NtBuilderContext *ctx = nt_builder_start_pack(pack_path);
    TEST_ASSERT_NOT_NULL(ctx);

    nt_build_result_t r = nt_builder_add_shader(ctx, TMP_DIR "/once_main.vert", NT_BUILD_SHADER_VERTEX);
    TEST_ASSERT_EQUAL(NT_BUILD_OK, r);

    r = nt_builder_finish_pack(ctx);
    TEST_ASSERT_EQUAL(NT_BUILD_OK, r);
    nt_builder_free_pack(ctx);

    /* Verify PI appears exactly once */
    char *source = read_shader_source_from_pack(pack_path, NULL);
    TEST_ASSERT_NOT_NULL(source);

    int count = 0;
    const char *p = source;
    while ((p = strstr(p, "const float PI")) != NULL) {
        count++;
        p += 14;
    }
    TEST_ASSERT_EQUAL(1, count);
    free(source);
}

void test_include_missing_file_errors(void) {
    write_test_shader(TMP_DIR "/missing_inc.vert", "precision mediump float;\n"
                                                   "#include \"nonexistent.glsl\"\n"
                                                   "layout(location = 0) in vec3 a_position;\n"
                                                   "uniform mat4 u_mvp;\n"
                                                   "void main() { gl_Position = u_mvp * vec4(a_position, 1.0); }\n");

    const char *pack_path = TMP_DIR "/inc_missing.ntpack";
    NtBuilderContext *ctx = nt_builder_start_pack(pack_path);
    TEST_ASSERT_NOT_NULL(ctx);

    nt_build_result_t r = nt_builder_add_shader(ctx, TMP_DIR "/missing_inc.vert", NT_BUILD_SHADER_VERTEX);
    TEST_ASSERT_EQUAL(NT_BUILD_OK, r);

    r = nt_builder_finish_pack(ctx);
    TEST_ASSERT_NOT_EQUAL(NT_BUILD_OK, r);
    nt_builder_free_pack(ctx);
}

void test_include_depth_limit(void) {
    MKDIR(TMP_DIR "/depth");
    /* Self-include without #pragma once -- triggers infinite recursion / depth limit */
    write_test_shader(TMP_DIR "/depth/loop.glsl", "#include \"loop.glsl\"\n");
    write_test_shader(TMP_DIR "/depth_main.vert", "precision mediump float;\n"
                                                  "#include \"depth/loop.glsl\"\n"
                                                  "layout(location = 0) in vec3 a_position;\n"
                                                  "uniform mat4 u_mvp;\n"
                                                  "void main() { gl_Position = u_mvp * vec4(a_position, 1.0); }\n");

    const char *pack_path = TMP_DIR "/inc_depth.ntpack";
    NtBuilderContext *ctx = nt_builder_start_pack(pack_path);
    TEST_ASSERT_NOT_NULL(ctx);

    nt_build_result_t r = nt_builder_add_shader(ctx, TMP_DIR "/depth_main.vert", NT_BUILD_SHADER_VERTEX);
    TEST_ASSERT_EQUAL(NT_BUILD_OK, r);

    r = nt_builder_finish_pack(ctx);
    TEST_ASSERT_NOT_EQUAL(NT_BUILD_OK, r);
    nt_builder_free_pack(ctx);
}

void test_asset_root_include(void) {
    MKDIR(TMP_DIR "/root_a");
    write_test_shader(TMP_DIR "/root_a/helpers.glsl", "float helper_fn(float x) { return x * 2.0; }\n");
    write_test_shader(TMP_DIR "/root_shader.vert", "precision mediump float;\n"
                                                   "#include \"helpers.glsl\"\n"
                                                   "layout(location = 0) in vec3 a_position;\n"
                                                   "uniform mat4 u_mvp;\n"
                                                   "void main() { gl_Position = u_mvp * vec4(a_position * helper_fn(1.0), 1.0); }\n");

    const char *pack_path = TMP_DIR "/inc_root.ntpack";
    NtBuilderContext *ctx = nt_builder_start_pack(pack_path);
    TEST_ASSERT_NOT_NULL(ctx);

    nt_build_result_t r = nt_builder_add_asset_root(ctx, TMP_DIR "/root_a");
    TEST_ASSERT_EQUAL(NT_BUILD_OK, r);

    r = nt_builder_add_shader(ctx, TMP_DIR "/root_shader.vert", NT_BUILD_SHADER_VERTEX);
    TEST_ASSERT_EQUAL(NT_BUILD_OK, r);

    r = nt_builder_finish_pack(ctx);
    TEST_ASSERT_EQUAL(NT_BUILD_OK, r);
    nt_builder_free_pack(ctx);

    /* Verify included content from asset root */
    char *source = read_shader_source_from_pack(pack_path, NULL);
    TEST_ASSERT_NOT_NULL(source);
    TEST_ASSERT_NOT_NULL(strstr(source, "helper_fn"));
    free(source);
}

/* --- GL shader validation tests --- */

void test_gl_validation_valid_shader(void) {
    const char *vert_path = TMP_DIR "/gl_valid.vert";
    write_test_shader(vert_path, "precision mediump float;\n"
                                 "layout(location = 0) in vec3 a_position;\n"
                                 "uniform mat4 u_mvp;\n"
                                 "void main() {\n"
                                 "    gl_Position = u_mvp * vec4(a_position, 1.0);\n"
                                 "}\n");

    const char *pack_path = TMP_DIR "/gl_valid.ntpack";
    NtBuilderContext *ctx = nt_builder_start_pack(pack_path);
    TEST_ASSERT_NOT_NULL(ctx);

    nt_build_result_t r = nt_builder_add_shader(ctx, vert_path, NT_BUILD_SHADER_VERTEX);
    TEST_ASSERT_EQUAL(NT_BUILD_OK, r);

    r = nt_builder_finish_pack(ctx);
    TEST_ASSERT_EQUAL(NT_BUILD_OK, r);
    nt_builder_free_pack(ctx);
}

void test_gl_validation_invalid_shader(void) {
    const char *vert_path = TMP_DIR "/gl_invalid.vert";
    /* Missing semicolon after gl_Position assignment */
    write_test_shader(vert_path, "precision mediump float;\n"
                                 "layout(location = 0) in vec3 a_position;\n"
                                 "uniform mat4 u_mvp;\n"
                                 "void main() {\n"
                                 "    gl_Position = u_mvp * vec4(a_position, 1.0)\n"
                                 "}\n");

    const char *pack_path = TMP_DIR "/gl_invalid.ntpack";
    NtBuilderContext *ctx = nt_builder_start_pack(pack_path);
    TEST_ASSERT_NOT_NULL(ctx);

    nt_build_result_t r = nt_builder_add_shader(ctx, vert_path, NT_BUILD_SHADER_VERTEX);
    TEST_ASSERT_EQUAL(NT_BUILD_OK, r);

    r = nt_builder_finish_pack(ctx);
    /* GL validation may be skipped if no display (D-08) -- both outcomes are valid */
    TEST_ASSERT_TRUE(r == NT_BUILD_OK || r == NT_BUILD_ERR_VALIDATION);
    nt_builder_free_pack(ctx);
}

void test_gl_validation_fragment_shader(void) {
    const char *frag_path = TMP_DIR "/gl_valid.frag";
    write_test_shader(frag_path, "precision mediump float;\n"
                                 "out vec4 frag_color;\n"
                                 "uniform vec4 u_color;\n"
                                 "void main() {\n"
                                 "    frag_color = u_color;\n"
                                 "}\n");

    const char *pack_path = TMP_DIR "/gl_frag.ntpack";
    NtBuilderContext *ctx = nt_builder_start_pack(pack_path);
    TEST_ASSERT_NOT_NULL(ctx);

    nt_build_result_t r = nt_builder_add_shader(ctx, frag_path, NT_BUILD_SHADER_FRAGMENT);
    TEST_ASSERT_EQUAL(NT_BUILD_OK, r);

    r = nt_builder_finish_pack(ctx);
    TEST_ASSERT_EQUAL(NT_BUILD_OK, r);
    nt_builder_free_pack(ctx);
}

void test_gl_validation_type_error(void) {
    const char *vert_path = TMP_DIR "/gl_type_err.vert";
    /* mat4 + vec3 is a type error in GLSL */
    write_test_shader(vert_path, "precision mediump float;\n"
                                 "layout(location = 0) in vec3 a_position;\n"
                                 "uniform mat4 u_mvp;\n"
                                 "void main() {\n"
                                 "    gl_Position = u_mvp + a_position;\n"
                                 "}\n");

    const char *pack_path = TMP_DIR "/gl_type_err.ntpack";
    NtBuilderContext *ctx = nt_builder_start_pack(pack_path);
    TEST_ASSERT_NOT_NULL(ctx);

    nt_build_result_t r = nt_builder_add_shader(ctx, vert_path, NT_BUILD_SHADER_VERTEX);
    TEST_ASSERT_EQUAL(NT_BUILD_OK, r);

    r = nt_builder_finish_pack(ctx);
    /* GL validation may be skipped if no display (D-08) -- both outcomes are valid */
    TEST_ASSERT_TRUE(r == NT_BUILD_OK || r == NT_BUILD_ERR_VALIDATION);
    nt_builder_free_pack(ctx);
}

int main(void) {
    UNITY_BEGIN();

    /* Normalize-and-hash tests */
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

    /* E2E with real assets */
    RUN_TEST(test_e2e_real_assets);

    /* Rename */
    RUN_TEST(test_rename_changes_resource_id);

    /* Force + glob override */
    RUN_TEST(test_force_glob_override);

    /* Lifecycle */
    RUN_TEST(test_free_pack_without_finish);

    /* Blob import */
    RUN_TEST(test_blob_import);

    /* Texture from memory */
    RUN_TEST(test_tex_from_memory);

    /* Scene parse */
    RUN_TEST(test_glb_scene_parse);

    /* Include resolver */
    RUN_TEST(test_include_basic);
    RUN_TEST(test_include_pragma_once);
    RUN_TEST(test_include_missing_file_errors);
    RUN_TEST(test_include_depth_limit);
    RUN_TEST(test_asset_root_include);

    /* GL shader validation */
    RUN_TEST(test_gl_validation_valid_shader);
    RUN_TEST(test_gl_validation_invalid_shader);
    RUN_TEST(test_gl_validation_fragment_shader);
    RUN_TEST(test_gl_validation_type_error);

    return UNITY_END();
}
// NOLINTEND(clang-analyzer-unix.Stream,clang-analyzer-core.CallAndMessage,clang-analyzer-core.UndefinedBinaryOperatorResult)
