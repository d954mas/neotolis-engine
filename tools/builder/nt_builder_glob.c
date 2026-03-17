/* clang-format off */
#include "nt_builder_internal.h"
/* clang-format on */

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <dirent.h>
#include <sys/stat.h>
#endif

/* --- Wildcard matching --- */

static bool wildcard_match(const char *pattern, const char *str) {
    const char *pp = pattern;
    const char *sp = str;
    const char *star_p = NULL;
    const char *star_s = NULL;

    while (*sp != '\0') {
        if (*pp == '*') {
            star_p = pp++;
            star_s = sp;
        } else if (*pp == '?' || *pp == *sp) {
            pp++;
            sp++;
        } else if (star_p != NULL) {
            pp = star_p + 1;
            sp = ++star_s;
        } else {
            return false;
        }
    }

    while (*pp == '*') {
        pp++;
    }
    return *pp == '\0';
}

/* --- Sorted directory iteration --- */

#define GLOB_MAX_MATCHES 1024

typedef void (*glob_callback_fn)(const char *full_path, void *user);

static int glob_strcmp(const void *a, const void *b) {
    return strcmp(*(const char *const *)a, *(const char *const *)b);
}

static void glob_iterate(const char *pattern, glob_callback_fn callback, void *user) {
    /* Split pattern into directory + filename pattern at last separator */
    const char *last_sep = NULL;
    for (const char *p = pattern; *p != '\0'; p++) {
        if (*p == '/' || *p == '\\') {
            last_sep = p;
        }
    }

    char directory[512];
    const char *file_pattern;

    if (last_sep != NULL) {
        size_t dir_len = (size_t)(last_sep - pattern);
        if (dir_len >= sizeof(directory)) {
            dir_len = sizeof(directory) - 1;
        }
        memcpy(directory, pattern, dir_len);
        directory[dir_len] = '\0';
        file_pattern = last_sep + 1;
    } else {
        directory[0] = '.';
        directory[1] = '\0';
        file_pattern = pattern;
    }

    /* Collect matching paths into array, then sort for deterministic order */
    char *matches[GLOB_MAX_MATCHES];
    uint32_t match_count = 0;

#ifdef _WIN32
    {
        WIN32_FIND_DATAA find_data;
        char search_path[512];
        (void)snprintf(search_path, sizeof(search_path), "%s/*", directory);

        HANDLE h_find = FindFirstFileA(search_path, &find_data);
        if (h_find == INVALID_HANDLE_VALUE) {
            return;
        }

        do {
            if (find_data.cFileName[0] == '.' && (find_data.cFileName[1] == '\0' || (find_data.cFileName[1] == '.' && find_data.cFileName[2] == '\0'))) {
                continue;
            }
            if (find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                continue;
            }
            if (wildcard_match(file_pattern, find_data.cFileName) && match_count < GLOB_MAX_MATCHES) {
                char full_path[512];
                (void)snprintf(full_path, sizeof(full_path), "%s/%s", directory, find_data.cFileName);
                matches[match_count] = strdup(full_path);
                if (matches[match_count]) {
                    match_count++;
                }
            }
        } while (FindNextFileA(h_find, &find_data));

        FindClose(h_find);
    }
#else
    {
        DIR *dir = opendir(directory);
        if (!dir) {
            return;
        }

        struct dirent *entry;
        while ((entry = readdir(dir)) != NULL) {
            if (entry->d_name[0] == '.' && (entry->d_name[1] == '\0' || (entry->d_name[1] == '.' && entry->d_name[2] == '\0'))) {
                continue;
            }
            char full_path[512];
            (void)snprintf(full_path, sizeof(full_path), "%s/%s", directory, entry->d_name);
            struct stat st;
            if (stat(full_path, &st) != 0 || S_ISDIR(st.st_mode)) {
                continue;
            }
            if (wildcard_match(file_pattern, entry->d_name) && match_count < GLOB_MAX_MATCHES) {
                matches[match_count] = strdup(full_path);
                if (matches[match_count]) {
                    match_count++;
                }
            }
        }

        closedir(dir);
    }
#endif

    /* Sort for deterministic pack layout across platforms */
    if (match_count > 1) {
        qsort(matches, match_count, sizeof(char *), glob_strcmp);
    }

    /* Invoke callbacks in sorted order */
    for (uint32_t i = 0; i < match_count; i++) {
        callback(matches[i], user);
        free(matches[i]);
    }
}

/* --- Batch callback context --- */

typedef struct {
    NtBuilderContext *ctx;
    const NtStreamLayout *layout;
    uint32_t stream_count;
    nt_build_shader_stage_t stage;
    nt_build_result_t last_result;
    uint32_t match_count;
} GlobCallbackData;

/* --- Callbacks --- */

static void mesh_glob_callback(const char *full_path, void *user) {
    GlobCallbackData *data = (GlobCallbackData *)user;
    data->match_count++;
    nt_build_result_t r = nt_builder_add_mesh(data->ctx, full_path, data->layout, data->stream_count);
    if (r != NT_BUILD_OK) {
        data->last_result = r;
    }
}

static void texture_glob_callback(const char *full_path, void *user) {
    GlobCallbackData *data = (GlobCallbackData *)user;
    data->match_count++;
    nt_build_result_t r = nt_builder_add_texture(data->ctx, full_path);
    if (r != NT_BUILD_OK) {
        data->last_result = r;
    }
}

static void shader_glob_callback(const char *full_path, void *user) {
    GlobCallbackData *data = (GlobCallbackData *)user;
    data->match_count++;
    nt_build_result_t r = nt_builder_add_shader(data->ctx, full_path, data->stage);
    if (r != NT_BUILD_OK) {
        data->last_result = r;
    }
}

/* --- Public batch API --- */

nt_build_result_t nt_builder_add_meshes(NtBuilderContext *ctx, const char *pattern, const NtStreamLayout *layout, uint32_t stream_count) {
    if (!ctx || !pattern || !layout) {
        return NT_BUILD_ERR_VALIDATION;
    }

    GlobCallbackData data;
    memset(&data, 0, sizeof(data));
    data.ctx = ctx;
    data.layout = layout;
    data.stream_count = stream_count;
    data.last_result = NT_BUILD_OK;

    glob_iterate(pattern, mesh_glob_callback, &data);

    if (data.match_count == 0) {
        (void)fprintf(stderr, "WARNING: no files matched pattern '%s'\n", pattern);
    }
    return data.last_result;
}

nt_build_result_t nt_builder_add_textures(NtBuilderContext *ctx, const char *pattern) {
    if (!ctx || !pattern) {
        return NT_BUILD_ERR_VALIDATION;
    }

    GlobCallbackData data;
    memset(&data, 0, sizeof(data));
    data.ctx = ctx;
    data.last_result = NT_BUILD_OK;

    glob_iterate(pattern, texture_glob_callback, &data);

    if (data.match_count == 0) {
        (void)fprintf(stderr, "WARNING: no files matched pattern '%s'\n", pattern);
    }
    return data.last_result;
}

nt_build_result_t nt_builder_add_shaders(NtBuilderContext *ctx, const char *pattern, nt_build_shader_stage_t stage) {
    if (!ctx || !pattern) {
        return NT_BUILD_ERR_VALIDATION;
    }

    GlobCallbackData data;
    memset(&data, 0, sizeof(data));
    data.ctx = ctx;
    data.stage = stage;
    data.last_result = NT_BUILD_OK;

    glob_iterate(pattern, shader_glob_callback, &data);

    if (data.match_count == 0) {
        (void)fprintf(stderr, "WARNING: no files matched pattern '%s'\n", pattern);
    }
    return data.last_result;
}
