#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#include "hull_visual_report.h"
#include "nt_builder_atlas_geometry.h"
#include "unity.h"

#define CORPUS_PATH "tests/fixtures/hull_visual_acceptance/corpus.json"
#define FRONTIER_PATH "tools/research/atlas_bench/hull_area_frontier.json"
#define REPORT_A "build/reports/test-hull-visual-a"
#define REPORT_B "build/reports/test-hull-visual-b"

static const char *REQUIRED_ROWS[] = {
    "sq9-aa-triangle:convex",           "rotated-diamond:convex", "concave-notch:concave", "transparent-donut:concave", "opaque-square-max3:convex", "connected-mask-adversarial:concave",
    "pixel-art-threshold-control:rect",
};
static const char *REAL_ART_ROWS[] = {
    "real-rhombus-outline:concave", "real-resource-wood:concave", "real-card-down-outline:concave", "real-d12-outline:concave", "real-flask-empty:concave", "real-tile-sparse:concave",
};
static const char *COLUMNS[] = {"percent-0", "percent-2", "percent-5", "percent-10", "percent-15", "percent-25"};
static const uint32_t COLUMN_VALUES[] = {0, 2, 5, 10, 15, 25};

void setUp(void) {}
void tearDown(void) {}

static uint8_t *read_bytes(const char *path, size_t *out_size) {
    FILE *file = fopen(path, "rb");
    TEST_ASSERT_NOT_NULL_MESSAGE(file, path);
    TEST_ASSERT_EQUAL_INT(0, fseek(file, 0, SEEK_END));
    long length = ftell(file);
    TEST_ASSERT_GREATER_OR_EQUAL_INT(0, length);
    TEST_ASSERT_EQUAL_INT(0, fseek(file, 0, SEEK_SET));
    const size_t allocation_size = length > 0 ? (size_t)length + 1U : 1U;
    uint8_t *bytes = (uint8_t *)malloc(allocation_size);
    TEST_ASSERT_NOT_NULL(bytes);
    TEST_ASSERT_EQUAL_UINT64((uint64_t)length, fread(bytes, 1, (size_t)length, file));
    TEST_ASSERT_EQUAL_INT(0, fclose(file));
    bytes[length] = 0;
    *out_size = (size_t)length;
    return bytes;
}

static void write_bytes(const char *path, const uint8_t *bytes, size_t size) {
    FILE *file = fopen(path, "wb");
    TEST_ASSERT_NOT_NULL_MESSAGE(file, path);
    TEST_ASSERT_EQUAL_UINT64(size, fwrite(bytes, 1, size, file));
    TEST_ASSERT_EQUAL_INT(0, fclose(file));
}

static uint32_t count_text(const char *text, const char *needle) {
    uint32_t count = 0;
    const size_t length = strlen(needle);
    for (const char *cursor = text; (cursor = strstr(cursor, needle)) != NULL; cursor += length) {
        count++;
    }
    return count;
}

static void assert_same_file(const char *left_path, const char *right_path) {
    size_t left_size = 0;
    size_t right_size = 0;
    uint8_t *left = read_bytes(left_path, &left_size);
    uint8_t *right = read_bytes(right_path, &right_size);
    TEST_ASSERT_EQUAL_UINT64(left_size, right_size);
    TEST_ASSERT_EQUAL_MEMORY(left, right, left_size);
    free(left);
    free(right);
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void assert_manifest_schema_and_html(void) {
    size_t manifest_size = 0;
    size_t html_size = 0;
    uint8_t *manifest_bytes = read_bytes(REPORT_A "/manifest.json", &manifest_size);
    uint8_t *html_bytes = read_bytes(REPORT_A "/index.html", &html_size);
    const char *manifest = (const char *)manifest_bytes;
    const char *html = (const char *)html_bytes;
    TEST_ASSERT_NOT_NULL(strstr(manifest, "\"schema_version\": 2"));
    TEST_ASSERT_NOT_NULL(strstr(manifest, "\"overall_pass\": true"));
    TEST_ASSERT_NOT_NULL(strstr(manifest, "\"failing_panel_ids\": []"));
    const uint32_t row_count = (uint32_t)(sizeof(REQUIRED_ROWS) / sizeof(REQUIRED_ROWS[0]));
    const uint32_t real_art_row_count = (uint32_t)(sizeof(REAL_ART_ROWS) / sizeof(REAL_ART_ROWS[0]));
    const uint32_t column_count = (uint32_t)(sizeof(COLUMNS) / sizeof(COLUMNS[0]));
    const uint32_t panel_count = (row_count + real_art_row_count) * column_count;
    TEST_ASSERT_EQUAL_UINT32(panel_count, count_text(manifest, "\"panel_id\""));
    const char *previous = manifest;
    for (uint32_t column = 0; column < column_count; column++) {
        char column_id[96];
        char percent[96];
        (void)snprintf(column_id, sizeof(column_id), "\"column_id\":\"%s\"", COLUMNS[column]);
        (void)snprintf(percent, sizeof(percent), "\"max_added_area_percent\":%u.000", COLUMN_VALUES[column]);
        const char *position = strstr(previous, column_id);
        TEST_ASSERT_NOT_NULL_MESSAGE(position, column_id);
        TEST_ASSERT_NOT_NULL_MESSAGE(strstr(position, percent), percent);
        previous = position + strlen(column_id);
    }
    TEST_ASSERT_EQUAL_UINT32(column_count, count_text(manifest, "\"sweep_sha256\""));
    TEST_ASSERT_EQUAL_UINT32(column_count, count_text(manifest, "\"sweep_source\""));
    TEST_ASSERT_EQUAL_UINT32(column_count, count_text(manifest, "\"measurement_source_commit\""));

    static const char *fields[] = {
        "\"effective_alpha_mask\"",
        "\"original_alpha_values\"",
        "\"baseline_polygon\"",
        "\"selected_polygon\"",
        "\"baseline_triangles\"",
        "\"selected_triangles\"",
        "\"numbered_vertices\"",
        "\"selected_vertex_count\"",
        "\"opaque_area2\"",
        "\"base_area2\"",
        "\"selected_area2\"",
        "\"added_area2\"",
        "\"exact_lost_area2\"",
        "\"base_overdraw_percent\"",
        "\"added_area_percent\"",
        "\"total_overdraw_percent\"",
        "\"baseline_pack_sha256\"",
        "\"selected_pack_sha256\"",
        "\"max_vertices\"",
        "\"production_gates\"",
        "\"inputs_valid\"",
        "\"opaque_area_valid\"",
        "\"base_bounds_valid\"",
        "\"base_topology_valid\"",
        "\"base_coverage_valid\"",
        "\"base_triangulation_valid\"",
        "\"selected_bounds_valid\"",
        "\"selected_topology_valid\"",
        "\"selected_coverage_valid\"",
        "\"selected_triangulation_valid\"",
        "\"metric_order_valid\"",
        "\"allowance_valid\"",
        "\"ceiling_valid\"",
        "\"result\"",
    };
    for (uint32_t i = 0; i < (uint32_t)(sizeof(fields) / sizeof(fields[0])); i++) {
        TEST_ASSERT_GREATER_OR_EQUAL_UINT32_MESSAGE(panel_count, count_text(manifest, fields[i]), fields[i]);
    }
    TEST_ASSERT_EQUAL_UINT32(panel_count + row_count + real_art_row_count, count_text(manifest, "\"result\":\"PASS\""));
    TEST_ASSERT_NULL(strstr(manifest, "\"result\":\"FAIL\""));
    TEST_ASSERT_NULL(strstr(html, "<script"));
    TEST_ASSERT_NULL(strstr(html, "http://"));
    TEST_ASSERT_NULL(strstr(html, "https://"));
    TEST_ASSERT_NOT_NULL(strstr(html, "Selected vertices / hard ceiling"));
    TEST_ASSERT_NOT_NULL(strstr(html, "Aopaque / Abase / Aselected"));
    TEST_ASSERT_NOT_NULL(strstr(html, "Base / added / total overdraw"));
    TEST_ASSERT_NOT_NULL(strstr(html, "Full retained-cell coverage"));
    TEST_ASSERT_NOT_NULL(strstr(html, "Exact lost area"));
    TEST_ASSERT_NOT_NULL(strstr(manifest, "\"connected_frontier_counts\":[6,7,8]"));
    TEST_ASSERT_NOT_NULL(strstr(manifest, "\"real_art_rows\""));
    TEST_ASSERT_EQUAL_UINT32(real_art_row_count * column_count, count_text(manifest, "\"real_art_sample\":true"));
    TEST_ASSERT_NOT_NULL(strstr(html, "Real art"));
    TEST_ASSERT_EQUAL_UINT32(3, count_text(manifest, "\"connected_frontier_vertex_count\""));
    TEST_ASSERT_NOT_NULL(strstr(manifest, "\"connected_frontier_vertex_count\":6"));
    TEST_ASSERT_NOT_NULL(strstr(manifest, "\"connected_frontier_vertex_count\":7"));
    TEST_ASSERT_NOT_NULL(strstr(manifest, "\"connected_frontier_vertex_count\":8"));
    const char *donut = strstr(manifest, "\"panel_id\":\"transparent-donut:concave:percent-0\"");
    TEST_ASSERT_NOT_NULL(donut);
    const char *donut_end = strstr(donut, "\"result\":\"PASS\"");
    TEST_ASSERT_NOT_NULL(donut_end);
    static const char *donut_identities[] = {
        "\"opaque_area2\":352",
        "\"base_area2\":448",
        "\"selected_area2\":448",
        "\"added_area2\":0",
        "\"exact_lost_area2\":0",
        "\"base_overdraw_percent\":27.27272727",
        "\"added_area_percent\":0.00000000",
        "\"total_overdraw_percent\":27.27272727",
    };
    for (uint32_t i = 0; i < (uint32_t)(sizeof(donut_identities) / sizeof(donut_identities[0])); i++) {
        const char *identity = strstr(donut, donut_identities[i]);
        TEST_ASSERT_NOT_NULL_MESSAGE(identity, donut_identities[i]);
        TEST_ASSERT_TRUE_MESSAGE(identity < donut_end, donut_identities[i]);
    }
    const char *card_10 = strstr(manifest, "\"panel_id\":\"real-card-down-outline:concave:percent-10\"");
    TEST_ASSERT_NOT_NULL(card_10);
    const char *card_10_end = strstr(card_10, "\"result\":\"PASS\"");
    TEST_ASSERT_NOT_NULL(card_10_end);
    const char *card_base = strstr(card_10, "\"baseline_vertex_count\":8");
    const char *card_selected = strstr(card_10, "\"selected_vertex_count\":6");
    TEST_ASSERT_NOT_NULL(card_base);
    TEST_ASSERT_NOT_NULL(card_selected);
    TEST_ASSERT_TRUE(card_base < card_10_end);
    TEST_ASSERT_TRUE(card_selected < card_10_end);
    const char *d12_10 = strstr(manifest, "\"panel_id\":\"real-d12-outline:concave:percent-10\"");
    TEST_ASSERT_NOT_NULL(d12_10);
    const char *d12_10_end = strstr(d12_10, "\"result\":\"PASS\"");
    TEST_ASSERT_NOT_NULL(d12_10_end);
    const char *d12_base = strstr(d12_10, "\"baseline_vertex_count\":8");
    const char *d12_selected = strstr(d12_10, "\"selected_vertex_count\":7");
    TEST_ASSERT_NOT_NULL(d12_base);
    TEST_ASSERT_NOT_NULL(d12_selected);
    TEST_ASSERT_TRUE(d12_base < d12_10_end);
    TEST_ASSERT_TRUE(d12_selected < d12_10_end);
    TEST_ASSERT_NOT_NULL(strstr(manifest, "\"error_kind\":\"ATLAS_HULL_INFEASIBLE\""));
    TEST_ASSERT_NOT_NULL(strstr(manifest, "\"error_atlas\":\"visual\""));
    TEST_ASSERT_NOT_NULL(strstr(manifest, "\"error_sprite\":\"fixture\""));
    TEST_ASSERT_NOT_NULL(strstr(manifest, "\"error_invariant\":\"no covering polygon within hard vertex ceiling\""));
    TEST_ASSERT_NULL(strstr(manifest, "fidelity_px"));
    TEST_ASSERT_NULL(strstr(manifest, "lost_pixels"));
    TEST_ASSERT_NULL(strstr(html, "Fidelity"));
    TEST_ASSERT_NULL(strstr(html, "2 px"));

    for (uint32_t row = 0; row < (uint32_t)(sizeof(REQUIRED_ROWS) / sizeof(REQUIRED_ROWS[0])); row++) {
        char row_id[256];
        (void)snprintf(row_id, sizeof(row_id), "\"row_id\":\"%s\"", REQUIRED_ROWS[row]);
        TEST_ASSERT_NOT_NULL_MESSAGE(strstr(manifest, row_id), REQUIRED_ROWS[row]);
        char html_row[256];
        (void)snprintf(html_row, sizeof(html_row), "id=\"row-%s\"", REQUIRED_ROWS[row]);
        for (char *cursor = html_row; *cursor != '\0'; cursor++) {
            if (*cursor == ':') {
                *cursor = '-';
            }
        }
        TEST_ASSERT_NOT_NULL_MESSAGE(strstr(html, html_row), html_row);
        for (uint32_t column = 0; column < column_count; column++) {
            char panel_id[320];
            (void)snprintf(panel_id, sizeof(panel_id), "\"panel_id\":\"%s:%s\"", REQUIRED_ROWS[row], COLUMNS[column]);
            TEST_ASSERT_NOT_NULL_MESSAGE(strstr(manifest, panel_id), panel_id);
        }
    }
    for (uint32_t row = 0; row < real_art_row_count; row++) {
        char row_id[256];
        (void)snprintf(row_id, sizeof(row_id), "\"row_id\":\"%s\"", REAL_ART_ROWS[row]);
        TEST_ASSERT_NOT_NULL_MESSAGE(strstr(manifest, row_id), REAL_ART_ROWS[row]);
        char html_row[256];
        (void)snprintf(html_row, sizeof(html_row), "id=\"row-%s\"", REAL_ART_ROWS[row]);
        for (char *cursor = html_row; *cursor != '\0'; cursor++) {
            if (*cursor == ':') {
                *cursor = '-';
            }
        }
        TEST_ASSERT_NOT_NULL_MESSAGE(strstr(html, html_row), html_row);
        for (uint32_t column = 0; column < column_count; column++) {
            char panel_id[320];
            (void)snprintf(panel_id, sizeof(panel_id), "\"panel_id\":\"%s:%s\"", REAL_ART_ROWS[row], COLUMNS[column]);
            TEST_ASSERT_NOT_NULL_MESSAGE(strstr(manifest, panel_id), panel_id);
        }
    }
    free(manifest_bytes);
    free(html_bytes);
}

static void assert_basic_geometry_helpers(void) {
    const Point2D square[] = {{0, 0}, {4, 0}, {4, 4}, {0, 4}};
    const uint8_t retained[16] = {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1};
    TEST_ASSERT_EQUAL(NT_POLYGON_VALID, polygon_validate(square, 4));
    const nt_polygon_coverage_metrics_t coverage = polygon_coverage_metrics(square, 4, retained, 4, 4);
    TEST_ASSERT_EQUAL_UINT32(0, coverage.lost_retained_pixels);
    TEST_ASSERT_EQUAL_UINT32(0, coverage.extra_covered_pixels);
    TEST_ASSERT_TRUE(fabs(polygon_max_boundary_distance(square, 4, square, 4)) < 0.000001);
    TEST_ASSERT_EQUAL_UINT64(32, polygon_abs_twice_area(square, 4));
    TEST_ASSERT_LESS_OR_EQUAL_UINT32(4, 4);
    const Point2D crossing[] = {{0, 0}, {4, 4}, {0, 4}, {4, 0}};
    TEST_ASSERT_NOT_EQUAL(NT_POLYGON_VALID, polygon_validate(crossing, 4));
}

static void assert_selected_geometry_valid_cases(void) {
    const uint8_t two_cells[2] = {1, 1};
    const Point2D quad[4] = {{0, 0}, {2, 0}, {2, 1}, {0, 1}};
    const uint16_t triangles[6] = {0, 1, 2, 0, 2, 3};
    TEST_ASSERT_TRUE(nt_selected_geometry_validate(two_cells, 2, 1, 4, quad, 4, 4, triangles, 6, quad, 4, 4, triangles, 6, 0.0F, 4).valid);

    const uint8_t retained_with_gap[3] = {1, 1, 0};
    const Point2D expanded[4] = {{0, 0}, {3, 0}, {3, 1}, {0, 1}};
    TEST_ASSERT_TRUE(nt_selected_geometry_validate(retained_with_gap, 3, 1, 4, quad, 4, 4, triangles, 6, expanded, 4, 6, triangles, 6, 50.0F, 4).valid);
}

static void assert_selected_geometry_invalid_cases(void) {
    const uint8_t two_cells[2] = {1, 1};
    const Point2D quad[4] = {{0, 0}, {2, 0}, {2, 1}, {0, 1}};
    const uint16_t triangles[6] = {0, 1, 2, 0, 2, 3};
    TEST_ASSERT_FALSE(nt_selected_geometry_validate(two_cells, 2, 1, 2, quad, 4, 4, triangles, 6, quad, 4, 4, triangles, 6, 0.0F, 4).valid);
    TEST_ASSERT_FALSE(nt_selected_geometry_validate(two_cells, 2, 1, 4, quad, 4, 6, triangles, 6, quad, 4, 4, triangles, 6, 0.0F, 4).valid);
    TEST_ASSERT_FALSE(nt_selected_geometry_validate(two_cells, 2, 1, 4, quad, 4, 4, triangles, 6, quad, 4, 6, triangles, 6, 0.0F, 4).valid);
    const uint16_t corrupt_triangles[6] = {0, 1, 4, 0, 2, 3};
    TEST_ASSERT_FALSE(nt_selected_geometry_validate(two_cells, 2, 1, 4, quad, 4, 4, triangles, 6, quad, 4, 4, corrupt_triangles, 6, 0.0F, 4).valid);
    TEST_ASSERT_FALSE(nt_selected_geometry_validate(two_cells, 2, 1, 4, quad, 4, 4, triangles, 6, quad, 4, 4, triangles, 6, 0.0F, 3).valid);

    const uint8_t retained_with_gap[3] = {1, 1, 0};
    const Point2D expanded[4] = {{0, 0}, {3, 0}, {3, 1}, {0, 1}};
    TEST_ASSERT_FALSE(nt_selected_geometry_validate(retained_with_gap, 3, 1, 4, quad, 4, 4, triangles, 6, expanded, 4, 6, triangles, 6, 49.99F, 4).valid);
}

static void assert_production_gate_helpers(void) {
    assert_basic_geometry_helpers();
    assert_selected_geometry_valid_cases();
    assert_selected_geometry_invalid_cases();
}

static void test_report_schema_production_gates_and_determinism(void) {
    TEST_ASSERT_EQUAL_INT(0, nt_hull_visual_generate(CORPUS_PATH, FRONTIER_PATH, REPORT_A));
    TEST_ASSERT_EQUAL_INT(0, nt_hull_visual_generate(CORPUS_PATH, FRONTIER_PATH, REPORT_B));
    TEST_ASSERT_EQUAL_INT(
        0, nt_hull_visual_validate(REPORT_A "/manifest.json", REPORT_A "/index.html",
                                   "sq9-aa-triangle:convex,rotated-diamond:convex,concave-notch:concave,transparent-donut:concave,opaque-square-max3:convex,connected-mask-adversarial:concave,"
                                   "pixel-art-threshold-control:rect,real-rhombus-outline:concave,real-resource-wood:concave,real-card-down-outline:concave,real-d12-outline:concave,"
                                   "real-flask-empty:concave,real-tile-sparse:concave"));
    assert_manifest_schema_and_html();
    assert_production_gate_helpers();
    assert_same_file(REPORT_A "/manifest.json", REPORT_B "/manifest.json");
    assert_same_file(REPORT_A "/index.html", REPORT_B "/index.html");
}

static void test_validation_and_provenance_fail_closed(void) {
    TEST_ASSERT_NOT_EQUAL(0, nt_hull_visual_validate(REPORT_A "/missing.json", REPORT_A "/index.html", NULL));
    size_t manifest_size = 0;
    uint8_t *manifest = read_bytes(REPORT_A "/manifest.json", &manifest_size);
    char *allowance = strstr((char *)manifest, "\"allowance_valid\":true");
    TEST_ASSERT_NOT_NULL(allowance);
    allowance += strlen("\"allowance_valid\":");
    allowance[0] = 'f';
    allowance[1] = 'a';
    allowance[2] = 'l';
    allowance[3] = 's';
    write_bytes(REPORT_A "/corrupt-manifest.json", manifest, manifest_size);
    TEST_ASSERT_NOT_EQUAL(0, nt_hull_visual_validate(REPORT_A "/corrupt-manifest.json", REPORT_A "/index.html", NULL));
    free(manifest);

    size_t frontier_size = 0;
    uint8_t *frontier = read_bytes(FRONTIER_PATH, &frontier_size);
    char *hash = strstr((char *)frontier, "\"sweep_sha256\": \"");
    TEST_ASSERT_NOT_NULL(hash);
    hash += strlen("\"sweep_sha256\": \"");
    *hash = *hash == '0' ? '1' : '0';
    write_bytes(REPORT_A "/corrupt-frontier.json", frontier, frontier_size);
    TEST_ASSERT_NOT_EQUAL(0, nt_hull_visual_generate(CORPUS_PATH, REPORT_A "/corrupt-frontier.json", REPORT_A "/provenance-must-fail"));
    free(frontier);
}

static void test_panel_ownership_failure_paths_balance(void) {
    for (uint32_t stage = 1; stage <= 6; stage++) {
        nt_hull_visual_test_fail_after_stage(stage);
        TEST_ASSERT_NOT_EQUAL(0, nt_hull_visual_generate(CORPUS_PATH, FRONTIER_PATH, REPORT_A "/failure-injection"));
        TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, nt_hull_visual_test_live_buffers(), "panel owner leaked a buffer");
    }
    nt_hull_visual_test_fail_after_stage(0);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_report_schema_production_gates_and_determinism);
    RUN_TEST(test_validation_and_provenance_fail_closed);
    RUN_TEST(test_panel_ownership_failure_paths_balance);
    return UNITY_END();
}
