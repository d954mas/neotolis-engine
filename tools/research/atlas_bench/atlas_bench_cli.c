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

static void remove_temporary_pack(const char *path) {
    char header[1024];
    (void)snprintf(header, sizeof(header), "%s", path);
    char *extension = strstr(header, ".ntpack");
    if (extension != NULL && extension[7] == '\0') {
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
