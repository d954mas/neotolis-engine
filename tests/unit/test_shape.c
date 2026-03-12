/* NT_SHAPE_TEST_ACCESS defined via CMake target_compile_definitions */
#include "graphics/nt_gfx.h"
#include "shape/nt_shape.h"
#include "unity.h"

#include <math.h>

/* Helper: float approximately equal (avoids UNITY_EXCLUDE_FLOAT issue) */
static bool float_near(float a, float b, float epsilon) { return fabsf(a - b) <= epsilon; }

void setUp(void) {
    nt_gfx_init(&(nt_gfx_desc_t){.max_shaders = 32, .max_pipelines = 16, .max_buffers = 128});
    nt_shape_init();
    /* Enter frame/pass so flush->draw_indexed doesn't assert */
    nt_gfx_begin_frame();
    nt_gfx_begin_pass(&(nt_pass_desc_t){.clear_depth = 1.0F});
}

void tearDown(void) {
    nt_gfx_end_pass();
    nt_gfx_end_frame();
    nt_shape_shutdown();
    nt_gfx_shutdown();
}

/* ---- 1. Init / Shutdown ---- */

void test_shape_init_shutdown(void) {
    TEST_ASSERT_TRUE(nt_shape_test_initialized());
    nt_shape_shutdown();
    TEST_ASSERT_FALSE(nt_shape_test_initialized());
    /* Re-init for tearDown */
    nt_shape_init();
}

/* ---- 2. Flush empty is no-op ---- */

void test_shape_flush_empty(void) {
    nt_shape_flush();
    TEST_ASSERT_EQUAL_UINT32(0, nt_shape_test_vertex_count());
    TEST_ASSERT_EQUAL_UINT32(0, nt_shape_test_index_count());
}

/* ---- 3. set_vp extracts camera position ---- */

void test_shape_set_vp_extracts_cam_pos(void) {
    /* Identity VP -> camera at origin */
    float identity[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    nt_shape_set_vp(identity);
    const float *cam = nt_shape_test_cam_pos();
    TEST_ASSERT_TRUE(float_near(cam[0], 0.0F, 0.001F));
    TEST_ASSERT_TRUE(float_near(cam[1], 0.0F, 0.001F));
    TEST_ASSERT_TRUE(float_near(cam[2], 0.0F, 0.001F));

    /* Translation matrix (column-major): T = I with col3=(5,3,-2,1).
       inv(T) has col3=(-5,-3,2,1). Camera pos = inv(VP) col3. */
    float vp_translated[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 5, 3, -2, 1};
    nt_shape_set_vp(vp_translated);
    cam = nt_shape_test_cam_pos();
    TEST_ASSERT_TRUE(float_near(cam[0], -5.0F, 0.001F));
    TEST_ASSERT_TRUE(float_near(cam[1], -3.0F, 0.001F));
    TEST_ASSERT_TRUE(float_near(cam[2], 2.0F, 0.001F));
}

/* ---- 4. set_depth auto-flushes non-empty batch ---- */

void test_shape_set_depth_auto_flush(void) {
    float a[3] = {0, 0, 0};
    float b[3] = {1, 0, 0};
    float col[4] = {1, 1, 1, 1};
    nt_shape_line(a, b, col);
    TEST_ASSERT_GREATER_THAN_UINT32(0, nt_shape_test_vertex_count());

    nt_shape_set_depth(false);
    /* After auto-flush, buffers should be reset */
    TEST_ASSERT_EQUAL_UINT32(0, nt_shape_test_vertex_count());
}

/* ---- 5. set_blend auto-flushes non-empty batch ---- */

void test_shape_set_blend_auto_flush(void) {
    float a[3] = {0, 0, 0};
    float b[3] = {1, 0, 0};
    float col[4] = {1, 1, 1, 1};
    nt_shape_line(a, b, col);
    TEST_ASSERT_GREATER_THAN_UINT32(0, nt_shape_test_vertex_count());

    nt_shape_set_blend(true);
    TEST_ASSERT_EQUAL_UINT32(0, nt_shape_test_vertex_count());
}

/* ---- 6. set_line_width stores value ---- */

void test_shape_set_line_width(void) {
    nt_shape_set_line_width(0.5F);
    TEST_ASSERT_TRUE(float_near(nt_shape_test_line_width(), 0.5F, 0.001F));
}

/* ---- 7. set_segments default and custom ---- */

void test_shape_set_segments_default(void) {
    nt_shape_set_segments(0);
    TEST_ASSERT_EQUAL_INT(32, nt_shape_test_segments());

    nt_shape_set_segments(16);
    TEST_ASSERT_EQUAL_INT(16, nt_shape_test_segments());

    nt_shape_set_segments(-5);
    TEST_ASSERT_EQUAL_INT(32, nt_shape_test_segments());
}

/* ---- 8. Line vertex/index counts ---- */

void test_shape_line_vertex_count(void) {
    float a[3] = {0, 0, 0};
    float b[3] = {1, 0, 0};
    float col[4] = {1, 0, 0, 1};
    nt_shape_line(a, b, col);
    TEST_ASSERT_EQUAL_UINT32(4, nt_shape_test_vertex_count());
    TEST_ASSERT_EQUAL_UINT32(6, nt_shape_test_index_count());
}

/* ---- 9. Rect fill counts ---- */

void test_shape_rect_fill_counts(void) {
    float pos[3] = {0, 0, 0};
    float size[2] = {2, 2};
    float col[4] = {0, 1, 0, 1};
    nt_shape_rect(pos, size, col);
    TEST_ASSERT_EQUAL_UINT32(4, nt_shape_test_vertex_count());
    TEST_ASSERT_EQUAL_UINT32(6, nt_shape_test_index_count());
}

/* ---- 10. Rect wire counts ---- */

void test_shape_rect_wire_counts(void) {
    float pos[3] = {0, 0, 0};
    float size[2] = {2, 2};
    float col[4] = {0, 1, 0, 1};
    nt_shape_rect_wire(pos, size, col);
    /* 4 edges x 4 verts = 16, 4 edges x 6 indices = 24 */
    TEST_ASSERT_EQUAL_UINT32(16, nt_shape_test_vertex_count());
    TEST_ASSERT_EQUAL_UINT32(24, nt_shape_test_index_count());
}

/* ---- 11. Triangle fill counts ---- */

void test_shape_triangle_fill_counts(void) {
    float a[3] = {0, 0, 0};
    float b[3] = {1, 0, 0};
    float c[3] = {0.5F, 1, 0};
    float col[4] = {0, 0, 1, 1};
    nt_shape_triangle(a, b, c, col);
    TEST_ASSERT_EQUAL_UINT32(3, nt_shape_test_vertex_count());
    TEST_ASSERT_EQUAL_UINT32(3, nt_shape_test_index_count());
}

/* ---- 12. Triangle wire counts ---- */

void test_shape_triangle_wire_counts(void) {
    float a[3] = {0, 0, 0};
    float b[3] = {1, 0, 0};
    float c[3] = {0.5F, 1, 0};
    float col[4] = {0, 0, 1, 1};
    nt_shape_triangle_wire(a, b, c, col);
    /* 3 edges x 4 verts = 12, 3 edges x 6 indices = 18 */
    TEST_ASSERT_EQUAL_UINT32(12, nt_shape_test_vertex_count());
    TEST_ASSERT_EQUAL_UINT32(18, nt_shape_test_index_count());
}

/* ---- 13. Triangle per-vertex color ---- */

void test_shape_triangle_col_per_vertex(void) {
    float a[3] = {0, 0, 0};
    float b[3] = {1, 0, 0};
    float c[3] = {0.5F, 1, 0};
    float ca[4] = {1, 0, 0, 1};
    float cb[4] = {0, 1, 0, 1};
    float cc[4] = {0, 0, 1, 1};
    nt_shape_triangle_col(a, b, c, ca, cb, cc);
    /* Same geometry as triangle fill: 3 verts, 3 indices */
    TEST_ASSERT_EQUAL_UINT32(3, nt_shape_test_vertex_count());
    TEST_ASSERT_EQUAL_UINT32(3, nt_shape_test_index_count());
}

/* ---- 14. Auto-flush on overflow ---- */

void test_shape_auto_flush_on_overflow(void) {
    /* Each triangle uses 3 verts + 3 indices.
       Fill buffer until auto-flush triggers. */
    float col[4] = {1, 1, 1, 1};
    uint32_t max_tris = NT_SHAPE_MAX_VERTICES / 3;
    bool flushed = false;

    for (uint32_t i = 0; i < max_tris + 1; i++) {
        float y = (float)i * 0.01F;
        float a[3] = {0, y, 0};
        float b[3] = {1, y, 0};
        float c[3] = {0.5F, y + 1.0F, 0};
        uint32_t before = nt_shape_test_vertex_count();
        nt_shape_triangle(a, b, c, col);
        uint32_t after = nt_shape_test_vertex_count();

        if (after < before + 3) {
            /* Auto-flush happened: vertex count reset then new triangle added */
            flushed = true;
            break;
        }
    }

    TEST_ASSERT_TRUE_MESSAGE(flushed, "Auto-flush should have triggered");
    /* The triangle that triggered flush was still emitted after flushing */
    TEST_ASSERT_EQUAL_UINT32(3, nt_shape_test_vertex_count());
}

/* ---- 15. Batch accumulates shapes ---- */

void test_shape_batch_accumulates(void) {
    float a[3] = {0, 0, 0};
    float b[3] = {1, 0, 0};
    float c[3] = {0.5F, 1, 0};
    float pos[3] = {0, 0, 0};
    float size[2] = {1, 1};
    float col[4] = {1, 1, 1, 1};

    nt_shape_line(a, b, col);        /* 4v + 6i */
    nt_shape_rect(pos, size, col);   /* 4v + 6i */
    nt_shape_triangle(a, b, c, col); /* 3v + 3i */

    TEST_ASSERT_EQUAL_UINT32(4 + 4 + 3, nt_shape_test_vertex_count());
    TEST_ASSERT_EQUAL_UINT32(6 + 6 + 3, nt_shape_test_index_count());
}

/* ---- 16. Circle fill counts (32 segments) ---- */

void test_shape_circle_fill_counts(void) {
    float center[3] = {0, 0, 0};
    float col[4] = {1, 0, 0, 1};
    /* Default segments = 32: center + 32 ring = 33 verts, 32*3 = 96 indices */
    nt_shape_circle(center, 1.0F, col);
    TEST_ASSERT_EQUAL_UINT32(33, nt_shape_test_vertex_count());
    TEST_ASSERT_EQUAL_UINT32(96, nt_shape_test_index_count());
}

/* ---- 17. Circle wire counts (32 segments) ---- */

void test_shape_circle_wire_counts(void) {
    float center[3] = {0, 0, 0};
    float col[4] = {1, 0, 0, 1};
    /* 32 edges x 4 verts = 128, 32 edges x 6 indices = 192 */
    nt_shape_circle_wire(center, 1.0F, col);
    TEST_ASSERT_EQUAL_UINT32(128, nt_shape_test_vertex_count());
    TEST_ASSERT_EQUAL_UINT32(192, nt_shape_test_index_count());
}

/* ---- 18. Circle with set_segments(8) ---- */

void test_shape_circle_fill_custom_segments(void) {
    float center[3] = {0, 0, 0};
    float col[4] = {1, 0, 0, 1};
    nt_shape_set_segments(8);
    /* 8 segments: center + 8 ring = 9 verts, 8*3 = 24 indices */
    nt_shape_circle(center, 1.0F, col);
    TEST_ASSERT_EQUAL_UINT32(9, nt_shape_test_vertex_count());
    TEST_ASSERT_EQUAL_UINT32(24, nt_shape_test_index_count());
}

/* ---- 19. Cube fill counts ---- */

void test_shape_cube_fill_counts(void) {
    float center[3] = {0, 0, 0};
    float size[3] = {1, 1, 1};
    float col[4] = {0, 1, 0, 1};
    /* 8 shared verts, 36 indices (6 faces x 2 triangles x 3 indices) */
    nt_shape_cube(center, size, col);
    TEST_ASSERT_EQUAL_UINT32(8, nt_shape_test_vertex_count());
    TEST_ASSERT_EQUAL_UINT32(36, nt_shape_test_index_count());
}

/* ---- 20. Cube wire counts ---- */

void test_shape_cube_wire_counts(void) {
    float center[3] = {0, 0, 0};
    float size[3] = {1, 1, 1};
    float col[4] = {0, 1, 0, 1};
    /* 12 edges x 4 verts = 48, 12 edges x 6 indices = 72 */
    nt_shape_cube_wire(center, size, col);
    TEST_ASSERT_EQUAL_UINT32(48, nt_shape_test_vertex_count());
    TEST_ASSERT_EQUAL_UINT32(72, nt_shape_test_index_count());
}

/* ---- 21. Sphere fill counts (32 segments) ---- */

void test_shape_sphere_fill_counts(void) {
    float center[3] = {0, 0, 0};
    float col[4] = {0, 0, 1, 1};
    /* 32 segments, 16 rings: (16+1)*(32+1) = 561 verts, 16*32*6 = 3072 indices */
    nt_shape_sphere(center, 1.0F, col);
    TEST_ASSERT_EQUAL_UINT32(561, nt_shape_test_vertex_count());
    TEST_ASSERT_EQUAL_UINT32(3072, nt_shape_test_index_count());
}

/* ---- 22. Sphere wire counts (32 segments) ---- */

void test_shape_sphere_wire_counts(void) {
    float center[3] = {0, 0, 0};
    float col[4] = {0, 0, 1, 1};
    /* Longitude: 32 lines x 16 edges each = 512 edges
       Latitude: 15 rings x 32 edges each = 480 edges
       Total: 992 edges x 4 verts = 3968, 992 edges x 6 indices = 5952 */
    nt_shape_sphere_wire(center, 1.0F, col);
    TEST_ASSERT_EQUAL_UINT32(3968, nt_shape_test_vertex_count());
    TEST_ASSERT_EQUAL_UINT32(5952, nt_shape_test_index_count());
}

/* ---- 23. Circle rot vertex count matches base ---- */

void test_shape_circle_rot_count(void) {
    float center[3] = {0, 0, 0};
    float col[4] = {1, 0, 0, 1};
    /* 90-degree rotation around Z axis: q = (0, 0, sin(45), cos(45)) */
    float rot[4] = {0, 0, 0.7071068F, 0.7071068F};
    nt_shape_circle_rot(center, 1.0F, rot, col);
    /* Same vertex count as base circle: 33 verts, 96 indices */
    TEST_ASSERT_EQUAL_UINT32(33, nt_shape_test_vertex_count());
    TEST_ASSERT_EQUAL_UINT32(96, nt_shape_test_index_count());
}

/* ---- 24. Cube rot vertex count matches base ---- */

void test_shape_cube_rot_count(void) {
    float center[3] = {0, 0, 0};
    float size[3] = {1, 1, 1};
    float col[4] = {0, 1, 0, 1};
    float rot[4] = {0, 0, 0.7071068F, 0.7071068F};
    nt_shape_cube_rot(center, size, rot, col);
    /* Same vertex count as base cube: 8 verts, 36 indices */
    TEST_ASSERT_EQUAL_UINT32(8, nt_shape_test_vertex_count());
    TEST_ASSERT_EQUAL_UINT32(36, nt_shape_test_index_count());
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_shape_init_shutdown);
    RUN_TEST(test_shape_flush_empty);
    RUN_TEST(test_shape_set_vp_extracts_cam_pos);
    RUN_TEST(test_shape_set_depth_auto_flush);
    RUN_TEST(test_shape_set_blend_auto_flush);
    RUN_TEST(test_shape_set_line_width);
    RUN_TEST(test_shape_set_segments_default);
    RUN_TEST(test_shape_line_vertex_count);
    RUN_TEST(test_shape_rect_fill_counts);
    RUN_TEST(test_shape_rect_wire_counts);
    RUN_TEST(test_shape_triangle_fill_counts);
    RUN_TEST(test_shape_triangle_wire_counts);
    RUN_TEST(test_shape_triangle_col_per_vertex);
    RUN_TEST(test_shape_auto_flush_on_overflow);
    RUN_TEST(test_shape_batch_accumulates);
    RUN_TEST(test_shape_circle_fill_counts);
    RUN_TEST(test_shape_circle_wire_counts);
    RUN_TEST(test_shape_circle_fill_custom_segments);
    RUN_TEST(test_shape_cube_fill_counts);
    RUN_TEST(test_shape_cube_wire_counts);
    RUN_TEST(test_shape_sphere_fill_counts);
    RUN_TEST(test_shape_sphere_wire_counts);
    RUN_TEST(test_shape_circle_rot_count);
    RUN_TEST(test_shape_cube_rot_count);
    return UNITY_END();
}
