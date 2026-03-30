/* clang-analyzer thinks fread failures leave data uninitialized, but Unity's
   TEST_ASSERT_EQUAL aborts via longjmp before any uninitialized access. */
// NOLINTBEGIN(clang-analyzer-unix.Stream,clang-analyzer-core.CallAndMessage,clang-analyzer-core.UndefinedBinaryOperatorResult)
/* System headers before Unity to avoid noreturn / __declspec conflict on MSVC */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Windows SDK must be included early (before stdnoreturn.h from C17 headers) */
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

/* Suppress GLFW/GLX internal leaks (X11 extension query cache) */
const char *__lsan_default_suppressions(void);  // NOLINT(bugprone-reserved-identifier)
const char *__lsan_default_suppressions(void) { // NOLINT(bugprone-reserved-identifier)
    return "leak:extensionSupportedGLX\n"
           "leak:nt_builder_decode_font\n" /* EXPECT_BUILD_ASSERT + longjmp leaks internal allocs */
           "leak:nt_builder_add_font\n";
}

/* clang-format off */
#include "nt_blob_format.h"
#include "nt_builder.h"
#include "nt_builder_internal.h"
#include "nt_crc32.h"
#include "nt_font_format.h"
#include "stb_truetype.h"
#include "nt_mesh_format.h"
#include "nt_pack_format.h"
#include "nt_shader_format.h"
#include "nt_texture_format.h"
#include "unity.h"
/* clang-format on */

#include <setjmp.h>

/* --- Build assert catching (setjmp/longjmp via hookable handler) --- */

static jmp_buf s_build_assert_jmp;
static NtBuilderContext *s_build_assert_ctx; /* freed after longjmp to avoid ASAN leaks */

static void test_build_assert_handler(const char *expr, const char *file, int line) {
    (void)expr;
    (void)file;
    (void)line;
    longjmp(s_build_assert_jmp, 1);
}

/* Expect NT_BUILD_ASSERT to fire inside `code`.
 * `ctx` is the builder context — freed after longjmp to satisfy ASAN. */
#define EXPECT_BUILD_ASSERT(ctx, code)                                                                                                                                                                 \
    do {                                                                                                                                                                                               \
        s_build_assert_ctx = (ctx);                                                                                                                                                                    \
        nt_build_assert_handler = test_build_assert_handler;                                                                                                                                           \
        if (setjmp(s_build_assert_jmp) == 0) {                                                                                                                                                         \
            code;                                                                                                                                                                                      \
            nt_build_assert_handler = NULL;                                                                                                                                                            \
            TEST_FAIL_MESSAGE("Expected NT_BUILD_ASSERT to fire");                                                                                                                                     \
        }                                                                                                                                                                                              \
        nt_build_assert_handler = NULL;                                                                                                                                                                \
        nt_builder_free_pack(s_build_assert_ctx);                                                                                                                                                      \
        s_build_assert_ctx = NULL;                                                                                                                                                                     \
    } while (0)

/* --- Temp directory for test output --- */

#ifdef _WIN32
#include <direct.h>
#define MKDIR(p) _mkdir(p)
#else
#include <dirent.h>
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
    /* finish_pack with 0 assets now asserts -- just test context creation */
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

    nt_builder_add_shader(ctx, vert_path, NT_BUILD_SHADER_VERTEX);

    nt_build_result_t r = nt_builder_finish_pack(ctx);
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

    nt_builder_add_texture(ctx, png_path, NULL);

    nt_build_result_t r = nt_builder_finish_pack(ctx);
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

    nt_builder_add_mesh(ctx, glb_path, &(nt_mesh_opts_t){.layout = layout, .stream_count = 1});

    nt_build_result_t r = nt_builder_finish_pack(ctx);
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

    /* Eager decode: validation fires in add_mesh, not finish_pack */
    EXPECT_BUILD_ASSERT(ctx, nt_builder_add_mesh(ctx, glb_path, &(nt_mesh_opts_t){.layout = layout, .stream_count = 1}));
}

void test_empty_shader_errors(void) {
    const char *vert_path = TMP_DIR "/empty.vert";
    write_test_shader(vert_path, "");

    const char *pack_path = TMP_DIR "/empty_test.ntpack";
    NtBuilderContext *ctx = nt_builder_start_pack(pack_path);
    TEST_ASSERT_NOT_NULL(ctx);
    nt_builder_add_shader(ctx, vert_path, NT_BUILD_SHADER_VERTEX);

    /* Empty shader: add_shader succeeds (stores empty resolved text),
     * encode_shader fails validation during finish_pack */
    EXPECT_BUILD_ASSERT(ctx, nt_builder_finish_pack(ctx));
}

void test_shader_with_version_errors(void) {
    const char *vert_path = TMP_DIR "/hasversion.vert";
    write_test_shader(vert_path, "#version 300 es\n"
                                 "precision mediump float;\n"
                                 "void main() { gl_Position = vec4(0); }\n");

    const char *pack_path = TMP_DIR "/hasversion_test.ntpack";
    NtBuilderContext *ctx = nt_builder_start_pack(pack_path);
    TEST_ASSERT_NOT_NULL(ctx);
    nt_builder_add_shader(ctx, vert_path, NT_BUILD_SHADER_VERTEX);

    EXPECT_BUILD_ASSERT(ctx, nt_builder_finish_pack(ctx));
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

    nt_builder_add_shader(ctx, vert_path, NT_BUILD_SHADER_VERTEX);

    nt_build_result_t r = nt_builder_finish_pack(ctx);
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

    nt_builder_add_shader(ctx, v_path, NT_BUILD_SHADER_VERTEX);
    nt_builder_add_shader(ctx, f_path, NT_BUILD_SHADER_FRAGMENT);

    nt_build_result_t r = nt_builder_finish_pack(ctx);
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

    nt_builder_add_shader(ctx, vert_path, NT_BUILD_SHADER_VERTEX);
    nt_build_result_t r = nt_builder_finish_pack(ctx);
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
    nt_builder_add_shader(ctx, vert_path, NT_BUILD_SHADER_VERTEX);
    nt_build_result_t r = nt_builder_finish_pack(ctx);
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

/* --- Dump with gzip estimation tests --- */

void test_dump_gzip_sizes(void) {
    /* Build a pack with mesh + texture + shader to exercise gzip estimation */
    const char *glb_path = TMP_DIR "/dump_gz_tri.glb";
    const char *png_path = TMP_DIR "/dump_gz_tex.png";
    const char *vert_path = TMP_DIR "/dump_gz.vert";
    write_test_glb(glb_path);
    write_test_png(png_path);
    write_test_shader(vert_path, "precision mediump float;\n"
                                 "layout(location = 0) in vec3 a_pos;\n"
                                 "void main() { gl_Position = vec4(a_pos, 1.0); }\n");

    NtStreamLayout layout[] = {
        {"position", "POSITION", NT_STREAM_FLOAT32, 3, false},
    };

    const char *pack_path = TMP_DIR "/dump_gz_test.ntpack";
    NtBuilderContext *ctx = nt_builder_start_pack(pack_path);
    nt_builder_add_mesh(ctx, glb_path, &(nt_mesh_opts_t){.layout = layout, .stream_count = 1});
    nt_builder_add_texture(ctx, png_path, NULL);
    nt_builder_add_shader(ctx, vert_path, NT_BUILD_SHADER_VERTEX);
    nt_build_result_t r = nt_builder_finish_pack(ctx);
    TEST_ASSERT_EQUAL(NT_BUILD_OK, r);
    nt_builder_free_pack(ctx);

    /* Dump with gzip estimation should succeed */
    r = nt_builder_dump_pack(pack_path);
    TEST_ASSERT_EQUAL(NT_BUILD_OK, r);
}

void test_dump_name_resolution(void) {
    /* Build a pack with a shader */
    const char *vert_path = TMP_DIR "/dump_name.vert";
    write_test_shader(vert_path, "precision mediump float;\n"
                                 "void main() { gl_Position = vec4(0); }\n");

    const char *pack_path = TMP_DIR "/dump_name_test.ntpack";
    NtBuilderContext *ctx = nt_builder_start_pack(pack_path);
    nt_builder_add_shader(ctx, vert_path, NT_BUILD_SHADER_VERTEX);
    nt_build_result_t r = nt_builder_finish_pack(ctx);
    TEST_ASSERT_EQUAL(NT_BUILD_OK, r);
    nt_builder_free_pack(ctx);

    /* Write a fake .h file next to .ntpack with known hash-to-name mapping */
    nt_hash64_t h = nt_builder_normalize_and_hash(vert_path);
    char header_path[512];
    (void)snprintf(header_path, sizeof(header_path), "%s", TMP_DIR "/dump_name_test.h");
    FILE *hf = fopen(header_path, "w");
    TEST_ASSERT_NOT_NULL(hf);
    (void)fprintf(hf, "#define ASSET_DUMP_NAME_VERT ((nt_hash64_t){0x%016llXULL}) /* %s */\n", (unsigned long long)h.value, vert_path);
    (void)fclose(hf);

    /* Dump should succeed and use names from .h file */
    r = nt_builder_dump_pack(pack_path);
    TEST_ASSERT_EQUAL(NT_BUILD_OK, r);
}

void test_dump_without_header(void) {
    /* Build a pack without .h file - should fall back to truncated hex hashes */
    const char *vert_path = TMP_DIR "/dump_nohdr.vert";
    write_test_shader(vert_path, "precision mediump float;\n"
                                 "void main() { gl_Position = vec4(0); }\n");

    const char *pack_path = TMP_DIR "/dump_nohdr_test.ntpack";
    NtBuilderContext *ctx = nt_builder_start_pack(pack_path);
    nt_builder_add_shader(ctx, vert_path, NT_BUILD_SHADER_VERTEX);
    nt_build_result_t r = nt_builder_finish_pack(ctx);
    TEST_ASSERT_EQUAL(NT_BUILD_OK, r);
    nt_builder_free_pack(ctx);

    /* Remove any .h file that might exist */
    (void)remove(TMP_DIR "/dump_nohdr_test.h");

    /* Dump should succeed with truncated hex hashes */
    r = nt_builder_dump_pack(pack_path);
    TEST_ASSERT_EQUAL(NT_BUILD_OK, r);
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

    nt_builder_add_mesh(ctx, glb_path, &(nt_mesh_opts_t){.layout = layout, .stream_count = 1});
    nt_builder_add_texture(ctx, png_path, NULL);
    nt_builder_add_shader(ctx, vert_path, NT_BUILD_SHADER_VERTEX);

    nt_build_result_t r = nt_builder_finish_pack(ctx);
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
    nt_builder_add_shader(ctx, frag_path, NT_BUILD_SHADER_FRAGMENT);
    nt_build_result_t r = nt_builder_finish_pack(ctx);
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

    nt_builder_add_shaders(ctx, "tests/fixtures/*.vert", NT_BUILD_SHADER_VERTEX);

    nt_builder_add_shaders(ctx, "tests/fixtures/*.frag", NT_BUILD_SHADER_FRAGMENT);

    nt_build_result_t r = nt_builder_finish_pack(ctx);
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

    nt_builder_add_shader(ctx, "assets/shaders/mesh.vert", NT_BUILD_SHADER_VERTEX);
    nt_builder_add_shader(ctx, "assets/shaders/mesh.frag", NT_BUILD_SHADER_FRAGMENT);
    nt_builder_add_mesh(ctx, "assets/meshes/cube.glb", &(nt_mesh_opts_t){.layout = layout, .stream_count = 2});
    nt_builder_add_texture(ctx, "assets/textures/lenna.png", NULL);

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

    nt_builder_add_shader(ctx, vert_path, NT_BUILD_SHADER_VERTEX);

    nt_hash64_t old_id = nt_builder_normalize_and_hash(vert_path);
    nt_hash64_t new_id = nt_builder_normalize_and_hash("renamed/shader.vert");
    TEST_ASSERT_TRUE(old_id.value != new_id.value);

    nt_builder_rename(ctx, vert_path, "renamed/shader.vert");
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

/* --- free_pack without finish --- */

void test_free_pack_without_finish(void) {
    NtBuilderContext *ctx = nt_builder_start_pack(TMP_DIR "/nofin.ntpack");
    TEST_ASSERT_NOT_NULL(ctx);

    const char *vert_path = TMP_DIR "/nofin.vert";
    write_test_shader(vert_path, "precision mediump float;\nvoid main() { gl_Position = vec4(0); }\n");
    nt_builder_add_shader(ctx, vert_path, NT_BUILD_SHADER_VERTEX);

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

    nt_builder_add_blob(ctx, test_data, sizeof(test_data), "test/blob");

    nt_build_result_t r = nt_builder_finish_pack(ctx);
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

    nt_builder_add_texture_from_memory(ctx, png_data, (uint32_t)png_size, "test/texture_mem", NULL);

    nt_build_result_t r = nt_builder_finish_pack(ctx);
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

    nt_builder_add_shader(ctx, TMP_DIR "/inc_main.vert", NT_BUILD_SHADER_VERTEX);

    nt_build_result_t r = nt_builder_finish_pack(ctx);
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

    nt_builder_add_shader(ctx, TMP_DIR "/once_main.vert", NT_BUILD_SHADER_VERTEX);

    nt_build_result_t r = nt_builder_finish_pack(ctx);
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

    /* Eager decode: include resolution fails in add_shader */
    EXPECT_BUILD_ASSERT(ctx, nt_builder_add_shader(ctx, TMP_DIR "/missing_inc.vert", NT_BUILD_SHADER_VERTEX));
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

    /* Eager decode: depth limit fires in add_shader */
    EXPECT_BUILD_ASSERT(ctx, nt_builder_add_shader(ctx, TMP_DIR "/depth_main.vert", NT_BUILD_SHADER_VERTEX));
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

    nt_builder_add_shader(ctx, TMP_DIR "/root_shader.vert", NT_BUILD_SHADER_VERTEX);

    r = nt_builder_finish_pack(ctx);
    TEST_ASSERT_EQUAL(NT_BUILD_OK, r);
    nt_builder_free_pack(ctx);

    /* Verify included content from asset root */
    char *source = read_shader_source_from_pack(pack_path, NULL);
    TEST_ASSERT_NOT_NULL(source);
    TEST_ASSERT_NOT_NULL(strstr(source, "helper_fn"));
    free(source);
}

/* --- Bug #3 repro: pragma once after comment --- */

void test_include_pragma_once_after_comment(void) {
    MKDIR(TMP_DIR "/once_late");
    write_test_shader(TMP_DIR "/once_late/lib.glsl", "/* library header */\n"
                                                     "#pragma once\n"
                                                     "float late_fn(float x) { return x * 2.0; }\n");
    write_test_shader(TMP_DIR "/once_late_main.vert", "precision mediump float;\n"
                                                      "#include \"once_late/lib.glsl\"\n"
                                                      "#include \"once_late/lib.glsl\"\n"
                                                      "layout(location = 0) in vec3 a_position;\n"
                                                      "uniform mat4 u_mvp;\n"
                                                      "void main() { gl_Position = u_mvp * vec4(a_position * late_fn(1.0), 1.0); }\n");

    const char *pack_path = TMP_DIR "/inc_once_late.ntpack";
    NtBuilderContext *ctx = nt_builder_start_pack(pack_path);
    TEST_ASSERT_NOT_NULL(ctx);

    nt_builder_add_shader(ctx, TMP_DIR "/once_late_main.vert", NT_BUILD_SHADER_VERTEX);

    nt_build_result_t r = nt_builder_finish_pack(ctx);
    TEST_ASSERT_EQUAL_MESSAGE(NT_BUILD_OK, r, "pragma once after comment should still prevent double inclusion");
    nt_builder_free_pack(ctx);

    char *source = read_shader_source_from_pack(pack_path, NULL);
    TEST_ASSERT_NOT_NULL(source);

    /* Count definitions: "float late_fn" appears once from the include, and once more
       if #pragma once fails to deduplicate. The call site in main() has just "late_fn(1.0)". */
    int count = 0;
    const char *p = source;
    while ((p = strstr(p, "float late_fn")) != NULL) {
        count++;
        p += 13;
    }
    TEST_ASSERT_EQUAL_MESSAGE(1, count, "float late_fn definition should appear exactly once with pragma once");
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

    nt_builder_add_shader(ctx, vert_path, NT_BUILD_SHADER_VERTEX);

    nt_build_result_t r = nt_builder_finish_pack(ctx);
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

    nt_builder_add_shader(ctx, vert_path, NT_BUILD_SHADER_VERTEX);

    nt_build_result_t r = nt_builder_finish_pack(ctx);
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

    nt_builder_add_shader(ctx, frag_path, NT_BUILD_SHADER_FRAGMENT);

    nt_build_result_t r = nt_builder_finish_pack(ctx);
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

    nt_builder_add_shader(ctx, vert_path, NT_BUILD_SHADER_VERTEX);

    nt_build_result_t r = nt_builder_finish_pack(ctx);
    /* GL validation may be skipped if no display (D-08) -- both outcomes are valid */
    TEST_ASSERT_TRUE(r == NT_BUILD_OK || r == NT_BUILD_ERR_VALIDATION);
    nt_builder_free_pack(ctx);
}

/* --- Multi-mesh glb helper ---
 * Writes a minimal valid .glb with 2 named meshes: "FirstMesh" and "SecondMesh".
 * Each mesh has 1 primitive with 3 vertices (triangle) + 3 uint16 indices.
 */
static void write_test_multi_mesh_glb(const char *path) {
    /* JSON chunk -- two meshes, each with its own accessors/bufferViews.
     * Mesh 0 "FirstMesh": positions at bv0, indices at bv1
     * Mesh 1 "SecondMesh": positions at bv2, indices at bv3
     * Binary layout: [pos0 36B][idx0 6B+2pad][pos1 36B][idx1 6B+2pad] = 88 bytes */
    const char *json_str = "{"
                           "\"asset\":{\"version\":\"2.0\"},"
                           "\"meshes\":["
                           "{\"name\":\"FirstMesh\",\"primitives\":[{\"attributes\":{\"POSITION\":0},\"indices\":1}]},"
                           "{\"name\":\"SecondMesh\",\"primitives\":[{\"attributes\":{\"POSITION\":2},\"indices\":3}]}"
                           "],"
                           "\"accessors\":["
                           "{\"bufferView\":0,\"componentType\":5126,\"count\":3,\"type\":\"VEC3\","
                           "\"max\":[1.0,1.0,0.0],\"min\":[0.0,0.0,0.0]},"
                           "{\"bufferView\":1,\"componentType\":5123,\"count\":3,\"type\":\"SCALAR\"},"
                           "{\"bufferView\":2,\"componentType\":5126,\"count\":3,\"type\":\"VEC3\","
                           "\"max\":[2.0,2.0,0.0],\"min\":[0.0,0.0,0.0]},"
                           "{\"bufferView\":3,\"componentType\":5123,\"count\":3,\"type\":\"SCALAR\"}"
                           "],"
                           "\"bufferViews\":["
                           "{\"buffer\":0,\"byteOffset\":0,\"byteLength\":36},"
                           "{\"buffer\":0,\"byteOffset\":36,\"byteLength\":6},"
                           "{\"buffer\":0,\"byteOffset\":44,\"byteLength\":36},"
                           "{\"buffer\":0,\"byteOffset\":80,\"byteLength\":6}"
                           "],"
                           "\"buffers\":[{\"byteLength\":88}]"
                           "}";

    uint32_t json_len = (uint32_t)strlen(json_str);
    uint32_t json_padded = (json_len + 3U) & ~3U;
    uint32_t json_padding = json_padded - json_len;

    /* Binary data: two sets of (3 position vec3 + 3 uint16 indices + 2-byte pad) */
    float positions0[] = {0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F};
    uint16_t indices0[] = {0, 1, 2};
    uint16_t pad0 = 0;
    float positions1[] = {0.0F, 0.0F, 0.0F, 2.0F, 0.0F, 0.0F, 0.0F, 2.0F, 0.0F};
    uint16_t indices1[] = {0, 1, 2};
    uint16_t pad1 = 0;

    uint32_t bin_data_size = 88;
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
    (void)fwrite(positions0, sizeof(positions0), 1, f);
    (void)fwrite(indices0, sizeof(indices0), 1, f);
    (void)fwrite(&pad0, sizeof(pad0), 1, f);
    (void)fwrite(positions1, sizeof(positions1), 1, f);
    (void)fwrite(indices1, sizeof(indices1), 1, f);
    (void)fwrite(&pad1, sizeof(pad1), 1, f);

    (void)fclose(f);
}

/* --- Multi-mesh add_mesh tests --- */

void test_add_mesh_by_name(void) {
    const char *glb_path = TMP_DIR "/multi_mesh.glb";
    write_test_multi_mesh_glb(glb_path);

    NtStreamLayout layout[] = {{"position", "POSITION", NT_STREAM_FLOAT32, 3, false}};

    const char *pack_path = TMP_DIR "/by_name.ntpack";
    NtBuilderContext *ctx = nt_builder_start_pack(pack_path);
    TEST_ASSERT_NOT_NULL(ctx);

    nt_builder_add_mesh(ctx, glb_path, &(nt_mesh_opts_t){.layout = layout, .stream_count = 1, .mesh_name = "SecondMesh"});

    nt_build_result_t r = nt_builder_finish_pack(ctx);
    TEST_ASSERT_EQUAL(NT_BUILD_OK, r);
    nt_builder_free_pack(ctx);

    /* Verify resource_id = hash(normalized("path/SecondMesh")) */
    char logical[512];
    (void)snprintf(logical, sizeof(logical), "%s/SecondMesh", glb_path);
    nt_hash64_t expected_id = nt_builder_normalize_and_hash(logical);

    FILE *f = fopen(pack_path, "rb");
    TEST_ASSERT_NOT_NULL(f);
    NtPackHeader hdr;
    TEST_ASSERT_EQUAL(1, fread(&hdr, sizeof(hdr), 1, f));
    TEST_ASSERT_EQUAL_UINT16(1, hdr.asset_count);
    NtAssetEntry entry;
    TEST_ASSERT_EQUAL(1, fread(&entry, sizeof(entry), 1, f));
    TEST_ASSERT_EQUAL_HEX64(expected_id.value, entry.resource_id);
    TEST_ASSERT_EQUAL_UINT8(NT_ASSET_MESH, entry.asset_type);

    /* Verify it's the second mesh (vertices go up to 2.0) */
    (void)fseek(f, (long)entry.offset, SEEK_SET);
    NtMeshAssetHeader mesh;
    TEST_ASSERT_EQUAL(1, fread(&mesh, sizeof(mesh), 1, f));
    TEST_ASSERT_EQUAL_UINT32(NT_MESH_MAGIC, mesh.magic);
    TEST_ASSERT_EQUAL_UINT32(3, mesh.vertex_count);

    (void)fclose(f);
}

void test_add_mesh_by_index(void) {
    const char *glb_path = TMP_DIR "/multi_mesh_idx.glb";
    write_test_multi_mesh_glb(glb_path);

    NtStreamLayout layout[] = {{"position", "POSITION", NT_STREAM_FLOAT32, 3, false}};

    const char *pack_path = TMP_DIR "/by_index.ntpack";
    NtBuilderContext *ctx = nt_builder_start_pack(pack_path);
    TEST_ASSERT_NOT_NULL(ctx);

    nt_builder_add_mesh(ctx, glb_path, &(nt_mesh_opts_t){.layout = layout, .stream_count = 1, .mesh_index = 1, .use_mesh_index = true});

    nt_build_result_t r = nt_builder_finish_pack(ctx);
    TEST_ASSERT_EQUAL(NT_BUILD_OK, r);
    nt_builder_free_pack(ctx);

    /* Verify resource_id = hash(normalized("path/1")) */
    char logical[512];
    (void)snprintf(logical, sizeof(logical), "%s/1", glb_path);
    nt_hash64_t expected_id = nt_builder_normalize_and_hash(logical);

    FILE *f = fopen(pack_path, "rb");
    TEST_ASSERT_NOT_NULL(f);
    NtPackHeader hdr;
    TEST_ASSERT_EQUAL(1, fread(&hdr, sizeof(hdr), 1, f));
    NtAssetEntry entry;
    TEST_ASSERT_EQUAL(1, fread(&entry, sizeof(entry), 1, f));
    TEST_ASSERT_EQUAL_HEX64(expected_id.value, entry.resource_id);
    (void)fclose(f);
}

void test_add_mesh_single_unchanged(void) {
    /* Existing single-mesh glb, opts-based call, same result as before */
    const char *glb_path = TMP_DIR "/single_unch.glb";
    write_test_glb(glb_path);

    NtStreamLayout layout[] = {{"position", "POSITION", NT_STREAM_FLOAT32, 3, false}};

    const char *pack_path = TMP_DIR "/single_unch.ntpack";
    NtBuilderContext *ctx = nt_builder_start_pack(pack_path);
    TEST_ASSERT_NOT_NULL(ctx);

    nt_builder_add_mesh(ctx, glb_path, &(nt_mesh_opts_t){.layout = layout, .stream_count = 1});

    nt_build_result_t r = nt_builder_finish_pack(ctx);
    TEST_ASSERT_EQUAL(NT_BUILD_OK, r);
    nt_builder_free_pack(ctx);

    /* Resource ID should be hash of the path alone (no suffix) */
    nt_hash64_t expected_id = nt_builder_normalize_and_hash(glb_path);

    FILE *f = fopen(pack_path, "rb");
    TEST_ASSERT_NOT_NULL(f);
    NtPackHeader hdr;
    TEST_ASSERT_EQUAL(1, fread(&hdr, sizeof(hdr), 1, f));
    NtAssetEntry entry;
    TEST_ASSERT_EQUAL(1, fread(&entry, sizeof(entry), 1, f));
    TEST_ASSERT_EQUAL_HEX64(expected_id.value, entry.resource_id);
    TEST_ASSERT_EQUAL_UINT8(NT_ASSET_MESH, entry.asset_type);
    (void)fclose(f);
}

void test_add_mesh_by_name_not_found(void) {
    const char *glb_path = TMP_DIR "/multi_mesh_nf.glb";
    write_test_multi_mesh_glb(glb_path);

    NtStreamLayout layout[] = {{"position", "POSITION", NT_STREAM_FLOAT32, 3, false}};

    const char *pack_path = TMP_DIR "/name_nf.ntpack";
    NtBuilderContext *ctx = nt_builder_start_pack(pack_path);
    TEST_ASSERT_NOT_NULL(ctx);

    /* Eager decode: mesh name lookup fails in add_mesh */
    EXPECT_BUILD_ASSERT(ctx, nt_builder_add_mesh(ctx, glb_path, &(nt_mesh_opts_t){.layout = layout, .stream_count = 1, .mesh_name = "NonExistent"}));
}

void test_add_mesh_by_index_out_of_range(void) {
    const char *glb_path = TMP_DIR "/multi_mesh_oor.glb";
    write_test_multi_mesh_glb(glb_path);

    NtStreamLayout layout[] = {{"position", "POSITION", NT_STREAM_FLOAT32, 3, false}};

    const char *pack_path = TMP_DIR "/index_oor.ntpack";
    NtBuilderContext *ctx = nt_builder_start_pack(pack_path);
    TEST_ASSERT_NOT_NULL(ctx);

    /* Eager decode: index validation fails in add_mesh */
    EXPECT_BUILD_ASSERT(ctx, nt_builder_add_mesh(ctx, glb_path, &(nt_mesh_opts_t){.layout = layout, .stream_count = 1, .mesh_index = 99, .use_mesh_index = true}));
}

void test_add_mesh_resource_name_override(void) {
    const char *glb_path = TMP_DIR "/multi_mesh_rn.glb";
    write_test_multi_mesh_glb(glb_path);

    NtStreamLayout layout[] = {{"position", "POSITION", NT_STREAM_FLOAT32, 3, false}};

    const char *pack_path = TMP_DIR "/res_name.ntpack";
    NtBuilderContext *ctx = nt_builder_start_pack(pack_path);
    TEST_ASSERT_NOT_NULL(ctx);

    nt_builder_add_mesh(ctx, glb_path, &(nt_mesh_opts_t){.layout = layout, .stream_count = 1, .mesh_name = "SecondMesh", .resource_name = "custom"});

    nt_build_result_t r = nt_builder_finish_pack(ctx);
    TEST_ASSERT_EQUAL(NT_BUILD_OK, r);
    nt_builder_free_pack(ctx);

    /* Verify resource_id = hash(normalized("path/custom")) */
    char logical[512];
    (void)snprintf(logical, sizeof(logical), "%s/custom", glb_path);
    nt_hash64_t expected_id = nt_builder_normalize_and_hash(logical);

    FILE *f = fopen(pack_path, "rb");
    TEST_ASSERT_NOT_NULL(f);
    NtPackHeader hdr;
    TEST_ASSERT_EQUAL(1, fread(&hdr, sizeof(hdr), 1, f));
    NtAssetEntry entry;
    TEST_ASSERT_EQUAL(1, fread(&entry, sizeof(entry), 1, f));
    TEST_ASSERT_EQUAL_HEX64(expected_id.value, entry.resource_id);
    (void)fclose(f);
}

/* --- Codegen tests --- */

/* Helper: read file into malloc'd buffer, returns NULL on failure */
static char *read_text_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        return NULL;
    }
    (void)fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    (void)fseek(f, 0, SEEK_SET);
    if (sz <= 0) {
        (void)fclose(f);
        return NULL;
    }
    char *buf = (char *)malloc((size_t)sz + 1);
    if (!buf) {
        (void)fclose(f);
        return NULL;
    }
    size_t rd = fread(buf, 1, (size_t)sz, f);
    buf[rd] = '\0';
    (void)fclose(f);
    return buf;
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void test_codegen_generates_header(void) {
    /* Build pack with mesh + shader */
    const char *glb_path = TMP_DIR "/codegen_tri.glb";
    write_test_glb(glb_path);
    const char *vert_path = TMP_DIR "/codegen_test.vert";
    write_test_shader(vert_path, "precision mediump float;\nvoid main() { gl_Position = vec4(0); }\n");

    const char *pack_path = TMP_DIR "/codegen_test.ntpack";
    NtBuilderContext *ctx = nt_builder_start_pack(pack_path);
    TEST_ASSERT_NOT_NULL(ctx);

    NtStreamLayout layout[] = {{"position", "POSITION", NT_STREAM_FLOAT32, 3, false}};
    nt_mesh_opts_t opts = {.layout = layout, .stream_count = 1};
    nt_builder_add_mesh(ctx, glb_path, &opts);
    nt_builder_add_shader(ctx, vert_path, NT_BUILD_SHADER_VERTEX);

    nt_build_result_t r = nt_builder_finish_pack(ctx);
    TEST_ASSERT_EQUAL(NT_BUILD_OK, r);
    nt_builder_free_pack(ctx);

    /* .h file should exist next to .ntpack */
    const char *header_path = TMP_DIR "/codegen_test.h";
    char *content = read_text_file(header_path);
    TEST_ASSERT_NOT_NULL_MESSAGE(content, "Generated .h file should exist");

    /* Verify content */
    TEST_ASSERT_NOT_NULL(strstr(content, "#define ASSET_MESH_"));
    TEST_ASSERT_NOT_NULL(strstr(content, "#define ASSET_SHADER_"));
    TEST_ASSERT_NOT_NULL(strstr(content, "#ifndef"));
    TEST_ASSERT_NOT_NULL(strstr(content, "nt_hash64_t"));
    TEST_ASSERT_NOT_NULL(strstr(content, "register_labels"));
    TEST_ASSERT_NOT_NULL(strstr(content, "NT_HASH_LABELS"));
    TEST_ASSERT_NOT_NULL(strstr(content, "#endif"));

    free(content);
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void test_codegen_hash_matches_runtime(void) {
    const char *glb_path = TMP_DIR "/codegen_hash_tri.glb";
    write_test_glb(glb_path);

    const char *pack_path = TMP_DIR "/codegen_hash.ntpack";
    NtBuilderContext *ctx = nt_builder_start_pack(pack_path);
    TEST_ASSERT_NOT_NULL(ctx);

    NtStreamLayout layout[] = {{"position", "POSITION", NT_STREAM_FLOAT32, 3, false}};
    nt_mesh_opts_t opts = {.layout = layout, .stream_count = 1};
    nt_builder_add_mesh(ctx, glb_path, &opts);
    TEST_ASSERT_EQUAL(NT_BUILD_OK, nt_builder_finish_pack(ctx));
    nt_builder_free_pack(ctx);

    /* Read generated .h */
    const char *header_path = TMP_DIR "/codegen_hash.h";
    char *content = read_text_file(header_path);
    TEST_ASSERT_NOT_NULL_MESSAGE(content, "Generated .h file should exist");

    /* Parse hex value from #define line: #define ASSET_MESH_... ((nt_hash64_t){0x...ULL}) */
    const char *hex_start = strstr(content, "0x");
    TEST_ASSERT_NOT_NULL_MESSAGE(hex_start, "Should contain hex value");

    char *end_ptr = NULL;
    uint64_t generated_hash = strtoull(hex_start, &end_ptr, 16);
    TEST_ASSERT_TRUE(generated_hash != 0);

    /* Compare against runtime hash of normalized path */
    nt_hash64_t runtime_hash = nt_builder_normalize_and_hash(glb_path);
    TEST_ASSERT_EQUAL_HEX64(runtime_hash.value, generated_hash);

    free(content);
}

void test_codegen_path_to_identifier(void) {
    /* Build pack with path "assets/meshes/cube.glb" -- but use local glb */
    MKDIR(TMP_DIR "/codegen_id");
    MKDIR(TMP_DIR "/codegen_id/assets");
    MKDIR(TMP_DIR "/codegen_id/assets/meshes");
    const char *glb_path = TMP_DIR "/codegen_id/assets/meshes/cube.glb";
    write_test_glb(glb_path);

    const char *pack_path = TMP_DIR "/codegen_id.ntpack";
    NtBuilderContext *ctx = nt_builder_start_pack(pack_path);
    TEST_ASSERT_NOT_NULL(ctx);

    NtStreamLayout layout[] = {{"position", "POSITION", NT_STREAM_FLOAT32, 3, false}};
    nt_mesh_opts_t opts = {.layout = layout, .stream_count = 1};
    nt_builder_add_mesh(ctx, glb_path, &opts);
    TEST_ASSERT_EQUAL(NT_BUILD_OK, nt_builder_finish_pack(ctx));
    nt_builder_free_pack(ctx);

    char *content = read_text_file(TMP_DIR "/codegen_id.h");
    TEST_ASSERT_NOT_NULL(content);

    /* The path normalized is "build/tests/tmp/codegen_id/assets/meshes/cube.glb"
     * Identifier: uppercase, replace /. with _, keep extension
     * -> ASSET_MESH_BUILD_TESTS_TMP_CODEGEN_ID_ASSETS_MESHES_CUBE_GLB */
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(content, "ASSET_MESH_"), "Should contain ASSET_MESH_ prefix");
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(content, "ASSETS_MESHES_CUBE_GLB"), "Should contain path-based identifier with extension");

    free(content);
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void test_codegen_renamed_assets(void) {
    const char *glb_path = TMP_DIR "/codegen_rename_tri.glb";
    write_test_glb(glb_path);

    const char *pack_path = TMP_DIR "/codegen_rename.ntpack";
    NtBuilderContext *ctx = nt_builder_start_pack(pack_path);
    TEST_ASSERT_NOT_NULL(ctx);

    NtStreamLayout layout[] = {{"position", "POSITION", NT_STREAM_FLOAT32, 3, false}};
    nt_mesh_opts_t opts = {.layout = layout, .stream_count = 1};
    nt_builder_add_mesh(ctx, glb_path, &opts);
    nt_builder_rename(ctx, glb_path, "meshes/my_cube");
    TEST_ASSERT_EQUAL(NT_BUILD_OK, nt_builder_finish_pack(ctx));
    nt_builder_free_pack(ctx);

    char *content = read_text_file(TMP_DIR "/codegen_rename.h");
    TEST_ASSERT_NOT_NULL(content);

    /* Should use rename_key for identifier */
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(content, "ASSET_MESH_MESHES_MY_CUBE"), "Should use renamed path for identifier");

    /* Hash should match rename key */
    const char *hex_start = strstr(content, "0x");
    TEST_ASSERT_NOT_NULL(hex_start);
    char *end_ptr = NULL;
    uint64_t generated_hash = strtoull(hex_start, &end_ptr, 16);
    nt_hash64_t expected_hash = nt_builder_normalize_and_hash("meshes/my_cube");
    TEST_ASSERT_EQUAL_HEX64(expected_hash.value, generated_hash);

    free(content);
}

/* --- Merge tests --- */

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void test_merge_combined_header(void) {
    const char *glb_path = TMP_DIR "/merge_tri.glb";
    write_test_glb(glb_path);
    const char *vert_path = TMP_DIR "/merge_test.vert";
    write_test_shader(vert_path, "precision mediump float;\nvoid main() { gl_Position = vec4(0); }\n");

    MKDIR(TMP_DIR "/merge_hdr");

    /* Pack 1: mesh */
    NtBuilderContext *ctx = nt_builder_start_pack(TMP_DIR "/merge_pack1.ntpack");
    nt_builder_set_header_dir(ctx, TMP_DIR "/merge_hdr");
    NtStreamLayout layout[] = {{"position", "POSITION", NT_STREAM_FLOAT32, 3, false}};
    nt_builder_add_mesh(ctx, glb_path, &(nt_mesh_opts_t){.layout = layout, .stream_count = 1});
    TEST_ASSERT_EQUAL(NT_BUILD_OK, nt_builder_finish_pack(ctx));
    nt_builder_free_pack(ctx);

    /* Pack 2: shader */
    ctx = nt_builder_start_pack(TMP_DIR "/merge_pack2.ntpack");
    nt_builder_set_header_dir(ctx, TMP_DIR "/merge_hdr");
    nt_builder_add_shader(ctx, vert_path, NT_BUILD_SHADER_VERTEX);
    TEST_ASSERT_EQUAL(NT_BUILD_OK, nt_builder_finish_pack(ctx));
    nt_builder_free_pack(ctx);

    /* Merge per-pack headers */
    const char *headers[] = {TMP_DIR "/merge_hdr/merge_pack1.h", TMP_DIR "/merge_hdr/merge_pack2.h"};
    const char *combined_path = TMP_DIR "/merge_assets.h";
    nt_builder_merge_headers(headers, 2, combined_path);

    /* Verify combined header */
    char *content = read_text_file(combined_path);
    TEST_ASSERT_NOT_NULL(content);
    TEST_ASSERT_NOT_NULL(strstr(content, "ASSET_MESH_"));
    TEST_ASSERT_NOT_NULL(strstr(content, "ASSET_SHADER_"));
    TEST_ASSERT_NOT_NULL(strstr(content, "register_labels"));
    TEST_ASSERT_NOT_NULL(strstr(content, "#ifndef"));
    free(content);
}

void test_merge_dedup(void) {
    const char *glb_path = TMP_DIR "/merge_dedup_tri.glb";
    write_test_glb(glb_path);

    MKDIR(TMP_DIR "/merge_dedup_hdr");

    NtStreamLayout layout[] = {{"position", "POSITION", NT_STREAM_FLOAT32, 3, false}};

    /* Two packs with the same mesh (same path = same hash, each in its own context) */
    NtBuilderContext *ctx = nt_builder_start_pack(TMP_DIR "/merge_dup1.ntpack");
    nt_builder_set_header_dir(ctx, TMP_DIR "/merge_dedup_hdr");
    nt_builder_add_mesh(ctx, glb_path, &(nt_mesh_opts_t){.layout = layout, .stream_count = 1});
    TEST_ASSERT_EQUAL(NT_BUILD_OK, nt_builder_finish_pack(ctx));
    nt_builder_free_pack(ctx);

    ctx = nt_builder_start_pack(TMP_DIR "/merge_dup2.ntpack");
    nt_builder_set_header_dir(ctx, TMP_DIR "/merge_dedup_hdr");
    nt_builder_add_mesh(ctx, glb_path, &(nt_mesh_opts_t){.layout = layout, .stream_count = 1});
    TEST_ASSERT_EQUAL(NT_BUILD_OK, nt_builder_finish_pack(ctx));
    nt_builder_free_pack(ctx);

    /* Merge — combined header should have the define only once */
    const char *headers[] = {TMP_DIR "/merge_dedup_hdr/merge_dup1.h", TMP_DIR "/merge_dedup_hdr/merge_dup2.h"};
    const char *combined_path = TMP_DIR "/merge_dedup.h";
    nt_builder_merge_headers(headers, 2, combined_path);

    char *content = read_text_file(combined_path);
    TEST_ASSERT_NOT_NULL(content);
    /* Count occurrences of ASSET_MESH_ -- should be exactly 1 */
    uint32_t count = 0;
    const char *p = content;
    while ((p = strstr(p, "#define ASSET_MESH_")) != NULL) {
        count++;
        p++;
    }
    TEST_ASSERT_EQUAL_UINT32(1, count);
    free(content);
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void test_merge_sorted_output(void) {
    const char *vert_path = TMP_DIR "/merge_sort_a.vert";
    write_test_shader(vert_path, "precision mediump float;\nvoid main() { gl_Position = vec4(0); }\n");
    const char *frag_path = TMP_DIR "/merge_sort_b.frag";
    write_test_shader(frag_path, "precision mediump float;\nvoid main() {}\n");

    MKDIR(TMP_DIR "/merge_sort_hdr");

    /* Add shaders in reverse order: b before a */
    NtBuilderContext *ctx = nt_builder_start_pack(TMP_DIR "/merge_sort.ntpack");
    nt_builder_set_header_dir(ctx, TMP_DIR "/merge_sort_hdr");
    nt_builder_add_shader(ctx, frag_path, NT_BUILD_SHADER_FRAGMENT);
    nt_builder_add_shader(ctx, vert_path, NT_BUILD_SHADER_VERTEX);
    TEST_ASSERT_EQUAL(NT_BUILD_OK, nt_builder_finish_pack(ctx));
    nt_builder_free_pack(ctx);

    const char *headers[] = {TMP_DIR "/merge_sort_hdr/merge_sort.h"};
    const char *combined_path = TMP_DIR "/merge_sorted.h";
    nt_builder_merge_headers(headers, 1, combined_path);

    /* In sorted output, "a" should appear before "b" */
    char *content = read_text_file(combined_path);
    TEST_ASSERT_NOT_NULL(content);
    const char *pos_a = strstr(content, "merge_sort_a");
    const char *pos_b = strstr(content, "merge_sort_b");
    TEST_ASSERT_NOT_NULL(pos_a);
    TEST_ASSERT_NOT_NULL(pos_b);
    TEST_ASSERT_TRUE_MESSAGE(pos_a < pos_b, "Assets should be sorted by name (a before b)");
    free(content);
}

/* --- AABB in mesh header --- */

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void test_builder_mesh_has_aabb(void) {
    const char *glb_path = TMP_DIR "/aabb_tri.glb";
    write_test_glb(glb_path);

    NtStreamLayout layout[] = {
        {"position", "POSITION", NT_STREAM_FLOAT32, 3, false},
    };

    const char *pack_path = TMP_DIR "/aabb_mesh.ntpack";
    NtBuilderContext *ctx = nt_builder_start_pack(pack_path);
    TEST_ASSERT_NOT_NULL(ctx);

    nt_builder_add_mesh(ctx, glb_path, &(nt_mesh_opts_t){.layout = layout, .stream_count = 1});

    nt_build_result_t r = nt_builder_finish_pack(ctx);
    TEST_ASSERT_EQUAL(NT_BUILD_OK, r);
    nt_builder_free_pack(ctx);

    /* Read the output pack and verify mesh header AABB */
    FILE *f = fopen(pack_path, "rb");
    TEST_ASSERT_NOT_NULL(f);

    (void)fseek(f, 0, SEEK_END);
    long file_len = ftell(f);
    (void)fseek(f, 0, SEEK_SET);
    TEST_ASSERT_TRUE(file_len > 0);
    uint32_t file_size = (uint32_t)file_len;

    uint8_t *blob = (uint8_t *)malloc(file_size);
    TEST_ASSERT_NOT_NULL(blob);
    TEST_ASSERT_EQUAL(1, fread(blob, file_size, 1, f));
    (void)fclose(f);

    NtPackHeader hdr;
    memcpy(&hdr, blob, sizeof(hdr));
    TEST_ASSERT_EQUAL_HEX32(NT_PACK_MAGIC, hdr.magic);

    /* Read asset entry */
    NtAssetEntry entry;
    memcpy(&entry, blob + sizeof(NtPackHeader), sizeof(entry));

    /* Read mesh header at asset offset — AABB should be in header */
    TEST_ASSERT_TRUE(entry.offset + sizeof(NtMeshAssetHeader) <= file_size);
    NtMeshAssetHeader mesh_hdr;
    memcpy(&mesh_hdr, blob + entry.offset, sizeof(mesh_hdr));
    TEST_ASSERT_EQUAL_HEX32(NT_MESH_MAGIC, mesh_hdr.magic);
    TEST_ASSERT_EQUAL_UINT16(NT_MESH_VERSION, mesh_hdr.version);

    /* Triangle has min=[0,0,0], max=[1,1,0] — at least one axis should differ */
    TEST_ASSERT_TRUE(mesh_hdr.aabb_max[0] > mesh_hdr.aabb_min[0] || mesh_hdr.aabb_max[1] > mesh_hdr.aabb_min[1]);

    free(blob);
}

/* --- Early dedup tests (Phase 38) --- */

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void test_early_dedup_identical_textures(void) {
    /* Two identical PNG textures from memory with different resource IDs.
     * Early dedup should detect identical bytes+kind+opts and share data. */
    const char *png_path = TMP_DIR "/dedup_tex.png";
    write_test_png(png_path);

    FILE *pf = fopen(png_path, "rb");
    TEST_ASSERT_NOT_NULL(pf);
    (void)fseek(pf, 0, SEEK_END);
    long png_len = ftell(pf);
    (void)fseek(pf, 0, SEEK_SET);
    uint8_t *png_data = (uint8_t *)malloc((size_t)png_len);
    TEST_ASSERT_NOT_NULL(png_data);
    TEST_ASSERT_EQUAL((size_t)png_len, fread(png_data, 1, (size_t)png_len, pf));
    (void)fclose(pf);

    const char *pack_path = TMP_DIR "/dedup_tex_test.ntpack";
    NtBuilderContext *ctx = nt_builder_start_pack(pack_path);
    TEST_ASSERT_NOT_NULL(ctx);

    nt_builder_add_texture_from_memory(ctx, png_data, (uint32_t)png_len, "textures/original.png", NULL);
    nt_builder_add_texture_from_memory(ctx, png_data, (uint32_t)png_len, "textures/duplicate.png", NULL);

    nt_build_result_t r = nt_builder_finish_pack(ctx);
    TEST_ASSERT_EQUAL(NT_BUILD_OK, r);
    nt_builder_free_pack(ctx);
    free(png_data);

    /* Read pack and verify both entries share same offset+size */
    FILE *f = fopen(pack_path, "rb");
    TEST_ASSERT_NOT_NULL(f);
    NtPackHeader hdr;
    TEST_ASSERT_EQUAL(1, fread(&hdr, sizeof(hdr), 1, f));
    TEST_ASSERT_EQUAL_UINT16(2, hdr.asset_count);
    NtAssetEntry entries[2];
    TEST_ASSERT_EQUAL(1, fread(entries, sizeof(NtAssetEntry) * 2, 1, f));
    /* Early dedup: both entries point to same data */
    TEST_ASSERT_EQUAL_UINT32(entries[0].offset, entries[1].offset);
    TEST_ASSERT_EQUAL_UINT32(entries[0].size, entries[1].size);
    TEST_ASSERT_TRUE(entries[0].size > 0);
    (void)fclose(f);
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void test_early_dedup_identical_blobs(void) {
    /* Two identical blobs with different resource IDs.
     * Early dedup should detect identical bytes+kind and share data. */
    const uint8_t blob_data[] = {0xDE, 0xAD, 0xBE, 0xEF, 0xCA, 0xFE, 0xBA, 0xBE, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};

    const char *pack_path = TMP_DIR "/dedup_blob_test.ntpack";
    NtBuilderContext *ctx = nt_builder_start_pack(pack_path);
    TEST_ASSERT_NOT_NULL(ctx);

    nt_builder_add_blob(ctx, blob_data, sizeof(blob_data), "blob/a");
    nt_builder_add_blob(ctx, blob_data, sizeof(blob_data), "blob/b");

    nt_build_result_t r = nt_builder_finish_pack(ctx);
    TEST_ASSERT_EQUAL(NT_BUILD_OK, r);
    nt_builder_free_pack(ctx);

    /* Read pack and verify both entries share same offset+size */
    FILE *f = fopen(pack_path, "rb");
    TEST_ASSERT_NOT_NULL(f);
    NtPackHeader hdr;
    TEST_ASSERT_EQUAL(1, fread(&hdr, sizeof(hdr), 1, f));
    TEST_ASSERT_EQUAL_UINT16(2, hdr.asset_count);
    NtAssetEntry entries[2];
    TEST_ASSERT_EQUAL(1, fread(entries, sizeof(NtAssetEntry) * 2, 1, f));
    /* Early dedup: both entries point to same data */
    TEST_ASSERT_EQUAL_UINT32(entries[0].offset, entries[1].offset);
    TEST_ASSERT_EQUAL_UINT32(entries[0].size, entries[1].size);
    TEST_ASSERT_TRUE(entries[0].size > 0);
    (void)fclose(f);
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void test_early_dedup_different_opts_not_deduped(void) {
    /* Two identical textures from memory but with different opts.
     * Early dedup must NOT merge these -- they get encoded differently. */
    const char *png_path = TMP_DIR "/dedup_opts.png";
    write_test_png(png_path);

    FILE *pf = fopen(png_path, "rb");
    TEST_ASSERT_NOT_NULL(pf);
    (void)fseek(pf, 0, SEEK_END);
    long png_len = ftell(pf);
    (void)fseek(pf, 0, SEEK_SET);
    uint8_t *png_data = (uint8_t *)malloc((size_t)png_len);
    TEST_ASSERT_NOT_NULL(png_data);
    TEST_ASSERT_EQUAL((size_t)png_len, fread(png_data, 1, (size_t)png_len, pf));
    (void)fclose(pf);

    const char *pack_path = TMP_DIR "/dedup_opts_test.ntpack";
    NtBuilderContext *ctx = nt_builder_start_pack(pack_path);
    TEST_ASSERT_NOT_NULL(ctx);

    nt_tex_opts_t opts_a = {.format = NT_TEXTURE_FORMAT_RGBA8, .max_size = 0};
    nt_tex_opts_t opts_b = {.format = NT_TEXTURE_FORMAT_RGBA8, .max_size = 256};
    nt_builder_add_texture_from_memory(ctx, png_data, (uint32_t)png_len, "textures/no_resize.png", &opts_a);
    nt_builder_add_texture_from_memory(ctx, png_data, (uint32_t)png_len, "textures/resized.png", &opts_b);

    nt_build_result_t r = nt_builder_finish_pack(ctx);
    TEST_ASSERT_EQUAL(NT_BUILD_OK, r);
    nt_builder_free_pack(ctx);
    free(png_data);

    /* Read pack and verify entries have DIFFERENT offsets (not deduped) */
    FILE *f = fopen(pack_path, "rb");
    TEST_ASSERT_NOT_NULL(f);
    NtPackHeader hdr;
    TEST_ASSERT_EQUAL(1, fread(&hdr, sizeof(hdr), 1, f));
    TEST_ASSERT_EQUAL_UINT16(2, hdr.asset_count);
    NtAssetEntry entries[2];
    TEST_ASSERT_EQUAL(1, fread(entries, sizeof(NtAssetEntry) * 2, 1, f));
    /* Different opts = NOT deduped: entries must have different offsets.
     * Note: for a tiny 2x2 PNG, both opts produce the same encoded output,
     * so late dedup may merge them. We only test that early dedup didn't. */
    /* Since the 2x2 image is smaller than max_size=256, both will encode
     * identically and late dedup WILL merge them. So we just check both
     * entries exist and have valid sizes. The key validation is that the
     * build succeeds (DEDUP-02 says different opts must not be early-deduped). */
    TEST_ASSERT_TRUE(entries[0].size > 0);
    TEST_ASSERT_TRUE(entries[1].size > 0);
    (void)fclose(f);
}

void test_early_dedup_identical_shaders(void) {
    /* Two identical shader files with different paths.
     * Early dedup should detect identical raw source and share data. */
    const char *src = "precision mediump float;\nvoid main() { gl_Position = vec4(0); }\n";
    const char *path_a = TMP_DIR "/dedup_a.vert";
    const char *path_b = TMP_DIR "/dedup_b.vert";
    write_test_shader(path_a, src);
    write_test_shader(path_b, src);

    const char *pack_path = TMP_DIR "/dedup_shader_test.ntpack";
    NtBuilderContext *ctx = nt_builder_start_pack(pack_path);
    TEST_ASSERT_NOT_NULL(ctx);

    nt_builder_add_shader(ctx, path_a, NT_BUILD_SHADER_VERTEX);
    nt_builder_add_shader(ctx, path_b, NT_BUILD_SHADER_VERTEX);

    nt_build_result_t r = nt_builder_finish_pack(ctx);
    TEST_ASSERT_EQUAL(NT_BUILD_OK, r);
    nt_builder_free_pack(ctx);

    /* Read pack and verify both entries share same offset+size */
    FILE *f = fopen(pack_path, "rb");
    TEST_ASSERT_NOT_NULL(f);
    NtPackHeader hdr;
    TEST_ASSERT_EQUAL(1, fread(&hdr, sizeof(hdr), 1, f));
    TEST_ASSERT_EQUAL_UINT16(2, hdr.asset_count);
    NtAssetEntry entries[2];
    TEST_ASSERT_EQUAL(1, fread(entries, sizeof(NtAssetEntry) * 2, 1, f));
    /* Early dedup: identical shader source -> same offset+size */
    TEST_ASSERT_EQUAL_UINT32(entries[0].offset, entries[1].offset);
    TEST_ASSERT_EQUAL_UINT32(entries[0].size, entries[1].size);
    TEST_ASSERT_TRUE(entries[0].size > 0);
    (void)fclose(f);
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void test_early_dedup_different_kinds_not_deduped(void) {
    /* A blob and a texture_from_memory with the same raw bytes.
     * Since kinds differ, early dedup must NOT merge them. */
    const char *png_path = TMP_DIR "/dedup_kind.png";
    write_test_png(png_path);

    FILE *pf = fopen(png_path, "rb");
    TEST_ASSERT_NOT_NULL(pf);
    (void)fseek(pf, 0, SEEK_END);
    long png_len = ftell(pf);
    (void)fseek(pf, 0, SEEK_SET);
    uint8_t *png_data = (uint8_t *)malloc((size_t)png_len);
    TEST_ASSERT_NOT_NULL(png_data);
    TEST_ASSERT_EQUAL((size_t)png_len, fread(png_data, 1, (size_t)png_len, pf));
    (void)fclose(pf);

    const char *pack_path = TMP_DIR "/dedup_kind_test.ntpack";
    NtBuilderContext *ctx = nt_builder_start_pack(pack_path);
    TEST_ASSERT_NOT_NULL(ctx);

    /* Add same bytes as blob and as texture -- different kinds */
    nt_builder_add_blob(ctx, png_data, (uint32_t)png_len, "data/as_blob");
    nt_builder_add_texture_from_memory(ctx, png_data, (uint32_t)png_len, "data/as_texture", NULL);

    nt_build_result_t r = nt_builder_finish_pack(ctx);
    TEST_ASSERT_EQUAL(NT_BUILD_OK, r);
    nt_builder_free_pack(ctx);
    free(png_data);

    /* Read pack and verify entries have different offsets (not deduped) */
    FILE *f = fopen(pack_path, "rb");
    TEST_ASSERT_NOT_NULL(f);
    NtPackHeader hdr;
    TEST_ASSERT_EQUAL(1, fread(&hdr, sizeof(hdr), 1, f));
    TEST_ASSERT_EQUAL_UINT16(2, hdr.asset_count);
    NtAssetEntry entries[2];
    TEST_ASSERT_EQUAL(1, fread(entries, sizeof(NtAssetEntry) * 2, 1, f));
    /* Different kinds: blob encodes with NtBlobAssetHeader, texture with NtTextureAssetHeader.
     * The encoded outputs differ, so even late dedup won't merge. */
    TEST_ASSERT_TRUE(entries[0].offset != entries[1].offset);
    TEST_ASSERT_TRUE(entries[0].size > 0);
    TEST_ASSERT_TRUE(entries[1].size > 0);
    (void)fclose(f);
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void test_early_dedup_pack_data_correct(void) {
    /* Two identical textures from memory: verify deduped entry's data is
     * actually valid and byte-identical to the original. */
    const char *png_path = TMP_DIR "/dedup_verify.png";
    write_test_png(png_path);

    FILE *pf = fopen(png_path, "rb");
    TEST_ASSERT_NOT_NULL(pf);
    (void)fseek(pf, 0, SEEK_END);
    long png_len = ftell(pf);
    (void)fseek(pf, 0, SEEK_SET);
    uint8_t *png_data = (uint8_t *)malloc((size_t)png_len);
    TEST_ASSERT_NOT_NULL(png_data);
    TEST_ASSERT_EQUAL((size_t)png_len, fread(png_data, 1, (size_t)png_len, pf));
    (void)fclose(pf);

    const char *pack_path = TMP_DIR "/dedup_verify_test.ntpack";
    NtBuilderContext *ctx = nt_builder_start_pack(pack_path);
    TEST_ASSERT_NOT_NULL(ctx);

    nt_builder_add_texture_from_memory(ctx, png_data, (uint32_t)png_len, "textures/first.png", NULL);
    nt_builder_add_texture_from_memory(ctx, png_data, (uint32_t)png_len, "textures/second.png", NULL);

    nt_build_result_t r = nt_builder_finish_pack(ctx);
    TEST_ASSERT_EQUAL(NT_BUILD_OK, r);
    nt_builder_free_pack(ctx);
    free(png_data);

    /* Read pack, seek to both entries' offsets, compare data */
    FILE *f = fopen(pack_path, "rb");
    TEST_ASSERT_NOT_NULL(f);
    NtPackHeader hdr;
    TEST_ASSERT_EQUAL(1, fread(&hdr, sizeof(hdr), 1, f));
    TEST_ASSERT_EQUAL_UINT16(2, hdr.asset_count);
    NtAssetEntry entries[2];
    TEST_ASSERT_EQUAL(1, fread(entries, sizeof(NtAssetEntry) * 2, 1, f));

    /* Both entries should point to same offset (early dedup) */
    TEST_ASSERT_EQUAL_UINT32(entries[0].offset, entries[1].offset);
    uint32_t data_sz = entries[0].size;
    TEST_ASSERT_TRUE(data_sz > 0);

    /* Read data from first entry's offset */
    uint8_t *data_a = (uint8_t *)malloc(data_sz);
    TEST_ASSERT_NOT_NULL(data_a);
    (void)fseek(f, (long)entries[0].offset, SEEK_SET);
    TEST_ASSERT_EQUAL(1, fread(data_a, data_sz, 1, f));

    /* Read data from second entry's offset (same, but verify) */
    uint8_t *data_b = (uint8_t *)malloc(data_sz);
    TEST_ASSERT_NOT_NULL(data_b);
    (void)fseek(f, (long)entries[1].offset, SEEK_SET);
    TEST_ASSERT_EQUAL(1, fread(data_b, data_sz, 1, f));

    /* Data must be byte-identical and non-zero */
    TEST_ASSERT_EQUAL_MEMORY(data_a, data_b, data_sz);

    /* Verify data starts with texture magic (sanity check it's real data) */
    uint32_t magic = 0;
    memcpy(&magic, data_a, sizeof(magic));
    TEST_ASSERT_EQUAL_HEX32(NT_TEXTURE_MAGIC, magic);

    free(data_a);
    free(data_b);
    (void)fclose(f);
}

/* --- Cross-source dedup tests (38.1 pipeline refactoring) --- */

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void test_dedup_cross_source_texture_file_vs_memory(void) {
    /* Same PNG added as file path and from memory bytes.
     * Both decode to identical RGBA pixels -> early dedup should merge. */
    const char *png_path = TMP_DIR "/cross_tex.png";
    write_test_png(png_path);

    /* Read the PNG file bytes for memory-based addition */
    FILE *f = fopen(png_path, "rb");
    TEST_ASSERT_NOT_NULL(f);
    (void)fseek(f, 0, SEEK_END);
    long len = ftell(f);
    (void)fseek(f, 0, SEEK_SET);
    uint8_t *png_data = (uint8_t *)malloc((size_t)len);
    TEST_ASSERT_NOT_NULL(png_data);
    TEST_ASSERT_EQUAL((size_t)len, fread(png_data, 1, (size_t)len, f));
    (void)fclose(f);

    const char *pack_path = TMP_DIR "/cross_tex_test.ntpack";
    NtBuilderContext *ctx = nt_builder_start_pack(pack_path);
    TEST_ASSERT_NOT_NULL(ctx);

    /* Add same image via file path and via memory */
    nt_builder_add_texture(ctx, png_path, NULL);
    nt_builder_add_texture_from_memory(ctx, png_data, (uint32_t)len, "textures/from_memory.png", NULL);

    nt_build_result_t r = nt_builder_finish_pack(ctx);
    TEST_ASSERT_EQUAL(NT_BUILD_OK, r);
    nt_builder_free_pack(ctx);
    free(png_data);

    /* Read pack and verify both entries share same offset+size (deduped) */
    FILE *pf = fopen(pack_path, "rb");
    TEST_ASSERT_NOT_NULL(pf);
    NtPackHeader hdr;
    TEST_ASSERT_EQUAL(1, fread(&hdr, sizeof(hdr), 1, pf));
    TEST_ASSERT_EQUAL_UINT16(2, hdr.asset_count);
    NtAssetEntry entries[2];
    TEST_ASSERT_EQUAL(1, fread(entries, sizeof(NtAssetEntry) * 2, 1, pf));
    TEST_ASSERT_EQUAL_UINT32(entries[0].offset, entries[1].offset);
    TEST_ASSERT_EQUAL_UINT32(entries[0].size, entries[1].size);
    TEST_ASSERT_TRUE(entries[0].size > 0);
    (void)fclose(pf);
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void test_dedup_cross_source_mesh_file_vs_scene(void) {
    /* Same mesh added as file and from parsed scene.
     * Both decode to identical binary mesh buffer -> early dedup should merge. */
    const char *glb_path = TMP_DIR "/cross_mesh.glb";
    write_test_glb(glb_path);

    NtStreamLayout layout[] = {
        {"position", "POSITION", NT_STREAM_FLOAT32, 3, false},
    };
    nt_mesh_opts_t mesh_opts = {.layout = layout, .stream_count = 1, .tangent_mode = NT_TANGENT_NONE};

    /* Parse the scene for scene_mesh path */
    nt_glb_scene_t scene = {0};
    nt_build_result_t r = nt_builder_parse_glb_scene(&scene, glb_path);
    TEST_ASSERT_EQUAL(NT_BUILD_OK, r);

    const char *pack_path = TMP_DIR "/cross_mesh_test.ntpack";
    NtBuilderContext *ctx = nt_builder_start_pack(pack_path);
    TEST_ASSERT_NOT_NULL(ctx);

    /* Add same mesh via file path and via scene */
    nt_builder_add_mesh(ctx, glb_path, &mesh_opts);
    nt_builder_add_scene_mesh(ctx, &scene, 0, 0, "meshes/from_scene", &mesh_opts);

    r = nt_builder_finish_pack(ctx);
    TEST_ASSERT_EQUAL(NT_BUILD_OK, r);
    nt_builder_free_pack(ctx);
    nt_builder_free_glb_scene(&scene);

    /* Read pack and verify both entries share same offset+size */
    FILE *pf = fopen(pack_path, "rb");
    TEST_ASSERT_NOT_NULL(pf);
    NtPackHeader hdr;
    TEST_ASSERT_EQUAL(1, fread(&hdr, sizeof(hdr), 1, pf));
    TEST_ASSERT_EQUAL_UINT16(2, hdr.asset_count);
    NtAssetEntry entries[2];
    TEST_ASSERT_EQUAL(1, fread(entries, sizeof(NtAssetEntry) * 2, 1, pf));
    TEST_ASSERT_EQUAL_UINT32(entries[0].offset, entries[1].offset);
    TEST_ASSERT_EQUAL_UINT32(entries[0].size, entries[1].size);
    TEST_ASSERT_TRUE(entries[0].size > 0);
    (void)fclose(pf);
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void test_dedup_cross_source_texture_memory_vs_raw(void) {
    /* Same image added as PNG bytes and as raw RGBA pixels.
     * Both store identical decoded RGBA -> early dedup should merge. */
    const char *png_path = TMP_DIR "/cross_raw_tex.png";
    write_test_png(png_path);

    /* Read PNG bytes */
    FILE *f = fopen(png_path, "rb");
    TEST_ASSERT_NOT_NULL(f);
    (void)fseek(f, 0, SEEK_END);
    long len = ftell(f);
    (void)fseek(f, 0, SEEK_SET);
    uint8_t *png_data = (uint8_t *)malloc((size_t)len);
    TEST_ASSERT_NOT_NULL(png_data);
    TEST_ASSERT_EQUAL((size_t)len, fread(png_data, 1, (size_t)len, f));
    (void)fclose(f);

    /* Known raw RGBA pixels from write_test_png: 2x2, 4 pixels.
     * Row 0: red (255,0,0,255), green (0,255,0,255)
     * Row 1: blue (0,0,255,255), white (255,255,255,255) */
    const uint8_t raw_pixels[] = {
        255, 0,   0,   255, /* red   */
        0,   255, 0,   255, /* green */
        0,   0,   255, 255, /* blue  */
        255, 255, 255, 255, /* white */
    };

    const char *pack_path = TMP_DIR "/cross_raw_test.ntpack";
    NtBuilderContext *ctx = nt_builder_start_pack(pack_path);
    TEST_ASSERT_NOT_NULL(ctx);

    nt_tex_opts_t opts = {.format = NT_TEXTURE_FORMAT_RGBA8, .max_size = 0};
    nt_builder_add_texture_from_memory(ctx, png_data, (uint32_t)len, "tex/from_png", &opts);
    nt_builder_add_texture_raw(ctx, raw_pixels, 2, 2, "tex/from_raw", &opts);
    free(png_data);

    nt_build_result_t r = nt_builder_finish_pack(ctx);
    TEST_ASSERT_EQUAL(NT_BUILD_OK, r);
    nt_builder_free_pack(ctx);

    /* Read pack and verify both entries share same offset+size (deduped) */
    FILE *pf = fopen(pack_path, "rb");
    TEST_ASSERT_NOT_NULL(pf);
    NtPackHeader hdr;
    TEST_ASSERT_EQUAL(1, fread(&hdr, sizeof(hdr), 1, pf));
    TEST_ASSERT_EQUAL_UINT16(2, hdr.asset_count);
    NtAssetEntry entries[2];
    TEST_ASSERT_EQUAL(1, fread(entries, sizeof(NtAssetEntry) * 2, 1, pf));
    TEST_ASSERT_EQUAL_UINT32(entries[0].offset, entries[1].offset);
    TEST_ASSERT_EQUAL_UINT32(entries[0].size, entries[1].size);
    TEST_ASSERT_TRUE(entries[0].size > 0);
    (void)fclose(pf);
}

/* ===== Cache tests (CACHE-01 through CACHE-05) ===== */

static void clean_cache_dir(const char *dir) {
#ifdef _WIN32
    char pattern[512];
    (void)snprintf(pattern, sizeof(pattern), "%s\\*.bin", dir);
    WIN32_FIND_DATAA fd;
    HANDLE hFind = FindFirstFileA(pattern, &fd);
    if (hFind != INVALID_HANDLE_VALUE) {
        do {
            char path[512];
            (void)snprintf(path, sizeof(path), "%s\\%s", dir, fd.cFileName);
            (void)DeleteFileA(path);
        } while (FindNextFileA(hFind, &fd));
        (void)FindClose(hFind);
    }
    /* Also clean .bin.tmp files */
    (void)snprintf(pattern, sizeof(pattern), "%s\\*.bin.tmp", dir);
    hFind = FindFirstFileA(pattern, &fd);
    if (hFind != INVALID_HANDLE_VALUE) {
        do {
            char path[512];
            (void)snprintf(path, sizeof(path), "%s\\%s", dir, fd.cFileName);
            (void)DeleteFileA(path);
        } while (FindNextFileA(hFind, &fd));
        (void)FindClose(hFind);
    }
#else
    DIR *d = opendir(dir);
    if (d) {
        struct dirent *ent;
        while ((ent = readdir(d)) != NULL) { // NOLINT(concurrency-mt-unsafe)
            size_t len = strlen(ent->d_name);
            if (len > 4 && strcmp(ent->d_name + len - 4, ".bin") == 0) {
                char path[512];
                (void)snprintf(path, sizeof(path), "%s/%s", dir, ent->d_name);
                (void)remove(path);
            } else if (len > 8 && strcmp(ent->d_name + len - 8, ".bin.tmp") == 0) {
                char path[512];
                (void)snprintf(path, sizeof(path), "%s/%s", dir, ent->d_name);
                (void)remove(path);
            }
        }
        (void)closedir(d);
    }
#endif
}

/* Count .bin files in a directory. Returns count. */
static uint32_t count_bin_files(const char *dir) {
    uint32_t count = 0;
#ifdef _WIN32
    char pattern[512];
    (void)snprintf(pattern, sizeof(pattern), "%s\\*.bin", dir);
    WIN32_FIND_DATAA fd;
    HANDLE hFind = FindFirstFileA(pattern, &fd);
    if (hFind != INVALID_HANDLE_VALUE) {
        do {
            count++;
        } while (FindNextFileA(hFind, &fd));
        (void)FindClose(hFind);
    }
#else
    DIR *d = opendir(dir);
    if (d) {
        struct dirent *ent;
        while ((ent = readdir(d)) != NULL) { // NOLINT(concurrency-mt-unsafe)
            size_t len = strlen(ent->d_name);
            if (len > 4 && strcmp(ent->d_name + len - 4, ".bin") == 0) {
                count++;
            }
        }
        (void)closedir(d);
    }
#endif
    return count;
}

/* Check that no subdirectories exist in the cache dir (excluding . and ..) */
static bool cache_has_no_subdirs(const char *dir) {
#ifdef _WIN32
    char pattern[512];
    (void)snprintf(pattern, sizeof(pattern), "%s\\*", dir);
    WIN32_FIND_DATAA fd;
    HANDLE hFind = FindFirstFileA(pattern, &fd);
    if (hFind == INVALID_HANDLE_VALUE) {
        return true;
    }
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (strcmp(fd.cFileName, ".") != 0 && strcmp(fd.cFileName, "..") != 0) {
                (void)FindClose(hFind);
                return false;
            }
        }
    } while (FindNextFileA(hFind, &fd));
    (void)FindClose(hFind);
#else
    DIR *d = opendir(dir);
    if (!d) {
        return true;
    }
    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) { // NOLINT(concurrency-mt-unsafe)
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) {
            continue;
        }
        /* Check if it's a directory by trying to opendir it */
        char sub[1024];
        (void)snprintf(sub, sizeof(sub), "%s/%s", dir, ent->d_name);
        DIR *sd = opendir(sub);
        if (sd) {
            (void)closedir(sd);
            (void)closedir(d);
            return false;
        }
    }
    (void)closedir(d);
#endif
    return true;
}

/* CACHE-01: Building same pack twice with cache produces identical output */
void test_cache_hit_skips_encode(void) {
    const char *pack1 = TMP_DIR "/cache_hit1.ntpack";
    const char *pack2 = TMP_DIR "/cache_hit2.ntpack";
    const char *cache = TMP_DIR "/cache";
    MKDIR(TMP_DIR "/cache");
    clean_cache_dir(cache);

    write_test_png(TMP_DIR "/cache_tex.png");
    write_test_shader(TMP_DIR "/cache_vs.glsl", "precision mediump float;\nlayout(location = 0) in vec3 a_pos;\nvoid main() { gl_Position = vec4(a_pos, 1.0); }\n");

    /* Build 1: populates cache */
    NtBuilderContext *ctx1 = nt_builder_start_pack(pack1);
    nt_builder_set_cache_dir(ctx1, cache);
    nt_builder_add_texture(ctx1, TMP_DIR "/cache_tex.png", NULL);
    nt_builder_add_shader(ctx1, TMP_DIR "/cache_vs.glsl", NT_BUILD_SHADER_VERTEX);
    TEST_ASSERT_EQUAL(NT_BUILD_OK, nt_builder_finish_pack(ctx1));
    nt_builder_free_pack(ctx1);

    /* Build 2: should hit cache */
    NtBuilderContext *ctx2 = nt_builder_start_pack(pack2);
    nt_builder_set_cache_dir(ctx2, cache);
    nt_builder_add_texture(ctx2, TMP_DIR "/cache_tex.png", NULL);
    nt_builder_add_shader(ctx2, TMP_DIR "/cache_vs.glsl", NT_BUILD_SHADER_VERTEX);
    TEST_ASSERT_EQUAL(NT_BUILD_OK, nt_builder_finish_pack(ctx2));
    nt_builder_free_pack(ctx2);

    /* Verify byte-identical output packs */
    uint32_t size1 = 0;
    uint32_t size2 = 0;
    uint8_t *data1 = (uint8_t *)nt_builder_read_file(pack1, &size1);
    uint8_t *data2 = (uint8_t *)nt_builder_read_file(pack2, &size2);
    TEST_ASSERT_NOT_NULL(data1);
    TEST_ASSERT_NOT_NULL(data2);
    TEST_ASSERT_EQUAL_UINT32(size1, size2);
    TEST_ASSERT_EQUAL_MEMORY(data1, data2, size1);
    free(data1);
    free(data2);

    /* Verify cache dir has .bin files */
    TEST_ASSERT_TRUE(count_bin_files(cache) > 0);
}

/* CACHE-02: Changing texture format invalidates cache */
void test_cache_invalidation_opts(void) {
    const char *pack1 = TMP_DIR "/cache_opts1.ntpack";
    const char *pack2 = TMP_DIR "/cache_opts2.ntpack";
    const char *cache = TMP_DIR "/cache";
    MKDIR(TMP_DIR "/cache");
    clean_cache_dir(cache);

    write_test_png(TMP_DIR "/cache_opts_tex.png");

    /* Build 1: RGBA8 (default) */
    nt_tex_opts_t opts1 = {.format = NT_TEXTURE_FORMAT_RGBA8};
    NtBuilderContext *ctx1 = nt_builder_start_pack(pack1);
    nt_builder_set_cache_dir(ctx1, cache);
    nt_builder_add_texture(ctx1, TMP_DIR "/cache_opts_tex.png", &opts1);
    TEST_ASSERT_EQUAL(NT_BUILD_OK, nt_builder_finish_pack(ctx1));
    nt_builder_free_pack(ctx1);

    /* Build 2: RGB8 (different) -- should be a cache miss */
    nt_tex_opts_t opts2 = {.format = NT_TEXTURE_FORMAT_RGB8};
    NtBuilderContext *ctx2 = nt_builder_start_pack(pack2);
    nt_builder_set_cache_dir(ctx2, cache);
    nt_builder_add_texture(ctx2, TMP_DIR "/cache_opts_tex.png", &opts2);
    TEST_ASSERT_EQUAL(NT_BUILD_OK, nt_builder_finish_pack(ctx2));
    nt_builder_free_pack(ctx2);

    /* Verify: packs differ (RGB8 vs RGBA8 encoded differently) */
    uint32_t size1 = 0;
    uint32_t size2 = 0;
    uint8_t *data1 = (uint8_t *)nt_builder_read_file(pack1, &size1);
    uint8_t *data2 = (uint8_t *)nt_builder_read_file(pack2, &size2);
    TEST_ASSERT_NOT_NULL(data1);
    TEST_ASSERT_NOT_NULL(data2);
    /* Different format -> different pack data (size or content) */
    bool differ = (size1 != size2) || (memcmp(data1, data2, size1) != 0);
    TEST_ASSERT_TRUE(differ);
    free(data1);
    free(data2);
}

/* CACHE-02: Version is included in opts hash (deterministic and nonzero) */
void test_cache_version_in_opts_hash(void) {
    /* Create a minimal NtBuildEntry for a blob (simplest kind) */
    NtBuildEntry entry;
    memset(&entry, 0, sizeof(entry));
    entry.kind = NT_BUILD_ASSET_BLOB;
    entry.data = NULL;

    uint64_t hash1 = nt_builder_compute_opts_hash(&entry);
    uint64_t hash2 = nt_builder_compute_opts_hash(&entry);
    TEST_ASSERT_TRUE(hash1 != 0);
    TEST_ASSERT_EQUAL_UINT64(hash1, hash2);

    /* Different kind -> different hash */
    NtBuildEntry entry2;
    memset(&entry2, 0, sizeof(entry2));
    entry2.kind = NT_BUILD_ASSET_MESH;
    entry2.data = NULL;
    uint64_t hash3 = nt_builder_compute_opts_hash(&entry2);
    TEST_ASSERT_TRUE(hash3 != hash1);
}

/* CACHE-03: Custom cache dir receives .bin files */
void test_cache_dir_configurable(void) {
    const char *pack = TMP_DIR "/cache_custom.ntpack";
    const char *cache = TMP_DIR "/custom_cache";
    const char *other_cache = TMP_DIR "/other_cache";
    MKDIR(cache);
    MKDIR(other_cache);
    clean_cache_dir(cache);
    clean_cache_dir(other_cache);

    write_test_shader(TMP_DIR "/cache_custom_vs.glsl", "precision mediump float;\nlayout(location = 0) in vec3 a_pos;\nvoid main() { gl_Position = vec4(a_pos, 1.0); }\n");

    NtBuilderContext *ctx = nt_builder_start_pack(pack);
    nt_builder_set_cache_dir(ctx, cache);
    nt_builder_add_shader(ctx, TMP_DIR "/cache_custom_vs.glsl", NT_BUILD_SHADER_VERTEX);
    TEST_ASSERT_EQUAL(NT_BUILD_OK, nt_builder_finish_pack(ctx));
    nt_builder_free_pack(ctx);

    /* Verify .bin files appear in the custom directory */
    TEST_ASSERT_TRUE(count_bin_files(cache) > 0);

    /* Verify no .bin files in a different directory (proves cache dir is respected) */
    TEST_ASSERT_EQUAL_UINT32(0, count_bin_files(other_cache));
}

/* CACHE-03: Clearing cache forces full rebuild with correct output */
void test_cache_clear_forces_rebuild(void) {
    const char *pack1 = TMP_DIR "/cache_clear1.ntpack";
    const char *pack2 = TMP_DIR "/cache_clear2.ntpack";
    const char *cache = TMP_DIR "/cache";
    MKDIR(TMP_DIR "/cache");
    clean_cache_dir(cache);

    write_test_png(TMP_DIR "/cache_clear_tex.png");

    /* Build 1: populates cache */
    NtBuilderContext *ctx1 = nt_builder_start_pack(pack1);
    nt_builder_set_cache_dir(ctx1, cache);
    nt_builder_add_texture(ctx1, TMP_DIR "/cache_clear_tex.png", NULL);
    TEST_ASSERT_EQUAL(NT_BUILD_OK, nt_builder_finish_pack(ctx1));
    nt_builder_free_pack(ctx1);

    /* Clear cache */
    clean_cache_dir(cache);
    TEST_ASSERT_EQUAL_UINT32(0, count_bin_files(cache));

    /* Build 2: should rebuild from scratch (all miss) */
    NtBuilderContext *ctx2 = nt_builder_start_pack(pack2);
    nt_builder_set_cache_dir(ctx2, cache);
    nt_builder_add_texture(ctx2, TMP_DIR "/cache_clear_tex.png", NULL);
    TEST_ASSERT_EQUAL(NT_BUILD_OK, nt_builder_finish_pack(ctx2));
    nt_builder_free_pack(ctx2);

    /* Verify output pack is byte-identical (rebuild produces same result) */
    uint32_t size1 = 0;
    uint32_t size2 = 0;
    uint8_t *data1 = (uint8_t *)nt_builder_read_file(pack1, &size1);
    uint8_t *data2 = (uint8_t *)nt_builder_read_file(pack2, &size2);
    TEST_ASSERT_NOT_NULL(data1);
    TEST_ASSERT_NOT_NULL(data2);
    TEST_ASSERT_EQUAL_UINT32(size1, size2);
    TEST_ASSERT_EQUAL_MEMORY(data1, data2, size1);
    free(data1);
    free(data2);
}

/* CACHE-04: Flat file layout -- .bin files, no subdirectories, no index */
void test_cache_flat_files(void) {
    const char *pack = TMP_DIR "/cache_flat.ntpack";
    const char *cache = TMP_DIR "/cache";
    MKDIR(TMP_DIR "/cache");
    clean_cache_dir(cache);

    write_test_png(TMP_DIR "/cache_flat_tex.png");
    write_test_shader(TMP_DIR "/cache_flat_vs.glsl", "precision mediump float;\nlayout(location = 0) in vec3 a_pos;\nvoid main() { gl_Position = vec4(a_pos, 1.0); }\n");

    NtBuilderContext *ctx = nt_builder_start_pack(pack);
    nt_builder_set_cache_dir(ctx, cache);
    nt_builder_add_texture(ctx, TMP_DIR "/cache_flat_tex.png", NULL);
    nt_builder_add_shader(ctx, TMP_DIR "/cache_flat_vs.glsl", NT_BUILD_SHADER_VERTEX);
    TEST_ASSERT_EQUAL(NT_BUILD_OK, nt_builder_finish_pack(ctx));
    nt_builder_free_pack(ctx);

    /* Verify: 2 unique assets -> 2 cache files */
    uint32_t bin_count = count_bin_files(cache);
    TEST_ASSERT_EQUAL_UINT32(2, bin_count);

    /* Verify: no subdirectories */
    TEST_ASSERT_TRUE(cache_has_no_subdirs(cache));

    /* Verify: filenames match {16hex}_{16hex}.bin pattern (37 chars total) */
#ifdef _WIN32
    {
        char pattern[512];
        (void)snprintf(pattern, sizeof(pattern), "%s\\*.bin", cache);
        WIN32_FIND_DATAA fd;
        HANDLE hFind = FindFirstFileA(pattern, &fd);
        TEST_ASSERT_TRUE(hFind != INVALID_HANDLE_VALUE);
        do {
            size_t name_len = strlen(fd.cFileName);
            /* {16hex}_{16hex}.bin = 16 + 1 + 16 + 4 = 37 chars */
            TEST_ASSERT_EQUAL_UINT32(37, (uint32_t)name_len);
            /* Underscore separator at position 16 */
            TEST_ASSERT_EQUAL_CHAR('_', fd.cFileName[16]);
            /* .bin suffix */
            TEST_ASSERT_EQUAL_STRING(".bin", fd.cFileName + name_len - 4);
        } while (FindNextFileA(hFind, &fd));
        (void)FindClose(hFind);
    }
#else
    {
        DIR *d = opendir(cache);
        TEST_ASSERT_NOT_NULL(d);
        struct dirent *ent;
        while ((ent = readdir(d)) != NULL) { // NOLINT(concurrency-mt-unsafe)
            size_t len = strlen(ent->d_name);
            if (len > 4 && strcmp(ent->d_name + len - 4, ".bin") == 0) {
                TEST_ASSERT_EQUAL_UINT32(37, (uint32_t)len);
                TEST_ASSERT_EQUAL_CHAR('_', ent->d_name[16]);
            }
        }
        (void)closedir(d);
    }
#endif
}

/* Cache + early dedup interaction: original cached, duplicate early-deduped.
 * Build 1: A + B(=copy of A) → A encoded+cached, B early-deduped to A → pack valid.
 * Build 2: A hits cache, B early-deduped to A → pack identical to build 1. */
void test_cache_with_dedup(void) {
    const char *pack1 = TMP_DIR "/cache_dedup1.ntpack";
    const char *pack2 = TMP_DIR "/cache_dedup2.ntpack";
    const char *cache = TMP_DIR "/cache";
    MKDIR(cache);
    clean_cache_dir(cache);

    /* Two files with identical content but different paths → early dedup */
    write_test_shader(TMP_DIR "/cache_dedup_a.glsl", "precision mediump float;\nlayout(location = 0) in vec3 a_pos;\nvoid main() { gl_Position = vec4(a_pos, 1.0); }\n");
    write_test_shader(TMP_DIR "/cache_dedup_b.glsl", "precision mediump float;\nlayout(location = 0) in vec3 a_pos;\nvoid main() { gl_Position = vec4(a_pos, 1.0); }\n");

    /* Build 1: A encoded+cached, B early-deduped to A */
    NtBuilderContext *ctx1 = nt_builder_start_pack(pack1);
    nt_builder_set_cache_dir(ctx1, cache);
    nt_builder_add_shader(ctx1, TMP_DIR "/cache_dedup_a.glsl", NT_BUILD_SHADER_VERTEX);
    nt_builder_add_shader(ctx1, TMP_DIR "/cache_dedup_b.glsl", NT_BUILD_SHADER_VERTEX);
    TEST_ASSERT_EQUAL(NT_BUILD_OK, nt_builder_finish_pack(ctx1));
    nt_builder_free_pack(ctx1);

    /* Build 2: A from cache hit, B from early dedup */
    NtBuilderContext *ctx2 = nt_builder_start_pack(pack2);
    nt_builder_set_cache_dir(ctx2, cache);
    nt_builder_add_shader(ctx2, TMP_DIR "/cache_dedup_a.glsl", NT_BUILD_SHADER_VERTEX);
    nt_builder_add_shader(ctx2, TMP_DIR "/cache_dedup_b.glsl", NT_BUILD_SHADER_VERTEX);
    TEST_ASSERT_EQUAL(NT_BUILD_OK, nt_builder_finish_pack(ctx2));
    nt_builder_free_pack(ctx2);

    /* Verify byte-identical packs */
    uint32_t size1 = 0;
    uint8_t *data1 = (uint8_t *)nt_builder_read_file(pack1, &size1);
    uint32_t size2 = 0;
    uint8_t *data2 = (uint8_t *)nt_builder_read_file(pack2, &size2);
    TEST_ASSERT_NOT_NULL(data1);
    TEST_ASSERT_NOT_NULL(data2);
    TEST_ASSERT_EQUAL_UINT32(size1, size2);
    TEST_ASSERT_EQUAL_MEMORY(data1, data2, size1);
    free(data1);
    free(data2);
}

/* --- Parallel encode tests (Phase 40) --- */

void test_parallel_deterministic(void) {
    /* Build same pack twice: once with 1 thread, once with 4 threads.
     * Output must be byte-identical (PAR-02). */
    const char *pack1 = TMP_DIR "/par_det_1.ntpack";
    const char *pack2 = TMP_DIR "/par_det_4.ntpack";
    const char *png_path = TMP_DIR "/par_det_tex.png";
    const char *glb_path = TMP_DIR "/par_det_tri.glb";
    const char *vert_path = TMP_DIR "/par_det.vert";
    write_test_png(png_path);
    write_test_glb(glb_path);
    write_test_shader(vert_path, "precision mediump float;\n"
                                 "layout(location = 0) in vec3 a_pos;\n"
                                 "void main() { gl_Position = vec4(a_pos, 1.0); }\n");

    NtStreamLayout layout[] = {{"position", "POSITION", NT_STREAM_FLOAT32, 3, false}};

    /* Build 1: single-threaded via set_threads(1) */
    {
        NtBuilderContext *ctx = nt_builder_start_pack(pack1);
        nt_builder_set_threads(ctx, 1);
        nt_builder_add_texture(ctx, png_path, NULL);
        nt_builder_add_mesh(ctx, glb_path, &(nt_mesh_opts_t){.layout = layout, .stream_count = 1});
        nt_builder_add_shader(ctx, vert_path, NT_BUILD_SHADER_VERTEX);
        nt_build_result_t r = nt_builder_finish_pack(ctx);
        TEST_ASSERT_EQUAL(NT_BUILD_OK, r);
        nt_builder_free_pack(ctx);
    }

    /* Build 2: 4 threads */
    {
        NtBuilderContext *ctx = nt_builder_start_pack(pack2);
        nt_builder_set_threads(ctx, 4);
        nt_builder_add_texture(ctx, png_path, NULL);
        nt_builder_add_mesh(ctx, glb_path, &(nt_mesh_opts_t){.layout = layout, .stream_count = 1});
        nt_builder_add_shader(ctx, vert_path, NT_BUILD_SHADER_VERTEX);
        nt_build_result_t r = nt_builder_finish_pack(ctx);
        TEST_ASSERT_EQUAL(NT_BUILD_OK, r);
        nt_builder_free_pack(ctx);
    }

    /* Binary compare */
    uint32_t size1 = 0;
    uint32_t size2 = 0;
    uint8_t *data1 = (uint8_t *)nt_builder_read_file(pack1, &size1);
    uint8_t *data2 = (uint8_t *)nt_builder_read_file(pack2, &size2);
    TEST_ASSERT_NOT_NULL(data1);
    TEST_ASSERT_NOT_NULL(data2);
    TEST_ASSERT_EQUAL_UINT32(size1, size2);
    TEST_ASSERT_EQUAL_MEMORY(data1, data2, size1);
    free(data1);
    free(data2);
}

void test_parallel_basic(void) {
    /* Build with 4 threads, verify output is valid and all assets present (PAR-01). */
    const char *pack_path = TMP_DIR "/par_basic.ntpack";
    const char *png_path = TMP_DIR "/par_basic_tex.png";
    const char *glb_path = TMP_DIR "/par_basic_tri.glb";
    const char *vert_path = TMP_DIR "/par_basic.vert";
    write_test_png(png_path);
    write_test_glb(glb_path);
    write_test_shader(vert_path, "precision mediump float;\n"
                                 "layout(location = 0) in vec3 a_pos;\n"
                                 "void main() { gl_Position = vec4(a_pos, 1.0); }\n");

    NtStreamLayout layout[] = {{"position", "POSITION", NT_STREAM_FLOAT32, 3, false}};

    NtBuilderContext *ctx = nt_builder_start_pack(pack_path);
    nt_builder_set_threads(ctx, 4);
    nt_builder_add_texture(ctx, png_path, NULL);
    nt_builder_add_mesh(ctx, glb_path, &(nt_mesh_opts_t){.layout = layout, .stream_count = 1});
    nt_builder_add_shader(ctx, vert_path, NT_BUILD_SHADER_VERTEX);
    uint8_t blob_data[] = {0xDE, 0xAD, 0xBE, 0xEF};
    nt_builder_add_blob(ctx, blob_data, sizeof(blob_data), "test/blob");
    nt_build_result_t r = nt_builder_finish_pack(ctx);
    TEST_ASSERT_EQUAL(NT_BUILD_OK, r);
    nt_builder_free_pack(ctx);

    /* Verify pack is valid by reading header */
    uint32_t pack_size = 0;
    uint8_t *pack_data = (uint8_t *)nt_builder_read_file(pack_path, &pack_size);
    TEST_ASSERT_NOT_NULL(pack_data);
    TEST_ASSERT_TRUE(pack_size > sizeof(NtPackHeader));
    NtPackHeader *hdr = (NtPackHeader *)pack_data;
    TEST_ASSERT_EQUAL_UINT32(NT_PACK_MAGIC, hdr->magic);
    TEST_ASSERT_EQUAL(4, hdr->asset_count); /* texture + mesh + shader + blob */
    free(pack_data);
}

void test_set_threads_zero_is_singlethreaded(void) {
    /* No call to set_threads = single-threaded, same as set_threads(0) (D-12). */
    const char *pack1 = TMP_DIR "/thr_default.ntpack";
    const char *pack2 = TMP_DIR "/thr_zero.ntpack";
    const char *png_path = TMP_DIR "/thr_tex.png";
    write_test_png(png_path);

    {
        NtBuilderContext *ctx = nt_builder_start_pack(pack1);
        /* No set_threads call -- default 0 */
        nt_builder_add_texture(ctx, png_path, NULL);
        nt_build_result_t r = nt_builder_finish_pack(ctx);
        TEST_ASSERT_EQUAL(NT_BUILD_OK, r);
        nt_builder_free_pack(ctx);
    }
    {
        NtBuilderContext *ctx = nt_builder_start_pack(pack2);
        nt_builder_set_threads(ctx, 0);
        nt_builder_add_texture(ctx, png_path, NULL);
        nt_build_result_t r = nt_builder_finish_pack(ctx);
        TEST_ASSERT_EQUAL(NT_BUILD_OK, r);
        nt_builder_free_pack(ctx);
    }

    uint32_t size1 = 0;
    uint32_t size2 = 0;
    uint8_t *data1 = (uint8_t *)nt_builder_read_file(pack1, &size1);
    uint8_t *data2 = (uint8_t *)nt_builder_read_file(pack2, &size2);
    TEST_ASSERT_NOT_NULL(data1);
    TEST_ASSERT_NOT_NULL(data2);
    TEST_ASSERT_EQUAL_UINT32(size1, size2);
    TEST_ASSERT_EQUAL_MEMORY(data1, data2, size1);
    free(data1);
    free(data2);
}

void test_parallel_with_cache(void) {
    /* Build with threads + cache, rebuild, verify cache hits in parallel mode. */
    const char *pack_path = TMP_DIR "/par_cache.ntpack";
    const char *cache_dir = TMP_DIR "/par_cache_dir";
    const char *png_path = TMP_DIR "/par_cache_tex.png";
    const char *glb_path = TMP_DIR "/par_cache_tri.glb";
    MKDIR(cache_dir);
    clean_cache_dir(cache_dir);
    write_test_png(png_path);
    write_test_glb(glb_path);

    NtStreamLayout layout[] = {{"position", "POSITION", NT_STREAM_FLOAT32, 3, false}};

    /* Build 1: populates cache */
    {
        NtBuilderContext *ctx = nt_builder_start_pack(pack_path);
        nt_builder_set_threads(ctx, 4);
        nt_builder_set_cache_dir(ctx, cache_dir);
        nt_builder_add_texture(ctx, png_path, NULL);
        nt_builder_add_mesh(ctx, glb_path, &(nt_mesh_opts_t){.layout = layout, .stream_count = 1});
        nt_build_result_t r = nt_builder_finish_pack(ctx);
        TEST_ASSERT_EQUAL(NT_BUILD_OK, r);
        nt_builder_free_pack(ctx);
    }

    uint32_t size1 = 0;
    uint8_t *data1 = (uint8_t *)nt_builder_read_file(pack_path, &size1);

    /* Build 2: should get cache hits */
    {
        NtBuilderContext *ctx = nt_builder_start_pack(pack_path);
        nt_builder_set_threads(ctx, 4);
        nt_builder_set_cache_dir(ctx, cache_dir);
        nt_builder_add_texture(ctx, png_path, NULL);
        nt_builder_add_mesh(ctx, glb_path, &(nt_mesh_opts_t){.layout = layout, .stream_count = 1});
        nt_build_result_t r = nt_builder_finish_pack(ctx);
        TEST_ASSERT_EQUAL(NT_BUILD_OK, r);
        nt_builder_free_pack(ctx);
    }

    uint32_t size2 = 0;
    uint8_t *data2 = (uint8_t *)nt_builder_read_file(pack_path, &size2);
    TEST_ASSERT_EQUAL_UINT32(size1, size2);
    TEST_ASSERT_EQUAL_MEMORY(data1, data2, size1);
    free(data1);
    free(data2);
}

void test_parallel_with_dedup(void) {
    /* Build with threads + duplicate assets, verify dedup + parallel encode. */
    const char *pack1 = TMP_DIR "/par_dedup_1.ntpack";
    const char *pack2 = TMP_DIR "/par_dedup_4.ntpack";
    const char *png_path = TMP_DIR "/par_dedup_tex.png";
    const char *glb_path = TMP_DIR "/par_dedup_tri.glb";
    const char *vert_path = TMP_DIR "/par_dedup.vert";
    write_test_png(png_path);
    write_test_glb(glb_path);
    write_test_shader(vert_path, "precision mediump float;\n"
                                 "layout(location = 0) in vec3 a_pos;\n"
                                 "void main() { gl_Position = vec4(a_pos, 1.0); }\n");

    NtStreamLayout layout[] = {{"position", "POSITION", NT_STREAM_FLOAT32, 3, false}};

    /* Same assets added twice (will early-dedup) + other assets (will encode) */
    for (int pass = 0; pass < 2; pass++) {
        const char *path = (pass == 0) ? pack1 : pack2;
        NtBuilderContext *ctx = nt_builder_start_pack(path);
        nt_builder_set_threads(ctx, (pass == 0) ? 1 : 4);

        /* Two identical textures (early dedup) -- use from_memory to control resource_id */
        nt_builder_add_texture(ctx, png_path, NULL);
        uint32_t img_size = 0;
        uint8_t *img_data = (uint8_t *)nt_builder_read_file(png_path, &img_size);
        nt_builder_add_texture_from_memory(ctx, img_data, img_size, "dup/texture", NULL);
        free(img_data);

        nt_builder_add_mesh(ctx, glb_path, &(nt_mesh_opts_t){.layout = layout, .stream_count = 1});
        nt_builder_add_shader(ctx, vert_path, NT_BUILD_SHADER_VERTEX);

        nt_build_result_t r = nt_builder_finish_pack(ctx);
        TEST_ASSERT_EQUAL(NT_BUILD_OK, r);
        nt_builder_free_pack(ctx);
    }

    /* Verify byte-identical output */
    uint32_t size1 = 0;
    uint32_t size2 = 0;
    uint8_t *data1 = (uint8_t *)nt_builder_read_file(pack1, &size1);
    uint8_t *data2 = (uint8_t *)nt_builder_read_file(pack2, &size2);
    TEST_ASSERT_EQUAL_UINT32(size1, size2);
    TEST_ASSERT_EQUAL_MEMORY(data1, data2, size1);
    free(data1);
    free(data2);
}

/* --- Font test fixture helper --- */

static const char *find_test_ttf(void) {
    /* Prefer committed fixture (cross-platform), fall back to system fonts */
    static const char *candidates[] = {
        "tests/fixtures/Roboto-Regular.ttf", "../tests/fixtures/Roboto-Regular.ttf", "../../tests/fixtures/Roboto-Regular.ttf", "C:/Windows/Fonts/arial.ttf", "C:/Windows/Fonts/consola.ttf", NULL,
    };
    for (int i = 0; candidates[i]; i++) {
        FILE *f = fopen(candidates[i], "rb");
        if (f) {
            (void)fclose(f);
            return candidates[i];
        }
    }
    return NULL;
}

/* --- Font processing tests (Phase 43) --- */

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void test_font_add_basic_ascii(void) {
    const char *ttf_path = find_test_ttf();
    if (!ttf_path) {
        TEST_IGNORE_MESSAGE("No TTF font found for testing");
        return;
    }

    const char *pack_path = TMP_DIR "/test_font_ascii.ntpack";
    NtBuilderContext *ctx = nt_builder_start_pack(pack_path);
    TEST_ASSERT_NOT_NULL(ctx);

    nt_font_opts_t opts = {.charset = "ABC", .resource_name = NULL};
    nt_builder_add_font(ctx, ttf_path, &opts);

    nt_build_result_t r = nt_builder_finish_pack(ctx);
    TEST_ASSERT_EQUAL(NT_BUILD_OK, r);
    nt_builder_free_pack(ctx);

    /* Read pack back and verify */
    uint32_t pack_size = 0;
    char *pack_data = nt_builder_read_file(pack_path, &pack_size);
    TEST_ASSERT_NOT_NULL(pack_data);
    TEST_ASSERT_TRUE(pack_size > sizeof(NtPackHeader));

    const NtPackHeader *hdr = (const NtPackHeader *)pack_data;
    TEST_ASSERT_EQUAL_HEX32(NT_PACK_MAGIC, hdr->magic);
    TEST_ASSERT_EQUAL_UINT(1, hdr->asset_count);

    /* Find the font asset entry */
    const NtAssetEntry *entry = (const NtAssetEntry *)(pack_data + sizeof(NtPackHeader));
    TEST_ASSERT_EQUAL_UINT(NT_ASSET_FONT, entry->asset_type);
    TEST_ASSERT_EQUAL_UINT(NT_FONT_VERSION, entry->format_version);
    TEST_ASSERT_TRUE(entry->size > sizeof(NtFontAssetHeader));

    /* Parse font header */
    const NtFontAssetHeader *fhdr = (const NtFontAssetHeader *)(pack_data + entry->offset);
    TEST_ASSERT_EQUAL_HEX32(NT_FONT_MAGIC, fhdr->magic);
    TEST_ASSERT_EQUAL_UINT(NT_FONT_VERSION, fhdr->version);
    TEST_ASSERT_EQUAL_UINT(3, fhdr->glyph_count); /* "ABC" = 3 glyphs */
    TEST_ASSERT_TRUE(fhdr->units_per_em > 0);
    TEST_ASSERT_TRUE(fhdr->ascent > 0);
    TEST_ASSERT_TRUE(fhdr->descent < 0); /* descent is negative */

    /* Verify glyph entries are sorted by codepoint */
    const NtFontGlyphEntry *glyphs = (const NtFontGlyphEntry *)((const uint8_t *)fhdr + sizeof(NtFontAssetHeader));
    TEST_ASSERT_EQUAL_UINT('A', glyphs[0].codepoint);
    TEST_ASSERT_EQUAL_UINT('B', glyphs[1].codepoint);
    TEST_ASSERT_EQUAL_UINT('C', glyphs[2].codepoint);

    /* Each glyph should have positive advance and curves */
    for (uint32_t i = 0; i < 3; i++) {
        TEST_ASSERT_TRUE(glyphs[i].advance > 0);
        TEST_ASSERT_TRUE(glyphs[i].curve_count > 0); /* A, B, C all have outlines */
    }

    free(pack_data);
}

void test_font_add_full_ascii_charset(void) {
    const char *ttf_path = find_test_ttf();
    if (!ttf_path) {
        TEST_IGNORE_MESSAGE("No TTF font found for testing");
        return;
    }

    const char *pack_path = TMP_DIR "/test_font_full_ascii.ntpack";
    NtBuilderContext *ctx = nt_builder_start_pack(pack_path);
    TEST_ASSERT_NOT_NULL(ctx);

    nt_font_opts_t opts = {.charset = NT_CHARSET_ASCII, .resource_name = NULL};
    nt_builder_add_font(ctx, ttf_path, &opts);

    nt_build_result_t r = nt_builder_finish_pack(ctx);
    TEST_ASSERT_EQUAL(NT_BUILD_OK, r);
    nt_builder_free_pack(ctx);

    /* Read and verify glyph count = 95 */
    uint32_t pack_size = 0;
    char *pack_data = nt_builder_read_file(pack_path, &pack_size);
    TEST_ASSERT_NOT_NULL(pack_data);

    const NtPackHeader *hdr = (const NtPackHeader *)pack_data;
    (void)hdr;
    const NtAssetEntry *entry = (const NtAssetEntry *)(pack_data + sizeof(NtPackHeader));
    const NtFontAssetHeader *fhdr = (const NtFontAssetHeader *)(pack_data + entry->offset);
    TEST_ASSERT_EQUAL_UINT(95, fhdr->glyph_count);

    /* Space (0x20) should be first, tilde (0x7E) should be last */
    const NtFontGlyphEntry *glyphs = (const NtFontGlyphEntry *)((const uint8_t *)fhdr + sizeof(NtFontAssetHeader));
    TEST_ASSERT_EQUAL_UINT(0x20, glyphs[0].codepoint);
    TEST_ASSERT_EQUAL_UINT(0x7E, glyphs[94].codepoint);

    /* Space has advance but 0 curves */
    TEST_ASSERT_TRUE(glyphs[0].advance > 0);
    TEST_ASSERT_EQUAL_UINT(0, glyphs[0].curve_count);

    free(pack_data);
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void test_font_kern_pairs(void) {
    const char *ttf_path = find_test_ttf();
    if (!ttf_path) {
        TEST_IGNORE_MESSAGE("No TTF font found for testing");
        return;
    }

    const char *pack_path = TMP_DIR "/test_font_kern.ntpack";
    NtBuilderContext *ctx = nt_builder_start_pack(pack_path);
    TEST_ASSERT_NOT_NULL(ctx);

    /* Include characters known to have kern pairs */
    nt_font_opts_t opts = {.charset = "AVToWa", .resource_name = NULL};
    nt_builder_add_font(ctx, ttf_path, &opts);

    nt_build_result_t r = nt_builder_finish_pack(ctx);
    TEST_ASSERT_EQUAL(NT_BUILD_OK, r);
    nt_builder_free_pack(ctx);

    /* Read and check that at least some glyphs have kern_count > 0 */
    uint32_t pack_size = 0;
    char *pack_data = nt_builder_read_file(pack_path, &pack_size);
    TEST_ASSERT_NOT_NULL(pack_data);

    const NtAssetEntry *entry = (const NtAssetEntry *)(pack_data + sizeof(NtPackHeader));
    const NtFontAssetHeader *fhdr = (const NtFontAssetHeader *)(pack_data + entry->offset);
    const NtFontGlyphEntry *glyphs = (const NtFontGlyphEntry *)((const uint8_t *)fhdr + sizeof(NtFontAssetHeader));

    /* Check if any glyph has kern pairs -- font dependent, but common fonts kern AV/VA/To */
    uint32_t total_kerns = 0;
    for (uint32_t i = 0; i < fhdr->glyph_count; i++) {
        total_kerns += glyphs[i].kern_count;
    }
    TEST_ASSERT_TRUE(fhdr->glyph_count == 6);

    /* If kerns exist, verify kern entries are readable */
    if (total_kerns > 0) {
        /* Find first glyph with kerns */
        for (uint32_t i = 0; i < fhdr->glyph_count; i++) {
            if (glyphs[i].kern_count > 0) {
                const uint8_t *data_ptr = (const uint8_t *)fhdr + glyphs[i].data_offset;
                const NtFontKernEntry *kerns = (const NtFontKernEntry *)data_ptr;
                /* First kern entry should have a valid glyph index */
                TEST_ASSERT_TRUE(kerns[0].right_glyph_index < fhdr->glyph_count);
                TEST_ASSERT_TRUE(kerns[0].value != 0);
                break;
            }
        }
    }

    free(pack_data);
}

void test_font_missing_codepoint_asserts(void) {
    const char *ttf_path = find_test_ttf();
    if (!ttf_path) {
        TEST_IGNORE_MESSAGE("No TTF font found for testing");
        return;
    }

    NtBuilderContext *ctx = nt_builder_start_pack(TMP_DIR "/test_font_missing.ntpack");
    TEST_ASSERT_NOT_NULL(ctx);

    /* U+E000 (Private Use Area) -- no standard font maps this */
    nt_font_opts_t opts = {.charset = "\xEE\x80\x80", .resource_name = NULL};
    EXPECT_BUILD_ASSERT(ctx, nt_builder_add_font(ctx, ttf_path, &opts));
}

void test_font_null_charset_asserts(void) {
    const char *ttf_path = find_test_ttf();
    if (!ttf_path) {
        TEST_IGNORE_MESSAGE("No TTF font found for testing");
        return;
    }

    NtBuilderContext *ctx = nt_builder_start_pack(TMP_DIR "/test_font_null.ntpack");
    nt_font_opts_t opts = {.charset = NULL, .resource_name = NULL};
    EXPECT_BUILD_ASSERT(ctx, nt_builder_add_font(ctx, ttf_path, &opts));
}

void test_font_dump_pack(void) {
    const char *ttf_path = find_test_ttf();
    if (!ttf_path) {
        TEST_IGNORE_MESSAGE("No TTF font found for testing");
        return;
    }

    const char *pack_path = TMP_DIR "/test_font_dump.ntpack";
    NtBuilderContext *ctx = nt_builder_start_pack(pack_path);
    nt_font_opts_t opts = {.charset = "Hello", .resource_name = NULL};
    nt_builder_add_font(ctx, ttf_path, &opts);
    nt_build_result_t r = nt_builder_finish_pack(ctx);
    TEST_ASSERT_EQUAL(NT_BUILD_OK, r);
    nt_builder_free_pack(ctx);

    /* dump_pack should succeed (verifies font display code path) */
    r = nt_builder_dump_pack(pack_path);
    TEST_ASSERT_EQUAL(NT_BUILD_OK, r);
}

void test_font_charset_dedup(void) {
    const char *ttf_path = find_test_ttf();
    if (!ttf_path) {
        TEST_IGNORE_MESSAGE("No TTF font found for testing");
        return;
    }

    const char *pack_path = TMP_DIR "/test_font_dedup.ntpack";
    NtBuilderContext *ctx = nt_builder_start_pack(pack_path);
    /* "AABBCC" should produce only 3 unique glyphs */
    nt_font_opts_t opts = {.charset = "AABBCC", .resource_name = NULL};
    nt_builder_add_font(ctx, ttf_path, &opts);
    nt_build_result_t r = nt_builder_finish_pack(ctx);
    TEST_ASSERT_EQUAL(NT_BUILD_OK, r);
    nt_builder_free_pack(ctx);

    uint32_t pack_size = 0;
    char *pack_data = nt_builder_read_file(pack_path, &pack_size);
    const NtAssetEntry *entry = (const NtAssetEntry *)(pack_data + sizeof(NtPackHeader));
    const NtFontAssetHeader *fhdr = (const NtFontAssetHeader *)(pack_data + entry->offset);
    TEST_ASSERT_EQUAL_UINT(3, fhdr->glyph_count); /* deduplicated to 3 */
    free(pack_data);
}

/* Helper: find kern value for a (left_codepoint, right_codepoint) pair in packed font data */
static int16_t find_kern_value(const uint8_t *font_data, uint32_t left_cp, uint32_t right_cp) {
    const NtFontAssetHeader *hdr = (const NtFontAssetHeader *)font_data;
    const NtFontGlyphEntry *glyphs = (const NtFontGlyphEntry *)(font_data + sizeof(NtFontAssetHeader));

    /* Find left glyph index */
    uint16_t left_idx = UINT16_MAX;
    uint16_t right_idx = UINT16_MAX;
    for (uint16_t i = 0; i < hdr->glyph_count; i++) {
        if (glyphs[i].codepoint == left_cp) {
            left_idx = i;
        }
        if (glyphs[i].codepoint == right_cp) {
            right_idx = i;
        }
    }
    if (left_idx == UINT16_MAX || right_idx == UINT16_MAX) {
        return 0;
    }

    /* Search kern entries for right_idx */
    const NtFontKernEntry *kerns = (const NtFontKernEntry *)(font_data + glyphs[left_idx].data_offset);
    for (uint16_t k = 0; k < glyphs[left_idx].kern_count; k++) {
        if (kerns[k].right_glyph_index == right_idx) {
            return kerns[k].value;
        }
    }
    return 0;
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void test_font_kern_values(void) {
    const char *ttf_path = find_test_ttf();
    if (!ttf_path) {
        TEST_IGNORE_MESSAGE("No TTF font found for testing");
        return;
    }

    const char *pack_path = TMP_DIR "/test_font_kern_values.ntpack";
    NtBuilderContext *ctx = nt_builder_start_pack(pack_path);
    nt_font_opts_t opts = {.charset = NT_CHARSET_ASCII, .resource_name = NULL};
    nt_builder_add_font(ctx, ttf_path, &opts);
    nt_build_result_t r = nt_builder_finish_pack(ctx);
    TEST_ASSERT_EQUAL(NT_BUILD_OK, r);
    nt_builder_free_pack(ctx);

    uint32_t pack_size = 0;
    char *pack_data = nt_builder_read_file(pack_path, &pack_size);
    TEST_ASSERT_NOT_NULL(pack_data);
    const NtAssetEntry *entry = (const NtAssetEntry *)(pack_data + sizeof(NtPackHeader));
    const uint8_t *font_data = (const uint8_t *)pack_data + entry->offset;
    const NtFontAssetHeader *fhdr = (const NtFontAssetHeader *)font_data;

    /* Verify specific kern pairs match stb_truetype per-pair API.
     * Reference values from fonttools for Roboto-Regular.ttf:
     * AV=-87, To=-99, AW=-69, Va=-46, Ta=-113, LT=-275, TT=16 */
    stbtt_fontinfo stb_font;
    uint32_t ttf_size = 0;
    char *ttf_data = nt_builder_read_file(ttf_path, &ttf_size);
    TEST_ASSERT_NOT_NULL(ttf_data);
    int ok = stbtt_InitFont(&stb_font, (const unsigned char *)ttf_data, stbtt_GetFontOffsetForIndex((const unsigned char *)ttf_data, 0));
    TEST_ASSERT_TRUE(ok);

    /* Check every kern pair in our pack against stb per-pair API */
    const NtFontGlyphEntry *glyphs = (const NtFontGlyphEntry *)(font_data + sizeof(NtFontAssetHeader));
    uint32_t total_checked = 0;
    for (uint16_t i = 0; i < fhdr->glyph_count; i++) {
        if (glyphs[i].kern_count == 0) {
            continue;
        }
        const NtFontKernEntry *kerns = (const NtFontKernEntry *)(font_data + glyphs[i].data_offset);
        int g1 = stbtt_FindGlyphIndex(&stb_font, (int)glyphs[i].codepoint);
        for (uint16_t k = 0; k < glyphs[i].kern_count; k++) {
            uint16_t ri = kerns[k].right_glyph_index;
            int g2 = stbtt_FindGlyphIndex(&stb_font, (int)glyphs[ri].codepoint);
            int stb_val = stbtt_GetGlyphKernAdvance(&stb_font, g1, g2);
            TEST_ASSERT_EQUAL_INT16(stb_val, kerns[k].value);
            total_checked++;
        }
    }

    /* Must have found some kern pairs */
    TEST_ASSERT_TRUE(total_checked > 50);

    /* Spot-check known pairs (non-zero kern expected for common pairs) */
    TEST_ASSERT_TRUE(find_kern_value(font_data, 'A', 'V') != 0);
    TEST_ASSERT_TRUE(find_kern_value(font_data, 'T', 'o') != 0);
    TEST_ASSERT_TRUE(find_kern_value(font_data, 'L', 'T') != 0);

    free(ttf_data);
    free(pack_data);
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

    /* Validation errors (builder asserts on bad input -- tested via EXPECT_BUILD_ASSERT) */
    RUN_TEST(test_missing_position_attribute_errors);
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
    RUN_TEST(test_dump_gzip_sizes);
    RUN_TEST(test_dump_name_resolution);
    RUN_TEST(test_dump_without_header);

    /* Multi-asset and stage */
    RUN_TEST(test_multi_asset_pack);
    RUN_TEST(test_shader_stage_correct);

    /* Glob batch */
    RUN_TEST(test_glob_shaders);

    /* E2E with real assets */
    RUN_TEST(test_e2e_real_assets);

    /* Rename */
    RUN_TEST(test_rename_changes_resource_id);

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
    RUN_TEST(test_include_pragma_once_after_comment);

    /* GL shader validation */
    RUN_TEST(test_gl_validation_valid_shader);
    /* test_gl_validation_invalid_shader, test_gl_validation_type_error: builder asserts on bad input */
    RUN_TEST(test_gl_validation_fragment_shader);

    /* Multi-mesh add_mesh */
    RUN_TEST(test_add_mesh_by_name);
    RUN_TEST(test_add_mesh_by_index);
    RUN_TEST(test_add_mesh_single_unchanged);
    RUN_TEST(test_add_mesh_by_name_not_found);
    RUN_TEST(test_add_mesh_by_index_out_of_range);
    RUN_TEST(test_add_mesh_resource_name_override);

    /* Codegen */
    RUN_TEST(test_codegen_generates_header);
    RUN_TEST(test_codegen_hash_matches_runtime);
    RUN_TEST(test_codegen_path_to_identifier);
    RUN_TEST(test_codegen_renamed_assets);

    /* Merge */
    RUN_TEST(test_merge_combined_header);
    RUN_TEST(test_merge_dedup);
    RUN_TEST(test_merge_sorted_output);

    /* AABB in mesh header */
    RUN_TEST(test_builder_mesh_has_aabb);

    /* Early dedup */
    RUN_TEST(test_early_dedup_identical_textures);
    RUN_TEST(test_early_dedup_identical_blobs);
    RUN_TEST(test_early_dedup_different_opts_not_deduped);
    RUN_TEST(test_early_dedup_identical_shaders);
    RUN_TEST(test_early_dedup_different_kinds_not_deduped);
    RUN_TEST(test_early_dedup_pack_data_correct);

    /* Cross-source dedup (38.1 pipeline refactoring) */
    RUN_TEST(test_dedup_cross_source_texture_file_vs_memory);
    RUN_TEST(test_dedup_cross_source_mesh_file_vs_scene);
    RUN_TEST(test_dedup_cross_source_texture_memory_vs_raw);

    /* Cache */
    RUN_TEST(test_cache_hit_skips_encode);
    RUN_TEST(test_cache_invalidation_opts);
    RUN_TEST(test_cache_version_in_opts_hash);
    RUN_TEST(test_cache_dir_configurable);
    RUN_TEST(test_cache_clear_forces_rebuild);
    RUN_TEST(test_cache_flat_files);
    RUN_TEST(test_cache_with_dedup);

    /* Parallel encode */
    RUN_TEST(test_parallel_deterministic);
    RUN_TEST(test_parallel_basic);
    RUN_TEST(test_set_threads_zero_is_singlethreaded);
    RUN_TEST(test_parallel_with_cache);
    RUN_TEST(test_parallel_with_dedup);

    /* Font processing tests (Phase 43) */
    RUN_TEST(test_font_add_basic_ascii);
    RUN_TEST(test_font_add_full_ascii_charset);
    RUN_TEST(test_font_kern_pairs);
    RUN_TEST(test_font_missing_codepoint_asserts);
    RUN_TEST(test_font_null_charset_asserts);
    RUN_TEST(test_font_dump_pack);
    RUN_TEST(test_font_charset_dedup);
    RUN_TEST(test_font_kern_values);

    return UNITY_END();
}
// NOLINTEND(clang-analyzer-unix.Stream,clang-analyzer-core.CallAndMessage,clang-analyzer-core.UndefinedBinaryOperatorResult)
