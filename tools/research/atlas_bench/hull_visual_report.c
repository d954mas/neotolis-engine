#include "hull_visual_report.h"

#include "nt_atlas_format.h"
#include "nt_builder.h"
#include "nt_builder_atlas_geometry.h"
#include "nt_pack_format.h"
#include "ntpack_parse.h"

#include <ctype.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <direct.h>
#define NT_MKDIR(path) _mkdir(path)
#else
#include <sys/stat.h>
#define NT_MKDIR(path) mkdir(path, 0755)
#endif

#define VISUAL_SCHEMA_VERSION 2
#define VISUAL_ROW_COUNT 7
#define VISUAL_COLUMN_COUNT 3
#define VISUAL_MAX_VERTICES 16
#define VISUAL_PATH_MAX 1024

uint32_t nt_atlas_test_concave_frontier_slot_mask(const uint8_t *binary, uint32_t width, uint32_t height, uint32_t max_vertices);

typedef struct {
    const char *id;
    double percent;
    char source[VISUAL_PATH_MAX];
    char sha256[65];
    char source_commit[41];
} VisualColumn;

typedef struct {
    const char *sample_id;
    const char *shape_name;
    nt_atlas_shape_t shape;
    uint8_t threshold;
    uint8_t budget;
} VisualRow;

typedef struct {
    uint32_t width;
    uint32_t height;
    uint8_t *rgba;
} VisualFixture;

typedef struct {
    Point2D baseline_polygon[VISUAL_MAX_VERTICES];
    Point2D polygon[VISUAL_MAX_VERTICES];
    uint16_t baseline_indices[NT_POLYGON_MAX_TRIANGLE_INDICES];
    uint16_t selected_indices[NT_POLYGON_MAX_TRIANGLE_INDICES];
    uint32_t baseline_vertex_count;
    uint32_t vertex_count;
    uint32_t trim_x;
    uint32_t trim_y;
    uint32_t trim_w;
    uint32_t trim_h;
    uint8_t *alpha_values;
    uint8_t *mask;
    uint32_t retained_pixels;
    nt_polygon_coverage_metrics_t coverage;
    nt_polygon_validity_t validity;
    int64_t signed_twice_area;
    bool triangles_valid;
    bool commit_ok;
    bool hull_infeasible;
    bool allowance_ok;
    bool result;
    nt_selected_geometry_proof_t proof;
    uint64_t lost_area2;
    char baseline_pack_sha256[65];
    char selected_pack_sha256[65];
    uint32_t connected_frontier_counts[3];
    bool connected_frontier_valid;
} VisualPanel;

static const VisualRow VISUAL_ROWS[VISUAL_ROW_COUNT] = {
    {"sq9-aa-triangle", "convex", NT_ATLAS_SHAPE_CONVEX_HULL, 1, 8},         {"sq9-aa-triangle", "concave", NT_ATLAS_SHAPE_CONCAVE_CONTOUR, 1, 8},
    {"opaque-square-max3", "convex", NT_ATLAS_SHAPE_CONVEX_HULL, 1, 3},      {"connected-mask-adversarial", "concave", NT_ATLAS_SHAPE_CONCAVE_CONTOUR, 1, 13},
    {"mixed-aa-representative", "convex", NT_ATLAS_SHAPE_CONVEX_HULL, 1, 8}, {"mixed-aa-representative", "concave", NT_ATLAS_SHAPE_CONCAVE_CONTOUR, 1, 8},
    {"pixel-art-threshold-control", "rect", NT_ATLAS_SHAPE_RECT, 128, 4},
};

static bool row_expects_hull_infeasible(const VisualRow *row) { return strcmp(row->sample_id, "opaque-square-max3") == 0; }

static uint32_t rotr32(uint32_t value, uint32_t count) { return (value >> count) | (value << (32U - count)); }

static void sha256_bytes(const uint8_t *data, size_t size, uint8_t digest[32]) {
    static const uint32_t k[64] = {
        0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U, 0x3956c25bU, 0x59f111f1U, 0x923f82a4U, 0xab1c5ed5U, 0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U, 0x72be5d74U,
        0x80deb1feU, 0x9bdc06a7U, 0xc19bf174U, 0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU, 0x2de92c6fU, 0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU, 0x983e5152U, 0xa831c66dU,
        0xb00327c8U, 0xbf597fc7U, 0xc6e00bf3U, 0xd5a79147U, 0x06ca6351U, 0x14292967U, 0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU, 0x53380d13U, 0x650a7354U, 0x766a0abbU, 0x81c2c92eU,
        0x92722c85U, 0xa2bfe8a1U, 0xa81a664bU, 0xc24b8b70U, 0xc76c51a3U, 0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U, 0x19a4c116U, 0x1e376c08U, 0x2748774cU, 0x34b0bcb5U,
        0x391c0cb3U, 0x4ed8aa4aU, 0x5b9cca4fU, 0x682e6ff3U, 0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U, 0x90befffaU, 0xa4506cebU, 0xbef9a3f7U, 0xc67178f2U,
    };
    uint32_t h[8] = {0x6a09e667U, 0xbb67ae85U, 0x3c6ef372U, 0xa54ff53aU, 0x510e527fU, 0x9b05688cU, 0x1f83d9abU, 0x5be0cd19U};
    size_t padded = size + 1U;
    while ((padded % 64U) != 56U) {
        padded++;
    }
    uint8_t *message = (uint8_t *)calloc(padded + 8U, 1U);
    if (message == NULL) {
        memset(digest, 0, 32);
        return;
    }
    memcpy(message, data, size);
    message[size] = 0x80U;
    uint64_t bits = (uint64_t)size * 8U;
    for (uint32_t i = 0; i < 8; i++) {
        message[padded + i] = (uint8_t)(bits >> (56U - (i * 8U)));
    }
    for (size_t block = 0; block < padded + 8U; block += 64U) {
        uint32_t w[64];
        for (uint32_t i = 0; i < 16; i++) {
            const uint8_t *p = message + block + (i * 4U);
            w[i] = ((uint32_t)p[0] << 24U) | ((uint32_t)p[1] << 16U) | ((uint32_t)p[2] << 8U) | p[3];
        }
        for (uint32_t i = 16; i < 64; i++) {
            uint32_t s0 = rotr32(w[i - 15], 7) ^ rotr32(w[i - 15], 18) ^ (w[i - 15] >> 3U);
            uint32_t s1 = rotr32(w[i - 2], 17) ^ rotr32(w[i - 2], 19) ^ (w[i - 2] >> 10U);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], x = h[7];
        for (uint32_t i = 0; i < 64; i++) {
            uint32_t s1 = rotr32(e, 6) ^ rotr32(e, 11) ^ rotr32(e, 25);
            uint32_t ch = (e & f) ^ ((~e) & g);
            uint32_t t1 = x + s1 + ch + k[i] + w[i];
            uint32_t s0 = rotr32(a, 2) ^ rotr32(a, 13) ^ rotr32(a, 22);
            uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
            uint32_t t2 = s0 + maj;
            x = g;
            g = f;
            f = e;
            e = d + t1;
            d = c;
            c = b;
            b = a;
            a = t1 + t2;
        }
        h[0] += a;
        h[1] += b;
        h[2] += c;
        h[3] += d;
        h[4] += e;
        h[5] += f;
        h[6] += g;
        h[7] += x;
    }
    free(message);
    for (uint32_t i = 0; i < 8; i++) {
        digest[(i * 4U) + 0U] = (uint8_t)(h[i] >> 24U);
        digest[(i * 4U) + 1U] = (uint8_t)(h[i] >> 16U);
        digest[(i * 4U) + 2U] = (uint8_t)(h[i] >> 8U);
        digest[(i * 4U) + 3U] = (uint8_t)h[i];
    }
}

static uint8_t *read_file(const char *path, size_t *out_size) {
    FILE *file = path != NULL ? fopen(path, "rb") : NULL;
    if (file == NULL || fseek(file, 0, SEEK_END) != 0) {
        if (file != NULL) {
            (void)fclose(file);
        }
        return NULL;
    }
    long length = ftell(file);
    (void)fseek(file, 0, SEEK_SET);
    if (length < 0) {
        (void)fclose(file);
        return NULL;
    }
    uint8_t *data = (uint8_t *)malloc((size_t)length + 1U);
    if (data == NULL || fread(data, 1, (size_t)length, file) != (size_t)length) {
        free(data);
        (void)fclose(file);
        return NULL;
    }
    (void)fclose(file);
    data[length] = 0;
    *out_size = (size_t)length;
    return data;
}

static bool file_sha256_hex(const char *path, char out[65]) {
    size_t size = 0;
    uint8_t *data = read_file(path, &size);
    if (data == NULL) {
        return false;
    }
    uint8_t digest[32];
    sha256_bytes(data, size, digest);
    free(data);
    for (uint32_t i = 0; i < 32; i++) {
        (void)snprintf(out + (i * 2U), 3, "%02x", digest[i]);
    }
    out[64] = '\0';
    return true;
}

static int64_t signed_twice_area(const Point2D *poly, uint32_t count) {
    int64_t area = 0;
    for (uint32_t i = 0; i < count; i++) {
        const Point2D a = poly[i];
        const Point2D b = poly[(i + 1U) % count];
        area += ((int64_t)a.x * b.y) - ((int64_t)b.x * a.y);
    }
    return area;
}

static uint32_t count_substring(const char *text, const char *needle) {
    uint32_t count = 0;
    size_t length = strlen(needle);
    for (const char *p = text; (p = strstr(p, needle)) != NULL; p += length) {
        count++;
    }
    return count;
}

static void html_escape(FILE *file, const char *text) {
    for (const char *p = text; *p != '\0'; p++) {
        switch (*p) {
        case '&':
            (void)fputs("&amp;", file);
            break;
        case '<':
            (void)fputs("&lt;", file);
            break;
        case '>':
            (void)fputs("&gt;", file);
            break;
        case '"':
            (void)fputs("&quot;", file);
            break;
        default:
            (void)fputc(*p, file);
            break;
        }
    }
}

static void json_escape(FILE *file, const char *text) {
    (void)fputc('"', file);
    for (const char *p = text; *p != '\0'; p++) {
        if (*p == '"' || *p == '\\') {
            (void)fputc('\\', file);
        }
        (void)fputc(*p, file);
    }
    (void)fputc('"', file);
}

static bool text_value(const char *object, const char *key, char *out, size_t out_size) {
    char needle[96];
    (void)snprintf(needle, sizeof(needle), "\"%s\": \"", key);
    const char *start = strstr(object, needle);
    if (start == NULL) {
        return false;
    }
    start += strlen(needle);
    const char *end = strchr(start, '"');
    if (end == NULL || (size_t)(end - start) >= out_size) {
        return false;
    }
    memcpy(out, start, (size_t)(end - start));
    out[end - start] = '\0';
    return true;
}

static bool number_value(const char *object, const char *key, double *out) {
    char needle[96];
    (void)snprintf(needle, sizeof(needle), "\"%s\":", key);
    const char *start = strstr(object, needle);
    if (start == NULL) {
        return false;
    }
    start += strlen(needle);
    char *end = NULL;
    *out = strtod(start, &end);
    return end != start && isfinite(*out);
}

static bool load_frontier(const char *path, VisualColumn columns[VISUAL_COLUMN_COUNT]) {
    static const char *ids[VISUAL_COLUMN_COUNT] = {"baseline", "candidate", "recommended"};
    size_t size = 0;
    uint8_t *bytes = read_file(path, &size);
    if (bytes == NULL || size == 0) {
        free(bytes);
        return false;
    }
    const char *cursor = (const char *)bytes;
    char source_commit[41];
    bool ok = strstr(cursor, "\"schema_version\": 2") != NULL && text_value(cursor, "measurement_source_commit", source_commit, sizeof(source_commit)) && strlen(source_commit) == 40U;
    for (uint32_t i = 0; ok && i < VISUAL_COLUMN_COUNT; i++) {
        char needle[96];
        (void)snprintf(needle, sizeof(needle), "\"column_id\": \"%s\"", ids[i]);
        const char *object = strstr(cursor, needle);
        double percent = 0.0;
        if (object == NULL || !number_value(object, "max_added_area_percent", &percent) || percent < 0.0 || !text_value(object, "sweep_source", columns[i].source, sizeof(columns[i].source)) ||
            !text_value(object, "sweep_sha256", columns[i].sha256, sizeof(columns[i].sha256))) {
            ok = false;
            break;
        }
        columns[i].id = ids[i];
        columns[i].percent = percent;
        (void)snprintf(columns[i].source_commit, sizeof(columns[i].source_commit), "%s", source_commit);
        char actual[65];
        size_t sweep_size = 0;
        uint8_t *sweep = read_file(columns[i].source, &sweep_size);
        double measured = -1.0;
        ok = file_sha256_hex(columns[i].source, actual) && strcmp(actual, columns[i].sha256) == 0 && sweep != NULL && number_value((const char *)sweep, "max_added_area_percent", &measured) &&
             fabs(measured - percent) < 0.0001;
        free(sweep);
        cursor = object + strlen(needle);
    }
    free(bytes);
    return ok;
}

static bool validate_corpus(const char *path) {
    size_t size = 0;
    uint8_t *bytes = read_file(path, &size);
    if (bytes == NULL || size == 0 || strstr((const char *)bytes, "\"schema_version\": 1") == NULL) {
        free(bytes);
        return false;
    }
    bool ok = true;
    for (uint32_t i = 0; i < VISUAL_ROW_COUNT; i++) {
        const char *sample = strstr((const char *)bytes, VISUAL_ROWS[i].sample_id);
        const char *shape = sample != NULL ? strstr(sample, VISUAL_ROWS[i].shape_name) : NULL;
        if (shape == NULL) {
            ok = false;
            break;
        }
    }
    free(bytes);
    return ok;
}

static bool make_parent_dir(const char *path) {
    if (path == NULL || path[0] == '\0') {
        return false;
    }
    char copy[VISUAL_PATH_MAX];
    if (strlen(path) >= sizeof(copy)) {
        return false;
    }
    (void)snprintf(copy, sizeof(copy), "%s", path);
    for (char *p = copy + 1; *p != '\0'; p++) {
        if (*p == '/' || *p == '\\') {
            char separator = *p;
            *p = '\0';
            (void)NT_MKDIR(copy);
            *p = separator;
        }
    }
    (void)NT_MKDIR(copy);
    FILE *probe = NULL;
    char marker[VISUAL_PATH_MAX];
    (void)snprintf(marker, sizeof(marker), "%s/.nt_hull_visual_probe", path);
    probe = fopen(marker, "wb");
    if (probe == NULL) {
        return false;
    }
    (void)fclose(probe);
    (void)remove(marker);
    return true;
}

static void set_pixel(VisualFixture *fixture, uint32_t x, uint32_t y, uint8_t alpha) {
    const size_t offset = (((size_t)y * fixture->width) + x) * 4U;
    fixture->rgba[offset + 0U] = 36;
    fixture->rgba[offset + 1U] = 188;
    fixture->rgba[offset + 2U] = 224;
    fixture->rgba[offset + 3U] = alpha;
}

static bool fixture_create(const VisualRow *row, VisualFixture *fixture) {
    if (strcmp(row->sample_id, "sq9-aa-triangle") == 0) {
        fixture->width = 28;
        fixture->height = 20;
    } else if (strcmp(row->sample_id, "opaque-square-max3") == 0) {
        fixture->width = 4;
        fixture->height = 4;
    } else if (strcmp(row->sample_id, "connected-mask-adversarial") == 0) {
        fixture->width = 24;
        fixture->height = 24;
    } else if (strcmp(row->sample_id, "mixed-aa-representative") == 0) {
        fixture->width = 20;
        fixture->height = 16;
    } else {
        fixture->width = 8;
        fixture->height = 8;
    }
    fixture->rgba = (uint8_t *)calloc((size_t)fixture->width * fixture->height * 4U, 1U);
    if (fixture->rgba == NULL) {
        return false;
    }
    if (strcmp(row->sample_id, "sq9-aa-triangle") == 0) {
        for (uint32_t y = 0; y < fixture->height; y++) {
            uint32_t edge = y + ((y >= 7 && y <= 10) ? 1U : 0U);
            for (uint32_t x = 0; x < fixture->width; x++) {
                uint8_t alpha = x >= edge + 3U ? 255U : (x >= edge ? (uint8_t)(64U + ((x - edge) * 64U)) : 0U);
                set_pixel(fixture, x, y, alpha);
            }
        }
    } else if (strcmp(row->sample_id, "opaque-square-max3") == 0) {
        for (uint32_t y = 0; y < fixture->height; y++) {
            for (uint32_t x = 0; x < fixture->width; x++) {
                set_pixel(fixture, x, y, 255);
            }
        }
    } else if (strcmp(row->sample_id, "connected-mask-adversarial") == 0) {
        static const Point2D source[] = {
            {15, 6},  {15, 7},  {16, 7},  {16, 6},  {17, 6},  {17, 7},  {19, 7},  {19, 6},  {20, 6},  {20, 7},  {21, 7},  {21, 8}, {19, 8}, {19, 9}, {22, 9}, {22, 7},  {23, 7}, {23, 10}, {18, 10},
            {18, 11}, {17, 11}, {17, 12}, {16, 12}, {16, 13}, {15, 13}, {15, 14}, {12, 14}, {12, 15}, {11, 15}, {11, 13}, {9, 13}, {9, 12}, {7, 12}, {7, 10}, {10, 10}, {10, 9}, {11, 9},  {11, 6},
        };
        for (uint32_t y = 0; y < fixture->height; y++) {
            for (uint32_t x = 0; x < fixture->width; x++) {
                bool inside = point_in_polygon_f(source, (uint32_t)(sizeof(source) / sizeof(source[0])), (double)x + 0.5, (double)y + 0.5);
                set_pixel(fixture, x, y, inside ? 255U : 0U);
            }
        }
    } else if (strcmp(row->sample_id, "mixed-aa-representative") == 0) {
        for (uint32_t y = 0; y < fixture->height; y++) {
            for (uint32_t x = 0; x < fixture->width; x++) {
                const double dx = ((double)x + 0.5) - 9.0;
                const double dy = ((double)y + 0.5) - 7.5;
                double distance = sqrt((dx * dx) / 64.0 + (dy * dy) / 36.0);
                uint8_t alpha = distance < 0.82 ? 255U : (distance < 0.94 ? 160U : (distance < 1.04 ? 64U : 0U));
                if (x > 9U && x < 14U && y < 6U) {
                    alpha = 0U;
                }
                set_pixel(fixture, x, y, alpha);
            }
        }
    } else {
        for (uint32_t y = 0; y < fixture->height; y++) {
            for (uint32_t x = 0; x < fixture->width; x++) {
                set_pixel(fixture, x, y, (x >= 2U && x <= 5U && y >= 2U && y <= 5U) ? 255U : 64U);
            }
        }
    }
    return true;
}

typedef struct {
    bool ok;
    bool hull_infeasible;
} PanelPackResult;

static void panel_remove_pack(const char *path) {
    char header[VISUAL_PATH_MAX];
    (void)snprintf(header, sizeof(header), "%s", path);
    char *extension = strstr(header, ".ntpack");
    if (extension != NULL && extension[7] == '\0') {
        (void)snprintf(extension, 8U, ".h");
        (void)remove(header);
    }
    (void)remove(path);
}

static PanelPackResult panel_write_pack(const VisualRow *row, const VisualFixture *fixture, const char *path, float percent, uint8_t max_vertices) {
    PanelPackResult result = {0};
    NtBuilderContext *context = nt_builder_start_pack(path);
    if (context == NULL) {
        return result;
    }
    nt_atlas_opts_t opts = nt_atlas_opts_defaults();
    opts.shape = row->shape;
    opts.alpha_threshold = strcmp(row->sample_id, "pixel-art-threshold-control") == 0 ? 1U : row->threshold;
    opts.max_vertices = max_vertices;
    opts.max_added_area_percent = percent;
    opts.allow_transform = false;
    opts.power_of_two = false;
    opts.gen_mipmaps = false;
    opts.filter_min = NT_TEXTURE_DEFAULT_FILTER_NEAREST;
    opts.filter_mag = NT_TEXTURE_DEFAULT_FILTER_NEAREST;
    NtAtlasBuild *atlas = nt_atlas_begin(context, "visual", &opts);
    nt_atlas_sprite_opts_t sprite = nt_atlas_sprite_opts_defaults();
    sprite.name = "fixture";
    if (strcmp(row->sample_id, "pixel-art-threshold-control") == 0) {
        sprite.alpha_threshold = row->threshold;
    }
    nt_atlas_add_raw(atlas, fixture->rgba, fixture->width, fixture->height, &sprite);
    const nt_build_result_t commit = nt_atlas_commit(atlas);
    const nt_build_result_t finish = nt_builder_finish_pack(context);
    uint32_t error_count = 0U;
    const nt_build_error_t *errors = nt_builder_get_errors(context, &error_count);
    result.ok = commit == NT_BUILD_OK && finish == NT_BUILD_OK;
    result.hull_infeasible = commit == NT_BUILD_ERR_VALIDATION && finish == NT_BUILD_ERR_VALIDATION && error_count == 1U && errors[0].kind == NT_BUILD_ERR_KIND_ATLAS_HULL_INFEASIBLE &&
                             errors[0].detail_a == max_vertices && errors[0].detail_b == (uint32_t)row->shape;
    nt_builder_free_pack(context);
    return result;
}

static bool panel_build(const VisualRow *row, const VisualColumn *column, const VisualFixture *fixture, const char *out_dir, uint32_t ordinal, VisualPanel *panel) {
    uint8_t *alpha = alpha_plane_extract(fixture->rgba, fixture->width, fixture->height);
    if (alpha == NULL || !alpha_trim(alpha, fixture->width, fixture->height, row->threshold, &panel->trim_x, &panel->trim_y, &panel->trim_w, &panel->trim_h)) {
        free(alpha);
        return false;
    }
    panel->mask = (uint8_t *)calloc((size_t)panel->trim_w * panel->trim_h, 1U);
    panel->alpha_values = (uint8_t *)calloc((size_t)panel->trim_w * panel->trim_h, 1U);
    if (panel->mask == NULL || panel->alpha_values == NULL) {
        free(panel->mask);
        free(panel->alpha_values);
        panel->mask = NULL;
        panel->alpha_values = NULL;
        free(alpha);
        return false;
    }
    for (uint32_t y = 0; y < panel->trim_h; y++) {
        for (uint32_t x = 0; x < panel->trim_w; x++) {
            const uint8_t value = alpha[((y + panel->trim_y) * fixture->width) + x + panel->trim_x];
            uint8_t retained = value >= row->threshold ? 1U : 0U;
            panel->alpha_values[(y * panel->trim_w) + x] = value;
            panel->mask[(y * panel->trim_w) + x] = retained;
            panel->retained_pixels += retained;
        }
    }
    free(alpha);

    char base_path[VISUAL_PATH_MAX];
    char pack_path[VISUAL_PATH_MAX];
    (void)snprintf(base_path, sizeof(base_path), "%s/panel-%02u-base.ntpack", out_dir, ordinal);
    (void)snprintf(pack_path, sizeof(pack_path), "%s/panel-%02u-selected.ntpack", out_dir, ordinal);
    panel_remove_pack(base_path);
    panel_remove_pack(pack_path);
    const PanelPackResult baseline_pack = panel_write_pack(row, fixture, base_path, 0.0F, row->budget);
    const PanelPackResult selected_pack = panel_write_pack(row, fixture, pack_path, (float)column->percent, row->budget);
    panel->commit_ok = baseline_pack.ok && selected_pack.ok;
    if (row_expects_hull_infeasible(row)) {
        panel->hull_infeasible = baseline_pack.hull_infeasible && selected_pack.hull_infeasible;
        panel_remove_pack(base_path);
        panel_remove_pack(pack_path);
        panel->allowance_ok = panel->hull_infeasible;
        panel->result = panel->hull_infeasible;
        return true;
    }
    nt_bench_selected_geometry_t baseline = {0};
    nt_bench_selected_geometry_t selected = {0};
    if (!panel->commit_ok || nt_bench_parse_selected_geometry(base_path, 0U, panel->trim_h, &baseline) != 0 || nt_bench_parse_selected_geometry(pack_path, 0U, panel->trim_h, &selected) != 0 ||
        nt_bench_file_sha256_hex(base_path, panel->baseline_pack_sha256) != 0 || nt_bench_file_sha256_hex(pack_path, panel->selected_pack_sha256) != 0) {
        nt_bench_selected_geometry_destroy(&selected);
        nt_bench_selected_geometry_destroy(&baseline);
        panel_remove_pack(base_path);
        panel_remove_pack(pack_path);
        return false;
    }
    panel->baseline_vertex_count = baseline.vertex_count;
    panel->vertex_count = selected.vertex_count;
    memcpy(panel->baseline_polygon, baseline.polygon, (size_t)baseline.vertex_count * sizeof(Point2D));
    memcpy(panel->polygon, selected.polygon, (size_t)selected.vertex_count * sizeof(Point2D));
    memcpy(panel->baseline_indices, baseline.triangle_indices, (size_t)baseline.triangle_index_count * sizeof(uint16_t));
    memcpy(panel->selected_indices, selected.triangle_indices, (size_t)selected.triangle_index_count * sizeof(uint16_t));
    const uint64_t opaque_area2 = (uint64_t)panel->retained_pixels * 2U;
    const uint64_t base_area2 = polygon_abs_twice_area(baseline.polygon, baseline.vertex_count);
    const uint64_t selected_area2 = polygon_abs_twice_area(selected.polygon, selected.vertex_count);
    panel->proof = nt_selected_geometry_validate(panel->mask, panel->trim_w, panel->trim_h, opaque_area2, baseline.polygon, baseline.vertex_count, base_area2, baseline.triangle_indices,
                                                 baseline.triangle_index_count, selected.polygon, selected.vertex_count, selected_area2, selected.triangle_indices, selected.triangle_index_count,
                                                 (float)column->percent, row->budget);
    const nt_polygon_feasibility_t feasibility = nt_polygon_feasibility(selected.polygon, selected.vertex_count, panel->mask, panel->trim_w, panel->trim_h, row->budget);
    panel->lost_area2 = feasibility.lost_area2;
    nt_bench_selected_geometry_destroy(&selected);
    nt_bench_selected_geometry_destroy(&baseline);
    panel_remove_pack(base_path);
    panel_remove_pack(pack_path);

    panel->validity = polygon_validate(panel->polygon, panel->vertex_count);
    panel->coverage = polygon_coverage_metrics(panel->polygon, panel->vertex_count, panel->mask, panel->trim_w, panel->trim_h);
    panel->signed_twice_area = signed_twice_area(panel->polygon, panel->vertex_count);
    panel->triangles_valid = panel->proof.selected_triangulation_valid;
    panel->allowance_ok = panel->proof.allowance_valid;
    panel->connected_frontier_valid = true;
    if (strcmp(row->sample_id, "connected-mask-adversarial") == 0) {
        const size_t mask_size = (size_t)fixture->width * fixture->height;
        uint8_t *frontier_mask = (uint8_t *)malloc(mask_size);
        if (frontier_mask == NULL) {
            return false;
        }
        for (size_t pixel = 0; pixel < mask_size; pixel++) {
            frontier_mask[pixel] = fixture->rgba[(pixel * 4U) + 3U] >= row->threshold ? 1U : 0U;
        }
        const uint32_t slot_mask = nt_atlas_test_concave_frontier_slot_mask(frontier_mask, fixture->width, fixture->height, row->budget);
        free(frontier_mask);
        for (uint32_t slot = 0; slot < 3U; slot++) {
            const uint32_t count = slot + 6U;
            const bool present = (slot_mask & (1U << count)) != 0U;
            panel->connected_frontier_counts[slot] = present ? count : 0U;
            panel->connected_frontier_valid = panel->connected_frontier_valid && present;
        }
    }
    panel->result = panel->commit_ok && panel->proof.valid && panel->connected_frontier_valid;
    return true;
}

static void write_mask_json(FILE *file, const VisualPanel *panel) {
    (void)fputc('[', file);
    for (uint32_t y = 0; y < panel->trim_h; y++) {
        if (y > 0) {
            (void)fputc(',', file);
        }
        (void)fputc('"', file);
        for (uint32_t x = 0; x < panel->trim_w; x++) {
            (void)fputc(panel->mask[(y * panel->trim_w) + x] != 0U ? '#' : '.', file);
        }
        (void)fputc('"', file);
    }
    (void)fputc(']', file);
}

static void write_alpha_json(FILE *file, const VisualPanel *panel) {
    (void)fputc('[', file);
    for (uint32_t y = 0; y < panel->trim_h; y++) {
        if (y > 0) {
            (void)fputc(',', file);
        }
        (void)fputc('[', file);
        for (uint32_t x = 0; x < panel->trim_w; x++) {
            (void)fprintf(file, "%s%u", x == 0 ? "" : ",", panel->alpha_values[(y * panel->trim_w) + x]);
        }
        (void)fputc(']', file);
    }
    (void)fputc(']', file);
}

static void write_points_json(FILE *file, const Point2D *polygon, uint32_t count, bool numbered) {
    (void)fputc('[', file);
    for (uint32_t i = 0; i < count; i++) {
        if (i > 0) {
            (void)fputc(',', file);
        }
        if (numbered) {
            (void)fprintf(file, "{\"n\":%u,\"x\":%d,\"y\":%d}", i + 1U, polygon[i].x, polygon[i].y);
        } else {
            (void)fprintf(file, "{\"x\":%d,\"y\":%d}", polygon[i].x, polygon[i].y);
        }
    }
    (void)fputc(']', file);
}

static void write_manifest_panel(FILE *file, const VisualRow *row, const VisualColumn *column, const VisualPanel *panel, bool first) {
    const double denominator = panel->proof.opaque_area2 > 0U ? (double)panel->proof.opaque_area2 : 1.0;
    const double base_overdraw = ((double)panel->proof.base_area2 - (double)panel->proof.opaque_area2) * 100.0 / denominator;
    const double added_area = (double)panel->proof.added_area2 * 100.0 / denominator;
    const double total_overdraw = ((double)panel->proof.selected_area2 - (double)panel->proof.opaque_area2) * 100.0 / denominator;
    (void)fprintf(file, "%s{\"panel_id\":\"%s:%s:%s\",\"row_id\":\"%s:%s\",\"sample_id\":\"%s\",\"shape\":\"%s\",\"column_id\":\"%s\",", first ? "" : ",", row->sample_id, row->shape_name, column->id,
                  row->sample_id, row->shape_name, row->sample_id, row->shape_name, column->id);
    (void)fprintf(file,
                  "\"max_added_area_percent\":%.3f,\"effective_alpha_threshold\":%u,\"threshold_source\":\"%s\","
                  "\"trim\":{\"x\":%u,\"y\":%u,\"w\":%u,\"h\":%u},\"original_alpha_values\":",
                  column->percent, row->threshold, strcmp(row->sample_id, "pixel-art-threshold-control") == 0 ? "per-sprite" : "atlas", panel->trim_x, panel->trim_y, panel->trim_w, panel->trim_h);
    write_alpha_json(file, panel);
    (void)fputs(",\"effective_alpha_mask\":", file);
    write_mask_json(file, panel);
    (void)fputs(",\"baseline_polygon\":", file);
    write_points_json(file, panel->baseline_polygon, panel->baseline_vertex_count, false);
    (void)fputs(",\"polygon\":", file);
    write_points_json(file, panel->polygon, panel->vertex_count, false);
    (void)fputs(",\"numbered_vertices\":", file);
    write_points_json(file, panel->polygon, panel->vertex_count, true);
    (void)fprintf(
        file,
        ",\"vertex_count\":%u,\"baseline_vertex_count\":%u,\"opaque_area2\":%llu,\"base_area2\":%llu,\"selected_area2\":%llu,\"added_area2\":%llu,\"exact_lost_area2\":%llu,"
        "\"base_overdraw_percent\":%.8f,\"added_area_percent\":%.8f,\"total_overdraw_percent\":%.8f,"
        "\"baseline_pack_sha256\":\"%s\",\"selected_pack_sha256\":\"%s\","
        "\"connected_frontier_counts\":[%u,%u,%u],\"connected_frontier_valid\":%s,"
        "\"self_intersection\":%s,\"signed_area\":%.1f,\"winding_valid\":%s,\"vertex_budget\":%u,"
        "\"expected_hull_infeasible\":%s,\"production_gates\":{\"commit\":%s,\"full_cell_coverage\":%s,\"simple_polygon\":%s,\"allowance\":%s,\"budget\":%s,\"triangles\":%s},\"result\":\"%s\"}",
        panel->vertex_count, panel->baseline_vertex_count, (unsigned long long)panel->proof.opaque_area2, (unsigned long long)panel->proof.base_area2, (unsigned long long)panel->proof.selected_area2,
        (unsigned long long)panel->proof.added_area2, (unsigned long long)panel->lost_area2, base_overdraw, added_area, total_overdraw, panel->baseline_pack_sha256, panel->selected_pack_sha256,
        panel->connected_frontier_counts[0], panel->connected_frontier_counts[1], panel->connected_frontier_counts[2], panel->connected_frontier_valid ? "true" : "false",
        panel->validity == NT_POLYGON_INVALID_SELF_INTERSECTION ? "true" : "false", (double)panel->signed_twice_area * 0.5, panel->signed_twice_area > 0 ? "true" : "false", row->budget,
        panel->hull_infeasible ? "true" : "false", (panel->commit_ok || panel->hull_infeasible) ? "true" : "false", (panel->proof.selected_coverage_valid || panel->hull_infeasible) ? "true" : "false",
        (panel->proof.selected_topology_valid || panel->hull_infeasible) ? "true" : "false", panel->allowance_ok ? "true" : "false",
        (panel->vertex_count <= row->budget || panel->hull_infeasible) ? "true" : "false", (panel->triangles_valid || panel->hull_infeasible) ? "true" : "false", panel->result ? "PASS" : "FAIL");
}

static bool write_manifest(const char *path, const VisualColumn columns[VISUAL_COLUMN_COUNT], const VisualPanel panels[VISUAL_ROW_COUNT][VISUAL_COLUMN_COUNT]) {
    FILE *file = fopen(path, "wb");
    if (file == NULL) {
        return false;
    }
    bool overall = true;
    for (uint32_t row = 0; row < VISUAL_ROW_COUNT; row++) {
        for (uint32_t column = 0; column < VISUAL_COLUMN_COUNT; column++) {
            overall = overall && panels[row][column].result;
        }
    }
    (void)fprintf(file, "{\n  \"schema_version\": %d,\n  \"overall_pass\": %s,\n  \"columns\": [", VISUAL_SCHEMA_VERSION, overall ? "true" : "false");
    for (uint32_t i = 0; i < VISUAL_COLUMN_COUNT; i++) {
        (void)fprintf(file, "%s{\"column_id\":\"%s\",\"max_added_area_percent\":%.3f,\"sweep_source\":", i == 0 ? "" : ",", columns[i].id, columns[i].percent);
        json_escape(file, columns[i].source);
        (void)fputs(",\"sweep_sha256\":", file);
        json_escape(file, columns[i].sha256);
        (void)fputs(",\"measurement_source_commit\":", file);
        json_escape(file, columns[i].source_commit);
        (void)fputc('}', file);
    }
    (void)fputs("],\n  \"rows\": [", file);
    for (uint32_t row = 0; row < VISUAL_ROW_COUNT; row++) {
        bool row_pass = true;
        for (uint32_t column = 0; column < VISUAL_COLUMN_COUNT; column++) {
            row_pass = row_pass && panels[row][column].result;
        }
        (void)fprintf(file, "%s{\"sample_id\":\"%s\",\"shape\":\"%s\",\"row_id\":\"%s:%s\",\"result\":\"%s\",\"panels\":[", row == 0 ? "" : ",", VISUAL_ROWS[row].sample_id,
                      VISUAL_ROWS[row].shape_name, VISUAL_ROWS[row].sample_id, VISUAL_ROWS[row].shape_name, row_pass ? "PASS" : "FAIL");
        for (uint32_t column = 0; column < VISUAL_COLUMN_COUNT; column++) {
            write_manifest_panel(file, &VISUAL_ROWS[row], &columns[column], &panels[row][column], column == 0);
        }
        (void)fputs("]}", file);
    }
    (void)fputs("],\n  \"failing_panel_ids\": [", file);
    bool first = true;
    for (uint32_t row = 0; row < VISUAL_ROW_COUNT; row++) {
        for (uint32_t column = 0; column < VISUAL_COLUMN_COUNT; column++) {
            if (!panels[row][column].result) {
                (void)fprintf(file, "%s\"%s:%s:%s\"", first ? "" : ",", VISUAL_ROWS[row].sample_id, VISUAL_ROWS[row].shape_name, columns[column].id);
                first = false;
            }
        }
    }
    (void)fputs("]\n}\n", file);
    return fclose(file) == 0;
}

static void write_svg(FILE *file, const VisualPanel *panel) {
    const double scale = 180.0 / (double)(panel->trim_w > panel->trim_h ? panel->trim_w : panel->trim_h);
    (void)fprintf(file, "<svg viewBox=\"-1 -1 %u %u\" role=\"img\" aria-label=\"effective alpha mask and emitted polygon\">", panel->trim_w + 2U, panel->trim_h + 2U);
    for (uint32_t y = 0; y < panel->trim_h; y++) {
        for (uint32_t x = 0; x < panel->trim_w; x++) {
            const uint8_t alpha = panel->alpha_values[(y * panel->trim_w) + x];
            if (alpha != 0U) {
                (void)fprintf(file, "<rect class=\"alpha\" x=\"%u\" y=\"%u\" width=\"1\" height=\"1\" fill-opacity=\"%.3f\"/>", x, y, (double)alpha / 255.0);
            }
            if (panel->mask[(y * panel->trim_w) + x] != 0U) {
                (void)fprintf(file, "<rect class=\"mask\" x=\"%u\" y=\"%u\" width=\"1\" height=\"1\"/>", x, y);
            }
        }
    }
    (void)fputs("<polygon points=\"", file);
    for (uint32_t i = 0; i < panel->vertex_count; i++) {
        (void)fprintf(file, "%d,%d ", panel->polygon[i].x, panel->polygon[i].y);
    }
    (void)fputs("\"/>", file);
    for (uint32_t i = 0; i < panel->vertex_count; i++) {
        (void)fprintf(file, "<circle cx=\"%d\" cy=\"%d\" r=\"%.3f\"/><text x=\"%d\" y=\"%d\">%u</text>", panel->polygon[i].x, panel->polygon[i].y, 2.2 / scale, panel->polygon[i].x,
                      panel->polygon[i].y, i + 1U);
    }
    (void)fputs("</svg>", file);
}

static bool write_html(const char *path, const VisualColumn columns[VISUAL_COLUMN_COUNT], const VisualPanel panels[VISUAL_ROW_COUNT][VISUAL_COLUMN_COUNT]) {
    FILE *file = fopen(path, "wb");
    if (file == NULL) {
        return false;
    }
    (void)fputs("<!doctype html><html lang=\"en\"><head><meta charset=\"utf-8\"><meta name=\"viewport\" content=\"width=device-width,initial-scale=1\"><title>Phase 80 Hull Visual Acceptance</title>",
                file);
    (void)fputs("<style>:root{color-scheme:dark;--bg:#09121d;--panel:#122235;--ink:#e8f3ff;--muted:#9eb4c9;--cyan:#38d9ff;--green:#53df89}*{box-sizing:border-box}body{margin:0;background:var(--bg);"
                "color:var(--ink);font:14px system-ui,sans-serif}main{max-width:1500px;margin:auto;padding:24px}h1{font-size:clamp(24px,4vw,44px);margin:0 0 8px}.lede{color:var(--muted);margin:0 0 "
                "28px}.row{margin:30px 0}.grid{display:grid;grid-template-columns:repeat(3,minmax(250px,1fr));gap:14px}.panel{background:var(--panel);border:1px solid "
                "#29445f;border-radius:10px;padding:14px}.badge{color:var(--green);font-weight:800}svg{width:100%;height:220px;background:#08101a;margin:10px "
                "0}.alpha{fill:#3c718e}.mask{fill:none;stroke:#53df89;stroke-width:.06}polygon{fill:#38d9ff22;stroke:var(--cyan);stroke-width:.18}circle{fill:#fff}text{fill:#fff;font-size:.8px}table{"
                "width:100%;border-collapse:collapse}td{padding:"
                "3px;border-bottom:1px solid #29445f}td:last-child{text-align:right}@media(max-width:900px){.grid{grid-template-columns:1fr}}</style></head><body><main>",
                file);
    (void)fputs("<h1>Phase 80 Hull Visual Acceptance</h1><p class=\"lede\">Serialized production atlas geometry. Columns are pinned to measured sweep evidence.</p>", file);
    for (uint32_t row = 0; row < VISUAL_ROW_COUNT; row++) {
        (void)fprintf(file, "<section class=\"row\" id=\"row-%s-%s\"><h2>", VISUAL_ROWS[row].sample_id, VISUAL_ROWS[row].shape_name);
        html_escape(file, VISUAL_ROWS[row].sample_id);
        (void)fputs(" · ", file);
        html_escape(file, VISUAL_ROWS[row].shape_name);
        (void)fputs("</h2><div class=\"grid\">", file);
        for (uint32_t column = 0; column < VISUAL_COLUMN_COUNT; column++) {
            const VisualPanel *panel = &panels[row][column];
            const double denominator = panel->proof.opaque_area2 > 0U ? (double)panel->proof.opaque_area2 : 1.0;
            const double base_overdraw = ((double)panel->proof.base_area2 - (double)panel->proof.opaque_area2) * 100.0 / denominator;
            const double added_area = (double)panel->proof.added_area2 * 100.0 / denominator;
            const double total_overdraw = ((double)panel->proof.selected_area2 - (double)panel->proof.opaque_area2) * 100.0 / denominator;
            (void)fprintf(file, "<article class=\"panel\" id=\"panel-%s-%s-%s\"><h3>%s · %.1f%% added-area limit</h3><span class=\"badge\">%s</span>", VISUAL_ROWS[row].sample_id,
                          VISUAL_ROWS[row].shape_name, columns[column].id, columns[column].id, columns[column].percent, panel->result ? "PASS" : "FAIL");
            write_svg(file, panel);
            if (panel->hull_infeasible) {
                (void)fputs("<p>Expected HULL_INFEASIBLE: no covering polygon fits the hard vertex ceiling.</p>", file);
            }
            (void)fprintf(file,
                          "<table><tr><td>Vertex count / budget</td><td>%u / %u</td></tr><tr><td>Opaque / baseline / selected area</td><td>%.1f / %.1f / %.1f</td></tr>"
                          "<tr><td>Base / added / total overdraw</td><td>%.3f%% / %.3f%% / %.3f%%</td></tr><tr><td>Exact lost area</td><td>%.1f</td></tr>"
                          "<tr><td>Self intersection</td><td>%s</td></tr><tr><td>Signed area / winding</td><td>%.1f / %s</td></tr><tr><td>Triangles "
                          "valid</td><td>%s</td></tr></table></article>",
                          panel->vertex_count, VISUAL_ROWS[row].budget, (double)panel->proof.opaque_area2 * 0.5, (double)panel->proof.base_area2 * 0.5, (double)panel->proof.selected_area2 * 0.5,
                          base_overdraw, added_area, total_overdraw, (double)panel->lost_area2 * 0.5, panel->validity == NT_POLYGON_INVALID_SELF_INTERSECTION ? "yes" : "no",
                          (double)panel->signed_twice_area * 0.5, panel->signed_twice_area > 0 ? "valid y-down" : "invalid", panel->triangles_valid ? "yes" : "no");
        }
        (void)fputs("</div></section>", file);
    }
    (void)fputs("</main></body></html>\n", file);
    return fclose(file) == 0;
}

int nt_hull_visual_generate(const char *corpus_path, const char *frontier_path, const char *out_dir) {
    VisualColumn columns[VISUAL_COLUMN_COUNT] = {0};
    VisualPanel panels[VISUAL_ROW_COUNT][VISUAL_COLUMN_COUNT] = {0};
    if (!validate_corpus(corpus_path) || !load_frontier(frontier_path, columns) || !make_parent_dir(out_dir)) {
        (void)fprintf(stderr, "atlas_hull_visual_report: invalid corpus/frontier/output\n");
        return 1;
    }
    bool ok = true;
    for (uint32_t row = 0; ok && row < VISUAL_ROW_COUNT; row++) {
        VisualFixture fixture = {0};
        ok = fixture_create(&VISUAL_ROWS[row], &fixture);
        for (uint32_t column = 0; ok && column < VISUAL_COLUMN_COUNT; column++) {
            ok = panel_build(&VISUAL_ROWS[row], &columns[column], &fixture, out_dir, (row * VISUAL_COLUMN_COUNT) + column, &panels[row][column]);
        }
        free(fixture.rgba);
    }
    char manifest_path[VISUAL_PATH_MAX];
    char html_path[VISUAL_PATH_MAX];
    (void)snprintf(manifest_path, sizeof(manifest_path), "%s/manifest.json", out_dir);
    (void)snprintf(html_path, sizeof(html_path), "%s/index.html", out_dir);
    ok = ok && write_manifest(manifest_path, columns, panels) && write_html(html_path, columns, panels);
    for (uint32_t row = 0; row < VISUAL_ROW_COUNT; row++) {
        for (uint32_t column = 0; column < VISUAL_COLUMN_COUNT; column++) {
            free(panels[row][column].mask);
            free(panels[row][column].alpha_values);
        }
    }
    if (!ok) {
        (void)fprintf(stderr, "atlas_hull_visual_report: generation failed\n");
        return 1;
    }
    return nt_hull_visual_validate(manifest_path, html_path, NULL);
}

static bool required_rows_present(const char *manifest, const char *html, const char *required_samples) {
    if (required_samples == NULL || required_samples[0] == '\0') {
        return true;
    }
    char copy[1024];
    if (strlen(required_samples) >= sizeof(copy)) {
        return false;
    }
    (void)snprintf(copy, sizeof(copy), "%s", required_samples);
    char *token = strtok(copy, ",");
    while (token != NULL) {
        char manifest_id[256];
        char html_id[256];
        (void)snprintf(manifest_id, sizeof(manifest_id), "\"row_id\":\"%s\"", token);
        (void)snprintf(html_id, sizeof(html_id), "id=\"row-%s\"", token);
        for (char *p = html_id; *p != '\0'; p++) {
            if (*p == ':') {
                *p = '-';
            }
        }
        if (strstr(manifest, manifest_id) == NULL || strstr(html, html_id) == NULL) {
            return false;
        }
        token = strtok(NULL, ",");
    }
    return true;
}

int nt_hull_visual_validate(const char *manifest_path, const char *html_path, const char *required_samples) {
    static const char *panel_fields[] = {
        "\"effective_alpha_mask\"", "\"original_alpha_values\"", "\"baseline_polygon\"", "\"polygon\"",          "\"numbered_vertices\"",     "\"vertex_count\"",       "\"opaque_area2\"",
        "\"base_area2\"",           "\"selected_area2\"",        "\"added_area2\"",      "\"exact_lost_area2\"", "\"base_overdraw_percent\"", "\"added_area_percent\"", "\"total_overdraw_percent\"",
        "\"self_intersection\"",    "\"signed_area\"",           "\"winding_valid\"",    "\"vertex_budget\"",    "\"production_gates\"",
    };
    size_t manifest_size = 0;
    size_t html_size = 0;
    uint8_t *manifest_bytes = read_file(manifest_path, &manifest_size);
    uint8_t *html_bytes = read_file(html_path, &html_size);
    if (manifest_bytes == NULL || html_bytes == NULL || manifest_size == 0 || html_size == 0) {
        free(manifest_bytes);
        free(html_bytes);
        (void)fprintf(stderr, "atlas_hull_visual_report: missing report artifact\n");
        return 1;
    }
    const char *manifest = (const char *)manifest_bytes;
    const char *html = (const char *)html_bytes;
    const char *baseline = strstr(manifest, "\"column_id\":\"baseline\"");
    const char *candidate = strstr(manifest, "\"column_id\":\"candidate\"");
    const char *recommended = strstr(manifest, "\"column_id\":\"recommended\"");
    bool ok = strstr(manifest, "\"schema_version\": 2") != NULL && strstr(manifest, "\"overall_pass\": true") != NULL && strstr(manifest, "\"result\":\"FAIL\"") == NULL &&
              strstr(manifest, "\"failing_panel_ids\": []") != NULL && count_substring(manifest, "\"panel_id\"") == VISUAL_ROW_COUNT * VISUAL_COLUMN_COUNT &&
              count_substring(manifest, "\"sweep_source\"") == VISUAL_COLUMN_COUNT && count_substring(manifest, "\"sweep_sha256\"") == VISUAL_COLUMN_COUNT &&
              count_substring(manifest, "\"measurement_source_commit\"") == VISUAL_COLUMN_COUNT && strstr(manifest, "\"sweep_source\":\"\"") == NULL &&
              strstr(manifest, "\"sweep_sha256\":\"\"") == NULL && baseline != NULL && candidate != NULL && recommended != NULL && baseline < candidate && candidate < recommended &&
              strstr(html, "<!doctype html>") != NULL && strstr(html, "Vertex count / budget") != NULL && strstr(html, "Base / added / total overdraw") != NULL &&
              strstr(html, "Exact lost area") != NULL && strstr(html, "<script") == NULL && strstr(html, "http://") == NULL && strstr(html, "https://") == NULL &&
              required_rows_present(manifest, html, required_samples);
    for (uint32_t field = 0; ok && field < (uint32_t)(sizeof(panel_fields) / sizeof(panel_fields[0])); field++) {
        ok = count_substring(manifest, panel_fields[field]) == VISUAL_ROW_COUNT * VISUAL_COLUMN_COUNT;
    }
    free(manifest_bytes);
    free(html_bytes);
    if (!ok) {
        (void)fprintf(stderr, "atlas_hull_visual_report: validation failed closed\n");
        return 1;
    }
    return 0;
}
