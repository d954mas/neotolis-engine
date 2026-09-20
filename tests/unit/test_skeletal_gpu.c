#include "test_helpers/nt_gfx_fake.h"

#include <string.h>

#include "core/nt_assert.h"
#include "entity/nt_entity.h"
#include "graphics/nt_gfx.h"
#include "skeletal/nt_skeletal.h"
#include "skeletal_gpu/nt_skeletal_gpu.h"
#include "skin_comp/nt_skin_comp.h"
#include "test_helpers/nt_assert_trap.h"
#include "unity.h"

#define TEXEL_FLOATS 4U

static bool s_gpu_up;

void setUp(void) {
    nt_gfx_init(&(nt_gfx_desc_t){.max_shaders = 4, .max_programs = 4, .max_pipelines = 4, .max_buffers = 4, .max_textures = 8, .max_meshes = 4, .max_vertex_inputs = 4, .max_render_targets = 4});
    nt_entity_init(&(nt_entity_desc_t){.max_entities = 8});
    nt_skin_comp_init(&(nt_skin_comp_desc_t){.capacity = 4});
    s_gpu_up = false;
}

void tearDown(void) {
    nt_assert_handler = NULL;
    if (s_gpu_up) {
        nt_skeletal_gpu_shutdown();
    }
    nt_skin_comp_shutdown();
    nt_entity_shutdown();
    nt_gfx_shutdown();
    nt_gfx_fake_reset();
}

static void gpu_init(uint16_t width, uint16_t height) {
    TEST_ASSERT_EQUAL(NT_OK, nt_skeletal_gpu_init(&(nt_skeletal_gpu_desc_t){.width = width, .height = height}));
    s_gpu_up = true;
}

/* Fills B[p] with a pattern unique per (p, row, column). */
static void fill_frame(nt_skeletal_mat34_t *frame, uint16_t count, float base) {
    for (uint16_t p = 0; p < count; p++) {
        for (int r = 0; r < 3; r++) {
            for (int c = 0; c < 4; c++) {
                frame[p].r[r][c] = base + (float)((p * 12) + (r * 4) + c);
            }
        }
    }
}

static const float *staging_texel(uint16_t width, uint16_t x, uint16_t y) { return nt_skeletal_gpu_test_staging() + ((((size_t)y * width) + x) * TEXEL_FLOATS); }

/* Texel (x0 + 3p + r, y0) == row r of B[p]. */
static void assert_frame_at(uint16_t width, const nt_deformation_binding_t *b, const nt_skeletal_mat34_t *expected, uint16_t count) {
    for (uint16_t p = 0; p < count; p++) {
        for (int r = 0; r < 3; r++) {
            const float *texel = staging_texel(width, (uint16_t)(b->x0 + (3 * p) + r), b->y0);
            /* Bit-exact: the pattern values are small integers, nothing is computed. */
            TEST_ASSERT_EQUAL_MEMORY(expected[p].r[r], texel, 4 * sizeof(float));
        }
    }
}

// #region init
static void test_init_creates_one_nearest_rgba32f_texture(void) {
    gpu_init(12, 2);
    TEST_ASSERT_EQUAL_UINT32(1, nt_gfx_fake_texture_create_count());
    nt_texture_desc_t d = nt_gfx_fake_last_texture_desc();
    TEST_ASSERT_EQUAL(NT_TEXTURE_FORMAT_RGBA32F, d.format);
    TEST_ASSERT_EQUAL(NT_FILTER_NEAREST, d.min_filter);
    TEST_ASSERT_EQUAL(NT_FILTER_NEAREST, d.mag_filter);
    TEST_ASSERT_EQUAL(NT_WRAP_CLAMP_TO_EDGE, d.wrap_u);
    TEST_ASSERT_EQUAL(NT_WRAP_CLAMP_TO_EDGE, d.wrap_v);
    TEST_ASSERT_FALSE(d.gen_mipmaps);
    TEST_ASSERT_TRUE(d.level_count <= 1);
    TEST_ASSERT_EQUAL_UINT16(12, d.width);
    TEST_ASSERT_EQUAL_UINT16(2, d.height);
}

static void test_init_width_zero_is_min_2048_max_texture_size(void) {
    /* The fake reports max_texture_size 4096. */
    gpu_init(0, 1);
    TEST_ASSERT_EQUAL_UINT16(2048, nt_gfx_fake_last_texture_desc().width);
}

static void test_init_rejects_zero_height(void) {
    nt_test_assert_install();
    NT_TEST_EXPECT_ASSERT(nt_skeletal_gpu_init(&(nt_skeletal_gpu_desc_t){.width = 12, .height = 0}));
}
// #endregion

// #region reserve
static void test_reserve_packs_frames_row_major_without_spanning(void) {
    gpu_init(12, 3);
    nt_skeletal_gpu_begin_frame();
    nt_deformation_binding_t a;
    nt_deformation_binding_t b;
    nt_deformation_binding_t c;
    nt_skeletal_mat34_t *fa = nt_skeletal_gpu_reserve(2, &a);
    nt_skeletal_mat34_t *fb = nt_skeletal_gpu_reserve(2, &b);
    nt_skeletal_mat34_t *fc = nt_skeletal_gpu_reserve(1, &c);

    TEST_ASSERT_EQUAL_UINT16(0, a.x0);
    TEST_ASSERT_EQUAL_UINT16(0, a.y0);
    TEST_ASSERT_EQUAL_UINT16(6, b.x0);
    TEST_ASSERT_EQUAL_UINT16(0, b.y0);
    /* Row 0 is full: the next frame starts row 1, nothing spans. */
    TEST_ASSERT_EQUAL_UINT16(0, c.x0);
    TEST_ASSERT_EQUAL_UINT16(1, c.y0);

    /* CPU binding: one frame twice, alpha 0, the module's texture. */
    TEST_ASSERT_EQUAL_UINT16(a.x0, a.x1);
    TEST_ASSERT_EQUAL_UINT16(a.y0, a.y1);
    TEST_ASSERT_EQUAL_UINT16(c.x0, c.x1);
    TEST_ASSERT_EQUAL_UINT16(c.y0, c.y1);
    TEST_ASSERT_TRUE(a.alpha == 0.0F);
    TEST_ASSERT_NOT_EQUAL_UINT32(0, a.texture.id);
    TEST_ASSERT_EQUAL_UINT32(a.texture.id, b.texture.id);
    TEST_ASSERT_EQUAL_UINT32(a.texture.id, c.texture.id);

    /* The returned pointers address exactly the texels the origins name. */
    nt_skeletal_mat34_t ea[2];
    nt_skeletal_mat34_t eb[2];
    nt_skeletal_mat34_t ec[1];
    fill_frame(ea, 2, 100.0F);
    fill_frame(eb, 2, 200.0F);
    fill_frame(ec, 1, 300.0F);
    memcpy(fa, ea, sizeof(ea));
    memcpy(fb, eb, sizeof(eb));
    memcpy(fc, ec, sizeof(ec));
    assert_frame_at(12, &a, ea, 2);
    assert_frame_at(12, &b, eb, 2);
    assert_frame_at(12, &c, ec, 1);
}

static void test_reserve_wraps_a_frame_that_does_not_fit_the_row(void) {
    /* Width 7 holds two entries (6 texels) and one spare texel. */
    gpu_init(7, 2);
    nt_skeletal_gpu_begin_frame();
    nt_deformation_binding_t a;
    nt_deformation_binding_t b;
    (void)nt_skeletal_gpu_reserve(2, &a);
    (void)nt_skeletal_gpu_reserve(1, &b);
    TEST_ASSERT_EQUAL_UINT16(0, b.x0);
    TEST_ASSERT_EQUAL_UINT16(1, b.y0);
}

static void test_begin_frame_resets_the_cursor(void) {
    gpu_init(12, 2);
    nt_skeletal_gpu_begin_frame();
    nt_deformation_binding_t a;
    (void)nt_skeletal_gpu_reserve(3, &a);
    nt_skeletal_gpu_begin_frame();
    (void)nt_skeletal_gpu_reserve(1, &a);
    TEST_ASSERT_EQUAL_UINT16(0, a.x0);
    TEST_ASSERT_EQUAL_UINT16(0, a.y0);
}

static void test_palette_build_writes_into_the_reserved_frame(void) {
    gpu_init(12, 1);
    /* Two-entry binding over a two-joint model at identity: B[p] = inverse_bind[p]. */
    nt_skeletal_mat34_t inverse_bind[2];
    fill_frame(inverse_bind, 2, 1.0F);
    const uint16_t remap[2] = {1, 0};
    nt_skin_binding_t binding = {.remap = remap, .inverse_bind = inverse_bind, .palette_count = 2};
    nt_skeletal_mat34_t model[2];
    memset(model, 0, sizeof(model));
    model[0].r[0][0] = model[0].r[1][1] = model[0].r[2][2] = 1.0F;
    model[1] = model[0];

    nt_skeletal_gpu_begin_frame();
    nt_deformation_binding_t b;
    nt_skeletal_mat34_t *frame = nt_skeletal_gpu_reserve(2, &b);
    nt_skin_palette_build(&binding, model, 2, frame, 2);
    assert_frame_at(12, &b, inverse_bind, 2);
}
// #endregion

// #region flush
static void test_flush_uploads_full_rows_plus_one_fragment(void) {
    gpu_init(12, 3);
    nt_skeletal_gpu_begin_frame();
    nt_deformation_binding_t b;
    (void)nt_skeletal_gpu_reserve(4, &b); /* row 0 exactly */
    (void)nt_skeletal_gpu_reserve(1, &b); /* row 1, 3 texels */
    nt_skeletal_gpu_flush();
    TEST_ASSERT_EQUAL_UINT32(2, nt_gfx_fake_update_texture_count());
    nt_gfx_fake_update_texture_rect_t rows = nt_gfx_fake_update_texture_rect_at(0);
    nt_gfx_fake_update_texture_rect_t frag = nt_gfx_fake_update_texture_rect_at(1);
    TEST_ASSERT_EQUAL_UINT16(0, rows.x);
    TEST_ASSERT_EQUAL_UINT16(0, rows.y);
    TEST_ASSERT_EQUAL_UINT16(12, rows.w);
    TEST_ASSERT_EQUAL_UINT16(1, rows.h);
    TEST_ASSERT_EQUAL_UINT16(0, frag.x);
    TEST_ASSERT_EQUAL_UINT16(1, frag.y);
    TEST_ASSERT_EQUAL_UINT16(3, frag.w);
    TEST_ASSERT_EQUAL_UINT16(1, frag.h);
}

static void test_flush_exactly_filled_rows_is_one_rectangle(void) {
    gpu_init(6, 2);
    nt_skeletal_gpu_begin_frame();
    nt_deformation_binding_t b;
    (void)nt_skeletal_gpu_reserve(2, &b);
    (void)nt_skeletal_gpu_reserve(2, &b);
    nt_skeletal_gpu_flush();
    TEST_ASSERT_EQUAL_UINT32(1, nt_gfx_fake_update_texture_count());
    nt_gfx_fake_update_texture_rect_t r = nt_gfx_fake_update_texture_rect_at(0);
    TEST_ASSERT_EQUAL_UINT16(6, r.w);
    TEST_ASSERT_EQUAL_UINT16(2, r.h);
}

static void test_flush_partial_first_row_is_one_fragment(void) {
    gpu_init(12, 2);
    nt_skeletal_gpu_begin_frame();
    nt_deformation_binding_t b;
    (void)nt_skeletal_gpu_reserve(1, &b);
    nt_skeletal_gpu_flush();
    TEST_ASSERT_EQUAL_UINT32(1, nt_gfx_fake_update_texture_count());
    nt_gfx_fake_update_texture_rect_t r = nt_gfx_fake_update_texture_rect_at(0);
    TEST_ASSERT_EQUAL_UINT16(0, r.y);
    TEST_ASSERT_EQUAL_UINT16(3, r.w);
    TEST_ASSERT_EQUAL_UINT16(1, r.h);
    /* A second flush re-uploads the same bytes. */
    nt_skeletal_gpu_flush();
    TEST_ASSERT_EQUAL_UINT32(2, nt_gfx_fake_update_texture_count());
}

static void test_flush_of_an_empty_frame_uploads_nothing(void) {
    gpu_init(12, 2);
    nt_skeletal_gpu_begin_frame();
    nt_skeletal_gpu_flush();
    TEST_ASSERT_EQUAL_UINT32(0, nt_gfx_fake_update_texture_count());
}
// #endregion

// #region restore
static void test_restore_recreates_the_texture_and_new_bindings_use_it(void) {
    gpu_init(12, 2);
    nt_skeletal_gpu_begin_frame();
    nt_deformation_binding_t before;
    (void)nt_skeletal_gpu_reserve(1, &before);

    TEST_ASSERT_EQUAL(NT_OK, nt_skeletal_gpu_restore_gpu());
    TEST_ASSERT_EQUAL_UINT32(1, nt_gfx_fake_texture_destroy_count());
    TEST_ASSERT_EQUAL_UINT32(2, nt_gfx_fake_texture_create_count());
    nt_texture_desc_t d = nt_gfx_fake_last_texture_desc();
    TEST_ASSERT_EQUAL(NT_TEXTURE_FORMAT_RGBA32F, d.format);
    TEST_ASSERT_EQUAL(NT_FILTER_NEAREST, d.min_filter);
    TEST_ASSERT_FALSE(d.gen_mipmaps);

    nt_skeletal_gpu_begin_frame();
    nt_deformation_binding_t after;
    (void)nt_skeletal_gpu_reserve(1, &after);
    TEST_ASSERT_NOT_EQUAL_UINT32(0, after.texture.id);
    TEST_ASSERT_NOT_EQUAL_UINT32(before.texture.id, after.texture.id);
}

static void test_restore_when_inactive_is_ok(void) { TEST_ASSERT_EQUAL(NT_OK, nt_skeletal_gpu_restore_gpu()); }
// #endregion

// #region skin_comp
static void test_skin_comp_add_starts_from_the_zero_binding(void) {
    nt_entity_t e = nt_entity_create();
    TEST_ASSERT_TRUE(nt_skin_comp_add(e));
    TEST_ASSERT_TRUE(nt_skin_comp_has(e));
    nt_deformation_binding_t zero = {0};
    TEST_ASSERT_EQUAL_MEMORY(&zero, nt_skin_comp_handle(e), sizeof(zero));
}

static void test_skin_comp_swap_and_pop_keeps_every_remaining_value(void) {
    nt_entity_t e0 = nt_entity_create();
    nt_entity_t e1 = nt_entity_create();
    nt_entity_t e2 = nt_entity_create();
    nt_skin_comp_add(e0);
    nt_skin_comp_add(e1);
    nt_skin_comp_add(e2);
    nt_deformation_binding_t b0 = {.texture = {.id = 10}, .x0 = 1, .y0 = 2, .x1 = 3, .y1 = 4, .alpha = 0.25F};
    nt_deformation_binding_t b1 = {.texture = {.id = 11}, .x0 = 5, .y0 = 6, .x1 = 7, .y1 = 8, .alpha = 0.5F};
    nt_deformation_binding_t b2 = {.texture = {.id = 12}, .x0 = 9, .y0 = 10, .x1 = 11, .y1 = 12, .alpha = 0.75F};
    *nt_skin_comp_handle(e0) = b0;
    *nt_skin_comp_handle(e1) = b1;
    *nt_skin_comp_handle(e2) = b2;

    nt_skin_comp_remove(e0); /* the last slot moves into slot 0 */
    TEST_ASSERT_FALSE(nt_skin_comp_has(e0));
    TEST_ASSERT_EQUAL_MEMORY(&b1, nt_skin_comp_handle(e1), sizeof(b1));
    TEST_ASSERT_EQUAL_MEMORY(&b2, nt_skin_comp_handle(e2), sizeof(b2));
    TEST_ASSERT_EQUAL_UINT32(0, nt_gfx_fake_texture_destroy_count());

    /* Slot reuse starts from the zero binding again. */
    nt_entity_t e3 = nt_entity_create();
    nt_skin_comp_add(e3);
    nt_deformation_binding_t zero = {0};
    TEST_ASSERT_EQUAL_MEMORY(&zero, nt_skin_comp_handle(e3), sizeof(zero));
}

static void test_skin_comp_entity_destroy_removes_the_component(void) {
    nt_entity_t e = nt_entity_create();
    nt_skin_comp_add(e);
    nt_entity_destroy(e);
    TEST_ASSERT_FALSE(nt_skin_comp_has(e));
}

static void test_skin_comp_holds_a_reserved_binding_by_value(void) {
    gpu_init(12, 1);
    nt_entity_t e = nt_entity_create();
    nt_skin_comp_add(e);
    nt_skeletal_gpu_begin_frame();
    (void)nt_skeletal_gpu_reserve(2, nt_skin_comp_handle(e));
    TEST_ASSERT_EQUAL_UINT16(0, nt_skin_comp_handle(e)->x0);
    TEST_ASSERT_NOT_EQUAL_UINT32(0, nt_skin_comp_handle(e)->texture.id);
    nt_deformation_binding_t b;
    (void)nt_skeletal_gpu_reserve(1, &b);
    TEST_ASSERT_EQUAL_UINT16(6, b.x0);
}
// #endregion

// #region asserts (last: a longjmp out of reserve leaves the cursor mid-frame)
static void test_reserve_rejects_zero_count(void) {
    gpu_init(12, 1);
    nt_skeletal_gpu_begin_frame();
    nt_deformation_binding_t b;
    nt_test_assert_install();
    NT_TEST_EXPECT_ASSERT((void)nt_skeletal_gpu_reserve(0, &b));
}

static void test_reserve_rejects_a_frame_wider_than_the_texture(void) {
    gpu_init(12, 4);
    nt_skeletal_gpu_begin_frame();
    nt_deformation_binding_t b;
    nt_test_assert_install();
    NT_TEST_EXPECT_ASSERT((void)nt_skeletal_gpu_reserve(5, &b));
}

static void test_reserve_asserts_when_the_texture_is_full(void) {
    gpu_init(6, 1);
    nt_skeletal_gpu_begin_frame();
    nt_deformation_binding_t b;
    (void)nt_skeletal_gpu_reserve(2, &b);
    nt_test_assert_install();
    NT_TEST_EXPECT_ASSERT((void)nt_skeletal_gpu_reserve(1, &b));
}
// #endregion

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_init_creates_one_nearest_rgba32f_texture);
    RUN_TEST(test_init_width_zero_is_min_2048_max_texture_size);
    RUN_TEST(test_init_rejects_zero_height);
    RUN_TEST(test_reserve_packs_frames_row_major_without_spanning);
    RUN_TEST(test_reserve_wraps_a_frame_that_does_not_fit_the_row);
    RUN_TEST(test_begin_frame_resets_the_cursor);
    RUN_TEST(test_palette_build_writes_into_the_reserved_frame);
    RUN_TEST(test_flush_uploads_full_rows_plus_one_fragment);
    RUN_TEST(test_flush_exactly_filled_rows_is_one_rectangle);
    RUN_TEST(test_flush_partial_first_row_is_one_fragment);
    RUN_TEST(test_flush_of_an_empty_frame_uploads_nothing);
    RUN_TEST(test_restore_recreates_the_texture_and_new_bindings_use_it);
    RUN_TEST(test_restore_when_inactive_is_ok);
    RUN_TEST(test_skin_comp_add_starts_from_the_zero_binding);
    RUN_TEST(test_skin_comp_swap_and_pop_keeps_every_remaining_value);
    RUN_TEST(test_skin_comp_entity_destroy_removes_the_component);
    RUN_TEST(test_skin_comp_holds_a_reserved_binding_by_value);
    RUN_TEST(test_reserve_rejects_zero_count);
    RUN_TEST(test_reserve_rejects_a_frame_wider_than_the_texture);
    RUN_TEST(test_reserve_asserts_when_the_texture_is_full);
    return UNITY_END();
}
