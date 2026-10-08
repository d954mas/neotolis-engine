#include "color/nt_color.h"
#include "test_helpers/nt_assert_trap.h"
#include "test_helpers/nt_gfx_fake.h"
#include "test_helpers/nt_gfx_test_desc.h"
#include "test_helpers/nt_gfx_test_frame.h"
/* NT_TEST_ACCESS defined via CMake target_compile_definitions */
#include "graphics/nt_gfx.h"
#include "graphics/nt_gfx_internal.h"
#include "renderers/nt_shape_renderer.h"
#include "unity.h"

#include <math.h>
#include <string.h>

/* Helper: float approximately equal (avoids UNITY_EXCLUDE_FLOAT issue) */
static bool float_near(float a, float b, float epsilon) { return fabsf(a - b) <= epsilon; }

void setUp(void) {
    nt_gfx_desc_t desc =
        NT_GFX_TEST_DESC(.max_shaders = 32, .max_programs = 32, .max_pipelines = 32, .max_buffers = 128, .max_textures = 32, .max_meshes = 32, .max_vertex_inputs = 32, .max_render_targets = 16);
    /* A test is one frame, and every flush of it adds to frame storage. */
    desc.frame_capacity[NT_GFX_FRAME_VERTEX] = 4U * 1024U * 1024U;
    nt_gfx_init(&desc);
    nt_gfx_begin_frame();
    nt_gfx_fake_reset();
    nt_shape_renderer_init();
    /* Enter frame/pass so flush->draw_indexed doesn't assert */
    nt_test_frame_begin_pass();
}

void tearDown(void) {
    nt_test_frame_teardown();
    nt_shape_renderer_shutdown();
    nt_gfx_shutdown();
}

/* ---- 1. Init / Shutdown ---- */

void test_shape_init_shutdown(void) {
    TEST_ASSERT_TRUE(nt_shape_renderer_test_initialized());
    nt_test_frame_close();
    nt_shape_renderer_shutdown();
    TEST_ASSERT_FALSE(nt_shape_renderer_test_initialized());
    /* Re-init for tearDown */
    nt_shape_renderer_init();
    nt_test_frame_open();
}

/* ---- 2. Flush empty is no-op ---- */

void test_shape_flush_empty(void) {
    nt_shape_renderer_flush();
    TEST_ASSERT_EQUAL_UINT32(0, nt_shape_renderer_test_vertex_count());
}

/* ---- 3. set_vp derives the stroke eye ---- */

void test_shape_set_vp_derives_eye(void) {
    /* Perspective (fov 90, near 1, far 10) looking down -z from (3, 4, 5). */
    const float perspective[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, -1.22222222F, -1, -3, -4, (-1.22222222F * -5.0F) - 2.22222222F, 5};
    nt_shape_renderer_set_vp(perspective);
    const float *eye = nt_shape_renderer_test_eye();
    TEST_ASSERT_TRUE(float_near(eye[0], 3.0F, 1e-4F));
    TEST_ASSERT_TRUE(float_near(eye[1], 4.0F, 1e-4F));
    TEST_ASSERT_TRUE(float_near(eye[2], 5.0F, 1e-4F));
    TEST_ASSERT_TRUE(float_near(eye[3], 1.0F, 1e-6F));

    /* Orthographic view down -z: no eye point, only the direction toward the viewer. */
    const float ortho[16] = {0.01F, 0, 0, 0, 0, 0.01F, 0, 0, 0, 0, -0.02F, 0, 0, 0, -0.8F, 1};
    nt_shape_renderer_set_vp(ortho);
    eye = nt_shape_renderer_test_eye();
    TEST_ASSERT_TRUE(eye[3] == 0.0F);
    TEST_ASSERT_TRUE(eye[0] == 0.0F && eye[1] == 0.0F);
    TEST_ASSERT_TRUE(eye[2] > 0.0F);
}

/* ---- 4. set_depth auto-flushes non-empty batch ---- */

void test_shape_set_depth_auto_flush(void) {
    float a[3] = {0, 0, 0};
    float b[3] = {1, 0, 0};
    uint32_t col = NT_RGBA8(255, 255, 255, 255);
    nt_shape_renderer_line(a, b, col);
    TEST_ASSERT_GREATER_THAN_UINT32(0, nt_shape_renderer_test_stroke_count());

    nt_shape_renderer_set_depth(false);
    /* After auto-flush, buffers should be reset */
    TEST_ASSERT_EQUAL_UINT32(0, nt_shape_renderer_test_stroke_count());
}

/* ---- 5. set_line_width stores value ---- */

void test_shape_set_line_width(void) {
    nt_shape_renderer_set_line_width(0.5F);
    TEST_ASSERT_TRUE(float_near(nt_shape_renderer_test_line_width(), 0.5F, 0.001F));
}

/* ---- 7. Line instance count (instanced billboard) ---- */

void test_shape_line_vertex_count(void) {
    float a[3] = {0, 0, 0};
    float b[3] = {1, 0, 0};
    uint32_t col = NT_RGBA8(255, 0, 0, 255);
    nt_shape_renderer_line(a, b, col);
    /* Lines use instanced draw: 1 line = 1 instance */
    TEST_ASSERT_EQUAL_UINT32(1, nt_shape_renderer_test_stroke_count());
    TEST_ASSERT_EQUAL_UINT32(0, nt_shape_renderer_test_vertex_count());
}

/* ---- 9. Rect fill counts (instanced) ---- */

void test_shape_rect_fill_counts(void) {
    float pos[3] = {0, 0, 0};
    float size[2] = {2, 2};
    uint32_t col = NT_RGBA8(0, 255, 0, 255);
    nt_shape_renderer_rect(pos, size, NULL, col);
    TEST_ASSERT_EQUAL_UINT32(1, nt_shape_renderer_test_instance_count(NT_SHAPE_TEST_RECT));
    TEST_ASSERT_EQUAL_UINT32(0, nt_shape_renderer_test_vertex_count());
}

/* ---- 10. Rect wire counts ---- */

void test_shape_rect_wire_counts(void) {
    float pos[3] = {0, 0, 0};
    float size[2] = {2, 2};
    uint32_t col = NT_RGBA8(0, 255, 0, 255);
    nt_shape_renderer_rect_wire(pos, size, NULL, col);
    /* 4 edges = 4 line instances */
    TEST_ASSERT_EQUAL_UINT32(4, nt_shape_renderer_test_stroke_count());
}

/* ---- 11. Triangle fill counts ---- */

void test_shape_triangle_fill_counts(void) {
    float a[3] = {0, 0, 0};
    float b[3] = {1, 0, 0};
    float c[3] = {0.5F, 1, 0};
    uint32_t col = NT_RGBA8(0, 0, 255, 255);
    nt_shape_renderer_triangle(a, b, c, col);
    TEST_ASSERT_EQUAL_UINT32(3, nt_shape_renderer_test_vertex_count());
}

/* ---- 12. Triangle wire counts ---- */

void test_shape_triangle_wire_counts(void) {
    float a[3] = {0, 0, 0};
    float b[3] = {1, 0, 0};
    float c[3] = {0.5F, 1, 0};
    uint32_t col = NT_RGBA8(0, 0, 255, 255);
    nt_shape_renderer_triangle_wire(a, b, c, col);
    /* 3 edges = 3 line instances */
    TEST_ASSERT_EQUAL_UINT32(3, nt_shape_renderer_test_stroke_count());
}

/* ---- 14. Auto-flush on overflow ---- */

void test_shape_auto_flush_on_overflow(void) {
    /* Each triangle uses 3 vertices. Fill the staging until auto-flush triggers. */
    uint32_t col = NT_RGBA8(255, 255, 255, 255);
    uint32_t max_tris = NT_SHAPE_RENDERER_MAX_VERTICES / 3;
    bool flushed = false;

    for (uint32_t i = 0; i < max_tris + 1; i++) {
        float y = (float)i * 0.01F;
        float a[3] = {0, y, 0};
        float b[3] = {1, y, 0};
        float c[3] = {0.5F, y + 1.0F, 0};
        uint32_t before = nt_shape_renderer_test_vertex_count();
        nt_shape_renderer_triangle(a, b, c, col);
        uint32_t after = nt_shape_renderer_test_vertex_count();

        if (after < before + 3) {
            /* Auto-flush happened: vertex count reset then new triangle added */
            flushed = true;
            break;
        }
    }

    TEST_ASSERT_TRUE_MESSAGE(flushed, "Auto-flush should have triggered");
    /* The triangle that triggered flush was still emitted after flushing */
    TEST_ASSERT_EQUAL_UINT32(3, nt_shape_renderer_test_vertex_count());
}

/* ---- 15. Batch accumulates shapes (mixed paths) ---- */

void test_shape_batch_accumulates(void) {
    float a[3] = {0, 0, 0};
    float b[3] = {1, 0, 0};
    float c[3] = {0.5F, 1, 0};
    float pos[3] = {0, 0, 0};
    float size[2] = {1, 1};
    uint32_t col = NT_RGBA8(255, 255, 255, 255);

    nt_shape_renderer_line(a, b, col);            /* 1 line instance */
    nt_shape_renderer_rect(pos, size, NULL, col); /* 1 rect instance */
    nt_shape_renderer_triangle(a, b, c, col);     /* 3 vertices (CPU batch) */

    TEST_ASSERT_EQUAL_UINT32(1, nt_shape_renderer_test_stroke_count());
    TEST_ASSERT_EQUAL_UINT32(1, nt_shape_renderer_test_instance_count(NT_SHAPE_TEST_RECT));
    TEST_ASSERT_EQUAL_UINT32(3, nt_shape_renderer_test_vertex_count());
}

/* ---- 16. Circle fill counts (instanced) ---- */

void test_shape_circle_fill_counts(void) {
    float center[3] = {0, 0, 0};
    uint32_t col = NT_RGBA8(255, 0, 0, 255);
    nt_shape_renderer_circle(center, 1.0F, NULL, col);
    TEST_ASSERT_EQUAL_UINT32(1, nt_shape_renderer_test_instance_count(NT_SHAPE_TEST_CIRCLE));
    TEST_ASSERT_EQUAL_UINT32(0, nt_shape_renderer_test_vertex_count());
}

/* ---- 17. Circle wire counts (32 segments) ---- */

void test_shape_circle_wire_counts(void) {
    float center[3] = {0, 0, 0};
    uint32_t col = NT_RGBA8(255, 0, 0, 255);
    nt_shape_renderer_circle_wire(center, 1.0F, NULL, col);
    nt_gfx_fake_draw_trace_reset(true);
    nt_shape_renderer_flush();
    nt_test_frame_next();
    TEST_ASSERT_EQUAL_UINT32(1, nt_gfx_fake_draw_trace_count());
    TEST_ASSERT_EQUAL_UINT32(16U * 12U, nt_gfx_fake_draw_trace_at(0).num_indices);
    TEST_ASSERT_EQUAL_UINT32(1, nt_gfx_fake_draw_trace_at(0).instance_count);
}

/* ---- 18. Cube fill counts (instanced) ---- */

void test_shape_cube_fill_counts(void) {
    float center[3] = {0, 0, 0};
    float size[3] = {1, 1, 1};
    uint32_t col = NT_RGBA8(0, 255, 0, 255);
    nt_shape_renderer_cube(center, size, NULL, col);
    TEST_ASSERT_EQUAL_UINT32(1, nt_shape_renderer_test_instance_count(NT_SHAPE_TEST_CUBE));
    TEST_ASSERT_EQUAL_UINT32(0, nt_shape_renderer_test_vertex_count());
}

/* ---- 20. Cube wire counts ---- */

void test_shape_cube_wire_counts(void) {
    float center[3] = {0, 0, 0};
    float size[3] = {1, 1, 1};
    uint32_t col = NT_RGBA8(0, 255, 0, 255);
    /* 12 edges = 12 line instances */
    nt_shape_renderer_cube_wire(center, size, NULL, col);
    TEST_ASSERT_EQUAL_UINT32(12, nt_shape_renderer_test_stroke_count());
}

/* ---- 21. Sphere fill counts (instanced) ---- */

void test_shape_sphere_fill_counts(void) {
    float center[3] = {0, 0, 0};
    uint32_t col = NT_RGBA8(0, 0, 255, 255);
    nt_shape_renderer_sphere(center, 1.0F, NULL, col);
    TEST_ASSERT_EQUAL_UINT32(1, nt_shape_renderer_test_instance_count(NT_SHAPE_TEST_SPHERE));
    TEST_ASSERT_EQUAL_UINT32(0, nt_shape_renderer_test_vertex_count());
}

/* ---- 22. Sphere wire counts (16 segments) ---- */

void test_shape_sphere_wire_counts(void) {
    float center[3] = {0, 0, 0};
    uint32_t col = NT_RGBA8(0, 0, 255, 255);
    nt_shape_renderer_sphere_wire(center, 1.0F, NULL, col);
    nt_gfx_fake_draw_trace_reset(true);
    nt_shape_renderer_flush();
    nt_test_frame_next();
    TEST_ASSERT_EQUAL_UINT32(1, nt_gfx_fake_draw_trace_count());
    TEST_ASSERT_EQUAL_UINT32(48U * 12U, nt_gfx_fake_draw_trace_at(0).num_indices);
    TEST_ASSERT_EQUAL_UINT32(1, nt_gfx_fake_draw_trace_at(0).instance_count);
}

/* ---- 22b. Sphere rot instance count ---- */

void test_shape_sphere_rot_count(void) {
    float center[3] = {0, 0, 0};
    uint32_t col = NT_RGBA8(0, 0, 255, 255);
    float rot[4] = {0, 0, 0.7071068F, 0.7071068F};
    nt_shape_renderer_sphere(center, 1.0F, rot, col);
    TEST_ASSERT_EQUAL_UINT32(1, nt_shape_renderer_test_instance_count(NT_SHAPE_TEST_SPHERE));
}

/* ---- 22c. Sphere wire rot counts (same as sphere_wire: 48 lines) ---- */

void test_shape_sphere_wire_rot_counts(void) {
    float center[3] = {0, 0, 0};
    uint32_t col = NT_RGBA8(0, 0, 255, 255);
    float rot[4] = {0, 0, 0.7071068F, 0.7071068F};
    nt_shape_renderer_sphere_wire(center, 1.0F, rot, col);
    nt_gfx_fake_draw_trace_reset(true);
    nt_shape_renderer_flush();
    nt_test_frame_next();
    TEST_ASSERT_EQUAL_UINT32(1, nt_gfx_fake_draw_trace_count());
    TEST_ASSERT_EQUAL_UINT32(48U * 12U, nt_gfx_fake_draw_trace_at(0).num_indices);
    TEST_ASSERT_EQUAL_UINT32(1, nt_gfx_fake_draw_trace_at(0).instance_count);
}

/* ---- 23. Circle rot instance count matches base ---- */

void test_shape_circle_rot_count(void) {
    float center[3] = {0, 0, 0};
    uint32_t col = NT_RGBA8(255, 0, 0, 255);
    float rot[4] = {0, 0, 0.7071068F, 0.7071068F};
    nt_shape_renderer_circle(center, 1.0F, rot, col);
    TEST_ASSERT_EQUAL_UINT32(1, nt_shape_renderer_test_instance_count(NT_SHAPE_TEST_CIRCLE));
}

/* ---- 24. Cube rot instance count matches base ---- */

void test_shape_cube_rot_count(void) {
    float center[3] = {0, 0, 0};
    float size[3] = {1, 1, 1};
    uint32_t col = NT_RGBA8(0, 255, 0, 255);
    float rot[4] = {0, 0, 0.7071068F, 0.7071068F};
    nt_shape_renderer_cube(center, size, rot, col);
    TEST_ASSERT_EQUAL_UINT32(1, nt_shape_renderer_test_instance_count(NT_SHAPE_TEST_CUBE));
}

/* ---- 25. Cylinder fill counts (instanced) ---- */

void test_shape_cylinder_fill_counts(void) {
    float center[3] = {0, 0, 0};
    uint32_t col = NT_RGBA8(255, 255, 0, 255);
    nt_shape_renderer_cylinder(center, 1.0F, 2.0F, NULL, col);
    TEST_ASSERT_EQUAL_UINT32(1, nt_shape_renderer_test_instance_count(NT_SHAPE_TEST_CYLINDER));
    TEST_ASSERT_EQUAL_UINT32(0, nt_shape_renderer_test_vertex_count());
}

/* ---- 26. Cylinder wire counts (16 segments) ---- */

void test_shape_cylinder_wire_counts(void) {
    float center[3] = {0, 0, 0};
    uint32_t col = NT_RGBA8(255, 255, 0, 255);
    nt_shape_renderer_cylinder_wire(center, 1.0F, 2.0F, NULL, col);
    nt_gfx_fake_draw_trace_reset(true);
    nt_shape_renderer_flush();
    nt_test_frame_next();
    TEST_ASSERT_EQUAL_UINT32(1, nt_gfx_fake_draw_trace_count());
    TEST_ASSERT_EQUAL_UINT32(36U * 12U, nt_gfx_fake_draw_trace_at(0).num_indices);
    TEST_ASSERT_EQUAL_UINT32(1, nt_gfx_fake_draw_trace_at(0).instance_count);
}

/* ---- 27. Capsule fill counts (32 segments) ---- */

void test_shape_capsule_fill_counts(void) {
    float center[3] = {0, 0, 0};
    uint32_t col = NT_RGBA8(0, 255, 255, 255);
    nt_shape_renderer_capsule(center, 0.5F, 2.0F, NULL, col);
    TEST_ASSERT_EQUAL_UINT32(1, nt_shape_renderer_test_instance_count(NT_SHAPE_TEST_CAPSULE));
    TEST_ASSERT_EQUAL_UINT32(0, nt_shape_renderer_test_vertex_count());
}

/* ---- 28. Capsule wire counts (16 segments) ---- */

void test_shape_capsule_wire_counts(void) {
    float center[3] = {0, 0, 0};
    uint32_t col = NT_RGBA8(0, 255, 255, 255);
    nt_shape_renderer_capsule_wire(center, 0.5F, 2.0F, NULL, col);
    nt_gfx_fake_draw_trace_reset(true);
    nt_shape_renderer_flush();
    nt_test_frame_next();
    TEST_ASSERT_EQUAL_UINT32(1, nt_gfx_fake_draw_trace_count());
    TEST_ASSERT_EQUAL_UINT32(68U * 12U, nt_gfx_fake_draw_trace_at(0).num_indices);
    TEST_ASSERT_EQUAL_UINT32(1, nt_gfx_fake_draw_trace_at(0).instance_count);
}

/* ---- 31. Cylinder rot instance count matches base ---- */

void test_shape_cylinder_rot_count(void) {
    float center[3] = {0, 0, 0};
    uint32_t col = NT_RGBA8(255, 255, 0, 255);
    float rot[4] = {0, 0, 0.7071068F, 0.7071068F};
    nt_shape_renderer_cylinder(center, 1.0F, 2.0F, rot, col);
    TEST_ASSERT_EQUAL_UINT32(1, nt_shape_renderer_test_instance_count(NT_SHAPE_TEST_CYLINDER));
}

/* ---- 32. Capsule rot instance count matches base ---- */

void test_shape_capsule_rot_count(void) {
    float center[3] = {0, 0, 0};
    uint32_t col = NT_RGBA8(0, 255, 255, 255);
    float rot[4] = {0, 0, 0.7071068F, 0.7071068F};
    nt_shape_renderer_capsule(center, 0.5F, 2.0F, rot, col);
    TEST_ASSERT_EQUAL_UINT32(1, nt_shape_renderer_test_instance_count(NT_SHAPE_TEST_CAPSULE));
}

static void assert_shape_staging_empty(void) {
    for (int type = NT_SHAPE_TEST_RECT; type <= NT_SHAPE_TEST_CAPSULE; type++) {
        TEST_ASSERT_EQUAL_UINT32(0, nt_shape_renderer_test_instance_count(type));
    }
    TEST_ASSERT_EQUAL_UINT32(0, nt_shape_renderer_test_vertex_count());
    TEST_ASSERT_EQUAL_UINT32(0, nt_shape_renderer_test_stroke_count());
}

static void emit_every_kind(void) {
    const float a[3] = {0, 0, 0};
    const float b[3] = {1, 0, 0};
    const float c[3] = {0, 1, 0};
    const float size[3] = {1, 2, 3};
    const uint32_t color = NT_RGBA8(255, 255, 255, 255);
    nt_shape_renderer_rect(a, size, NULL, color);
    nt_shape_renderer_cube(a, size, NULL, color);
    nt_shape_renderer_circle(a, 1, NULL, color);
    nt_shape_renderer_sphere(a, 1, NULL, color);
    nt_shape_renderer_cylinder(a, 1, 2, NULL, color);
    nt_shape_renderer_capsule(a, 1, 3, NULL, color);
    nt_shape_renderer_triangle(a, b, c, color);
    nt_shape_renderer_circle_wire(a, 1, NULL, color);
    nt_shape_renderer_sphere_wire(a, 1, NULL, color);
    nt_shape_renderer_cylinder_wire(a, 1, 3, NULL, color);
    nt_shape_renderer_capsule_wire(a, 1, 3, NULL, color);
    nt_shape_renderer_polyline((const float[][3]){{0, 0, 0}, {1, 0, 0}, {1, 1, 0}}, 3, false, color);
    nt_shape_renderer_line(a, b, color);
}

/* Shapes reach the GPU through frame storage only: flushes write no buffer, end_frame uploads once. */
void test_shape_flushes_write_no_buffer_until_end_frame(void) {
    const uint32_t uploads = nt_gfx_fake_update_buffer_count();
    for (int flush = 0; flush < 3; flush++) {
        emit_every_kind();
        nt_shape_renderer_flush();
        nt_shape_renderer_set_depth(flush % 2 == 0);
    }
    TEST_ASSERT_EQUAL_UINT32(uploads, nt_gfx_fake_update_buffer_count());
    nt_test_frame_next();
    TEST_ASSERT_EQUAL_UINT32(uploads + 1, nt_gfx_fake_update_buffer_count());
}

/* Batch vertices are absolute in the frame vertex buffer, after whatever other code allocated. */
void test_shape_batch_draws_from_its_frame_storage_offset(void) {
    const float a[3] = {0, 0, 0};
    const float b[3] = {1, 0, 0};
    const float c[3] = {0, 1, 0};
    const uint32_t color = NT_RGBA8(255, 255, 255, 255);
    nt_gfx_fake_draw_trace_reset(true);
    uint32_t foreign = 0;
    (void)nt_gfx_frame_alloc(NT_GFX_FRAME_VERTEX, 5, 1, &foreign);
    nt_shape_renderer_triangle(a, b, c, color);
    nt_shape_renderer_flush();
    nt_shape_renderer_triangle(a, b, c, color);
    nt_shape_renderer_triangle(a, b, c, color);
    nt_shape_renderer_flush();
    nt_test_frame_next();
    TEST_ASSERT_EQUAL_UINT32(2, nt_gfx_fake_draw_trace_count());
    TEST_ASSERT_EQUAL_UINT32(1, nt_gfx_fake_draw_trace_at(0).first_vertex); /* 5 foreign bytes round up to one 16-byte vertex */
    TEST_ASSERT_EQUAL_UINT32(3, nt_gfx_fake_draw_trace_at(0).num_vertices);
    TEST_ASSERT_EQUAL_UINT32(4, nt_gfx_fake_draw_trace_at(1).first_vertex);
    TEST_ASSERT_EQUAL_UINT32(6, nt_gfx_fake_draw_trace_at(1).num_vertices);
}

/* Filled and wire shapes draw their own ranges of the shared template buffers. */
void test_shape_templates_draw_disjoint_ranges(void) {
    nt_gfx_fake_draw_trace_reset(true);
    emit_every_kind();
    nt_shape_renderer_flush();
    nt_test_frame_next();
    /* rect, cube, circle, sphere, cylinder, capsule, batch, 4 wire templates, strokes, lines */
    TEST_ASSERT_EQUAL_UINT32(13, nt_gfx_fake_draw_trace_count());
    uint32_t fill_next = 0;
    for (uint32_t i = 0; i < 6; i++) {
        const nt_gfx_fake_draw_t draw = nt_gfx_fake_draw_trace_at(i);
        TEST_ASSERT_EQUAL_UINT32(fill_next, draw.first_index);
        fill_next += draw.num_indices;
    }
    TEST_ASSERT_EQUAL_UINT32(NT_INDEX_NONE, nt_gfx_fake_draw_trace_at(6).index_type);
    uint32_t wire_next = 0;
    for (uint32_t i = 7; i < 11; i++) {
        const nt_gfx_fake_draw_t draw = nt_gfx_fake_draw_trace_at(i);
        TEST_ASSERT_EQUAL_UINT32(wire_next, draw.first_index);
        wire_next += draw.num_indices;
    }
    TEST_ASSERT_EQUAL_UINT32((16U + 48U + 36U + 68U) * 12U, wire_next);
}

#if NT_ASSERT_MODE == NT_ASSERT_FULL
void test_shape_init_asserts_without_a_vertex_budget(void) {
    nt_gfx_frame_storage_t *storage = &g_nt_gfx_frame_storage[NT_GFX_FRAME_VERTEX];
    const uint32_t capacity = storage->capacity;
    storage->capacity = 0;
    NT_TEST_EXPECT_ASSERT(nt_shape_renderer_init());
    storage->capacity = capacity;
}
#endif

/* A second loss can land inside the restore: it latches in gfx, the queues and settings stay sane,
 * shapes draw nothing meanwhile, and the next restore draws with the old settings. */
void test_shape_loss_during_restore_is_retried_by_the_next_one(void) {
    const float vp[16] = {2, 0, 0, 0, 0, 3, 0, 0, 0, 0, 4, 0, 5, 6, 7, 1};
    nt_shape_renderer_set_vp(vp);
    nt_shape_renderer_set_line_width_pixels(3, 800, 600);
    nt_shape_renderer_set_depth(false);
    emit_every_kind();

    nt_gfx_end_pass();
    nt_gfx_end_frame();
    nt_gfx_begin_frame();
    nt_gfx_fake_lose_context_on_program_create();
    nt_shape_renderer_restore_gpu();
    TEST_ASSERT_TRUE(g_nt_gfx.context_lost);
    TEST_ASSERT_TRUE(nt_shape_renderer_test_initialized());
    assert_shape_staging_empty(); /* queued shapes named objects the loss freed */
    nt_gfx_begin_pass(&(nt_pass_desc_t){.clear_depth = 1.0F});
    nt_gfx_fake_draw_trace_reset(true);
    emit_every_kind();
    nt_shape_renderer_flush();
    assert_shape_staging_empty();

    nt_gfx_end_pass();
    nt_gfx_fake_set_context_lost(false);
    nt_gfx_end_frame();
    TEST_ASSERT_EQUAL_UINT32(0, nt_gfx_fake_draw_trace_count());
    nt_gfx_begin_frame();
    TEST_ASSERT_TRUE(g_nt_gfx.context_restored);
    nt_shape_renderer_restore_gpu();
    nt_gfx_begin_pass(&(nt_pass_desc_t){.clear_depth = 1.0F});
    TEST_ASSERT_EQUAL_MEMORY(vp, nt_shape_renderer_test_vp(), sizeof(vp));
    TEST_ASSERT_FALSE(nt_shape_renderer_test_depth_enabled());
    nt_gfx_fake_draw_trace_reset(true);
    /* The triangle batch reads the frame vertex buffer the restore made. */
    nt_shape_renderer_triangle((const float[3]){0, 0, 0}, (const float[3]){1, 0, 0}, (const float[3]){0, 1, 0}, NT_RGBA8(255, 255, 255, 255));
    nt_shape_renderer_line((const float[3]){0, 0, 0}, (const float[3]){1, 0, 0}, NT_RGBA8(255, 255, 255, 255));
    nt_shape_renderer_flush();
    nt_test_frame_next();
    TEST_ASSERT_EQUAL_UINT32(2, nt_gfx_fake_draw_trace_count());
    TEST_ASSERT_EQUAL_UINT32(3, nt_gfx_fake_draw_trace_at(0).num_vertices);
    bool found = false;
    for (uint32_t i = 0; i < nt_gfx_fake_uniform_vec4_count(); i++) {
        if (nt_gfx_fake_uniform_vec4_hash_at(i) == nt_hash32_str("u_pixel_scale").value) {
            float value[4];
            nt_gfx_fake_uniform_vec4_value_at(i, value);
            TEST_ASSERT_TRUE(float_near(value[0], 2.0F / 800.0F, 1e-7F));
            TEST_ASSERT_TRUE(float_near(value[1], 2.0F / 600.0F, 1e-7F));
            found = true;
        }
    }
    TEST_ASSERT_TRUE(found);
}

/* The restore contract is "every ACTIVE renderer". A game that calls all four
 * unconditionally must not have this one silently initialize itself and take
 * program and pipeline slots the game sized for its own materials. */
void test_shape_restore_on_inactive_renderer_does_nothing(void) {
    nt_test_frame_close();
    nt_shape_renderer_shutdown();
    const uint32_t programs = nt_gfx_fake_program_create_count();
    const uint32_t pipelines = nt_gfx_fake_pipeline_create_count();

    nt_shape_renderer_restore_gpu();

    TEST_ASSERT_FALSE(nt_shape_renderer_test_initialized());
    TEST_ASSERT_EQUAL_UINT32(programs, nt_gfx_fake_program_create_count());
    TEST_ASSERT_EQUAL_UINT32(pipelines, nt_gfx_fake_pipeline_create_count());
    nt_shape_renderer_init();
    nt_test_frame_open();
}

static void test_polyline_skips_repeated_points_and_closes_once(void) {
    const float points[][3] = {{0, 0, 0}, {0, 0, 0}, {1, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 0, 0}};
    const uint32_t color = NT_RGBA8(255, 255, 255, 255);
    nt_shape_renderer_polyline(NULL, 0, false, color);
    nt_shape_renderer_polyline(points, 1, true, color);
    TEST_ASSERT_EQUAL_UINT32(0, nt_shape_renderer_test_stroke_count());
    nt_shape_renderer_polyline(points, 6, true, color);
    TEST_ASSERT_EQUAL_UINT32(3, nt_shape_renderer_test_stroke_count());
    nt_shape_renderer_flush();
    nt_shape_renderer_polyline(points, 5, false, color);
    TEST_ASSERT_EQUAL_UINT32(2, nt_shape_renderer_test_stroke_count());
}

static void test_polyline_asserts_non_finite_points(void) {
    float points[][3] = {{0, 0, 0}, {1, NAN, 0}};
    NT_TEST_EXPECT_ASSERT(nt_shape_renderer_polyline((const float(*)[3])points, 2, false, NT_RGBA8(255, 255, 255, 255)));
    points[1][1] = 0;
    points[0][2] = INFINITY;
    NT_TEST_EXPECT_ASSERT(nt_shape_renderer_polyline((const float(*)[3])points, 2, false, NT_RGBA8(255, 255, 255, 255)));
}

static void test_width_mode_and_viewport_changes_flush_wires(void) {
    const float center[3] = {0, 0, 0};
    const uint32_t color = NT_RGBA8(255, 255, 255, 255);
    nt_gfx_fake_draw_trace_reset(true);
    nt_shape_renderer_circle_wire(center, 1, NULL, color);
    nt_shape_renderer_set_line_width_pixels(4, 640, 480);
    nt_test_frame_next();
    TEST_ASSERT_EQUAL_UINT32(1, nt_gfx_fake_draw_trace_count());
    nt_shape_renderer_circle_wire(center, 1, NULL, color);
    nt_shape_renderer_set_line_width_pixels(4, 640, 480);
    nt_test_frame_next();
    TEST_ASSERT_EQUAL_UINT32(1, nt_gfx_fake_draw_trace_count());
    nt_shape_renderer_set_line_width_pixels(4, 1280, 960);
    nt_test_frame_next();
    TEST_ASSERT_EQUAL_UINT32(2, nt_gfx_fake_draw_trace_count());
    nt_shape_renderer_circle_wire(center, 1, NULL, color);
    nt_shape_renderer_set_line_width(4);
    nt_test_frame_next();
    TEST_ASSERT_EQUAL_UINT32(3, nt_gfx_fake_draw_trace_count());
}

static void test_interleaved_wires_batch_by_kind(void) {
    const uint32_t color = NT_RGBA8(255, 255, 255, 255);
    const float points[][3] = {{0, 0, 0}, {1, 0, 0}, {1, 1, 0}};
    nt_gfx_fake_draw_trace_reset(true);
    for (int i = 0; i < 3; i++) {
        nt_shape_renderer_rect((float[3]){-1, 0, 0}, (float[2]){1, 1}, NULL, color);
        nt_shape_renderer_circle_wire(points[0], 1, NULL, color);
        nt_shape_renderer_sphere_wire(points[0], 1, NULL, color);
        nt_shape_renderer_line(points[0], points[1], color);
        nt_shape_renderer_polyline(points, 3, false, color);
    }
    nt_test_frame_next();
    TEST_ASSERT_EQUAL_UINT32(0, nt_gfx_fake_draw_trace_count());

    nt_shape_renderer_flush();
    nt_test_frame_next();
    /* Fills first, then circle and sphere templates, connected segments, independent lines. */
    static const uint32_t indices[5] = {6, 16 * 12, 48 * 12, 12, 6};
    static const uint32_t instances[5] = {3, 3, 3, 6, 3};
    TEST_ASSERT_EQUAL_UINT32(5, nt_gfx_fake_draw_trace_count());
    for (uint32_t i = 0; i < 5; i++) {
        TEST_ASSERT_EQUAL_UINT32(indices[i], nt_gfx_fake_draw_trace_at(i).num_indices);
        TEST_ASSERT_EQUAL_UINT32(instances[i], nt_gfx_fake_draw_trace_at(i).instance_count);
    }
}

static void test_width_change_with_pending_strokes_draws_fills_first(void) {
    const uint32_t color = NT_RGBA8(255, 255, 255, 255);
    nt_gfx_fake_draw_trace_reset(true);
    nt_shape_renderer_circle_wire((float[3]){0, 0, 0}, 1, NULL, color);
    nt_shape_renderer_rect((float[3]){-1, 0, 0}, (float[2]){1, 1}, NULL, color);
    nt_shape_renderer_rect((float[3]){1, 0, 0}, (float[2]){1, 1}, NULL, color);
    nt_shape_renderer_set_line_width(4);
    nt_test_frame_next();
    TEST_ASSERT_EQUAL_UINT32(2, nt_gfx_fake_draw_trace_count());
    TEST_ASSERT_EQUAL_UINT32(2, nt_gfx_fake_draw_trace_at(0).instance_count);
    TEST_ASSERT_EQUAL_UINT32(1, nt_gfx_fake_draw_trace_at(1).instance_count);
}

static void test_polyline_overflow_preserves_all_segments(void) {
    static float points[NT_SHAPE_RENDERER_MAX_POLYLINE_SEGMENTS + 3][3];
    for (uint32_t i = 0; i < NT_SHAPE_RENDERER_MAX_POLYLINE_SEGMENTS + 3; i++) {
        points[i][0] = (float)i;
        points[i][1] = (float)(i % 2);
    }
    nt_gfx_fake_draw_trace_reset(true);
    nt_shape_renderer_polyline((const float(*)[3])points, NT_SHAPE_RENDERER_MAX_POLYLINE_SEGMENTS + 3, false, NT_RGBA8(255, 255, 255, 255));
    nt_shape_renderer_flush();
    nt_test_frame_next();
    TEST_ASSERT_EQUAL_UINT32(2, nt_gfx_fake_draw_trace_count());
    TEST_ASSERT_EQUAL_UINT32(NT_SHAPE_RENDERER_MAX_POLYLINE_SEGMENTS, nt_gfx_fake_draw_trace_at(0).instance_count);
    TEST_ASSERT_EQUAL_UINT32(2, nt_gfx_fake_draw_trace_at(1).instance_count);
}

static void test_wire_instances_overflow_without_losing_shapes(void) {
    uint32_t count = NT_SHAPE_RENDERER_MAX_INSTANCES + 1;
    nt_gfx_fake_draw_trace_reset(true);
    for (uint32_t i = 0; i < count; i++) {
        nt_shape_renderer_capsule_wire((float[3]){1, 2, 3}, 0.5F, 3, NULL, NT_RGBA8(255, 255, 255, 255));
    }
    nt_shape_renderer_flush();
    nt_test_frame_next();
    uint32_t actual = 0;
    for (uint32_t i = 0; i < nt_gfx_fake_draw_trace_count(); i++) {
        actual += nt_gfx_fake_draw_trace_at(i).instance_count;
    }
    TEST_ASSERT_EQUAL_UINT32(count, actual);
    TEST_ASSERT_FALSE(nt_gfx_fake_draw_trace_overflowed());
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_polyline_overflow_preserves_all_segments);
    RUN_TEST(test_wire_instances_overflow_without_losing_shapes);
    RUN_TEST(test_polyline_skips_repeated_points_and_closes_once);
    RUN_TEST(test_polyline_asserts_non_finite_points);
    RUN_TEST(test_width_mode_and_viewport_changes_flush_wires);
    RUN_TEST(test_interleaved_wires_batch_by_kind);
    RUN_TEST(test_width_change_with_pending_strokes_draws_fills_first);
    RUN_TEST(test_shape_init_shutdown);
    RUN_TEST(test_shape_flush_empty);
    RUN_TEST(test_shape_set_vp_derives_eye);
    RUN_TEST(test_shape_set_depth_auto_flush);
    RUN_TEST(test_shape_set_line_width);
    RUN_TEST(test_shape_line_vertex_count);
    RUN_TEST(test_shape_rect_fill_counts);
    RUN_TEST(test_shape_rect_wire_counts);
    RUN_TEST(test_shape_triangle_fill_counts);
    RUN_TEST(test_shape_triangle_wire_counts);
    RUN_TEST(test_shape_auto_flush_on_overflow);
    RUN_TEST(test_shape_batch_accumulates);
    RUN_TEST(test_shape_circle_fill_counts);
    RUN_TEST(test_shape_circle_wire_counts);
    RUN_TEST(test_shape_cube_fill_counts);
    RUN_TEST(test_shape_cube_wire_counts);
    RUN_TEST(test_shape_sphere_fill_counts);
    RUN_TEST(test_shape_sphere_wire_counts);
    RUN_TEST(test_shape_sphere_rot_count);
    RUN_TEST(test_shape_sphere_wire_rot_counts);
    RUN_TEST(test_shape_circle_rot_count);
    RUN_TEST(test_shape_cube_rot_count);
    RUN_TEST(test_shape_cylinder_fill_counts);
    RUN_TEST(test_shape_cylinder_wire_counts);
    RUN_TEST(test_shape_capsule_fill_counts);
    RUN_TEST(test_shape_capsule_wire_counts);
    RUN_TEST(test_shape_cylinder_rot_count);
    RUN_TEST(test_shape_capsule_rot_count);
    RUN_TEST(test_shape_flushes_write_no_buffer_until_end_frame);
    RUN_TEST(test_shape_batch_draws_from_its_frame_storage_offset);
    RUN_TEST(test_shape_templates_draw_disjoint_ranges);
#if NT_ASSERT_MODE == NT_ASSERT_FULL
    RUN_TEST(test_shape_init_asserts_without_a_vertex_budget);
#endif
    RUN_TEST(test_shape_loss_during_restore_is_retried_by_the_next_one);
    RUN_TEST(test_shape_restore_on_inactive_renderer_does_nothing);
    return UNITY_END();
}
