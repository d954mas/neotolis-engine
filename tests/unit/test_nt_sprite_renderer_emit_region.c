/* tests/unit/test_nt_sprite_renderer_emit_region.c — Plan 52-00 stub
 *
 * Covers D-52-01 / D-52-02 emit_region API + capacity guard + polygon-hull
 * vertex_count preservation. Drift 4 (set_material auto-flush) also lands here.
 * Wave 0 ships TEST_IGNORE bodies; Plan 52-01 fills with real assertions.
 */

#include <stdbool.h>
#include <stdint.h>

#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

/* D-52-01: nt_sprite_renderer_emit_region single-emit, mat4 by pointer, color packed */
static void test_emit_region_direct_call(void) { TEST_IGNORE_MESSAGE("Wave 0 stub — filled by plan 52-01"); }

/* D-52-01: capacity guard — overflow triggers flush+reopen */
static void test_emit_region_capacity_guard(void) { TEST_IGNORE_MESSAGE("Wave 0 stub — filled by plan 52-01"); }

/* D-52-04: polygon-hull regions preserve vertex_count (6 verts in == 6 verts out) */
static void test_emit_region_polygon_hull_vertex_count_preserved(void) { TEST_IGNORE_MESSAGE("Wave 0 stub — filled by plan 52-01"); }

/* Drift 4: nt_sprite_renderer_set_material auto-flushes on material change */
static void test_set_material_auto_flush_on_change(void) { TEST_IGNORE_MESSAGE("Wave 0 stub — filled by plan 52-01"); }

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_emit_region_direct_call);
    RUN_TEST(test_emit_region_capacity_guard);
    RUN_TEST(test_emit_region_polygon_hull_vertex_count_preserved);
    RUN_TEST(test_set_material_auto_flush_on_change);
    return UNITY_END();
}
