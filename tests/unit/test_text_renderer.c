#include "test_helpers/nt_gfx_fake.h"
/* System headers before Unity to avoid noreturn / __declspec conflict on MSVC */
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* clang-format off */
#include "core/nt_assert.h"
#include "font/nt_font.h"
#include "graphics/nt_gfx.h"
#include "graphics/nt_gfx_internal.h"
#include "hash/nt_hash.h"
#include "log/nt_log.h"
#include "material/nt_material.h"
#include "material/nt_program_ref.h"
#include "nt_font_format.h"
#include "nt_pack_format.h"
#include "renderers/nt_text_renderer.h"
#include "resource/nt_resource.h"
#include "test_helpers/nt_assert_trap.h"
#include "time/nt_time.h"
#include "unity.h"
/* clang-format on */

/* ---- Test blob builder (identical to test_font.c) ---- */

static uint8_t *build_test_font_blob(uint32_t *out_size) {
    uint32_t contour_size = 14; /* v4: cc(2)+pc(2)+flags(2)+first(4)+deltas(4) */
    uint32_t header_size = (uint32_t)sizeof(NtFontAssetHeader);
    uint32_t glyphs_size = 3 * (uint32_t)sizeof(NtFontGlyphEntry);
    uint32_t total_size = header_size + glyphs_size + (3 * contour_size);

    uint8_t *blob = (uint8_t *)calloc(total_size, 1);
    NT_ASSERT(blob);

    NtFontAssetHeader hdr;
    memset(&hdr, 0, sizeof(hdr));
    hdr.magic = NT_FONT_MAGIC;
    hdr.version = NT_FONT_VERSION;
    hdr.glyph_count = 3;
    hdr.units_per_em = 1000;
    hdr.ascent = 800;
    hdr.descent = -200;
    hdr.line_gap = 0;
    /* v5 decoration metrics (font units): underline below baseline, strike above. */
    hdr.underline_position = -100;
    hdr.underline_thickness = 50;
    hdr.strikeout_position = 250;
    hdr.strikeout_size = 40;
    memcpy(blob, &hdr, sizeof(hdr));

    uint32_t data_base = header_size + glyphs_size;
    uint32_t codepoints[3] = {65, 66, 67}; /* 'A', 'B', 'C' */

    for (int g = 0; g < 3; g++) {
        NtFontGlyphEntry entry;
        memset(&entry, 0, sizeof(entry));
        entry.codepoint = codepoints[g];
        entry.data_offset = data_base + ((uint32_t)g * contour_size);
        entry.advance = 500;
        entry.bbox_x0 = 0;
        entry.bbox_y0 = -200;
        entry.bbox_x1 = 400;
        entry.bbox_y1 = 800;
        entry.curve_count = 3; /* 3 on-curve points in closed contour → 3 line segments */
        entry.kern_count = 0;
        memcpy(blob + header_size + ((size_t)g * sizeof(NtFontGlyphEntry)), &entry, sizeof(entry));
    }

    /* Write v4 contour data per glyph: contour_count, then per-contour:
     * point_count, flags_bitmask, first_point(abs), varlen_deltas */
    for (int g = 0; g < 3; g++) {
        uint8_t *wp = blob + data_base + ((size_t)g * contour_size);
        /* contour_count = 1 */
        uint16_t cc = 1;
        memcpy(wp, &cc, 2);
        wp += 2;
        /* point_count = 3 (triangle: 3 on-curve points → 3 line-segment curves) */
        uint16_t pc = 3;
        memcpy(wp, &pc, 2);
        wp += 2;
        /* flags bitmask: NT_FONT_BITMASK_BYTES(3) = 2, all on-curve → bits 0,1,2 set */
        wp[0] = 0x07;
        wp[1] = 0x00;
        wp += 2;
        /* first point absolute (0, 0) */
        int16_t fx = 0;
        int16_t fy = 0;
        memcpy(wp, &fx, 2);
        wp += 2;
        memcpy(wp, &fy, 2);
        wp += 2;
        /* delta point 1: (50, 0) — fits in int8, single byte each */
        *wp++ = 50;
        *wp++ = 0;
        /* delta point 2: (-50, 50) — fits in int8 */
        *wp++ = (uint8_t)(int8_t)-50;
        *wp++ = 50;
    }

    *out_size = total_size;
    return blob;
}

/* ---- Helper: register font blob as test resource ---- */

static nt_resource_t register_font_resource(const char *name, const uint8_t *blob, uint32_t blob_size) {
    /* fonts resolve bytes from a resident pack blob, so wrap the blob in a
     * real parsed pack (owned by the font module) rather than a virtual pack. */
    (void)name;
    return nt_font_test_resource(nt_font_test_register_data(blob, blob_size));
}

/* ---- Shared test state ---- */

static uint8_t *s_blob;
static uint32_t s_blob_size;
static nt_font_t s_font;
static uint32_t s_error_count;

static void capture_errors(nt_log_level_t level, const char *domain, const char *msg, void *user) {
    (void)domain;
    (void)msg;
    (void)user;
    if (level == NT_LOG_LEVEL_ERROR) {
        s_error_count++;
    }
}

/* ---- Default identity matrix ---- */

static const float s_identity[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
static const float s_white[4] = {1.0F, 1.0F, 1.0F, 1.0F};

static nt_material_t create_test_material_with_blend(nt_blend_state_t blend) {
    nt_shader_t vs = nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_VERTEX, .source = "void main(){}"});
    nt_shader_t fs = nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_FRAGMENT, .source = "void main(){}"});
    nt_material_t material = nt_material_create(&(nt_material_create_desc_t){
        .program = nt_gfx_make_program(vs, fs),
        .blend = blend,
        .cull_mode = NT_CULL_NONE,
    });
    nt_material_step();
    return material;
}

/* The first staged quad resolves the pipeline; set_material alone does not. */
static void draw_and_flush(void) {
    nt_text_renderer_draw("AB", s_identity, 32.0F, s_white, 0.0F, 0.0F);
    nt_gfx_begin_frame();
    nt_gfx_begin_pass(&(nt_pass_desc_t){.clear_depth = 1.0F});
    nt_text_renderer_flush();
    nt_gfx_end_pass();
    nt_gfx_end_frame();
}

/* ---- nt_program_ref: the async link gate ---- */

static nt_resource_t publish_stage(const char *name, uint32_t shader_id) {
    nt_hash32_t pid = nt_hash32_str(name);
    nt_hash64_t rid = nt_hash64_str(name);
    nt_resource_create_pack(pid, 0);
    nt_resource_register(pid, rid, NT_ASSET_SHADER_CODE, shader_id);
    return nt_resource_request(rid, NT_ASSET_SHADER_CODE);
}

static void republish_stage(const char *name, uint32_t shader_id) { nt_resource_register(nt_hash32_str(name), nt_hash64_str(name), NT_ASSET_SHADER_CODE, shader_id); }

/* Text programs must sample the two font textures; the renderer binds them at the
 * units the link assigned and asserts the program samples nothing else. */
static nt_shader_t make_stage(nt_shader_type_t type) { return nt_gfx_make_shader(&(nt_shader_desc_t){.type = type, .source = (type == NT_SHADER_FRAGMENT) ? "f" : "void main(){}"}); }

static nt_gfx_test_draw_t warm_material_program(nt_material_t material, nt_program_t program) {
    nt_material_set_program(material, program);
    nt_text_renderer_set_material(material);
    nt_gfx_test_draw_trace_reset(true);
    draw_and_flush();
    TEST_ASSERT_EQUAL_UINT32(1U, nt_gfx_test_draw_trace_count());
    nt_gfx_test_draw_t draw = nt_gfx_test_draw_trace_at(0U);
    TEST_ASSERT_EQUAL_UINT32(program.id, draw.program.id);
    nt_gfx_test_draw_trace_reset(false);
    return draw;
}

static void assert_text_draw(uint32_t index, nt_gfx_test_draw_t expected, uint32_t glyphs) {
    nt_gfx_test_draw_t draw = nt_gfx_test_draw_trace_at(index);
    TEST_ASSERT_EQUAL_UINT32(expected.program.id, draw.program.id);
    TEST_ASSERT_EQUAL_UINT32(expected.pipeline.id, draw.pipeline.id);
    TEST_ASSERT_EQUAL_UINT32(glyphs * 6U, draw.num_indices);
    TEST_ASSERT_EQUAL_UINT32(glyphs * 4U, draw.num_vertices);
    TEST_ASSERT_EQUAL_UINT32(1U, draw.instance_count);
}

/* A ref reclaims a program lost with the context and waits for ready stages before relinking. */
void test_program_ref_reclaims_a_program_killed_by_context_loss(void) {
    nt_program_ref_t ref = {0};
    ref.vs = publish_stage("ref_vs", make_stage(NT_SHADER_VERTEX).id);
    ref.fs = publish_stage("ref_fs", make_stage(NT_SHADER_FRAGMENT).id);
    nt_resource_step();

    TEST_ASSERT_TRUE(nt_program_ref_update(&ref));
    const nt_program_t first = ref.program;
    TEST_ASSERT_FALSE(nt_program_ref_update(&ref)); /* idempotent once linked */

    /* Context dies: handles stay valid, GPU objects do not. */
    nt_gfx_fake_set_context_lost(true);
    nt_gfx_begin_frame();
    nt_gfx_fake_set_context_lost(false);
    TEST_ASSERT_FALSE(nt_gfx_program_ready(first));

    /* No drop() anywhere. The stages are dead too, so the ref waits instead of
     * relinking from corpses -- and must not keep handing out the old program. */
    TEST_ASSERT_FALSE(nt_program_ref_update(&ref));
    TEST_ASSERT_FALSE(nt_gfx_program_valid(first));

    /* Stages come back the way the resource step brings them back. */
    republish_stage("ref_vs", make_stage(NT_SHADER_VERTEX).id);
    republish_stage("ref_fs", make_stage(NT_SHADER_FRAGMENT).id);
    nt_resource_step();

    TEST_ASSERT_FALSE(nt_program_ref_update(&ref));
    TEST_ASSERT_EQUAL_UINT32(0, ref.program.id);

    nt_gfx_begin_frame();
    TEST_ASSERT_TRUE(g_nt_gfx.context_restored);
    nt_gfx_end_frame();
    republish_stage("ref_vs", make_stage(NT_SHADER_VERTEX).id);
    republish_stage("ref_fs", make_stage(NT_SHADER_FRAGMENT).id);
    nt_resource_step();

    TEST_ASSERT_TRUE(nt_program_ref_update(&ref));
    TEST_ASSERT_NOT_EQUAL_UINT32(first.id, ref.program.id);
    TEST_ASSERT_TRUE(nt_gfx_program_ready(ref.program));

    nt_program_ref_drop(&ref);
}

/* ---- Unity setUp / tearDown ---- */

static void test_assert_handler(const char *expr, const char *file, int line) {
    (void)fprintf(stderr, "ASSERT FAILED: %s at %s:%d\n", expr, file, line);
    (void)fflush(stderr);
}

void setUp(void) {
    nt_assert_handler = test_assert_handler;
    nt_gfx_fake_reset();
    nt_gfx_init(&(nt_gfx_desc_t){.max_shaders = 8, .max_programs = 4, .max_pipelines = 4, .max_buffers = 16, .max_textures = 32, .max_meshes = 8, .max_vertex_inputs = 16, .max_render_targets = 16});
    /* Band before curve: units the renderer must query, not the 0/1 a hardcode would use. */
    nt_gfx_fake_set_samplers((const char *const[]){"u_band_texture", "u_curve_texture"}, 2);
    nt_hash_init(&(nt_hash_desc_t){0});
    nt_resource_init(&(nt_resource_desc_t){0});
    nt_material_init(&(nt_material_desc_t){.max_materials = 4});
    nt_font_init(&(nt_font_desc_t){.max_fonts = 4});

    /* Build font and create handle */
    s_blob = build_test_font_blob(&s_blob_size);
    nt_font_create_desc_t desc = {
        .curve_texture_width = 64,
        .curve_texture_height = 64,
        .band_texture_height = 16,
        .band_count = 4,
    };
    s_font = nt_font_create(&desc);
    nt_resource_t res = register_font_resource("test_text_font", s_blob, s_blob_size);
    nt_font_add(s_font, res);
    nt_resource_step();
    nt_font_step();

    /* Init text renderer */
    nt_text_renderer_init();
    nt_text_renderer_set_font(s_font);
}

void tearDown(void) {
    nt_log_remove_sink(capture_errors, NULL);
    nt_text_renderer_shutdown();
    nt_font_destroy(s_font);
    free(s_blob);
    s_blob = NULL;
    nt_font_shutdown();
    nt_material_shutdown();
    nt_resource_shutdown();
    nt_hash_shutdown();
    nt_gfx_shutdown();
}

/* ---- Test 1: UTF-8 decode ASCII (TEXT-03) ---- */

void test_utf8_decode_ascii(void) {
    nt_text_renderer_draw("ABC", s_identity, 32.0F, s_white, 0.0F, 0.0F);
    TEST_ASSERT_EQUAL_UINT32(3, nt_text_renderer_test_glyph_count());
}

void test_text_renderer_forwards_material_blend_state(void) {
    nt_blend_state_t blend = nt_blend_alpha();
    blend.constant_color[2] = 0.75F;
    blend.src_rgb = NT_BLEND_CONSTANT_COLOR;
    blend.dst_rgb = NT_BLEND_ONE_MINUS_DST_COLOR;
    blend.src_alpha = NT_BLEND_SRC_ALPHA_SATURATE;
    blend.dst_alpha = NT_BLEND_ONE_MINUS_DST_ALPHA;
    blend.op_rgb = NT_BLEND_OP_SUBTRACT;
    blend.op_alpha = NT_BLEND_OP_MAX;
    nt_material_t material = create_test_material_with_blend(blend);

    nt_text_renderer_set_material(material);
    nt_gfx_fake_reset();
    draw_and_flush();

    nt_blend_state_t actual = nt_gfx_fake_last_pipeline_blend();
    TEST_ASSERT_EQUAL_MEMORY(&blend, &actual, sizeof(blend));

    /* Sampler units are fixed at link: the only int a flush writes is the curve width. */
    TEST_ASSERT_EQUAL_UINT32(1, nt_gfx_fake_uniform_int_count());
    TEST_ASSERT_EQUAL_UINT32(nt_hash32_str("u_curve_tex_width").value, nt_gfx_fake_uniform_int_hash_at(0));
    TEST_ASSERT_EQUAL_INT((int)nt_font_get_curve_texture_width(s_font), nt_gfx_fake_uniform_int_value_at(0));
}

/* The two font textures land on the units their program assigned, in declaration
 * order, and nothing writes a sampler int. */
void test_text_renderer_font_textures_land_on_program_units(void) {
    nt_material_t material = create_test_material_with_blend(nt_blend_alpha());
    nt_text_renderer_set_material(material);
    nt_gfx_fake_reset();
    draw_and_flush();

    const nt_program_t prog = nt_material_get_info(material)->program;
    const int curve_unit = nt_gfx_program_sampler_unit(prog, nt_hash32_str("u_curve_texture"));
    const int band_unit = nt_gfx_program_sampler_unit(prog, nt_hash32_str("u_band_texture"));
    TEST_ASSERT_EQUAL_INT(1, curve_unit);
    TEST_ASSERT_EQUAL_INT(0, band_unit);

    TEST_ASSERT_EQUAL_UINT32(2, nt_gfx_fake_bound_texture_count());
    TEST_ASSERT_EQUAL_UINT32(nt_gfx_test_texture_backend_id(nt_font_get_curve_texture(s_font)), nt_gfx_fake_bound_texture_at(0));
    TEST_ASSERT_EQUAL_UINT32((uint32_t)curve_unit, nt_gfx_fake_bound_texture_slot_at(0));
    TEST_ASSERT_EQUAL_UINT32(nt_gfx_test_texture_backend_id(nt_font_get_band_texture(s_font)), nt_gfx_fake_bound_texture_at(1));
    TEST_ASSERT_EQUAL_UINT32((uint32_t)band_unit, nt_gfx_fake_bound_texture_slot_at(1));
    TEST_ASSERT_EQUAL_UINT32(1, nt_gfx_fake_uniform_int_count());
}

/* ---- Test 2: UTF-8 decode Cyrillic (TEXT-03) ---- */

void test_utf8_decode_cyrillic(void) {
    /* "При" in Russian = 3 codepoints, 6 bytes */
    /* These are non-ASCII, so they won't be in our test font -> tofu glyphs */
    nt_text_renderer_draw("\xd0\x9f\xd1\x80\xd0\xb8", s_identity, 32.0F, s_white, 0.0F, 0.0F);
    /* Tofu glyphs still produce quads (has visible bbox) */
    TEST_ASSERT_EQUAL_UINT32(3, nt_text_renderer_test_glyph_count());
}

/* ---- Test 3: UTF-8 decode CJK (TEXT-03) ---- */

void test_utf8_decode_cjk(void) {
    /* "你好" = 2 codepoints, 6 bytes */
    nt_text_renderer_draw("\xe4\xbd\xa0\xe5\xa5\xbd", s_identity, 32.0F, s_white, 0.0F, 0.0F);
    TEST_ASSERT_EQUAL_UINT32(2, nt_text_renderer_test_glyph_count());
}

/* ---- Test 4: Measure returns nonzero (TEXT-02) ---- */

void test_measure_returns_nonzero(void) {
    /* "ABC" -> all in test font with advance=500, units_per_em=1000 */
    nt_text_size_t sz = nt_font_measure(s_font, "ABC", 32.0F, 0.0F);
    TEST_ASSERT_TRUE(sz.width > 0.0F);
    TEST_ASSERT_TRUE(sz.height > 0.0F);
}

/* ---- Test 5: Measure empty string (TEXT-02 edge) ---- */

void test_measure_empty_string(void) {
    nt_text_size_t sz = nt_font_measure(s_font, "", 32.0F, 0.0F);
    TEST_ASSERT_TRUE(sz.width == 0.0F);
    TEST_ASSERT_TRUE(sz.height == 0.0F);
}

/* ---- Test 6: Measure NULL string (TEXT-02 edge) ---- */

void test_measure_null_string(void) {
    nt_text_size_t sz = nt_font_measure(s_font, NULL, 32.0F, 0.0F);
    TEST_ASSERT_TRUE(sz.width == 0.0F);
    TEST_ASSERT_TRUE(sz.height == 0.0F);
}

/* ---- Test 7: Vertex stride is 72 bytes (TEXT-01) ---- */

void test_vertex_stride_72(void) {
    nt_text_renderer_draw("A", s_identity, 32.0F, s_white, 0.0F, 0.0F);
    TEST_ASSERT_EQUAL_UINT32(1, nt_text_renderer_test_glyph_count());

    /* 4 vertices for one glyph, at 72 bytes stride */
    const uint8_t *verts = (const uint8_t *)nt_text_renderer_test_vertices();
    TEST_ASSERT_NOT_NULL(verts);

    /* Vertex 0 and vertex 1 should be at offsets 0 and 72 */
    /* They represent different quad corners, so position data differs */
    TEST_ASSERT_FALSE(memcmp(verts, verts + 72, 72) == 0);
}

/* ---- Test 8: 4 vertices per glyph (TEXT-01) ---- */

void test_vertex_count_4_per_glyph(void) {
    nt_text_renderer_draw("AB", s_identity, 32.0F, s_white, 0.0F, 0.0F);
    /* 2 visible glyphs -> 8 vertices */
    TEST_ASSERT_EQUAL_UINT32(8, nt_text_renderer_test_vertex_count());
}

/* Units 0 and 1 carry the font's curve and band textures, so a text material
 * that declares its own would have them silently overwritten. */
void test_text_material_with_textures_asserts_at_flush(void) {
    nt_shader_t vs = nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_VERTEX, .source = "void main(){}"});
    nt_shader_t fs = nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_FRAGMENT, .source = "void main(){}"});
    nt_material_t textured = nt_material_create(&(nt_material_create_desc_t){
        .program = nt_gfx_make_program(vs, fs),
        .cull_mode = NT_CULL_NONE,
        .textures[0] = {.name = "u_extra", .resource = NT_RESOURCE_INVALID},
        .texture_count = 1,
    });
    nt_material_step();

    nt_text_renderer_set_material(textured);
    nt_text_renderer_draw("AB", s_identity, 32.0F, s_white, 0.0F, 0.0F);
    nt_gfx_begin_frame();
    nt_gfx_begin_pass(&(nt_pass_desc_t){.clear_depth = 1.0F});
    NT_TEST_EXPECT_ASSERT(nt_text_renderer_flush());
    nt_gfx_end_pass();
    nt_gfx_end_frame();
}

/* ---- Test 9: Flush resets counts (TEXT-05) ---- */

void test_flush_resets_counts(void) {
    nt_text_renderer_draw("A", s_identity, 32.0F, s_white, 0.0F, 0.0F);
    TEST_ASSERT_GREATER_THAN(0U, nt_text_renderer_test_glyph_count());

    nt_text_renderer_flush();
    TEST_ASSERT_EQUAL_UINT32(0, nt_text_renderer_test_vertex_count());
    TEST_ASSERT_EQUAL_UINT32(0, nt_text_renderer_test_glyph_count());
}

/* ---- Test 10: Measure width increases with more chars (TEXT-04) ---- */

void test_measure_width_increases(void) {
    nt_text_size_t sz_a = nt_font_measure(s_font, "A", 32.0F, 0.0F);
    nt_text_size_t sz_ab = nt_font_measure(s_font, "AB", 32.0F, 0.0F);
    TEST_ASSERT_TRUE(sz_ab.width > sz_a.width);
}

/* ---- Test 11: Newlines reset x and advance y ---- */

void test_draw_newline_advances_to_next_line(void) {
    nt_text_renderer_draw("A\nB", s_identity, 32.0F, s_white, 0.0F, 0.0F);
    TEST_ASSERT_EQUAL_UINT32(2, nt_text_renderer_test_glyph_count());

    const uint8_t *verts = (const uint8_t *)nt_text_renderer_test_vertices();
    TEST_ASSERT_NOT_NULL(verts);

    float first_x = 0.0F;
    float first_y = 0.0F;
    float second_x = 0.0F;
    float second_y = 0.0F;
    memcpy(&first_x, verts + 0, sizeof(float));
    memcpy(&first_y, verts + 4, sizeof(float));
    memcpy(&second_x, verts + ((size_t)4U * 72U), sizeof(float));
    memcpy(&second_y, verts + ((size_t)4U * 72U) + 4U, sizeof(float));

    TEST_ASSERT_TRUE(first_x == second_x);
    TEST_ASSERT_TRUE(second_y < first_y);
}

/* Clearing the material's program skips new batches without destroying the borrowed program or its pipelines. */
void test_flush_stops_after_program_cleared(void) {
    nt_material_t material = create_test_material_with_blend(nt_blend_alpha());
    nt_text_renderer_set_material(material);

    nt_gfx_begin_frame();
    nt_gfx_begin_pass(&(nt_pass_desc_t){.clear_depth = 1.0F});

    nt_text_renderer_draw("AB", s_identity, 32.0F, s_white, 0.0F, 0.0F);
    nt_text_renderer_flush();
    TEST_ASSERT_EQUAL_UINT32(1U, nt_text_renderer_test_nonempty_flush_calls());

    nt_material_set_program(material, NT_PROGRAM_INVALID);

    nt_text_renderer_draw("AB", s_identity, 32.0F, s_white, 0.0F, 0.0F);
    nt_text_renderer_flush();
    TEST_ASSERT_EQUAL_UINT32(1U, nt_text_renderer_test_nonempty_flush_calls());
    TEST_ASSERT_EQUAL_UINT32(0U, nt_text_renderer_test_glyph_count());

    nt_gfx_end_pass();
    nt_gfx_end_frame();
}

/* A recoverable vertex-input creation failure must not disable text until the
 * next restore: flush retries the creation lazily, like the pipeline cache. */
void test_flush_retries_vertex_input_after_backend_failure(void) {
    nt_material_t material = create_test_material_with_blend(nt_blend_alpha());
    nt_text_renderer_set_material(material);

    /* Buffers recreate fine; the vertex input creation fails once. A failed
     * vertex-input bake alone does not fail the restore -- flush retries it. */
    nt_gfx_fake_fail_next_vertex_input_create();
    TEST_ASSERT_EQUAL_INT(NT_OK, nt_text_renderer_restore_gpu());

    nt_gfx_begin_frame();
    nt_gfx_begin_pass(&(nt_pass_desc_t){.clear_depth = 1.0F});
    nt_text_renderer_draw("AB", s_identity, 32.0F, s_white, 0.0F, 0.0F);
    nt_text_renderer_flush(); /* without the retry this discards the glyphs */
    TEST_ASSERT_EQUAL_UINT32(1U, nt_text_renderer_test_nonempty_flush_calls());
    nt_gfx_end_pass();
    nt_gfx_end_frame();
}

/* Restore contract: failure releases partial GPU resources (resource.md). */
void test_failed_restore_releases_partial_buffers(void) {
    nt_gfx_fake_fail_buffer_creates(2); /* vbo succeeds, ibo fails */
    TEST_ASSERT_EQUAL_INT(NT_ERR_INIT_FAILED, nt_text_renderer_restore_gpu());

    /* The orphaned vbo would hold one of the 16 buffer pool slots. */
    nt_buffer_t buffers[16];
    for (uint32_t i = 0; i < 16; i++) {
        buffers[i] = nt_gfx_make_buffer(&(nt_buffer_desc_t){.type = NT_BUFFER_VERTEX, .usage = NT_USAGE_DYNAMIC, .size = 16});
        TEST_ASSERT_NOT_EQUAL_UINT32(0, buffers[i].id);
    }
    for (uint32_t i = 0; i < 16; i++) {
        nt_gfx_destroy_buffer(buffers[i]);
    }

    TEST_ASSERT_EQUAL_INT(NT_OK, nt_text_renderer_restore_gpu());
}

/* Two materials on one program and one render state are one pipeline: the key is
 * the pipeline signature, not the material's identity. */
void test_materials_sharing_a_program_share_one_pipeline(void) {
    nt_shader_t vs = nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_VERTEX, .source = "void main(){}"});
    nt_shader_t fs = nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_FRAGMENT, .source = "void main(){}"});
    nt_program_t shared = nt_gfx_make_program(vs, fs);
    nt_material_t a = nt_material_create(&(nt_material_create_desc_t){.program = shared, .blend = nt_blend_alpha(), .cull_mode = NT_CULL_NONE});
    nt_material_t b = nt_material_create(&(nt_material_create_desc_t){.program = shared, .blend = nt_blend_alpha(), .cull_mode = NT_CULL_NONE});
    nt_material_step();

    nt_gfx_fake_reset();
    nt_text_renderer_set_material(a);
    draw_and_flush();
    nt_text_renderer_set_material(b);
    draw_and_flush();

    TEST_ASSERT_EQUAL_UINT32(1U, nt_gfx_fake_pipeline_create_count());
    TEST_ASSERT_EQUAL_UINT16(1U, nt_text_renderer_test_pipeline_cache_count());
}

/* Render state is folded in, so one program with two states is two pipelines. */
void test_one_program_with_two_render_states_builds_two_pipelines(void) {
    nt_shader_t vs = nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_VERTEX, .source = "void main(){}"});
    nt_shader_t fs = nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_FRAGMENT, .source = "void main(){}"});
    nt_program_t shared = nt_gfx_make_program(vs, fs);
    nt_material_t opaque_mat = nt_material_create(&(nt_material_create_desc_t){.program = shared, .blend = nt_blend_opaque(), .cull_mode = NT_CULL_NONE});
    nt_material_t blended = nt_material_create(&(nt_material_create_desc_t){.program = shared, .blend = nt_blend_alpha(), .cull_mode = NT_CULL_NONE});
    nt_material_step();

    nt_gfx_fake_reset();
    nt_text_renderer_set_material(opaque_mat);
    draw_and_flush();
    nt_text_renderer_set_material(blended);
    draw_and_flush();

    TEST_ASSERT_EQUAL_UINT32(2U, nt_gfx_fake_pipeline_create_count());
    TEST_ASSERT_EQUAL_UINT16(2U, nt_text_renderer_test_pipeline_cache_count());
}

/* Programs come out of the pool in creation order, so two shader pairs get
 * neighbouring ids; one cull step on the neighbour must not land on the same key. */
void test_neighbouring_programs_one_cull_step_apart_get_their_own_pipelines(void) {
    nt_shader_t vs0 = nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_VERTEX, .source = "void main(){}"});
    nt_shader_t fs0 = nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_FRAGMENT, .source = "void main(){}"});
    nt_program_t p0 = nt_gfx_make_program(vs0, fs0);
    nt_shader_t vs1 = nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_VERTEX, .source = "void main(){}"});
    nt_shader_t fs1 = nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_FRAGMENT, .source = "void main(){}"});
    nt_program_t p1 = nt_gfx_make_program(vs1, fs1);
    TEST_ASSERT_EQUAL_UINT32(p0.id + 1, p1.id);
    nt_material_t a = nt_material_create(&(nt_material_create_desc_t){.program = p0, .blend = nt_blend_alpha(), .cull_mode = NT_CULL_BACK});
    nt_material_t b = nt_material_create(&(nt_material_create_desc_t){.program = p1, .blend = nt_blend_alpha(), .cull_mode = NT_CULL_NONE});
    nt_material_step();

    nt_gfx_fake_reset();
    nt_gfx_test_draw_trace_reset(true);
    nt_text_renderer_set_material(a);
    draw_and_flush();
    nt_text_renderer_set_material(b);
    draw_and_flush();

    TEST_ASSERT_EQUAL_UINT32(2U, nt_gfx_fake_pipeline_create_count());
    TEST_ASSERT_EQUAL_UINT16(2U, nt_text_renderer_test_pipeline_cache_count());
    TEST_ASSERT_EQUAL_UINT32(2U, nt_gfx_test_draw_trace_count());
    TEST_ASSERT_EQUAL_UINT32(p0.id, nt_gfx_test_draw_trace_at(0).program.id);
    TEST_ASSERT_EQUAL_UINT32(p1.id, nt_gfx_test_draw_trace_at(1).program.id);
}

/* Reusing the program slot changes its generation. The first quad of a new batch
 * must resolve the replacement even when the material handle is unchanged. */
void test_a_reused_program_slot_does_not_hit_the_dead_entry(void) {
    nt_material_t mat = create_test_material_with_blend(nt_blend_alpha());
    const nt_program_t dead = nt_material_get_info(mat)->program;

    nt_gfx_fake_reset();
    nt_text_renderer_set_material(mat);
    draw_and_flush();
    TEST_ASSERT_EQUAL_UINT32(1U, nt_gfx_fake_pipeline_create_count());

    nt_gfx_destroy_program(dead);
    nt_shader_t vs = nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_VERTEX, .source = "void main(){}"});
    nt_shader_t fs = nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_FRAGMENT, .source = "void main(){}"});
    nt_program_t reborn = nt_gfx_make_program(vs, fs); /* same pool slot, new generation */
    TEST_ASSERT_NOT_EQUAL_UINT32(dead.id, reborn.id);

    nt_material_set_program(mat, NT_PROGRAM_INVALID);
    nt_material_set_program(mat, reborn);

    nt_text_renderer_draw("AB", s_identity, 32.0F, s_white, 0.0F, 0.0F);
    nt_gfx_begin_frame();
    nt_gfx_begin_pass(&(nt_pass_desc_t){.clear_depth = 1.0F});
    nt_text_renderer_flush();
    nt_gfx_end_pass();
    nt_gfx_end_frame();

    TEST_ASSERT_EQUAL_UINT32(2U, nt_gfx_fake_pipeline_create_count());
    /* One entry, not two: destroying the program destroyed the pipeline built on
     * it, and the scan swap-removed the dead entry instead of pinning a slot. */
    TEST_ASSERT_EQUAL_UINT16(1U, nt_text_renderer_test_pipeline_cache_count());
}

/* Glyphs belong to the pipeline they were staged under. A replace between draw
 * and flush does not redirect them: they go out through the program they were
 * laid out for, and nothing is built for the new one until the next batch. */
void test_a_replaced_program_does_not_redirect_a_staged_batch(void) {
    nt_material_t mat = create_test_material_with_blend(nt_blend_alpha());
    const nt_program_t first = nt_material_get_info(mat)->program;
    const nt_program_t second = nt_gfx_make_program(make_stage(NT_SHADER_VERTEX), make_stage(NT_SHADER_FRAGMENT));
    const nt_gfx_test_draw_t first_draw = warm_material_program(mat, first);
    const nt_gfx_test_draw_t second_draw = warm_material_program(mat, second);
    nt_material_set_program(mat, first);

    nt_gfx_begin_frame();
    nt_gfx_begin_pass(&(nt_pass_desc_t){.clear_depth = 1.0F});
    nt_gfx_test_draw_trace_reset(true);
    nt_text_renderer_draw("AB", s_identity, 32.0F, s_white, 0.0F, 0.0F);
    nt_material_set_program(mat, second);
    nt_text_renderer_draw("AB", s_identity, 32.0F, s_white, 0.0F, 0.0F);
    nt_text_renderer_flush();
    nt_text_renderer_draw("AB", s_identity, 32.0F, s_white, 0.0F, 0.0F);
    nt_text_renderer_flush();
    nt_gfx_end_pass();
    nt_gfx_end_frame();

    TEST_ASSERT_EQUAL_UINT32(2U, nt_gfx_test_draw_trace_count());
    assert_text_draw(0U, first_draw, 4U);
    assert_text_draw(1U, second_draw, 2U);
    TEST_ASSERT_FALSE(nt_gfx_test_draw_trace_overflowed());
    TEST_ASSERT_EQUAL_UINT32(0U, nt_text_renderer_test_glyph_count());
}

/* An overflow flush inside a draw clears staging mid-batch. The tail is a new
 * batch and needs a pipeline of its own -- left on the flushed one it would be
 * discarded, or drawn through a program it was never laid out for. */
void test_overflow_flush_reopens_the_batch_pipeline(void) {
    nt_material_t mat = create_test_material_with_blend(nt_blend_alpha());
    const nt_program_t first = nt_material_get_info(mat)->program;
    const nt_program_t second = nt_gfx_make_program(make_stage(NT_SHADER_VERTEX), make_stage(NT_SHADER_FRAGMENT));
    const nt_gfx_test_draw_t first_draw = warm_material_program(mat, first);
    const nt_gfx_test_draw_t second_draw = warm_material_program(mat, second);
    nt_material_set_program(mat, first);

    nt_gfx_begin_frame();
    nt_gfx_begin_pass(&(nt_pass_desc_t){.clear_depth = 1.0F});
    nt_gfx_test_draw_trace_reset(true);

    /* Fill staging to one draw short of the cap, under program A. */
    for (uint32_t i = 0; i < (NT_TEXT_RENDERER_MAX_GLYPHS / 2U) - 1U; i++) {
        nt_text_renderer_draw("AB", s_identity, 32.0F, s_white, 0.0F, 0.0F);
    }

    /* The game swaps the program while that batch is still staged. */
    nt_material_set_program(mat, second);

    /* This draw overflows: emit flushes the A batch (drawn through A's pipeline)
     * and reopens on B for the glyphs that follow. */
    nt_text_renderer_draw("AB", s_identity, 32.0F, s_white, 0.0F, 0.0F);
    nt_text_renderer_draw("AB", s_identity, 32.0F, s_white, 0.0F, 0.0F);
    nt_text_renderer_test_reset_call_counters();
    nt_text_renderer_flush();

    nt_gfx_end_pass();
    nt_gfx_end_frame();

    /* The B tail must draw. Without reopening it is discarded instead. */
    TEST_ASSERT_EQUAL_UINT32(1U, nt_text_renderer_test_nonempty_flush_calls());
    TEST_ASSERT_EQUAL_UINT32(2U, nt_gfx_test_draw_trace_count());
    assert_text_draw(0U, first_draw, NT_TEXT_RENDERER_MAX_GLYPHS);
    assert_text_draw(1U, second_draw, 2U);
    TEST_ASSERT_FALSE(nt_gfx_test_draw_trace_overflowed());
}

void test_font_cache_flush_preserves_the_entire_run(void) {
    nt_font_create_desc_t desc = {
        .curve_texture_width = 16,
        .curve_texture_height = 2,
        .band_texture_height = 16,
        .band_count = 4,
    };
    nt_font_t tiny_font = nt_font_create(&desc);
    nt_font_add(tiny_font, register_font_resource("text_cache_flush", s_blob, s_blob_size));
    nt_resource_step();
    nt_font_step();
    nt_text_renderer_set_material(create_test_material_with_blend(nt_blend_alpha()));
    nt_text_renderer_set_font(tiny_font);
    const uint32_t generation = nt_font_get_cache_generation(tiny_font);

    nt_gfx_begin_frame();
    nt_gfx_begin_pass(&(nt_pass_desc_t){.clear_depth = 1.0F});
    nt_text_renderer_draw("ABCABC", s_identity, 32.0F, s_white, 0.0F, 0.0F);
    nt_text_renderer_flush();
    nt_gfx_end_pass();
    nt_gfx_end_frame();

    TEST_ASSERT_GREATER_THAN_UINT32(generation, nt_font_get_cache_generation(tiny_font));
    TEST_ASSERT_EQUAL_UINT32(36U, g_nt_gfx.frame_stats.indices);
    TEST_ASSERT_EQUAL_UINT32(24U, g_nt_gfx.frame_stats.vertices);
    TEST_ASSERT_EQUAL_UINT32(0U, nt_text_renderer_test_glyph_count());

    nt_text_renderer_set_underline(true);
    nt_text_renderer_set_strikethrough(true);
    nt_gfx_begin_frame();
    nt_gfx_begin_pass(&(nt_pass_desc_t){.clear_depth = 1.0F});
    nt_text_renderer_draw("ABCABC", s_identity, 32.0F, s_white, 0.0F, 0.0F);
    nt_text_renderer_flush();
    nt_gfx_end_pass();
    nt_gfx_end_frame();
    TEST_ASSERT_EQUAL_UINT32(48U, g_nt_gfx.frame_stats.indices);
    TEST_ASSERT_EQUAL_UINT32(32U, g_nt_gfx.frame_stats.vertices);
    TEST_ASSERT_EQUAL_UINT32(0U, nt_text_renderer_test_glyph_count());
    nt_text_renderer_set_font(s_font);
    nt_font_destroy(tiny_font);
}

void test_decoration_only_run_opens_its_pipeline(void) {
    uint32_t blob_size = 0;
    uint8_t *blob = build_test_font_blob(&blob_size);
    NtFontGlyphEntry entry;
    memcpy(&entry, blob + sizeof(NtFontAssetHeader), sizeof(entry));
    entry.bbox_x1 = entry.bbox_x0;
    entry.curve_count = 0;
    memcpy(blob + sizeof(NtFontAssetHeader), &entry, sizeof(entry));
    nt_font_create_desc_t desc = nt_font_create_desc_defaults();
    nt_font_t font = nt_font_create(&desc);
    nt_font_add(font, register_font_resource("text_decoration_only", blob, blob_size));
    nt_resource_step();
    nt_font_step();
    nt_material_t material = create_test_material_with_blend(nt_blend_alpha());
    nt_text_renderer_set_material(material);
    nt_text_renderer_set_font(font);
    nt_text_renderer_set_underline(true);
    nt_text_renderer_set_strikethrough(true);

    nt_gfx_begin_frame();
    nt_gfx_begin_pass(&(nt_pass_desc_t){.clear_depth = 1.0F});
    nt_gfx_test_draw_trace_reset(true);
    nt_text_renderer_draw("A", s_identity, 32.0F, s_white, 0.0F, 0.0F);
    nt_text_renderer_flush();
    nt_gfx_end_pass();
    nt_gfx_end_frame();
    TEST_ASSERT_EQUAL_UINT32(1U, nt_gfx_test_draw_trace_count());
    nt_gfx_test_draw_t draw = nt_gfx_test_draw_trace_at(0U);
    TEST_ASSERT_EQUAL_UINT32(nt_material_get_info(material)->program.id, draw.program.id);
    TEST_ASSERT_EQUAL_UINT32(12U, draw.num_indices);
    TEST_ASSERT_EQUAL_UINT32(8U, draw.num_vertices);
    TEST_ASSERT_FALSE(nt_gfx_test_draw_trace_overflowed());
    nt_text_renderer_set_font(s_font);
    nt_font_destroy(font);
    free(blob);
}

void test_destroyed_replaced_program_drops_staged_work(void) {
    nt_material_t material = create_test_material_with_blend(nt_blend_alpha());
    const nt_program_t first = nt_material_get_info(material)->program;
    const nt_program_t second = nt_gfx_make_program(make_stage(NT_SHADER_VERTEX), make_stage(NT_SHADER_FRAGMENT));
    (void)warm_material_program(material, first);
    const nt_gfx_test_draw_t second_draw = warm_material_program(material, second);
    nt_material_set_program(material, first);

    nt_gfx_begin_frame();
    nt_gfx_begin_pass(&(nt_pass_desc_t){.clear_depth = 1.0F});
    nt_gfx_test_draw_trace_reset(true);
    nt_text_renderer_draw("AB", s_identity, 32.0F, s_white, 0.0F, 0.0F);
    nt_material_set_program(material, second);
    nt_gfx_destroy_program(first);
    nt_text_renderer_flush();
    TEST_ASSERT_EQUAL_UINT32(0U, nt_gfx_test_draw_trace_count());
    nt_text_renderer_draw("AB", s_identity, 32.0F, s_white, 0.0F, 0.0F);
    nt_text_renderer_flush();
    nt_gfx_end_pass();
    nt_gfx_end_frame();
    TEST_ASSERT_EQUAL_UINT32(1U, nt_gfx_test_draw_trace_count());
    assert_text_draw(0U, second_draw, 2U);
    TEST_ASSERT_FALSE(nt_gfx_test_draw_trace_overflowed());
}

void test_unready_font_skips_glyph_and_decoration_uploads(void) {
    nt_log_add_sink(capture_errors, NULL);
    nt_text_renderer_set_material(create_test_material_with_blend(nt_blend_alpha()));
    for (uint8_t mask = 1U; mask <= 3U; mask++) {
        nt_text_renderer_reset_decoration();
        nt_gfx_fake_fail_texture_creates(mask);
        nt_font_create_desc_t desc = nt_font_create_desc_defaults();
        nt_font_t font = nt_font_create(&desc);
        nt_font_add(font, register_font_resource("text_unready_font", s_blob, s_blob_size));
        nt_resource_step();
        nt_gfx_fake_fail_texture_creates(mask);
        nt_font_step();
        nt_text_renderer_set_font(font);

        s_error_count = 0;
        const uint32_t uploads = nt_gfx_fake_update_texture_count();
        const uint32_t binds = nt_gfx_fake_bound_texture_count();
        nt_gfx_begin_frame();
        nt_gfx_begin_pass(&(nt_pass_desc_t){.clear_depth = 1.0F});
        nt_text_renderer_draw("ABC", s_identity, 32.0F, s_white, 0.0F, 0.0F);
        nt_text_renderer_set_weight(0.04F);
        nt_text_renderer_set_underline(true);
        nt_text_renderer_set_strikethrough(true);
        nt_text_renderer_draw("ABC", s_identity, 32.0F, s_white, 0.0F, 0.0F);
        nt_text_renderer_flush();
        nt_gfx_end_pass();
        nt_gfx_end_frame();

        TEST_ASSERT_EQUAL_UINT32(uploads, nt_gfx_fake_update_texture_count());
        TEST_ASSERT_EQUAL_UINT32(binds, nt_gfx_fake_bound_texture_count());
        TEST_ASSERT_EQUAL_UINT32(0U, g_nt_gfx.frame_stats.indices);
        TEST_ASSERT_EQUAL_UINT32(0U, s_error_count);
        TEST_ASSERT_EQUAL_UINT32(0U, nt_text_renderer_test_glyph_count());

        nt_gfx_fake_fail_texture_creates(0U);
        nt_font_step();
        const nt_glyph_cache_entry_t *glyph = nt_font_lookup_glyph(font, 'A');
        TEST_ASSERT_NOT_NULL(glyph);
        TEST_ASSERT_FALSE(glyph->is_tofu);
        nt_gfx_begin_frame();
        nt_gfx_begin_pass(&(nt_pass_desc_t){.clear_depth = 1.0F});
        nt_text_renderer_draw("ABC", s_identity, 32.0F, s_white, 0.0F, 0.0F);
        nt_text_renderer_flush();
        nt_gfx_end_pass();
        nt_gfx_end_frame();
        TEST_ASSERT_GREATER_THAN_UINT32(uploads, nt_gfx_fake_update_texture_count());
        TEST_ASSERT_EQUAL_UINT32(30U, g_nt_gfx.frame_stats.indices);
        TEST_ASSERT_EQUAL_UINT32(20U, g_nt_gfx.frame_stats.vertices);
        TEST_ASSERT_EQUAL_UINT32(0U, s_error_count);
        nt_text_renderer_set_font(s_font);
        nt_font_destroy(font);
    }
}

/* Alternating UI materials should reuse cached pipelines instead of rebuilding their VAOs. */
void test_switching_back_to_a_material_reuses_its_pipeline(void) {
    nt_material_t a = create_test_material_with_blend(nt_blend_alpha());
    nt_material_t b = create_test_material_with_blend(nt_blend_opaque());

    nt_gfx_fake_reset();
    nt_text_renderer_set_material(a);
    draw_and_flush();
    nt_text_renderer_set_material(b);
    draw_and_flush();
    nt_text_renderer_set_material(a);
    draw_and_flush();

    /* Three switches, two distinct materials: the third must be a cache hit. */
    TEST_ASSERT_EQUAL_UINT32(2U, nt_gfx_fake_pipeline_create_count());
}

/* A reset leaves nothing behind: the next draw builds a pipeline for the new
 * program rather than reusing the entry built on the old one. */
void test_a_new_program_after_a_reset_does_not_reuse_the_old_pipeline(void) {
    nt_material_t mat = create_test_material_with_blend(nt_blend_alpha());
    nt_shader_t vs = nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_VERTEX, .source = "void main(){}"});
    nt_shader_t fs = nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_FRAGMENT, .source = "void main(){}"});

    nt_gfx_fake_reset();
    nt_text_renderer_set_material(mat);
    draw_and_flush();
    TEST_ASSERT_EQUAL_UINT32(1U, nt_gfx_fake_pipeline_create_count());

    nt_text_renderer_restore_gpu();
    nt_material_set_program(mat, nt_gfx_make_program(vs, fs));

    nt_text_renderer_set_material(mat);
    nt_text_renderer_draw("AB", s_identity, 32.0F, s_white, 0.0F, 0.0F);
    nt_gfx_begin_frame();
    nt_gfx_begin_pass(&(nt_pass_desc_t){.clear_depth = 1.0F});
    nt_text_renderer_flush();
    nt_gfx_end_pass();
    nt_gfx_end_frame();

    TEST_ASSERT_EQUAL_UINT32(2U, nt_gfx_fake_pipeline_create_count());
}

/* A material still holding a destroyed program is what the whole restore window
 * looks like, so flush discards the glyphs and retries rather than trapping. */
void test_flush_discards_glyphs_on_a_destroyed_program(void) {
    nt_material_t material = create_test_material_with_blend(nt_blend_alpha());
    const nt_program_t dead = nt_material_get_info(material)->program;
    nt_text_renderer_set_material(material);

    nt_gfx_begin_frame();
    nt_gfx_begin_pass(&(nt_pass_desc_t){.clear_depth = 1.0F});

    /* The material keeps the handle: recovery destroys programs, not materials. */
    nt_gfx_destroy_program(dead);

    nt_text_renderer_draw("AB", s_identity, 32.0F, s_white, 0.0F, 0.0F);
    nt_gfx_fake_reset();
    nt_text_renderer_flush();

    /* Nothing drawn, nothing built, staging cleared for the next frame. */
    TEST_ASSERT_EQUAL_UINT32(0U, nt_gfx_fake_pipeline_create_count());
    TEST_ASSERT_EQUAL_UINT32(0U, nt_text_renderer_test_glyph_count());

    nt_gfx_end_pass();
    nt_gfx_end_frame();
}

/* Restore preserves the material and clears staging; a relink rebuilds its pipeline and resumes drawing. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void test_restore_cycle_reuses_the_material_and_rebuilds_the_pipeline(void) {
    nt_material_t material = create_test_material_with_blend(nt_blend_alpha());
    const nt_material_t handle_before = material;
    const nt_program_t first = nt_material_get_info(material)->program;

    nt_gfx_fake_reset();
    nt_text_renderer_set_material(material);
    nt_text_renderer_draw("AB", s_identity, 32.0F, s_white, 0.0F, 0.0F);
    nt_gfx_begin_frame();
    nt_gfx_begin_pass(&(nt_pass_desc_t){.clear_depth = 1.0F});
    nt_text_renderer_flush();
    nt_gfx_end_pass();
    nt_gfx_end_frame();
    TEST_ASSERT_EQUAL_UINT32(1U, nt_gfx_fake_pipeline_create_count());

    nt_text_renderer_draw("C", s_identity, 32.0F, s_white, 0.0F, 0.0F);
    TEST_ASSERT_EQUAL_UINT32(1U, nt_text_renderer_test_glyph_count());
    /* Context dies: handles stay valid, GPU objects do not. */
    nt_gfx_fake_set_context_lost(true);
    nt_gfx_begin_frame();
    TEST_ASSERT_TRUE(nt_gfx_program_valid(first));
    TEST_ASSERT_FALSE(nt_gfx_program_ready(first));

    /* Restore frame: reset the renderer, drop the program, keep the material. */
    nt_gfx_fake_set_context_lost(false);
    nt_gfx_begin_frame();
    TEST_ASSERT_EQUAL_INT(NT_OK, nt_text_renderer_restore_gpu());
    TEST_ASSERT_EQUAL_UINT32(0U, nt_text_renderer_test_glyph_count());
    nt_gfx_destroy_program(first);
    nt_gfx_end_frame();
    TEST_ASSERT_EQUAL_UINT16(0U, nt_text_renderer_test_pipeline_cache_count());
    /* The handle still names a live material -- recovery destroys programs, not
     * materials, which is why ECS components need no re-binding. */
    TEST_ASSERT_TRUE(nt_material_valid(handle_before));
    TEST_ASSERT_NOT_NULL(nt_material_get_info(handle_before));

    /* The game's gate relinks and re-assigns onto the same material. */
    nt_program_t second = nt_gfx_make_program(nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_VERTEX, .source = "void main(){}"}),
                                              nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_FRAGMENT, .source = "void main(){}"}));
    nt_material_set_program(material, second);
    TEST_ASSERT_NOT_EQUAL_UINT32(first.id, second.id);

    nt_gfx_fake_reset();
    nt_font_step();
    nt_text_renderer_set_material(material);
    nt_text_renderer_draw("AB", s_identity, 32.0F, s_white, 0.0F, 0.0F);
    nt_gfx_begin_frame();
    nt_gfx_begin_pass(&(nt_pass_desc_t){.clear_depth = 1.0F});
    nt_text_renderer_flush();
    nt_gfx_end_pass();
    nt_gfx_end_frame();

    TEST_ASSERT_EQUAL_UINT32(1U, nt_gfx_fake_pipeline_create_count());
    TEST_ASSERT_EQUAL_UINT16(1U, nt_text_renderer_test_pipeline_cache_count());
    TEST_ASSERT_GREATER_THAN_UINT32(0U, nt_gfx_fake_update_texture_count());
    TEST_ASSERT_EQUAL_UINT32(12U, g_nt_gfx.frame_stats.indices);
    TEST_ASSERT_EQUAL_UINT32(8U, g_nt_gfx.frame_stats.vertices);
}

/* ---- Test 12: TEXT-01 — _draw_n produces byte-identical vertex stream to _draw ---- */

void test_draw_n_matches_draw(void) {
    /* Capture vertex stream from existing _draw on NUL-terminated "AB" */
    nt_text_renderer_draw("AB", s_identity, 32.0F, s_white, 0.0F, 0.0F);
    const uint32_t draw_vcount = nt_text_renderer_test_vertex_count();
    const uint32_t draw_gcount = nt_text_renderer_test_glyph_count();
    TEST_ASSERT_EQUAL_UINT32(8U, draw_vcount); /* 2 visible glyphs × 4 verts */
    TEST_ASSERT_EQUAL_UINT32(2U, draw_gcount);

    /* Snapshot vertex bytes — flush will zero the staging buffer counters next,
     * so we copy out before reset. Stride is 72 bytes per nt_text_vertex_t. */
    const size_t bytes_to_copy = (size_t)draw_vcount * 72U;
    uint8_t buf_draw[8U * 72U];
    memcpy(buf_draw, nt_text_renderer_test_vertices(), bytes_to_copy);

    /* Reset staging counters (no pipeline → flush warns + zeros counters). */
    nt_text_renderer_flush();
    TEST_ASSERT_EQUAL_UINT32(0U, nt_text_renderer_test_vertex_count());

    /* Call length-aware variant with matching length */
    nt_text_renderer_draw_n("AB", 2U, s_identity, 32.0F, s_white, 0.0F, 0.0F);
    const uint32_t draw_n_vcount = nt_text_renderer_test_vertex_count();
    const uint32_t draw_n_gcount = nt_text_renderer_test_glyph_count();

    TEST_ASSERT_EQUAL_UINT32(draw_vcount, draw_n_vcount);
    TEST_ASSERT_EQUAL_UINT32(draw_gcount, draw_n_gcount);
    TEST_ASSERT_EQUAL_MEMORY(buf_draw, nt_text_renderer_test_vertices(), bytes_to_copy);
}

/* ---- Test 13b: letter_spacing shifts subsequent glyphs by spacing pixels ---- */

void test_draw_n_letter_spacing_advances_pen(void) {
    /* Baseline: AB with zero spacing -- second glyph's first vertex x at advance(A). */
    nt_text_renderer_draw_n("AB", 2U, s_identity, 32.0F, s_white, 0.0F, 0.0F);
    TEST_ASSERT_EQUAL_UINT32(8U, nt_text_renderer_test_vertex_count());
    const uint8_t *vraw = (const uint8_t *)nt_text_renderer_test_vertices();
    float base_b_x = 0.0F;
    memcpy(&base_b_x, vraw + ((size_t)4U * 72U), sizeof(float));

    nt_text_renderer_flush();
    TEST_ASSERT_EQUAL_UINT32(0U, nt_text_renderer_test_vertex_count());

    /* Same call with spacing=7: B's first vertex shifts by exactly 7 px. */
    nt_text_renderer_draw_n("AB", 2U, s_identity, 32.0F, s_white, 7.0F, 0.0F);
    const uint8_t *vspaced = (const uint8_t *)nt_text_renderer_test_vertices();
    float spaced_b_x = 0.0F;
    memcpy(&spaced_b_x, vspaced + ((size_t)4U * 72U), sizeof(float));

    /* UNITY_EXCLUDE_FLOAT in this build: int-truncate to compare. */
    TEST_ASSERT_EQUAL_INT32((int32_t)(base_b_x + 7.0F), (int32_t)spaced_b_x);
}

/* ---- line_leading shifts subsequent lines by leading px on \n ---- */

void test_draw_n_line_leading_advances_pen_y(void) {
    /* Baseline: "A\nB" with zero leading -- B at y = -natural_line_advance. */
    nt_text_renderer_draw_n("A\nB", 3U, s_identity, 32.0F, s_white, 0.0F, 0.0F);
    TEST_ASSERT_EQUAL_UINT32(8U, nt_text_renderer_test_vertex_count());
    const uint8_t *vraw = (const uint8_t *)nt_text_renderer_test_vertices();
    /* vertex 0 = A's first corner, vertex 4 = B's first corner. y is float[1]. */
    float base_b_y = 0.0F;
    memcpy(&base_b_y, vraw + ((size_t)4U * 72U) + sizeof(float), sizeof(float));

    nt_text_renderer_flush();

    /* Same call with leading=10: B shifts by 10 more px downward. */
    nt_text_renderer_draw_n("A\nB", 3U, s_identity, 32.0F, s_white, 0.0F, 10.0F);
    const uint8_t *vleading = (const uint8_t *)nt_text_renderer_test_vertices();
    float leading_b_y = 0.0F;
    memcpy(&leading_b_y, vleading + ((size_t)4U * 72U) + sizeof(float), sizeof(float));

    /* B drew lower by exactly 10px (pen_y decreases by line_advance, which got +10). */
    TEST_ASSERT_EQUAL_INT32((int32_t)(base_b_y - 10.0F), (int32_t)leading_b_y);
}

/* ---- Test 13: TEXT-01b — poisoned byte at utf8[len] does NOT contribute (no over-read) ---- */

void test_draw_n_does_not_over_read(void) {
    /* Reference: _draw on the clean "AB" string */
    nt_text_renderer_draw("AB", s_identity, 32.0F, s_white, 0.0F, 0.0F);
    const uint32_t ref_vcount = nt_text_renderer_test_vertex_count();
    TEST_ASSERT_EQUAL_UINT32(8U, ref_vcount);

    const size_t bytes_to_copy = (size_t)ref_vcount * 72U;
    uint8_t buf_ref[8U * 72U];
    memcpy(buf_ref, nt_text_renderer_test_vertices(), bytes_to_copy);

    nt_text_renderer_flush();
    TEST_ASSERT_EQUAL_UINT32(0U, nt_text_renderer_test_vertex_count());

    /* Stack buffer with poisoned bytes at and past index 2. Stack-array literal
     * expresses "intentionally not NUL-terminated" without tripping
     * bugprone-not-null-terminated-result (matches test_font.c precedent). */
    const char buf[8] = {'A', 'B', 'X', 'X', 'X', 'X', 'X', 'X'};

    nt_text_renderer_draw_n(buf, 2U, s_identity, 32.0F, s_white, 0.0F, 0.0F);
    const uint32_t bounded_vcount = nt_text_renderer_test_vertex_count();

    TEST_ASSERT_EQUAL_UINT32(ref_vcount, bounded_vcount);
    TEST_ASSERT_EQUAL_MEMORY(buf_ref, nt_text_renderer_test_vertices(), bytes_to_copy);
}

/* ---- C10: a single large rich block routes ALL its glyphs through this shared text-renderer staging
 * buffer during emit_custom self-emit. A run whose glyph total approaches/exceeds the staging cap must
 * NOT overflow the fixed staging arrays: emit_quad flushes at the NT_TEXT_RENDERER_MAX_GLYPHS boundary,
 * so glyph_count never exceeds the cap and vertex_count never exceeds MAX_VERTICES.
 *
 * Cap interaction (documented): a single rich block caps its ATOM stream at NT_UI_RICH_MAX_GLYPHS (2048),
 * but a non-effect run emits ALL its glyphs through ONE draw_n span -- so the glyph total a single span
 * pushes into staging is bounded by the run's text length, NOT by the atom cap. The text-renderer
 * staging cap (4096) is therefore the binding overflow guard for a big paragraph, exercised here by a
 * single draw_n far larger than the cap. ---- */
void test_draw_n_large_run_flushes_at_staging_cap(void) {
    nt_text_renderer_flush(); /* start from an empty staging buffer */
    TEST_ASSERT_EQUAL_UINT32(0U, nt_text_renderer_test_glyph_count());

    /* A single run well past the renderer cap (no \n -> one continuous span, like a big rich line).
     * The synthetic font has 'A' as a real glyph (advance 500), so every char stages a quad. */
    const uint32_t over_cap = (uint32_t)NT_TEXT_RENDERER_MAX_GLYPHS + 137U; /* deliberately past the cap */
    char *big = (char *)malloc((size_t)over_cap + 1U);
    TEST_ASSERT_NOT_NULL(big);
    memset(big, 'A', over_cap);
    big[over_cap] = '\0';

    /* One draw_n with the whole oversized run. emit_quad must flush at the cap so neither staging
     * array overflows; the call must not trap or write OOB. */
    nt_text_renderer_draw_n(big, (size_t)over_cap, s_identity, 32.0F, s_white, 0.0F, 0.0F);

    /* After the call the staging buffer holds only the post-flush remainder -- bounded by the cap. */
    TEST_ASSERT_TRUE_MESSAGE(nt_text_renderer_test_glyph_count() <= (uint32_t)NT_TEXT_RENDERER_MAX_GLYPHS, "glyph_count never exceeds the staging cap (flush-at-boundary kept it bounded)");
    TEST_ASSERT_TRUE_MESSAGE(nt_text_renderer_test_vertex_count() <= (uint32_t)NT_TEXT_RENDERER_MAX_VERTICES, "vertex_count never exceeds MAX_VERTICES (4 per glyph, bounded by the cap)");

    /* A second draw of a small run on top still stays bounded (the renderer recovered cleanly). */
    nt_text_renderer_draw_n("ABC", 3U, s_identity, 32.0F, s_white, 0.0F, 0.0F);
    TEST_ASSERT_TRUE_MESSAGE(nt_text_renderer_test_glyph_count() <= (uint32_t)NT_TEXT_RENDERER_MAX_GLYPHS, "staging stays bounded after a follow-up draw");

    nt_text_renderer_flush();
    free(big);
}

/* ---- The actual safety net for a single rich TEXT run is the renderer's SELF-FLUSH at the staging cap
 * (test_draw_n_large_run_flushes_at_staging_cap), NOT a cap ratio: a non-effect run emits at most one
 * glyph per codepoint, so a single run's worst-case glyph total is bounded by its byte count
 * (NT_UI_RICH_MAX_TEXT_BYTES, one glyph per byte upper bound). That cap EQUALS the renderer staging cap
 * (NT_TEXT_RENDERER_MAX_GLYPHS), so a single max run sits right at the boundary -- the self-flush, not
 * headroom, is what keeps it bounded. Pin <= here (mirrored literal; this TU does not link nt_ui) so a
 * future bump of either cap that pushed the text-byte cap ABOVE the renderer cap (where a single run
 * could overrun BEFORE the flush) trips this gate. ---- */
#define NT_UI_RICH_MAX_TEXT_BYTES_MIRROR 4096U /* must match NT_UI_RICH_MAX_TEXT_BYTES in ui/nt_ui_rich_text.h */
void test_rich_atom_cap_below_text_staging_cap(void) {
    TEST_ASSERT_TRUE_MESSAGE(NT_UI_RICH_MAX_TEXT_BYTES_MIRROR <= (uint32_t)NT_TEXT_RENDERER_MAX_GLYPHS,
                             "rich text-byte cap (1 glyph/byte upper bound for one run) must not exceed the renderer staging cap; the renderer self-flush is the real net");
}

/* ---- Benchmark cases (printed as [BENCH] lines; cover draw hot-loop perf) ---- */

static void bench_draw_short_warm(void) {
    /* GPU-side glyph cache and font ASCII fast-path warm via setup. We flush
     * once before timing so the renderer buffer starts empty, and flush per
     * loop iteration to keep glyph_count from saturating across 1000 draws. */
    nt_text_renderer_flush();

    const int n_calls = 1000;
    const uint64_t t0 = nt_time_nanos();
    for (int i = 0; i < n_calls; i++) {
        nt_text_renderer_draw_n("ABC", 3U, s_identity, 32.0F, s_white, 0.0F, 0.0F);
        nt_text_renderer_flush(); /* exclude buffer-overflow path from timing */
    }
    const uint64_t t1 = nt_time_nanos();

    const double per_call_ns = (double)(t1 - t0) / (double)n_calls;
    (void)printf("[BENCH] draw_short_warm (3 chars + flush): %.2f ns/call\n", per_call_ns);
    (void)fflush(stdout);
}

static void bench_draw_mixed_ui(void) {
    /* 6 hot labels each redrawn over 200 "frames" with a flush between frames.
     * Approximates a stable UI re-render path where the GPU glyph cache and
     * the ASCII fast-path are both warm. */
    const char *const labels[] = {"OK", "AB", "BC", "CA", "ABC", "BCA"};
    const size_t lens[] = {2U, 2U, 2U, 2U, 3U, 3U};
    const int label_count = (int)(sizeof(labels) / sizeof(labels[0]));

    /* Warm up the glyph cache once. */
    for (int i = 0; i < label_count; i++) {
        nt_text_renderer_draw_n(labels[i], lens[i], s_identity, 32.0F, s_white, 0.0F, 0.0F);
    }
    nt_text_renderer_flush();

    const int frames = 200;
    int total_calls = 0;
    const uint64_t t0 = nt_time_nanos();
    for (int f = 0; f < frames; f++) {
        for (int i = 0; i < label_count; i++) {
            nt_text_renderer_draw_n(labels[i], lens[i], s_identity, 32.0F, s_white, 0.0F, 0.0F);
            total_calls++;
        }
        nt_text_renderer_flush();
    }
    const uint64_t t1 = nt_time_nanos();

    const double per_call_ns = (double)(t1 - t0) / (double)total_calls;
    (void)printf("[BENCH] draw_mixed_ui (6 labels × 200 frames): %.2f ns/call (%d calls)\n", per_call_ns, total_calls);
    (void)fflush(stdout);
}

/* ---- glyph depth bias lifecycle ---- */

/* Renderer state, not a file-static: preserved across a GPU context-loss restore like material/font
 * (a game sets it once for world-space nameplates and must not lose it on restore). */
void test_glyph_depth_bias_persists_across_restore(void) {
    nt_text_renderer_set_glyph_depth_bias(0.25F); /* exactly representable, so == is safe */
    nt_text_renderer_restore_gpu();
    TEST_ASSERT_TRUE(nt_text_renderer_test_glyph_depth_bias() == 0.25F);
}

/* Cold shutdown/init clears it (test isolation; no leak across renderer reinit). */
void test_glyph_depth_bias_resets_on_reinit(void) {
    nt_text_renderer_set_glyph_depth_bias(0.25F);
    nt_text_renderer_shutdown();
    nt_text_renderer_init();
    TEST_ASSERT_TRUE(nt_text_renderer_test_glyph_depth_bias() == 0.0F);
}

/* ---- synthetic-oblique (faux-italic) ---- */

/* set_oblique shears the model so a glyph's top edge shifts +x relative to its bottom (lean about the
 * baseline). 0 = upright: top and bottom share x under the identity model. The test font 'A' spans
 * em y -200..800 → ~33px tall at size 32, so oblique 0.5 leans the top ~16px. */
void test_oblique_leans_glyph_top(void) {
    nt_text_renderer_set_oblique(0.0F); /* explicit upright (init already zeroed it) */
    nt_text_renderer_draw("A", s_identity, 32.0F, s_white, 0.0F, 0.0F);
    TEST_ASSERT_EQUAL_UINT32(1U, nt_text_renderer_test_glyph_count());
    const uint8_t *v = (const uint8_t *)nt_text_renderer_test_vertices();
    float bl_x = 0.0F; /* vertex 0 = BL */
    float tl_x = 0.0F; /* vertex 3 = TL */
    memcpy(&bl_x, v + 0, sizeof(float));
    memcpy(&tl_x, v + ((size_t)3U * 72U), sizeof(float));
    TEST_ASSERT_TRUE(bl_x == tl_x); /* upright: no shear -> top and bottom share x exactly */

    nt_text_renderer_flush();
    nt_text_renderer_set_oblique(0.5F);
    TEST_ASSERT_TRUE(nt_text_renderer_test_oblique() == 0.5F);
    nt_text_renderer_draw("A", s_identity, 32.0F, s_white, 0.0F, 0.0F);
    v = (const uint8_t *)nt_text_renderer_test_vertices();
    memcpy(&bl_x, v + 0, sizeof(float));
    memcpy(&tl_x, v + ((size_t)3U * 72U), sizeof(float));
    TEST_ASSERT_TRUE_MESSAGE(tl_x > bl_x + 1.0F, "oblique leans the glyph top toward +x");

    nt_text_renderer_set_oblique(0.0F); /* restore upright for test isolation */
}

/* Renderer state, not a file-static: survives a GPU context-loss restore like material/font/depth-bias. */
void test_oblique_persists_across_restore(void) {
    nt_text_renderer_set_oblique(0.25F); /* exactly representable, so == is safe */
    nt_text_renderer_restore_gpu();
    TEST_ASSERT_TRUE(nt_text_renderer_test_oblique() == 0.25F);
    nt_text_renderer_set_oblique(0.0F);
}

/* Cold shutdown/init clears it (test isolation; no lean leaks across renderer reinit). */
void test_oblique_resets_on_reinit(void) {
    nt_text_renderer_set_oblique(0.25F);
    nt_text_renderer_shutdown();
    nt_text_renderer_init();
    TEST_ASSERT_TRUE(nt_text_renderer_test_oblique() == 0.0F);
}

/* ---- sticky decoration state lifetime ---- */

/* Decoration state is renderer state like oblique: survives a GPU context-loss restore. */
void test_decoration_persists_across_restore(void) {
    nt_text_renderer_set_weight(0.25F); /* exactly representable */
    const float red[4] = {1.0F, 0.0F, 0.0F, 1.0F};
    nt_text_renderer_set_outline(0.5F, red);
    nt_text_renderer_set_shadow(2.0F, -1.0F, 0.0F, red);
    nt_text_renderer_set_underline(true);
    nt_text_renderer_restore_gpu();
    TEST_ASSERT_TRUE(nt_text_renderer_test_weight() == 0.25F);
    TEST_ASSERT_TRUE(nt_text_renderer_test_outline_width() == 0.5F);
    TEST_ASSERT_TRUE(nt_text_renderer_test_shadow_dx() == 2.0F);
    TEST_ASSERT_TRUE(nt_text_renderer_test_underline());
    nt_text_renderer_reset_decoration();
}

/* reset_decoration clears every axis (weight/outline/shadow/underline) AND oblique in one call. */
void test_reset_decoration_clears_all(void) {
    const float red[4] = {1.0F, 0.0F, 0.0F, 1.0F};
    nt_text_renderer_set_weight(0.25F);
    nt_text_renderer_set_outline(0.5F, red);
    nt_text_renderer_set_shadow(2.0F, -1.0F, 0.0F, red);
    nt_text_renderer_set_underline(true);
    nt_text_renderer_set_strikethrough(true);
    nt_text_renderer_set_oblique(0.5F);

    nt_text_renderer_reset_decoration();

    TEST_ASSERT_TRUE(nt_text_renderer_test_weight() == 0.0F);
    TEST_ASSERT_TRUE(nt_text_renderer_test_outline_width() == 0.0F);
    TEST_ASSERT_TRUE(nt_text_renderer_test_shadow_dx() == 0.0F);
    TEST_ASSERT_FALSE(nt_text_renderer_test_underline());
    TEST_ASSERT_TRUE(nt_text_renderer_test_oblique() == 0.0F);
}

/* Cold shutdown/init clears decoration state (test isolation; no leak across renderer reinit). */
void test_decoration_resets_on_reinit(void) {
    const float red[4] = {1.0F, 0.0F, 0.0F, 1.0F};
    nt_text_renderer_set_weight(0.25F);
    nt_text_renderer_set_outline(0.5F, red);
    nt_text_renderer_set_underline(true);
    nt_text_renderer_shutdown();
    nt_text_renderer_init();
    TEST_ASSERT_TRUE(nt_text_renderer_test_weight() == 0.0F);
    TEST_ASSERT_TRUE(nt_text_renderer_test_outline_width() == 0.0F);
    TEST_ASSERT_FALSE(nt_text_renderer_test_underline());
}

/* ---- three-pass painter-order emit + sentinel decoration quad ---- */

static const float s_black[4] = {0.0F, 0.0F, 0.0F, 1.0F};

/* Outline adds a second draw span (fill + outline pass) → exactly 2× the fill-only vertex count. */
void test_outline_emits_extra_span(void) {
    nt_text_renderer_draw("A", s_identity, 32.0F, s_white, 0.0F, 0.0F);
    const uint32_t fill_only = nt_text_renderer_test_vertex_count();
    TEST_ASSERT_EQUAL_UINT32(4U, fill_only); /* one visible glyph */
    nt_text_renderer_flush();

    nt_text_renderer_set_outline(0.05F, s_black);
    nt_text_renderer_draw("A", s_identity, 32.0F, s_white, 0.0F, 0.0F);
    TEST_ASSERT_EQUAL_UINT32(2U * fill_only, nt_text_renderer_test_vertex_count()); /* fill + outline */
    nt_text_renderer_reset_decoration();
}

/* Shadow adds one more pass; shadow+outline = three passes. */
void test_shadow_emits_extra_span(void) {
    nt_text_renderer_set_shadow(2.0F, 2.0F, 0.0F, s_black);
    nt_text_renderer_draw("A", s_identity, 32.0F, s_white, 0.0F, 0.0F);
    TEST_ASSERT_EQUAL_UINT32(8U, nt_text_renderer_test_vertex_count()); /* fill + shadow */
    nt_text_renderer_flush();

    nt_text_renderer_set_outline(0.05F, s_black);
    nt_text_renderer_draw("A", s_identity, 32.0F, s_white, 0.0F, 0.0F);
    TEST_ASSERT_EQUAL_UINT32(12U, nt_text_renderer_test_vertex_count()); /* fill + outline + shadow */
    nt_text_renderer_reset_decoration();
}

/* Shadow pass reuses the fill variant translated by (dx,dy)*scale — no new cache key, exact offset. */
void test_shadow_pass_offset(void) {
    /* Shadow offset is em: px = d * size. Use size != units_per_em so the em contract is unambiguous
     * (a design-unit *scale would give a different number) — (0.1,-0.05)em at size 200 = (+20,-10). */
    nt_text_renderer_set_shadow(0.1F, -0.05F, 0.0F, s_black);
    nt_text_renderer_draw("A", s_identity, 200.0F, s_white, 0.0F, 0.0F);
    TEST_ASSERT_EQUAL_UINT32(2U, nt_text_renderer_test_glyph_count()); /* shadow (quad0) + fill (quad1) */
    const uint8_t *v = (const uint8_t *)nt_text_renderer_test_vertices();
    float shadow_x = 0.0F;
    float shadow_y = 0.0F;
    float fill_x = 0.0F;
    float fill_y = 0.0F;
    memcpy(&shadow_x, v + 0, sizeof(float)); /* quad0 v0 = shadow */
    memcpy(&shadow_y, v + sizeof(float), sizeof(float));
    memcpy(&fill_x, v + ((size_t)4U * 72U), sizeof(float)); /* quad1 v0 = fill */
    memcpy(&fill_y, v + ((size_t)4U * 72U) + sizeof(float), sizeof(float));
    TEST_ASSERT_EQUAL_INT32(20, (int32_t)(shadow_x - fill_x));
    TEST_ASSERT_EQUAL_INT32(-10, (int32_t)(shadow_y - fill_y));
    nt_text_renderer_reset_decoration();
}

/* Passes emit GROUPED (all shadows, then all fills) — never interleaved per glyph. With "AB" the order
 * must be [shadow_A, shadow_B, fill_A, fill_B]: both shadow quads sit +20 from their fills. If interleaved
 * ([shadow_A, fill_A, shadow_B, fill_B]) quad1 would be fill_A and the +20 check on quad1 vs quad3 fails. */
void test_passes_grouped_not_interleaved(void) {
    nt_text_renderer_set_shadow(0.1F, 0.0F, 0.0F, s_black); /* em -> +20px at size 200 */
    nt_text_renderer_draw("AB", s_identity, 200.0F, s_white, 0.0F, 0.0F);
    TEST_ASSERT_EQUAL_UINT32(4U, nt_text_renderer_test_glyph_count()); /* 2 shadow + 2 fill */
    const uint8_t *v = (const uint8_t *)nt_text_renderer_test_vertices();
    float sh_a = 0.0F;
    float sh_b = 0.0F;
    float fl_a = 0.0F;
    float fl_b = 0.0F;
    memcpy(&sh_a, v + ((size_t)0U * 4U * 72U), sizeof(float)); /* quad0 = shadow_A */
    memcpy(&sh_b, v + ((size_t)1U * 4U * 72U), sizeof(float)); /* quad1 = shadow_B */
    memcpy(&fl_a, v + ((size_t)2U * 4U * 72U), sizeof(float)); /* quad2 = fill_A */
    memcpy(&fl_b, v + ((size_t)3U * 4U * 72U), sizeof(float)); /* quad3 = fill_B */
    TEST_ASSERT_EQUAL_INT32(20, (int32_t)(sh_a - fl_a));
    TEST_ASSERT_EQUAL_INT32(20, (int32_t)(sh_b - fl_b)); /* grouping: quad1 is shadow_B, not fill_A */
    nt_text_renderer_reset_decoration();
}

/* Underline emits exactly ONE solid sentinel quad (band_count=0) per line after the glyph passes. */
void test_underline_one_quad_per_segment(void) {
    nt_text_renderer_set_underline(true);
    nt_text_renderer_draw("AB", s_identity, 32.0F, s_white, 0.0F, 0.0F);
    /* 2 fill glyphs + 1 underline quad. */
    TEST_ASSERT_EQUAL_UINT32(3U, nt_text_renderer_test_glyph_count());

    /* The 3rd quad is the decoration sentinel: band_count (glyph_data[3]) packed as uint 0. */
    const uint8_t *v = (const uint8_t *)nt_text_renderer_test_vertices();
    uint32_t band_count = 0xFFFFFFFFU;
    /* vertex 8 (quad2 v0), glyph_data at byte offset 20, [3] at +12 = 32. */
    memcpy(&band_count, v + ((size_t)8U * 72U) + 20U + 12U, sizeof(uint32_t));
    TEST_ASSERT_EQUAL_UINT32(0U, band_count);
    nt_text_renderer_reset_decoration();
}

/* One decoration quad PER LINE: two lines → two underline quads (continuous per same-style segment). */
void test_underline_quad_per_line(void) {
    nt_text_renderer_set_underline(true);
    nt_text_renderer_draw("A\nB", s_identity, 32.0F, s_white, 0.0F, 0.0F);
    /* 2 fill glyphs + 2 underline quads (one per line). */
    TEST_ASSERT_EQUAL_UINT32(4U, nt_text_renderer_test_glyph_count());
    nt_text_renderer_reset_decoration();
}

/* After reset_decoration a draw emits fill-only vertices (no pass/quad leak). */
void test_reset_decoration_fill_only(void) {
    nt_text_renderer_set_outline(0.05F, s_black);
    nt_text_renderer_set_shadow(2.0F, 2.0F, 0.0F, s_black);
    nt_text_renderer_set_underline(true);
    nt_text_renderer_reset_decoration();

    nt_text_renderer_draw("A", s_identity, 32.0F, s_white, 0.0F, 0.0F);
    TEST_ASSERT_EQUAL_UINT32(4U, nt_text_renderer_test_vertex_count());
    TEST_ASSERT_EQUAL_UINT32(1U, nt_text_renderer_test_glyph_count());
}

/* ---- main ---- */

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_utf8_decode_ascii);
    RUN_TEST(test_text_renderer_forwards_material_blend_state);
    RUN_TEST(test_text_renderer_font_textures_land_on_program_units);
    RUN_TEST(test_utf8_decode_cyrillic);
    RUN_TEST(test_utf8_decode_cjk);
    RUN_TEST(test_measure_returns_nonzero);
    RUN_TEST(test_measure_empty_string);
    RUN_TEST(test_measure_null_string);
    RUN_TEST(test_vertex_stride_72);
    RUN_TEST(test_vertex_count_4_per_glyph);
    RUN_TEST(test_text_material_with_textures_asserts_at_flush);
    RUN_TEST(test_flush_resets_counts);
    RUN_TEST(test_measure_width_increases);
    RUN_TEST(test_draw_newline_advances_to_next_line);
    RUN_TEST(test_flush_stops_after_program_cleared);
    RUN_TEST(test_flush_retries_vertex_input_after_backend_failure);
    RUN_TEST(test_failed_restore_releases_partial_buffers);
    RUN_TEST(test_flush_discards_glyphs_on_a_destroyed_program);
    RUN_TEST(test_restore_cycle_reuses_the_material_and_rebuilds_the_pipeline);
    RUN_TEST(test_materials_sharing_a_program_share_one_pipeline);
    RUN_TEST(test_one_program_with_two_render_states_builds_two_pipelines);
    RUN_TEST(test_neighbouring_programs_one_cull_step_apart_get_their_own_pipelines);
    RUN_TEST(test_a_reused_program_slot_does_not_hit_the_dead_entry);
    RUN_TEST(test_program_ref_reclaims_a_program_killed_by_context_loss);
    RUN_TEST(test_a_replaced_program_does_not_redirect_a_staged_batch);
    RUN_TEST(test_overflow_flush_reopens_the_batch_pipeline);
    RUN_TEST(test_font_cache_flush_preserves_the_entire_run);
    RUN_TEST(test_decoration_only_run_opens_its_pipeline);
    RUN_TEST(test_destroyed_replaced_program_drops_staged_work);
    RUN_TEST(test_unready_font_skips_glyph_and_decoration_uploads);
    RUN_TEST(test_switching_back_to_a_material_reuses_its_pipeline);
    RUN_TEST(test_a_new_program_after_a_reset_does_not_reuse_the_old_pipeline);
    RUN_TEST(test_draw_n_matches_draw);
    RUN_TEST(test_draw_n_letter_spacing_advances_pen);
    RUN_TEST(test_draw_n_line_leading_advances_pen_y);
    RUN_TEST(test_draw_n_does_not_over_read);
    RUN_TEST(test_draw_n_large_run_flushes_at_staging_cap);
    RUN_TEST(test_rich_atom_cap_below_text_staging_cap);
    RUN_TEST(test_glyph_depth_bias_persists_across_restore);
    RUN_TEST(test_glyph_depth_bias_resets_on_reinit);
    RUN_TEST(test_oblique_leans_glyph_top);
    RUN_TEST(test_oblique_persists_across_restore);
    RUN_TEST(test_oblique_resets_on_reinit);
    RUN_TEST(test_decoration_persists_across_restore);
    RUN_TEST(test_reset_decoration_clears_all);
    RUN_TEST(test_decoration_resets_on_reinit);
    RUN_TEST(test_outline_emits_extra_span);
    RUN_TEST(test_shadow_emits_extra_span);
    RUN_TEST(test_shadow_pass_offset);
    RUN_TEST(test_passes_grouped_not_interleaved);
    RUN_TEST(test_underline_one_quad_per_segment);
    RUN_TEST(test_underline_quad_per_line);
    RUN_TEST(test_reset_decoration_fill_only);
    RUN_TEST(bench_draw_short_warm);
    RUN_TEST(bench_draw_mixed_ui);
    return UNITY_END();
}
