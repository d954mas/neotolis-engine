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
#include "ntpack_parse.h"
#include "unity.h"

#define FRONTIER_PATH "tools/research/atlas_bench/hull_area_frontier.json"
#define REPORT_A "build/reports/test-hull-visual-a"
#define REPORT_B "build/reports/test-hull-visual-b"
#define REPORT_NTPACK_DIR "build/reports/test-hull-visual.ntpack-path"

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
void tearDown(void) {
    (void)remove(REPORT_NTPACK_DIR "/panel-00-base.h");
    (void)remove(REPORT_NTPACK_DIR "/panel-00-selected.h");
}

static bool file_exists(const char *path) {
    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        return false;
    }
    (void)fclose(file);
    return true;
}

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

static uint8_t *replace_text_once(const uint8_t *input, size_t input_size, const char *needle, const char *replacement, size_t *out_size) {
    enum { REPLACEMENT_CAPACITY = 65536U };
    const char *match = strstr((const char *)input, needle);
    TEST_ASSERT_NOT_NULL_MESSAGE(match, needle);
    const size_t prefix_size = (size_t)(match - (const char *)input);
    const size_t needle_size = strlen(needle);
    const size_t replacement_size = strlen(replacement);
    if (input_size > REPLACEMENT_CAPACITY || prefix_size > input_size || needle_size > input_size - prefix_size || replacement_size > REPLACEMENT_CAPACITY - (input_size - needle_size)) {
        TEST_FAIL_MESSAGE("replacement fixture exceeds test buffer");
        return NULL;
    }
    *out_size = input_size - needle_size + replacement_size;
    uint8_t *output = (uint8_t *)malloc(REPLACEMENT_CAPACITY + 1U);
    TEST_ASSERT_NOT_NULL(output);
    memcpy(output, input, prefix_size);
    memcpy(output + prefix_size, replacement, replacement_size);
    memcpy(output + prefix_size + replacement_size, input + prefix_size + needle_size, input_size - prefix_size - needle_size);
    output[*out_size] = 0U;
    return output;
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
    TEST_ASSERT_NOT_NULL(strstr(manifest, "\"schema_version\": 3"));
    TEST_ASSERT_NOT_NULL(strstr(manifest, "\"overall_pass\": true"));
    TEST_ASSERT_NOT_NULL(strstr(manifest, "\"failing_panel_ids\": []"));
    TEST_ASSERT_NOT_NULL(strstr(manifest, "\"visual_input_sha256\""));
    TEST_ASSERT_NULL(strstr(manifest, "\"html_sha256\""));
    TEST_ASSERT_NULL(strstr(manifest, "\"visual_corpus_sha256\""));
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
    TEST_ASSERT_NOT_NULL(strstr(html, "Selected vertices / hard ceiling"));
    TEST_ASSERT_NOT_NULL(strstr(html, "Aopaque / Abase / Aselected"));
    TEST_ASSERT_NOT_NULL(strstr(html, "Base / added / total overdraw"));
    TEST_ASSERT_NOT_NULL(strstr(html, "Full retained-cell coverage"));
    TEST_ASSERT_NOT_NULL(strstr(html, "Exact lost area"));
    TEST_ASSERT_NULL(strstr(manifest, "connected_frontier_counts"));
    TEST_ASSERT_NOT_NULL(strstr(manifest, "\"real_art_rows\""));
    TEST_ASSERT_EQUAL_UINT32(real_art_row_count * column_count, count_text(manifest, "\"real_art_sample\":true"));
    TEST_ASSERT_NOT_NULL(strstr(html, "Real art"));
    TEST_ASSERT_NULL(strstr(manifest, "connected_frontier_evidence"));
    TEST_ASSERT_NOT_NULL(strstr(manifest, "\"production_selected_count_evidence\""));
    TEST_ASSERT_EQUAL_UINT32(3, count_text(manifest, "\"source_panel_id\""));
    TEST_ASSERT_NOT_NULL(strstr(manifest, "\"source_panel_id\":\"transparent-donut:concave:percent-10\",\"selected_vertex_count\":6"));
    TEST_ASSERT_NOT_NULL(strstr(manifest, "\"source_panel_id\":\"concave-notch:concave:percent-15\",\"selected_vertex_count\":7"));
    TEST_ASSERT_NOT_NULL(strstr(manifest, "\"source_panel_id\":\"concave-notch:concave:percent-10\",\"selected_vertex_count\":8"));
    static const char *synthetic_10_pins[][4] = {
        {"\"panel_id\":\"sq9-aa-triangle:convex:percent-10\"", "\"selected_vertex_count\":5", "\"selected_area2\":759", "\"opaque_area2\":732"},
        {"\"panel_id\":\"concave-notch:concave:percent-10\"", "\"selected_vertex_count\":8", "\"selected_area2\":314", "\"opaque_area2\":314"},
        {"\"panel_id\":\"connected-mask-adversarial:concave:percent-10\"", "\"selected_vertex_count\":9", "\"selected_area2\":206", "\"opaque_area2\":144"},
    };
    for (uint32_t i = 0; i < (uint32_t)(sizeof(synthetic_10_pins) / sizeof(synthetic_10_pins[0])); i++) {
        const char *panel = strstr(manifest, synthetic_10_pins[i][0]);
        TEST_ASSERT_NOT_NULL_MESSAGE(panel, synthetic_10_pins[i][0]);
        const char *panel_end = strstr(panel, "\"result\":\"PASS\"");
        TEST_ASSERT_NOT_NULL(panel_end);
        for (uint32_t j = 1; j < 4; j++) {
            const char *pin = strstr(panel, synthetic_10_pins[i][j]);
            TEST_ASSERT_NOT_NULL_MESSAGE(pin, synthetic_10_pins[i][j]);
            TEST_ASSERT_TRUE_MESSAGE(pin < panel_end, synthetic_10_pins[i][j]);
        }
    }
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
    TEST_ASSERT_NULL(strstr(html, "real 0% production pack"));
    TEST_ASSERT_NOT_NULL(strstr(html, "Public selector evidence"));
    TEST_ASSERT_NULL(strstr(html, "В·"));
    TEST_ASSERT_NULL(strstr(html, "Р’В·"));

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

static void assert_frontier_declares_portable_proofs(void) {
    size_t frontier_size = 0;
    uint8_t *frontier = read_bytes(FRONTIER_PATH, &frontier_size);
    TEST_ASSERT_NOT_NULL(frontier);
    TEST_ASSERT_NOT_NULL(strstr((char *)frontier, "\"schema_version\": 3"));
    TEST_ASSERT_NOT_NULL(strstr((char *)frontier, "\"proof_format\": \"portable-v1\""));
    TEST_ASSERT_NOT_NULL(strstr((char *)frontier, "\"builder_binary_sha256\": \""));
    free(frontier);
}

static void assert_proof_is_portable(const char *path) {
    size_t proof_size = 0;
    uint8_t *proof = read_bytes(path, &proof_size);
    TEST_ASSERT_NOT_NULL_MESSAGE(proof, path);
    TEST_ASSERT_NULL_MESSAGE(strstr((char *)proof, "\"os\""), path);
    TEST_ASSERT_NULL_MESSAGE(strstr((char *)proof, "\"cpu\""), path);
    TEST_ASSERT_NULL_MESSAGE(strstr((char *)proof, "\"pack_ms\""), path);
    free(proof);
}

static void assert_frontier_proofs_are_portable(void) {
    assert_frontier_declares_portable_proofs();

    static const char *proofs[] = {
        "tests/fixtures/hull_visual_acceptance/proof/00-percent-0.json",  "tests/fixtures/hull_visual_acceptance/proof/01-percent-2.json",
        "tests/fixtures/hull_visual_acceptance/proof/02-percent-5.json",  "tests/fixtures/hull_visual_acceptance/proof/03-percent-10.json",
        "tests/fixtures/hull_visual_acceptance/proof/04-percent-15.json", "tests/fixtures/hull_visual_acceptance/proof/05-percent-25.json",
    };
    for (uint32_t i = 0; i < (uint32_t)(sizeof(proofs) / sizeof(proofs[0])); i++) {
        assert_proof_is_portable(proofs[i]);
    }
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
    TEST_ASSERT_EQUAL_INT(0, nt_hull_visual_generate(FRONTIER_PATH, REPORT_A));
    TEST_ASSERT_EQUAL_INT(0, nt_hull_visual_generate(FRONTIER_PATH, REPORT_B));
    TEST_ASSERT_EQUAL_INT(
        0, nt_hull_visual_validate(REPORT_A "/manifest.json", REPORT_A "/index.html",
                                   "sq9-aa-triangle:convex,rotated-diamond:convex,concave-notch:concave,transparent-donut:concave,opaque-square-max3:convex,connected-mask-adversarial:concave,"
                                   "pixel-art-threshold-control:rect,real-rhombus-outline:concave,real-resource-wood:concave,real-card-down-outline:concave,real-d12-outline:concave,"
                                   "real-flask-empty:concave,real-tile-sparse:concave"));
    assert_manifest_schema_and_html();
    assert_frontier_proofs_are_portable();
    assert_production_gate_helpers();
    assert_same_file(REPORT_A "/manifest.json", REPORT_B "/manifest.json");
    assert_same_file(REPORT_A "/index.html", REPORT_B "/index.html");
}

static void assert_validation_rejects_corrupt_manifest(void) {
    TEST_ASSERT_NOT_EQUAL(0, nt_hull_visual_validate(REPORT_A "/missing.json", REPORT_A "/index.html", NULL));
    size_t manifest_size = 0;
    uint8_t *manifest = read_bytes(REPORT_A "/manifest.json", &manifest_size);
    char *allowance = strstr((char *)manifest, "\"allowance_valid\":true");
    TEST_ASSERT_NOT_NULL(allowance);
    allowance += strlen("\"allowance_valid\":");
    allowance[0] = 'n';
    allowance[1] = 'u';
    allowance[2] = 'l';
    allowance[3] = 'l';
    write_bytes(REPORT_A "/corrupt-manifest.json", manifest, manifest_size);
    TEST_ASSERT_NOT_EQUAL(0, nt_hull_visual_validate(REPORT_A "/corrupt-manifest.json", REPORT_A "/index.html", NULL));
    free(manifest);

    manifest = read_bytes(REPORT_A "/manifest.json", &manifest_size);
    char *selected_area = strstr((char *)manifest, "\"selected_area2\":759");
    TEST_ASSERT_NOT_NULL(selected_area);
    selected_area += strlen("\"selected_area2\":");
    selected_area[0] = '"';
    selected_area[1] = 'x';
    selected_area[2] = '"';
    write_bytes(REPORT_A "/corrupt-manifest.json", manifest, manifest_size);
    TEST_ASSERT_NOT_EQUAL(0, nt_hull_visual_validate(REPORT_A "/corrupt-manifest.json", REPORT_A "/index.html", NULL));
    free(manifest);
}

static void assert_generation_rejects_trailing_frontier(void) {
    size_t frontier_size = 0;
    uint8_t *frontier = read_bytes(FRONTIER_PATH, &frontier_size);
    static const char trailing[] = "\ntrailing garbage\n";
    uint8_t *trailing_frontier = (uint8_t *)malloc(frontier_size + sizeof(trailing));
    TEST_ASSERT_NOT_NULL(trailing_frontier);
    memcpy(trailing_frontier, frontier, frontier_size);
    memcpy(trailing_frontier + frontier_size, trailing, sizeof(trailing));
    write_bytes(REPORT_A "/trailing-frontier.json", trailing_frontier, frontier_size + sizeof(trailing) - 1U);
    TEST_ASSERT_NOT_EQUAL(0, nt_hull_visual_generate(REPORT_A "/trailing-frontier.json", REPORT_A "/trailing-must-fail"));
    free(trailing_frontier);
    free(frontier);
}

static void assert_generation_rejects_corrupt_frontier(void) {
    size_t frontier_size = 0;
    uint8_t *frontier = read_bytes(FRONTIER_PATH, &frontier_size);
    char *hash = strstr((char *)frontier, "\"sweep_sha256\": \"");
    TEST_ASSERT_NOT_NULL(hash);
    hash += strlen("\"sweep_sha256\": \"");
    *hash = *hash == '0' ? '1' : '0';
    write_bytes(REPORT_A "/corrupt-frontier.json", frontier, frontier_size);
    TEST_ASSERT_NOT_EQUAL(0, nt_hull_visual_generate(REPORT_A "/corrupt-frontier.json", REPORT_A "/provenance-must-fail"));
    free(frontier);

    assert_generation_rejects_trailing_frontier();

    frontier = read_bytes(FRONTIER_PATH, &frontier_size);
    char *schema = strstr((char *)frontier, "\"schema_version\": 3");
    TEST_ASSERT_NOT_NULL(schema);
    schema[strlen("\"schema_version\": ")] = '2';
    write_bytes(REPORT_A "/legacy-frontier.json", frontier, frontier_size);
    TEST_ASSERT_NOT_EQUAL(0, nt_hull_visual_generate(REPORT_A "/legacy-frontier.json", REPORT_A "/legacy-must-fail"));
    free(frontier);

    static const char *source_path = "tests/fixtures/hull_visual_acceptance/proof/00-percent-0.json";
    static const char *corrupt_proof_path = REPORT_A "/corrupt-proof-source.json";
    size_t proof_size = 0;
    uint8_t *proof = read_bytes(source_path, &proof_size);
    size_t corrupt_proof_size = 0;
    uint8_t *corrupt_proof = replace_text_once(proof, proof_size, "\"topology\":true", "\"topology\":false", &corrupt_proof_size);
    write_bytes(corrupt_proof_path, corrupt_proof, corrupt_proof_size);
    free(corrupt_proof);
    free(proof);

    char corrupt_hash[65];
    TEST_ASSERT_EQUAL_INT(0, nt_bench_file_sha256_hex(corrupt_proof_path, corrupt_hash));
    frontier = read_bytes(FRONTIER_PATH, &frontier_size);
    size_t corrupt_frontier_size = 0;
    uint8_t *corrupt_frontier = replace_text_once(frontier, frontier_size, source_path, corrupt_proof_path, &corrupt_frontier_size);
    free(frontier);
    char *frontier_hash = strstr((char *)corrupt_frontier, "\"sweep_sha256\": \"");
    TEST_ASSERT_NOT_NULL(frontier_hash);
    frontier_hash += strlen("\"sweep_sha256\": \"");
    memcpy(frontier_hash, corrupt_hash, 64U);
    write_bytes(REPORT_A "/corrupt-proof-frontier.json", corrupt_frontier, corrupt_frontier_size);
    free(corrupt_frontier);
    TEST_ASSERT_NOT_EQUAL(0, nt_hull_visual_generate(REPORT_A "/corrupt-proof-frontier.json", REPORT_A "/corrupt-proof-must-fail"));
    (void)remove(REPORT_A "/corrupt-proof-frontier.json");
    (void)remove(corrupt_proof_path);
}

static void test_validation_and_provenance_fail_closed(void) {
    assert_validation_rejects_corrupt_manifest();
    assert_generation_rejects_corrupt_frontier();
}

static void test_panel_ownership_failure_paths_balance(void) {
    for (uint32_t stage = 1; stage <= 6; stage++) {
        nt_hull_visual_test_fail_after_stage(stage);
        TEST_ASSERT_NOT_EQUAL(0, nt_hull_visual_generate(FRONTIER_PATH, REPORT_A "/failure-injection"));
        TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, nt_hull_visual_test_live_buffers(), "panel owner leaked a buffer");
    }
    nt_hull_visual_test_fail_after_stage(0);
}

static void test_panel_cleanup_removes_headers_when_directory_contains_ntpack(void) {
    nt_hull_visual_test_fail_after_stage(3);
    TEST_ASSERT_NOT_EQUAL(0, nt_hull_visual_generate(FRONTIER_PATH, REPORT_NTPACK_DIR));
    nt_hull_visual_test_fail_after_stage(0);
    TEST_ASSERT_FALSE(file_exists(REPORT_NTPACK_DIR "/panel-00-base.h"));
    TEST_ASSERT_FALSE(file_exists(REPORT_NTPACK_DIR "/panel-00-selected.h"));
}

static void test_panel_pack_paths_reject_truncation(void) {
    char baseline[64];
    char selected[64];
    TEST_ASSERT_TRUE(nt_hull_visual_test_panel_pack_paths("report", 7U, baseline, sizeof(baseline), selected, sizeof(selected)));
    TEST_ASSERT_EQUAL_STRING("report/panel-07-base.ntpack", baseline);
    TEST_ASSERT_EQUAL_STRING("report/panel-07-selected.ntpack", selected);

    char too_small[12] = "marker";
    TEST_ASSERT_FALSE(nt_hull_visual_test_panel_pack_paths("report", 7U, too_small, sizeof(too_small), selected, sizeof(selected)));
    TEST_ASSERT_EQUAL_STRING("", too_small);
    TEST_ASSERT_EQUAL_STRING("", selected);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_report_schema_production_gates_and_determinism);
    RUN_TEST(test_validation_and_provenance_fail_closed);
    RUN_TEST(test_panel_ownership_failure_paths_balance);
    RUN_TEST(test_panel_cleanup_removes_headers_when_directory_contains_ntpack);
    RUN_TEST(test_panel_pack_paths_reject_truncation);
    return UNITY_END();
}
