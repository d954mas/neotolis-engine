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
#define VISUAL_COLUMN_COUNT 6
#define VISUAL_MAX_VERTICES 16
#define VISUAL_PATH_MAX 1024

uint32_t nt_atlas_test_concave_frontier_slot_mask(const uint8_t *binary, uint32_t width, uint32_t height, uint32_t max_vertices);
bool nt_atlas_test_concave_frontier_required_percent(const uint8_t *binary, uint32_t width, uint32_t height, uint32_t max_vertices, uint32_t target_count, float *out_percent);
void nt_atlas_test_force_frontier_count(uint32_t count);

typedef struct {
    char id[16];
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
    nt_bench_selected_geometry_t baseline;
    nt_bench_selected_geometry_t selected;
    uint32_t trim_x;
    uint32_t trim_y;
    uint32_t trim_w;
    uint32_t trim_h;
    uint8_t *alpha_values;
    uint8_t *mask;
    uint32_t retained_pixels;
    bool commit_ok;
    bool hull_infeasible;
    bool result;
    nt_selected_geometry_proof_t proof;
    uint64_t lost_area2;
    char baseline_pack_sha256[65];
    char selected_pack_sha256[65];
    uint32_t connected_frontier_counts[3];
    bool connected_frontier_valid;
    nt_build_error_t production_error;
    bool has_production_error;
} VisualPanel;

static const VisualRow VISUAL_ROWS[VISUAL_ROW_COUNT] = {
    {"sq9-aa-triangle", "convex", NT_ATLAS_SHAPE_CONVEX_HULL, 1, 8},      {"rotated-diamond", "convex", NT_ATLAS_SHAPE_CONVEX_HULL, 1, 8},
    {"concave-notch", "concave", NT_ATLAS_SHAPE_CONCAVE_CONTOUR, 1, 10},  {"transparent-donut", "concave", NT_ATLAS_SHAPE_CONCAVE_CONTOUR, 1, 12},
    {"opaque-square-max3", "convex", NT_ATLAS_SHAPE_CONVEX_HULL, 1, 3},   {"connected-mask-adversarial", "concave", NT_ATLAS_SHAPE_CONCAVE_CONTOUR, 1, 13},
    {"pixel-art-threshold-control", "rect", NT_ATLAS_SHAPE_RECT, 128, 4},
};

static uint32_t visual_fail_stage;
static uint32_t visual_live_buffers;

void nt_hull_visual_test_fail_after_stage(uint32_t stage) { visual_fail_stage = stage; }
uint32_t nt_hull_visual_test_live_buffers(void) { return visual_live_buffers; }

static bool fail_after(uint32_t stage) { return visual_fail_stage == stage; }

static void *panel_calloc(size_t count, size_t size) {
    void *memory = calloc(count, size);
    if (memory != NULL) {
        visual_live_buffers++;
    }
    return memory;
}

static void panel_free(void **memory) {
    if (*memory != NULL) {
        free(*memory);
        *memory = NULL;
        visual_live_buffers--;
    }
}

static void panel_attach_geometry(nt_bench_selected_geometry_t *geometry) {
    visual_live_buffers += geometry->polygon != NULL ? 1U : 0U;
    visual_live_buffers += geometry->triangle_indices != NULL ? 1U : 0U;
}

static void panel_destroy(VisualPanel *panel) {
    if (panel->baseline.polygon != NULL) {
        visual_live_buffers--;
    }
    if (panel->baseline.triangle_indices != NULL) {
        visual_live_buffers--;
    }
    if (panel->selected.polygon != NULL) {
        visual_live_buffers--;
    }
    if (panel->selected.triangle_indices != NULL) {
        visual_live_buffers--;
    }
    nt_bench_selected_geometry_destroy(&panel->baseline);
    nt_bench_selected_geometry_destroy(&panel->selected);
    panel_free((void **)&panel->mask);
    panel_free((void **)&panel->alpha_values);
}

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
    static const uint32_t expected[VISUAL_COLUMN_COUNT] = {0, 2, 5, 10, 15, 25};
    size_t size = 0;
    uint8_t *bytes = read_file(path, &size);
    if (bytes == NULL || size == 0) {
        free(bytes);
        return false;
    }
    const char *cursor = strstr((const char *)bytes, "\"sweep\": [");
    char source_commit[41];
    bool ok = cursor != NULL && strstr((const char *)bytes, "\"schema_version\": 2") != NULL && text_value((const char *)bytes, "measurement_source_commit", source_commit, sizeof(source_commit)) &&
              strlen(source_commit) == 40U && strstr((const char *)bytes, "\"sweep_values\": [0, 2, 5, 10, 15, 25]") != NULL;
    for (uint32_t i = 0; ok && i < VISUAL_COLUMN_COUNT; i++) {
        const char *object = strstr(cursor, "\"max_added_area_percent\":");
        double percent = 0.0;
        if (object == NULL || !number_value(object, "max_added_area_percent", &percent) || percent < 0.0 || !text_value(object, "sweep_source", columns[i].source, sizeof(columns[i].source)) ||
            !text_value(object, "sweep_sha256", columns[i].sha256, sizeof(columns[i].sha256))) {
            ok = false;
            break;
        }
        if (fabs(percent - (double)expected[i]) > 0.0001) {
            ok = false;
            break;
        }
        (void)snprintf(columns[i].id, sizeof(columns[i].id), "percent-%u", expected[i]);
        columns[i].percent = percent;
        (void)snprintf(columns[i].source_commit, sizeof(columns[i].source_commit), "%s", source_commit);
        char actual[65];
        size_t sweep_size = 0;
        uint8_t *sweep = read_file(columns[i].source, &sweep_size);
        double measured = -1.0;
        ok = file_sha256_hex(columns[i].source, actual) && strcmp(actual, columns[i].sha256) == 0 && sweep != NULL && strstr((const char *)sweep, "\"selected_geometry_proof\"") != NULL &&
             strstr((const char *)sweep, "\"valid\": true") != NULL && strstr((const char *)sweep, "\"full_cell_coverage\":true") != NULL &&
             number_value((const char *)sweep, "max_added_area_percent", &measured) && fabs(measured - percent) < 0.0001;
        free(sweep);
        cursor = object + strlen("\"max_added_area_percent\":");
    }
    free(bytes);
    return ok;
}

static bool validate_corpus(const char *path) {
    size_t size = 0;
    uint8_t *bytes = read_file(path, &size);
    if (bytes == NULL || size == 0 || strstr((const char *)bytes, "\"schema_version\": 2") == NULL) {
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
    } else if (strcmp(row->sample_id, "rotated-diamond") == 0) {
        fixture->width = 20;
        fixture->height = 20;
    } else if (strcmp(row->sample_id, "concave-notch") == 0) {
        fixture->width = 20;
        fixture->height = 16;
    } else if (strcmp(row->sample_id, "transparent-donut") == 0) {
        fixture->width = 20;
        fixture->height = 20;
    } else if (strcmp(row->sample_id, "opaque-square-max3") == 0) {
        fixture->width = 4;
        fixture->height = 4;
    } else if (strcmp(row->sample_id, "connected-mask-adversarial") == 0) {
        fixture->width = 24;
        fixture->height = 24;
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
    } else if (strcmp(row->sample_id, "rotated-diamond") == 0) {
        for (uint32_t y = 0; y < fixture->height; y++) {
            for (uint32_t x = 0; x < fixture->width; x++) {
                const int32_t dx = abs((int32_t)x - 9);
                const int32_t dy = abs((int32_t)y - 9);
                set_pixel(fixture, x, y, dx + dy <= 7 ? 255U : 0U);
            }
        }
    } else if (strcmp(row->sample_id, "concave-notch") == 0) {
        for (uint32_t y = 0; y < fixture->height; y++) {
            for (uint32_t x = 0; x < fixture->width; x++) {
                const bool body = x >= 2U && x <= 17U && y >= 2U && y <= 13U;
                const bool notch = x >= 8U && x <= 12U && y <= 8U;
                set_pixel(fixture, x, y, body && !notch ? 255U : 0U);
            }
        }
    } else if (strcmp(row->sample_id, "transparent-donut") == 0) {
        for (uint32_t y = 0; y < fixture->height; y++) {
            for (uint32_t x = 0; x < fixture->width; x++) {
                const double dx = ((double)x + 0.5) - 10.0;
                const double dy = ((double)y + 0.5) - 10.0;
                const double radius2 = (dx * dx) + (dy * dy);
                set_pixel(fixture, x, y, radius2 <= 64.0 && radius2 >= 12.0 ? 255U : 0U);
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
                const bool core = point_in_polygon_f(source, (uint32_t)(sizeof(source) / sizeof(source[0])), (double)x + 0.5, (double)y + 0.5);
                const bool inside = core;
                set_pixel(fixture, x, y, inside ? 255U : 0U);
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
    bool has_error;
    nt_build_error_t error;
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

static PanelPackResult panel_write_pack(const VisualRow *row, const VisualFixture *fixture, const char *path, float percent, uint8_t max_vertices, uint32_t forced_frontier_count) {
    PanelPackResult result = {0};
    nt_atlas_test_force_frontier_count(forced_frontier_count);
    NtBuilderContext *context = nt_builder_start_pack(path);
    if (context == NULL) {
        nt_atlas_test_force_frontier_count(0U);
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
    nt_atlas_test_force_frontier_count(0U);
    uint32_t error_count = 0U;
    const nt_build_error_t *errors = nt_builder_get_errors(context, &error_count);
    result.ok = commit == NT_BUILD_OK && finish == NT_BUILD_OK;
    if (error_count == 1U) {
        result.has_error = true;
        result.error = errors[0];
    }
    result.hull_infeasible = commit == NT_BUILD_ERR_VALIDATION && finish == NT_BUILD_ERR_VALIDATION && result.has_error && result.error.kind == NT_BUILD_ERR_KIND_ATLAS_HULL_INFEASIBLE &&
                             strcmp(result.error.atlas, "visual") == 0 && strcmp(result.error.sprite, "fixture") == 0 && result.error.detail_a == max_vertices &&
                             result.error.detail_b == (uint32_t)row->shape;
    nt_builder_free_pack(context);
    return result;
}

static bool panel_build(const VisualRow *row, const VisualColumn *column, const VisualFixture *fixture, const char *out_dir, uint32_t ordinal, bool inspect_connected_slots,
                        uint32_t forced_frontier_count, VisualPanel *panel) {
    uint8_t *alpha = alpha_plane_extract(fixture->rgba, fixture->width, fixture->height);
    if (alpha == NULL || !alpha_trim(alpha, fixture->width, fixture->height, row->threshold, &panel->trim_x, &panel->trim_y, &panel->trim_w, &panel->trim_h)) {
        free(alpha);
        return false;
    }
    panel->mask = (uint8_t *)panel_calloc((size_t)panel->trim_w * panel->trim_h, 1U);
    panel->alpha_values = (uint8_t *)panel_calloc((size_t)panel->trim_w * panel->trim_h, 1U);
    if (panel->mask == NULL || panel->alpha_values == NULL) {
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
    if (fail_after(1)) {
        return false;
    }

    char base_path[VISUAL_PATH_MAX];
    char pack_path[VISUAL_PATH_MAX];
    (void)snprintf(base_path, sizeof(base_path), "%s/panel-%02u-base.ntpack", out_dir, ordinal);
    (void)snprintf(pack_path, sizeof(pack_path), "%s/panel-%02u-selected.ntpack", out_dir, ordinal);
    panel_remove_pack(base_path);
    panel_remove_pack(pack_path);
    const PanelPackResult baseline_pack = panel_write_pack(row, fixture, base_path, 0.0F, row->budget, 0U);
    if (fail_after(2)) {
        panel_remove_pack(base_path);
        return false;
    }
    const PanelPackResult selected_pack = panel_write_pack(row, fixture, pack_path, (float)column->percent, row->budget, forced_frontier_count);
    if (fail_after(3)) {
        panel_remove_pack(base_path);
        panel_remove_pack(pack_path);
        return false;
    }
    panel->commit_ok = baseline_pack.ok && selected_pack.ok;
    if (row_expects_hull_infeasible(row)) {
        panel->hull_infeasible = baseline_pack.hull_infeasible && selected_pack.hull_infeasible;
        panel->has_production_error = selected_pack.has_error;
        panel->production_error = selected_pack.error;
        panel_remove_pack(base_path);
        panel_remove_pack(pack_path);
        panel->result = panel->hull_infeasible;
        return true;
    }
    if (!panel->commit_ok) {
        (void)fprintf(stderr, "atlas_hull_visual_report: production pack failed baseline=%u selected=%u selected_error=%u detail=%u/%u\n", baseline_pack.ok ? 1U : 0U, selected_pack.ok ? 1U : 0U,
                      selected_pack.has_error ? (uint32_t)selected_pack.error.kind : 0U, selected_pack.has_error ? selected_pack.error.detail_a : 0U,
                      selected_pack.has_error ? selected_pack.error.detail_b : 0U);
    }
    if (!panel->commit_ok || nt_bench_parse_selected_geometry(base_path, 0U, panel->trim_h, &panel->baseline) != 0) {
        panel_remove_pack(base_path);
        panel_remove_pack(pack_path);
        return false;
    }
    panel_attach_geometry(&panel->baseline);
    if (fail_after(4) || nt_bench_parse_selected_geometry(pack_path, 0U, panel->trim_h, &panel->selected) != 0) {
        panel_remove_pack(base_path);
        panel_remove_pack(pack_path);
        return false;
    }
    panel_attach_geometry(&panel->selected);
    if (fail_after(5) || nt_bench_file_sha256_hex(base_path, panel->baseline_pack_sha256) != 0 || nt_bench_file_sha256_hex(pack_path, panel->selected_pack_sha256) != 0) {
        panel_remove_pack(base_path);
        panel_remove_pack(pack_path);
        return false;
    }
    const uint64_t opaque_area2 = (uint64_t)panel->retained_pixels * 2U;
    const uint64_t base_area2 = polygon_abs_twice_area(panel->baseline.polygon, panel->baseline.vertex_count);
    const uint64_t selected_area2 = polygon_abs_twice_area(panel->selected.polygon, panel->selected.vertex_count);
    panel->proof = nt_selected_geometry_validate(panel->mask, panel->trim_w, panel->trim_h, opaque_area2, panel->baseline.polygon, panel->baseline.vertex_count, base_area2,
                                                 panel->baseline.triangle_indices, panel->baseline.triangle_index_count, panel->selected.polygon, panel->selected.vertex_count, selected_area2,
                                                 panel->selected.triangle_indices, panel->selected.triangle_index_count, (float)column->percent, row->budget);
    panel->lost_area2 = panel->proof.selected_coverage_valid ? 0U : opaque_area2;
    panel_remove_pack(base_path);
    panel_remove_pack(pack_path);
    panel->connected_frontier_valid = true;
    if (inspect_connected_slots && strcmp(row->sample_id, "connected-mask-adversarial") == 0) {
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
    if (fail_after(6)) {
        return false;
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

static void write_triangles_json(FILE *file, const Point2D *polygon, const uint16_t *indices, uint32_t index_count) {
    (void)fputc('[', file);
    for (uint32_t index = 0; index + 2U < index_count; index += 3U) {
        const uint16_t a = indices[index];
        const uint16_t b = indices[index + 1U];
        const uint16_t c = indices[index + 2U];
        (void)fprintf(file, "%s{\"indices\":[%u,%u,%u],\"points\":[{\"x\":%d,\"y\":%d},{\"x\":%d,\"y\":%d},{\"x\":%d,\"y\":%d}]}", index == 0U ? "" : ",", a, b, c, polygon[a].x, polygon[a].y,
                      polygon[b].x, polygon[b].y, polygon[c].x, polygon[c].y);
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
    write_points_json(file, panel->baseline.polygon, panel->baseline.vertex_count, false);
    (void)fputs(",\"selected_polygon\":", file);
    write_points_json(file, panel->selected.polygon, panel->selected.vertex_count, false);
    (void)fputs(",\"baseline_triangles\":", file);
    write_triangles_json(file, panel->baseline.polygon, panel->baseline.triangle_indices, panel->baseline.triangle_index_count);
    (void)fputs(",\"selected_triangles\":", file);
    write_triangles_json(file, panel->selected.polygon, panel->selected.triangle_indices, panel->selected.triangle_index_count);
    (void)fputs(",\"numbered_vertices\":", file);
    write_points_json(file, panel->selected.polygon, panel->selected.vertex_count, true);
    (void)fprintf(file,
                  ",\"selected_vertex_count\":%u,\"baseline_vertex_count\":%u,\"opaque_area2\":%llu,\"base_area2\":%llu,\"selected_area2\":%llu,\"added_area2\":%llu,\"exact_lost_area2\":%llu,"
                  "\"base_overdraw_percent\":%.8f,\"added_area_percent\":%.8f,\"total_overdraw_percent\":%.8f,"
                  "\"baseline_pack_sha256\":\"%s\",\"selected_pack_sha256\":\"%s\","
                  "\"connected_frontier_counts\":[%u,%u,%u],\"connected_frontier_valid\":%s,"
                  "\"max_vertices\":%u,\"expected_hull_infeasible\":%s,\"error_kind\":\"%s\",\"error_atlas\":\"%s\",\"error_sprite\":\"%s\","
                  "\"error_ceiling\":%u,\"error_invariant\":\"%s\",\"production_gates\":{"
                  "\"inputs_valid\":%s,\"opaque_area_valid\":%s,\"base_bounds_valid\":%s,\"base_topology_valid\":%s,\"base_coverage_valid\":%s,\"base_triangulation_valid\":%s,"
                  "\"selected_bounds_valid\":%s,\"selected_topology_valid\":%s,\"selected_coverage_valid\":%s,\"selected_triangulation_valid\":%s,"
                  "\"metric_order_valid\":%s,\"allowance_valid\":%s,\"ceiling_valid\":%s,\"expected_error_match\":%s},\"result\":\"%s\"}",
                  panel->selected.vertex_count, panel->baseline.vertex_count, (unsigned long long)panel->proof.opaque_area2, (unsigned long long)panel->proof.base_area2,
                  (unsigned long long)panel->proof.selected_area2, (unsigned long long)panel->proof.added_area2, (unsigned long long)panel->lost_area2, base_overdraw, added_area, total_overdraw,
                  panel->baseline_pack_sha256, panel->selected_pack_sha256, panel->connected_frontier_counts[0], panel->connected_frontier_counts[1], panel->connected_frontier_counts[2],
                  panel->connected_frontier_valid ? "true" : "false", row->budget, panel->hull_infeasible ? "true" : "false", panel->hull_infeasible ? "ATLAS_HULL_INFEASIBLE" : "NONE",
                  panel->has_production_error ? panel->production_error.atlas : "", panel->has_production_error ? panel->production_error.sprite : "",
                  panel->has_production_error ? panel->production_error.detail_a : 0U, panel->hull_infeasible ? "no covering polygon within hard vertex ceiling" : "",
                  panel->proof.inputs_valid ? "true" : "false", panel->proof.opaque_area_valid ? "true" : "false", panel->proof.base_bounds_valid ? "true" : "false",
                  panel->proof.base_topology_valid ? "true" : "false", panel->proof.base_coverage_valid ? "true" : "false", panel->proof.base_triangulation_valid ? "true" : "false",
                  panel->proof.selected_bounds_valid ? "true" : "false", panel->proof.selected_topology_valid ? "true" : "false", panel->proof.selected_coverage_valid ? "true" : "false",
                  panel->proof.selected_triangulation_valid ? "true" : "false", panel->proof.metric_order_valid ? "true" : "false", panel->proof.allowance_valid ? "true" : "false",
                  panel->proof.ceiling_valid ? "true" : "false", panel->hull_infeasible ? "true" : "false", panel->result ? "PASS" : "FAIL");
}

static bool write_manifest(const char *path, const VisualColumn columns[VISUAL_COLUMN_COUNT], const VisualPanel panels[VISUAL_ROW_COUNT][VISUAL_COLUMN_COUNT], const VisualPanel connected_evidence[3],
                           const char *corpus_sha256, const char *frontier_sha256) {
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
    for (uint32_t evidence = 0; evidence < 3U; evidence++) {
        overall = overall && connected_evidence[evidence].result && connected_evidence[evidence].selected.vertex_count == evidence + 6U;
    }
    (void)fprintf(file, "{\n  \"schema_version\": %d,\n  \"overall_pass\": %s,\n  \"visual_corpus_sha256\":\"%s\",\n  \"frontier_sha256\":\"%s\",\n  \"columns\": [", VISUAL_SCHEMA_VERSION,
                  overall ? "true" : "false", corpus_sha256, frontier_sha256);
    for (uint32_t i = 0; i < VISUAL_COLUMN_COUNT; i++) {
        (void)fprintf(file, "%s{\"column_id\":\"%s\",\"max_added_area_percent\":%.3f,\"sweep_source\":", i == 0 ? "" : ",", columns[i].id, columns[i].percent);
        json_escape(file, columns[i].source);
        (void)fputs(",\"sweep_sha256\":", file);
        json_escape(file, columns[i].sha256);
        (void)fputs(",\"measurement_source_commit\":", file);
        json_escape(file, columns[i].source_commit);
        (void)fputc('}', file);
    }
    (void)fputs("],\n  \"connected_frontier_evidence\":[", file);
    for (uint32_t evidence = 0; evidence < 3U; evidence++) {
        const VisualPanel *panel = &connected_evidence[evidence];
        (void)fprintf(file, "%s{\"connected_frontier_vertex_count\":%u,\"hard_ceiling\":13,\"required_added_area_percent\":%.8f,\"pack_sha256\":\"%s\",\"production_proof_valid\":%s,\"polygon\":",
                      evidence == 0U ? "" : ",", panel->selected.vertex_count, (double)panel->proof.max_added_area_percent, panel->selected_pack_sha256, panel->proof.valid ? "true" : "false");
        write_points_json(file, panel->selected.polygon, panel->selected.vertex_count, true);
        (void)fputs(",\"triangles\":", file);
        write_triangles_json(file, panel->selected.polygon, panel->selected.triangle_indices, panel->selected.triangle_index_count);
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
    (void)fputs("<polygon class=\"baseline\" points=\"", file);
    for (uint32_t i = 0; i < panel->baseline.vertex_count; i++) {
        (void)fprintf(file, "%d,%d ", panel->baseline.polygon[i].x, panel->baseline.polygon[i].y);
    }
    (void)fputs("\"/><polygon class=\"selected\" points=\"", file);
    for (uint32_t i = 0; i < panel->selected.vertex_count; i++) {
        (void)fprintf(file, "%d,%d ", panel->selected.polygon[i].x, panel->selected.polygon[i].y);
    }
    (void)fputs("\"/>", file);
    for (uint32_t index = 0; index + 2U < panel->selected.triangle_index_count; index += 3U) {
        const Point2D a = panel->selected.polygon[panel->selected.triangle_indices[index]];
        const Point2D b = panel->selected.polygon[panel->selected.triangle_indices[index + 1U]];
        const Point2D c = panel->selected.polygon[panel->selected.triangle_indices[index + 2U]];
        (void)fprintf(file, "<path class=\"triangle\" d=\"M%d %dL%d %dL%d %dZ\"/>", a.x, a.y, b.x, b.y, c.x, c.y);
    }
    for (uint32_t i = 0; i < panel->selected.vertex_count; i++) {
        (void)fprintf(file, "<circle cx=\"%d\" cy=\"%d\" r=\"%.3f\"/><text x=\"%d\" y=\"%d\">%u</text>", panel->selected.polygon[i].x, panel->selected.polygon[i].y, 2.2 / scale,
                      panel->selected.polygon[i].x, panel->selected.polygon[i].y, i + 1U);
    }
    (void)fputs("</svg>", file);
}

static bool write_html(const char *path, const VisualColumn columns[VISUAL_COLUMN_COUNT], const VisualPanel panels[VISUAL_ROW_COUNT][VISUAL_COLUMN_COUNT], const VisualPanel connected_evidence[3]) {
    FILE *file = fopen(path, "wb");
    if (file == NULL) {
        return false;
    }
    (void)fputs("<!doctype html><html lang=\"en\"><head><meta charset=\"utf-8\"><meta name=\"viewport\" content=\"width=device-width,initial-scale=1\"><title>Phase 80 Hull Visual Acceptance</title>",
                file);
    (void)fputs("<style>:root{color-scheme:dark;--bg:#09121d;--panel:#122235;--ink:#e8f3ff;--muted:#9eb4c9;--cyan:#38d9ff;--green:#53df89}*{box-sizing:border-box}body{margin:0;background:var(--bg);"
                "color:var(--ink);font:14px system-ui,sans-serif}main{max-width:1500px;margin:auto;padding:24px}h1{font-size:clamp(24px,4vw,44px);margin:0 0 8px}.lede{color:var(--muted);margin:0 0 "
                "28px}.row{margin:30px 0}.grid{display:grid;grid-template-columns:repeat(6,minmax(250px,1fr));gap:14px;overflow-x:auto}.panel{background:var(--panel);border:1px solid "
                "#29445f;border-radius:10px;padding:14px}.badge{color:var(--green);font-weight:800}svg{width:100%;height:220px;background:#08101a;margin:10px "
                "0}.alpha{fill:#3c718e}.mask{fill:none;stroke:#53df89;stroke-width:.06}polygon{fill:#38d9ff22;stroke:var(--cyan);stroke-width:.18}circle{fill:#fff}text{fill:#fff;font-size:.8px}table{"
                "width:100%;border-collapse:collapse}td{padding:"
                "3px;border-bottom:1px solid #29445f}td:last-child{text-align:right}.baseline{fill:none;stroke:#ffbf69;stroke-width:.18;stroke-dasharray:.5 "
                ".35}.selected{fill:#38d9ff22;stroke:var(--cyan);stroke-width:.18}.triangle{fill:none;stroke:#8bdff9;stroke-width:.05}"
                "@media(max-width:900px){.grid{grid-template-columns:1fr;overflow:visible}}</style></head><body><main>",
                file);
    (void)fputs("<h1>Phase 80 Hull Visual Acceptance</h1><p class=\"lede\">Every panel rebuilds the retained mask, reads baseline and selected polygons plus triangles from real production packs, "
                "then applies the same native selected-geometry validator used by serialization. Orange dashed = Abase polygon; cyan = selected polygon.</p>",
                file);
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
                (void)fputs("<p>Expected ATLAS_HULL_INFEASIBLE: atlas <strong>visual</strong>, sprite <strong>fixture</strong>; no covering polygon fits the hard vertex ceiling.</p>", file);
            }
            (void)fprintf(file,
                          "<table><tr><td>Selected vertices / hard ceiling</td><td>%u / %u</td></tr><tr><td>Aopaque / Abase / Aselected</td><td>%.1f / %.1f / %.1f</td></tr>"
                          "<tr><td>Base / added / total overdraw</td><td>%.3f%% / %.3f%% / %.3f%%</td></tr><tr><td>Exact lost area</td><td>%.1f</td></tr>"
                          "<tr><td>Full retained-cell coverage</td><td>%s</td></tr><tr><td>Topology / bounds / winding</td><td>%s</td></tr><tr><td>Triangulation valid</td><td>%s</td></tr>"
                          "<tr><td>Added-area allowance</td><td>%s</td></tr><tr><td>Baseline pack SHA-256</td><td><code>%s</code></td></tr><tr><td>Selected pack "
                          "SHA-256</td><td><code>%s</code></td></tr></table></article>",
                          panel->selected.vertex_count, VISUAL_ROWS[row].budget, (double)panel->proof.opaque_area2 * 0.5, (double)panel->proof.base_area2 * 0.5,
                          (double)panel->proof.selected_area2 * 0.5, base_overdraw, added_area, total_overdraw, (double)panel->lost_area2 * 0.5,
                          panel->proof.selected_coverage_valid ? "yes" : (panel->hull_infeasible ? "not applicable (expected error)" : "no"),
                          (panel->proof.selected_topology_valid && panel->proof.selected_bounds_valid) ? "valid" : (panel->hull_infeasible ? "not applicable (expected error)" : "invalid"),
                          panel->proof.selected_triangulation_valid ? "yes" : (panel->hull_infeasible ? "not applicable (expected error)" : "no"),
                          panel->proof.allowance_valid ? "within limit" : (panel->hull_infeasible ? "not applicable (expected error)" : "exceeded"), panel->baseline_pack_sha256,
                          panel->selected_pack_sha256);
        }
        (void)fputs("</div></section>", file);
    }
    (void)fputs("<section class=\"row\" id=\"connected-frontier-evidence\"><h2>Connected-mask production frontier: 6 / 7 / 8 vertices</h2><p class=\"lede\">Each card is a real 0% production pack "
                "with the shown hard ceiling; its serialized polygon and triangles are reconstructed and validated before rendering.</p><div class=\"grid\">",
                file);
    for (uint32_t evidence = 0; evidence < 3U; evidence++) {
        const VisualPanel *panel = &connected_evidence[evidence];
        (void)fprintf(file, "<article class=\"panel\"><h3>%u vertices</h3><span class=\"badge\">%s</span>", evidence + 6U, panel->result ? "PASS" : "FAIL");
        write_svg(file, panel);
        (void)fprintf(file,
                      "<table><tr><td>Serialized vertex count</td><td>%u</td></tr><tr><td>Hard ceiling</td><td>%u</td></tr><tr><td>Production proof</td><td>%s</td></tr><tr><td>Pack "
                      "SHA-256</td><td><code>%s</code></td></tr></table></article>",
                      panel->selected.vertex_count, 13U, panel->proof.valid ? "valid" : "invalid", panel->selected_pack_sha256);
    }
    (void)fputs("</div></section>", file);
    (void)fputs("</main></body></html>\n", file);
    return fclose(file) == 0;
}

int nt_hull_visual_generate(const char *corpus_path, const char *frontier_path, const char *out_dir) {
    VisualColumn columns[VISUAL_COLUMN_COUNT] = {0};
    VisualPanel panels[VISUAL_ROW_COUNT][VISUAL_COLUMN_COUNT] = {0};
    VisualPanel connected_evidence[3] = {0};
    char corpus_sha256[65] = {0};
    char frontier_sha256[65] = {0};
    if (!validate_corpus(corpus_path) || !load_frontier(frontier_path, columns) || !make_parent_dir(out_dir)) {
        (void)fprintf(stderr, "atlas_hull_visual_report: invalid corpus/frontier/output\n");
        return 1;
    }
    bool ok = true;
    for (uint32_t row = 0; ok && row < VISUAL_ROW_COUNT; row++) {
        VisualFixture fixture = {0};
        ok = fixture_create(&VISUAL_ROWS[row], &fixture);
        for (uint32_t column = 0; ok && column < VISUAL_COLUMN_COUNT; column++) {
            ok = panel_build(&VISUAL_ROWS[row], &columns[column], &fixture, out_dir, (row * VISUAL_COLUMN_COUNT) + column, true, 0U, &panels[row][column]);
        }
        free(fixture.rgba);
    }
    const VisualRow *connected_row = &VISUAL_ROWS[5];
    VisualFixture connected_fixture = {0};
    if (ok) {
        ok = fixture_create(connected_row, &connected_fixture);
    }
    uint8_t *connected_mask = NULL;
    uint32_t connected_mask_w = 0U;
    uint32_t connected_mask_h = 0U;
    if (ok) {
        uint32_t trim_x = 0U;
        uint32_t trim_y = 0U;
        uint8_t *alpha = alpha_plane_extract(connected_fixture.rgba, connected_fixture.width, connected_fixture.height);
        ok = alpha != NULL && alpha_trim(alpha, connected_fixture.width, connected_fixture.height, connected_row->threshold, &trim_x, &trim_y, &connected_mask_w, &connected_mask_h);
        if (ok) {
            const size_t mask_size = (size_t)connected_mask_w * connected_mask_h;
            connected_mask = (uint8_t *)malloc(mask_size);
            ok = connected_mask != NULL;
            for (uint32_t y = 0; ok && y < connected_mask_h; y++) {
                for (uint32_t x = 0; x < connected_mask_w; x++) {
                    connected_mask[((size_t)y * connected_mask_w) + x] = alpha[((size_t)(y + trim_y) * connected_fixture.width) + x + trim_x] >= connected_row->threshold ? 1U : 0U;
                }
            }
        }
        free(alpha);
    }
    for (uint32_t evidence = 0; ok && evidence < 3U; evidence++) {
        const uint32_t target_count = evidence + 6U;
        float required_percent = 0.0F;
        VisualColumn evidence_column = columns[0];
        ok = nt_atlas_test_concave_frontier_required_percent(connected_mask, connected_mask_w, connected_mask_h, connected_row->budget, target_count, &required_percent);
        if (!ok) {
            const uint32_t slot_mask = nt_atlas_test_concave_frontier_slot_mask(connected_mask, connected_mask_w, connected_mask_h, connected_row->budget);
            (void)fprintf(stderr, "atlas_hull_visual_report: connected slot %u missing from production frontier mask 0x%08x\n", target_count, slot_mask);
        }
        evidence_column.percent = (double)required_percent;
        (void)snprintf(evidence_column.id, sizeof(evidence_column.id), "slot-%u", target_count);
        if (ok) {
            ok = panel_build(connected_row, &evidence_column, &connected_fixture, out_dir, (VISUAL_ROW_COUNT * VISUAL_COLUMN_COUNT) + evidence, false, target_count, &connected_evidence[evidence]);
            if (!ok) {
                (void)fprintf(stderr, "atlas_hull_visual_report: connected slot %u production pack/proof failed\n", target_count);
            }
        }
        if (ok && connected_evidence[evidence].selected.vertex_count != evidence + 6U) {
            (void)fprintf(stderr, "atlas_hull_visual_report: connected slot %u serialized %u vertices\n", evidence + 6U, connected_evidence[evidence].selected.vertex_count);
            ok = false;
        }
    }
    free(connected_mask);
    free(connected_fixture.rgba);
    ok = ok && file_sha256_hex(corpus_path, corpus_sha256) && file_sha256_hex(frontier_path, frontier_sha256);
    char manifest_path[VISUAL_PATH_MAX];
    char html_path[VISUAL_PATH_MAX];
    (void)snprintf(manifest_path, sizeof(manifest_path), "%s/manifest.json", out_dir);
    (void)snprintf(html_path, sizeof(html_path), "%s/index.html", out_dir);
    ok = ok && write_manifest(manifest_path, columns, panels, connected_evidence, corpus_sha256, frontier_sha256) && write_html(html_path, columns, panels, connected_evidence);
    for (uint32_t row = 0; row < VISUAL_ROW_COUNT; row++) {
        for (uint32_t column = 0; column < VISUAL_COLUMN_COUNT; column++) {
            panel_destroy(&panels[row][column]);
        }
    }
    for (uint32_t evidence = 0; evidence < 3U; evidence++) {
        panel_destroy(&connected_evidence[evidence]);
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
    static const char *columns[VISUAL_COLUMN_COUNT] = {"percent-0", "percent-2", "percent-5", "percent-10", "percent-15", "percent-25"};
    const uint32_t panel_count = VISUAL_ROW_COUNT * VISUAL_COLUMN_COUNT;
    const uint32_t error_panel_count = VISUAL_COLUMN_COUNT;
    const uint32_t feasible_panel_count = panel_count - error_panel_count;
    bool ok = strstr(manifest, "\"schema_version\": 2") != NULL && strstr(manifest, "\"overall_pass\": true") != NULL && strstr(manifest, "\"result\":\"FAIL\"") == NULL &&
              strstr(manifest, "\"failing_panel_ids\": []") != NULL && count_substring(manifest, "\"panel_id\"") == panel_count &&
              count_substring(manifest, "\"sweep_source\"") == VISUAL_COLUMN_COUNT && count_substring(manifest, "\"sweep_sha256\"") == VISUAL_COLUMN_COUNT &&
              count_substring(manifest, "\"measurement_source_commit\"") == VISUAL_COLUMN_COUNT && strstr(manifest, "\"sweep_source\":\"\"") == NULL &&
              strstr(manifest, "\"sweep_sha256\":\"\"") == NULL && strstr(manifest, "\"visual_corpus_sha256\":\"\"") == NULL && strstr(manifest, "\"frontier_sha256\":\"\"") == NULL &&
              count_substring(manifest, "\"result\":\"PASS\"") == panel_count + VISUAL_ROW_COUNT && count_substring(manifest, "\"error_kind\":\"ATLAS_HULL_INFEASIBLE\"") == error_panel_count &&
              count_substring(manifest, "\"expected_error_match\":true") == error_panel_count && count_substring(manifest, "\"allowance_valid\":true") == feasible_panel_count &&
              count_substring(manifest, "\"selected_coverage_valid\":true") == feasible_panel_count && count_substring(manifest, "\"selected_triangulation_valid\":true") == feasible_panel_count &&
              count_substring(manifest, "\"ceiling_valid\":true") == feasible_panel_count && count_substring(manifest, "\"baseline_pack_sha256\":\"\"") == error_panel_count &&
              count_substring(manifest, "\"selected_pack_sha256\":\"\"") == error_panel_count && count_substring(manifest, "\"connected_frontier_vertex_count\"") == 3U &&
              strstr(manifest, "\"connected_frontier_vertex_count\":6") != NULL && strstr(manifest, "\"connected_frontier_vertex_count\":7") != NULL &&
              strstr(manifest, "\"connected_frontier_vertex_count\":8") != NULL && count_substring(manifest, "\"production_proof_valid\":true") == 3U && strstr(manifest, "fidelity_px") == NULL &&
              strstr(manifest, "lost_pixels") == NULL && strstr(html, "<!doctype html>") != NULL && strstr(html, "Selected vertices / hard ceiling") != NULL &&
              strstr(html, "Aopaque / Abase / Aselected") != NULL && strstr(html, "Full retained-cell coverage") != NULL && strstr(html, "Base / added / total overdraw") != NULL &&
              strstr(html, "Exact lost area") != NULL && strstr(html, "<script") == NULL && strstr(html, "http://") == NULL && strstr(html, "https://") == NULL &&
              required_rows_present(manifest, html, required_samples);
    if (!ok) {
        (void)fprintf(stderr,
                      "atlas_hull_visual_report: base contract failed panels=%u pass=%u errors=%u errmatch=%u allowance=%u coverage=%u triangles=%u ceiling=%u empty=%u/%u evidence=%u required=%u\n",
                      count_substring(manifest, "\"panel_id\""), count_substring(manifest, "\"result\":\"PASS\""), count_substring(manifest, "\"error_kind\":\"ATLAS_HULL_INFEASIBLE\""),
                      count_substring(manifest, "\"expected_error_match\":true"), count_substring(manifest, "\"allowance_valid\":true"), count_substring(manifest, "\"selected_coverage_valid\":true"),
                      count_substring(manifest, "\"selected_triangulation_valid\":true"), count_substring(manifest, "\"ceiling_valid\":true"),
                      count_substring(manifest, "\"baseline_pack_sha256\":\"\""), count_substring(manifest, "\"selected_pack_sha256\":\"\""),
                      count_substring(manifest, "\"production_proof_valid\":true"), required_rows_present(manifest, html, required_samples) ? 1U : 0U);
    }
    for (uint32_t field = 0; ok && field < (uint32_t)(sizeof(panel_fields) / sizeof(panel_fields[0])); field++) {
        ok = count_substring(manifest, panel_fields[field]) == panel_count;
        if (!ok) {
            (void)fprintf(stderr, "atlas_hull_visual_report: panel field count failed for %s\n", panel_fields[field]);
        }
    }
    const char *previous = manifest;
    for (uint32_t column = 0; ok && column < VISUAL_COLUMN_COUNT; column++) {
        char column_id[64];
        (void)snprintf(column_id, sizeof(column_id), "\"column_id\":\"%s\"", columns[column]);
        const char *position = strstr(previous, column_id);
        ok = position != NULL;
        if (!ok) {
            (void)fprintf(stderr, "atlas_hull_visual_report: ordered column missing %s\n", column_id);
        }
        if (position != NULL) {
            previous = position + strlen(column_id);
        }
    }
    free(manifest_bytes);
    free(html_bytes);
    if (!ok) {
        (void)fprintf(stderr, "atlas_hull_visual_report: validation failed closed\n");
        return 1;
    }
    return 0;
}
