#ifndef NT_BUILDER_ATLAS_TEST_H
#define NT_BUILDER_ATLAS_TEST_H

#include "nt_builder_atlas_geometry.h"

#include <stdbool.h>
#include <stdint.h>

// #region test_access
#ifdef NT_TEST_ACCESS
typedef struct {
    uint32_t slot_mask;
    uint32_t selected_count;
    uint32_t selected_index_count;
    uint64_t opaque_area2;
    uint64_t base_area2;
    uint64_t selected_area2;
    uint64_t slot_area2[NT_POLYGON_MAX_VERTICES + 1];
    Point2D selected_poly[NT_POLYGON_MAX_VERTICES];
    uint16_t selected_indices[NT_POLYGON_MAX_TRIANGLE_INDICES];
    double base_overdraw_percent;
    double added_area_percent;
    double total_overdraw_percent;
} NtAtlasFrontierTestResult;

bool nt_atlas_test_frontier_evaluate(const Point2D *const *polygons, const uint32_t *counts, uint32_t candidate_count, const uint8_t *binary, uint32_t width, uint32_t height, uint32_t max_vertices,
                                     float max_added_area_percent, NtAtlasFrontierTestResult *out_result);
uint32_t nt_atlas_test_frontier_select_areas(const uint64_t *slot_area2, uint32_t slot_mask, uint64_t opaque_area2, uint32_t max_vertices, float max_added_area_percent);
void nt_atlas_test_frontier_selection_proof_mismatch(void);
uint32_t nt_atlas_test_concave_frontier_slot_mask(const uint8_t *binary, uint32_t width, uint32_t height, uint32_t max_vertices);
bool nt_atlas_test_merge_disjoint_components(uint8_t *binary, uint32_t width, uint32_t height, uint32_t *out_pass_count);
uint32_t nt_atlas_test_collect_transform_values(uint8_t mask, uint8_t out_values[8]);

/* Force the first alias of every later build to this relative transform; 0xFF disables.
 * Negative control for the per-alias coverage proof. */
void nt_atlas_test_force_alias_rel(uint8_t rel);

/* Directed Hausdorff distance from the full reference boundary to the
 * candidate boundary. Both polygons are closed implicitly. */
double polygon_max_boundary_distance(const Point2D *reference, uint32_t reference_count, const Point2D *candidate, uint32_t candidate_count);
#endif
// #endregion

#endif
