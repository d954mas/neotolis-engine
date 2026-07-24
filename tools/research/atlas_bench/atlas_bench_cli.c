#include "atlas_bench_cli.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

bool atlas_bench_parse_u32_strict(const char *text, uint32_t *out_value) {
    if (text == NULL || text[0] == '\0' || out_value == NULL) {
        return false;
    }
    for (const char *cursor = text; *cursor != '\0'; cursor++) {
        if (*cursor < '0' || *cursor > '9') {
            return false;
        }
    }
    errno = 0;
    char *end = NULL;
    const unsigned long value = strtoul(text, &end, 10);
    if (errno == ERANGE || end == text || *end != '\0' || value > UINT32_MAX) {
        return false;
    }
    *out_value = (uint32_t)value;
    return true;
}

bool atlas_bench_parse_max_size(const char *text, uint32_t *out_value) {
    uint32_t value = 0U;
    if (!atlas_bench_parse_u32_strict(text, &value) || value == 0U || value > ATLAS_BENCH_MAX_ATLAS_SIZE) {
        return false;
    }
    *out_value = value;
    return true;
}

static bool ci_equal(const char *a, const char *b) {
    for (; *a != '\0' && *b != '\0'; a++, b++) {
        char ca = *a;
        char cb = *b;
        if (ca >= 'A' && ca <= 'Z') {
            ca = (char)(ca + ('a' - 'A'));
        }
        if (cb >= 'A' && cb <= 'Z') {
            cb = (char)(cb + ('a' - 'A'));
        }
        if (ca != cb) {
            return false;
        }
    }
    return *a == '\0' && *b == '\0';
}

bool atlas_bench_parse_transforms(const char *text, uint8_t *out_mask) {
    if (text == NULL || text[0] == '\0' || out_mask == NULL) {
        return false;
    }
    /* A leading 0x/0X is an explicit hex byte; everything else is a preset word. */
    if (text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
        errno = 0;
        char *end = NULL;
        const unsigned long value = strtoul(text, &end, 16);
        if (errno == ERANGE || end == text || *end != '\0' || value > 0xFFUL) {
            return false;
        }
        *out_mask = (uint8_t)value;
        return true;
    }
    if (ci_equal(text, "all")) {
        *out_mask = ATLAS_BENCH_TRANSFORMS_ALL;
    } else if (ci_equal(text, "identity")) {
        *out_mask = ATLAS_BENCH_TRANSFORMS_IDENTITY;
    } else if (ci_equal(text, "identity-rot90")) {
        *out_mask = ATLAS_BENCH_TRANSFORMS_IDENTITY_ROT90;
    } else if (ci_equal(text, "rotations")) {
        *out_mask = ATLAS_BENCH_TRANSFORMS_ROTATIONS;
    } else if (ci_equal(text, "flips")) {
        *out_mask = ATLAS_BENCH_TRANSFORMS_FLIPS;
    } else {
        return false;
    }
    return true;
}

bool atlas_bench_derive_pack_paths(const char *out_json, char *selected, size_t selected_size, char *baseline, size_t baseline_size) {
    if (out_json == NULL || selected == NULL || selected_size == 0U || baseline == NULL || baseline_size == 0U) {
        return false;
    }
    const int selected_length = snprintf(selected, selected_size, "%s.ntpack", out_json);
    const int baseline_length = snprintf(baseline, baseline_size, "%s.baseline.ntpack", out_json);
    if (selected_length < 0 || (size_t)selected_length >= selected_size || baseline_length < 0 || (size_t)baseline_length >= baseline_size) {
        selected[0] = '\0';
        baseline[0] = '\0';
        return false;
    }
    return true;
}

static void remove_temporary_pack(const char *path) {
    char header[1024];
    const int path_length = snprintf(header, sizeof(header), "%s", path);
    char *extension = strrchr(header, '.');
    if (path_length >= 0 && (size_t)path_length < sizeof(header) && extension != NULL && strcmp(extension, ".ntpack") == 0) {
        (void)snprintf(extension, 8U, ".h");
        (void)remove(header);
    }
    (void)remove(path);
}

void atlas_bench_cleanup_outputs(const char *out_json, const char *baseline_pack_path, const char *selected_pack_path, bool keep_selected, bool remove_json) {
    remove_temporary_pack(baseline_pack_path);
    if (!keep_selected) {
        remove_temporary_pack(selected_pack_path);
    }
    if (remove_json) {
        (void)remove(out_json);
    }
}
