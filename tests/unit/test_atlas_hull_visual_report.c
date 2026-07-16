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
    "sq9-aa-triangle:convex",         "sq9-aa-triangle:concave",         "opaque-square-max3:convex",        "connected-mask-adversarial:concave",
    "mixed-aa-representative:convex", "mixed-aa-representative:concave", "pixel-art-threshold-control:rect",
};
static const char *COLUMNS[] = {"baseline", "candidate", "recommended"};

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
    TEST_ASSERT_EQUAL_UINT32(21, count_text(manifest, "\"panel_id\""));
    const char *baseline = strstr(manifest, "\"column_id\":\"baseline\"");
    const char *candidate = strstr(manifest, "\"column_id\":\"candidate\"");
    const char *recommended = strstr(manifest, "\"column_id\":\"recommended\"");
    TEST_ASSERT_NOT_NULL(baseline);
    TEST_ASSERT_NOT_NULL(candidate);
    TEST_ASSERT_NOT_NULL(recommended);
    TEST_ASSERT_TRUE(baseline < candidate && candidate < recommended);
    TEST_ASSERT_EQUAL_UINT32(3, count_text(manifest, "\"sweep_sha256\""));
    TEST_ASSERT_EQUAL_UINT32(3, count_text(manifest, "\"sweep_source\""));
    TEST_ASSERT_EQUAL_UINT32(3, count_text(manifest, "\"measurement_source_commit\""));

    static const char *fields[] = {
        "\"effective_alpha_mask\"", "\"original_alpha_values\"",
        "\"baseline_polygon\"",     "\"polygon\"",
        "\"numbered_vertices\"",    "\"vertex_count\"",
        "\"opaque_area2\"",         "\"base_area2\"",
        "\"selected_area2\"",       "\"added_area2\"",
        "\"exact_lost_area2\"",     "\"base_overdraw_percent\"",
        "\"added_area_percent\"",   "\"total_overdraw_percent\"",
        "\"self_intersection\"",    "\"signed_area\"",
        "\"winding_valid\"",        "\"vertex_budget\"",
        "\"production_gates\"",     "\"result\"",
    };
    for (uint32_t i = 0; i < (uint32_t)(sizeof(fields) / sizeof(fields[0])); i++) {
        TEST_ASSERT_GREATER_OR_EQUAL_UINT32_MESSAGE(21, count_text(manifest, fields[i]), fields[i]);
    }
    TEST_ASSERT_EQUAL_UINT32(28, count_text(manifest, "\"result\":\"PASS\""));
    TEST_ASSERT_NULL(strstr(manifest, "\"result\":\"FAIL\""));
    TEST_ASSERT_NULL(strstr(html, "<script"));
    TEST_ASSERT_NULL(strstr(html, "http://"));
    TEST_ASSERT_NULL(strstr(html, "https://"));
    TEST_ASSERT_NOT_NULL(strstr(html, "Vertex count / budget"));
    TEST_ASSERT_NOT_NULL(strstr(html, "Base / added / total overdraw"));
    TEST_ASSERT_NOT_NULL(strstr(html, "Exact lost area"));
    TEST_ASSERT_NOT_NULL(strstr(manifest, "\"connected_frontier_counts\":[6,7,8]"));

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
        for (uint32_t column = 0; column < (uint32_t)(sizeof(COLUMNS) / sizeof(COLUMNS[0])); column++) {
            char panel_id[320];
            (void)snprintf(panel_id, sizeof(panel_id), "\"panel_id\":\"%s:%s\"", REQUIRED_ROWS[row], COLUMNS[column]);
            TEST_ASSERT_NOT_NULL_MESSAGE(strstr(manifest, panel_id), panel_id);
        }
    }
    free(manifest_bytes);
    free(html_bytes);
}

static void assert_production_gate_helpers(void) {
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

static void test_report_schema_production_gates_and_determinism(void) {
    TEST_ASSERT_EQUAL_INT(0, nt_hull_visual_generate(CORPUS_PATH, FRONTIER_PATH, REPORT_A));
    TEST_ASSERT_EQUAL_INT(0, nt_hull_visual_generate(CORPUS_PATH, FRONTIER_PATH, REPORT_B));
    TEST_ASSERT_EQUAL_INT(0,
                          nt_hull_visual_validate(REPORT_A "/manifest.json", REPORT_A "/index.html",
                                                  "sq9-aa-triangle:convex,sq9-aa-triangle:concave,opaque-square-max3:convex,connected-mask-adversarial:concave,mixed-aa-representative:convex,mixed-aa-"
                                                  "representative:concave,pixel-art-threshold-control:rect"));
    assert_manifest_schema_and_html();
    assert_production_gate_helpers();
    assert_same_file(REPORT_A "/manifest.json", REPORT_B "/manifest.json");
    assert_same_file(REPORT_A "/index.html", REPORT_B "/index.html");
}

static void test_validation_and_provenance_fail_closed(void) {
    TEST_ASSERT_NOT_EQUAL(0, nt_hull_visual_validate(REPORT_A "/missing.json", REPORT_A "/index.html", NULL));
    size_t manifest_size = 0;
    uint8_t *manifest = read_bytes(REPORT_A "/manifest.json", &manifest_size);
    char *result = strstr((char *)manifest, "\"result\":\"PASS\"");
    TEST_ASSERT_NOT_NULL(result);
    result += strlen("\"result\":\"");
    result[0] = 'F';
    result[1] = 'A';
    result[2] = 'I';
    result[3] = 'L';
    write_bytes(REPORT_A "/corrupt-manifest.json", manifest, manifest_size);
    TEST_ASSERT_NOT_EQUAL(0, nt_hull_visual_validate(REPORT_A "/corrupt-manifest.json", REPORT_A "/index.html", NULL));
    free(manifest);

    size_t frontier_size = 0;
    uint8_t *frontier = read_bytes(FRONTIER_PATH, &frontier_size);
    char *column = strstr((char *)frontier, "\"column_id\": \"baseline\"");
    TEST_ASSERT_NOT_NULL(column);
    char *hash = strstr(column, "\"sweep_sha256\": \"");
    TEST_ASSERT_NOT_NULL(hash);
    hash += strlen("\"sweep_sha256\": \"");
    *hash = *hash == '0' ? '1' : '0';
    write_bytes(REPORT_A "/corrupt-frontier.json", frontier, frontier_size);
    TEST_ASSERT_NOT_EQUAL(0, nt_hull_visual_generate(CORPUS_PATH, REPORT_A "/corrupt-frontier.json", REPORT_A "/provenance-must-fail"));
    free(frontier);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_report_schema_production_gates_and_determinism);
    RUN_TEST(test_validation_and_provenance_fail_closed);
    return UNITY_END();
}
