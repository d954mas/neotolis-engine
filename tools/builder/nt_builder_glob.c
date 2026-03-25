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

static int glob_strcmp(const void *a, const void *b) { return strcmp(*(const char *const *)a, *(const char *const *)b); }

/* Returns false if glob overflow (too many matches). */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static bool glob_iterate(const char *pattern, glob_callback_fn callback, void *user) {
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
    bool overflow = false;

#ifdef _WIN32
    {
        WIN32_FIND_DATAA find_data;
        char search_path[512];
        (void)snprintf(search_path, sizeof(search_path), "%s/*", directory);

        HANDLE h_find = FindFirstFileA(search_path, &find_data);
        if (h_find == INVALID_HANDLE_VALUE) {
            return true;
        }

        do {
            if (find_data.cFileName[0] == '.' && (find_data.cFileName[1] == '\0' || (find_data.cFileName[1] == '.' && find_data.cFileName[2] == '\0'))) {
                continue;
            }
            if (find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                continue;
            }
            if (wildcard_match(file_pattern, find_data.cFileName)) {
                if (match_count >= GLOB_MAX_MATCHES) {
                    NT_LOG_ERROR("glob match limit reached (%d), aborting pattern '%s'", GLOB_MAX_MATCHES, pattern);
                    overflow = true;
                    break;
                }
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
            return true;
        }

        struct dirent *entry;
        while ((entry = readdir(dir)) != NULL) { // NOLINT(concurrency-mt-unsafe)
            if (entry->d_name[0] == '.' && (entry->d_name[1] == '\0' || (entry->d_name[1] == '.' && entry->d_name[2] == '\0'))) {
                continue;
            }
            char full_path[512];
            (void)snprintf(full_path, sizeof(full_path), "%s/%s", directory, entry->d_name);
            struct stat st;
            if (stat(full_path, &st) != 0 || S_ISDIR(st.st_mode)) {
                continue;
            }
            if (wildcard_match(file_pattern, entry->d_name)) {
                if (match_count >= GLOB_MAX_MATCHES) {
                    NT_LOG_ERROR("glob match limit reached (%d), aborting pattern '%s'", GLOB_MAX_MATCHES, pattern);
                    overflow = true;
                    break;
                }
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
        qsort((void *)matches, match_count, sizeof(char *), glob_strcmp);
    }

    /* On overflow, free collected matches without invoking callbacks */
    if (overflow) {
        for (uint32_t i = 0; i < match_count; i++) {
            free(matches[i]);
        }
        return false;
    }

    /* Invoke callbacks in sorted order */
    for (uint32_t i = 0; i < match_count; i++) {
        callback(matches[i], user);
        free(matches[i]);
    }
    return true;
}

/* --- Batch callback context --- */

typedef struct {
    NtBuilderContext *ctx;
    uint32_t match_count;
    void *type_data; /* per-type params, NULL for texture */
} GlobCallbackData;

typedef struct {
    const nt_mesh_opts_t *opts;
} GlobMeshParams;

typedef struct {
    nt_build_shader_stage_t stage;
} GlobShaderParams;

/* --- Callbacks --- */

static void mesh_glob_callback(const char *full_path, void *user) {
    GlobCallbackData *cb = (GlobCallbackData *)user;
    GlobMeshParams *p = (GlobMeshParams *)cb->type_data;
    cb->match_count++;
    nt_builder_add_mesh(cb->ctx, full_path, p->opts);
}

static void texture_glob_callback(const char *full_path, void *user) {
    GlobCallbackData *cb = (GlobCallbackData *)user;
    cb->match_count++;
    nt_builder_add_texture(cb->ctx, full_path);
}

static void shader_glob_callback(const char *full_path, void *user) {
    GlobCallbackData *cb = (GlobCallbackData *)user;
    GlobShaderParams *p = (GlobShaderParams *)cb->type_data;
    cb->match_count++;
    nt_builder_add_shader(cb->ctx, full_path, p->stage);
}

/* --- Public batch API --- */

static void nt_builder_glob_add(NtBuilderContext *ctx, const char *pattern, glob_callback_fn callback, void *type_data) {
    GlobCallbackData cb;
    memset(&cb, 0, sizeof(cb));
    cb.ctx = ctx;
    cb.type_data = type_data;

    NT_BUILD_ASSERT(glob_iterate(pattern, callback, &cb) && "glob overflow");
    NT_BUILD_ASSERT(cb.match_count > 0 && "no files matched pattern");
}

void nt_builder_add_meshes(NtBuilderContext *ctx, const char *pattern, const nt_mesh_opts_t *opts) {
    NT_BUILD_ASSERT(ctx && pattern && opts && "invalid add_meshes args");
    GlobMeshParams p = {opts};
    nt_builder_glob_add(ctx, pattern, mesh_glob_callback, &p);
}

void nt_builder_add_textures(NtBuilderContext *ctx, const char *pattern) {
    NT_BUILD_ASSERT(ctx && pattern && "invalid add_textures args");
    nt_builder_glob_add(ctx, pattern, texture_glob_callback, NULL);
}

void nt_builder_add_shaders(NtBuilderContext *ctx, const char *pattern, nt_build_shader_stage_t stage) {
    NT_BUILD_ASSERT(ctx && pattern && "invalid add_shaders args");
    GlobShaderParams p = {stage};
    nt_builder_glob_add(ctx, pattern, shader_glob_callback, &p);
}
