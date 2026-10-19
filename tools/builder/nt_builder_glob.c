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
#include <unistd.h>
#endif
#ifdef __APPLE__
#include <mach-o/dyld.h>
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

#ifndef GLOB_MAX_MATCHES
#define GLOB_MAX_MATCHES 8192
#endif

/* Recorded patterns are absolute: never truncate, a cut path re-lists differently. */
#define GLOB_PATH_MAX 1024

static int glob_strcmp(const void *a, const void *b) { return strcmp(*(const char *const *)a, *(const char *const *)b); }

/* Sorted "<dir>/<name>" files matching pattern; a missing directory matches
 * nothing. Past GLOB_MAX_MATCHES returns false and no matches. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static bool glob_list(const char *pattern, char ***out_matches, uint32_t *out_count) {
    /* Split pattern into directory + filename pattern at last separator */
    const char *last_sep = NULL;
    for (const char *p = pattern; *p != '\0'; p++) {
        if (*p == '/' || *p == '\\') {
            last_sep = p;
        }
    }

    char directory[GLOB_PATH_MAX];
    const char *file_pattern;

    if (last_sep != NULL) {
        size_t dir_len = (size_t)(last_sep - pattern);
        NT_BUILD_ASSERT(dir_len < sizeof(directory) && "glob: directory path too long");
        memcpy(directory, pattern, dir_len);
        directory[dir_len] = '\0';
        file_pattern = last_sep + 1;
    } else {
        directory[0] = '.';
        directory[1] = '\0';
        file_pattern = pattern;
    }

    /* Collect matching paths into array, then sort for deterministic order */
    char **matches = (char **)malloc((size_t)GLOB_MAX_MATCHES * sizeof(char *));
    NT_BUILD_ASSERT(matches && "glob: out of memory");
    uint32_t match_count = 0;
    bool overflow = false;

#ifdef _WIN32
    {
        WIN32_FIND_DATAA find_data;
        char search_path[GLOB_PATH_MAX + 2];
        (void)snprintf(search_path, sizeof(search_path), "%s/*", directory);

        HANDLE h_find = FindFirstFileA(search_path, &find_data);
        if (h_find == INVALID_HANDLE_VALUE) {
            *out_matches = matches;
            *out_count = 0;
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
                char full_path[GLOB_PATH_MAX + 256]; /* + NAME_MAX */
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
            *out_matches = matches;
            *out_count = 0;
            return true;
        }

        struct dirent *entry;
        while ((entry = readdir(dir)) != NULL) { // NOLINT(concurrency-mt-unsafe)
            if (entry->d_name[0] == '.' && (entry->d_name[1] == '\0' || (entry->d_name[1] == '.' && entry->d_name[2] == '\0'))) {
                continue;
            }
            char full_path[GLOB_PATH_MAX + 256]; /* + NAME_MAX */
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

    /* On overflow, drop collected matches: no callbacks run */
    if (overflow) {
        for (uint32_t i = 0; i < match_count; i++) {
            free(matches[i]);
        }
        match_count = 0;
    }
    *out_matches = matches;
    *out_count = match_count;
    return !overflow;
}

/* --- Build record --- */

/* One builder process is one build step that may write several packs, so the
 * record belongs to the process, not to a pack context. */
typedef enum { RECORD_INPUT, RECORD_OUTPUT, RECORD_GLOB } NtRecordKind;

typedef struct {
    const char *text; /* absolute path, or a whole "glob" block */
    int64_t mtime;
    int64_t size;
    uint32_t seq;
    NtRecordKind kind;
} NtRecordLine;

static NtRecordLine *s_record;
static uint32_t s_record_count;
static uint32_t s_record_capacity;

/* mtime and size, or -1 and -1 for a missing file. */
static void file_stat(const char *path, int64_t *mtime, int64_t *size) {
#ifdef _WIN32
    WIN32_FILE_ATTRIBUTE_DATA data;
    if (GetFileAttributesExA(path, GetFileExInfoStandard, &data) && !(data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
        *mtime = (int64_t)(((uint64_t)data.ftLastWriteTime.dwHighDateTime << 32U) | data.ftLastWriteTime.dwLowDateTime);
        *size = (int64_t)(((uint64_t)data.nFileSizeHigh << 32U) | data.nFileSizeLow);
        return;
    }
#else
    struct stat st;
    if (stat(path, &st) == 0 && !S_ISDIR(st.st_mode)) {
#ifdef __APPLE__
        *mtime = ((int64_t)st.st_mtimespec.tv_sec * 1000000000) + st.st_mtimespec.tv_nsec;
#else
        *mtime = ((int64_t)st.st_mtim.tv_sec * 1000000000) + st.st_mtim.tv_nsec;
#endif
        *size = (int64_t)st.st_size;
        return;
    }
#endif
    *mtime = -1;
    *size = -1;
}

/* Absolute and '/'-separated; the record must work from any working directory.
 * Windows resolves ".." lexically, so _fullpath collapses it. POSIX resolves it
 * through symlinks, so it stays. */
static char *absolute_path(const char *path) {
#ifdef _WIN32
    char *abs_path = _fullpath(NULL, path, 0);
    NT_BUILD_ASSERT(abs_path && "build record: cannot make path absolute");
    for (char *c = abs_path; *c != '\0'; c++) {
        if (*c == '\\') {
            *c = '/';
        }
    }
#else
    while (path[0] == '.' && path[1] == '/') {
        path += 2;
    }
    char cwd[4096] = "";
    if (path[0] != '/') {
        char *got = getcwd(cwd, sizeof(cwd));
        NT_BUILD_ASSERT(got && "build record: cannot make path absolute");
    }
    size_t size = strlen(cwd) + strlen(path) + 2;
    char *abs_path = (char *)malloc(size);
    NT_BUILD_ASSERT(abs_path && "build record: out of memory");
    (void)snprintf(abs_path, size, "%s%s%s", cwd, cwd[0] ? "/" : "", path);
#endif
    return abs_path;
}

static void record_push(NtRecordKind kind, const char *text, int64_t mtime, int64_t size) {
    if (s_record_count == s_record_capacity) {
        s_record_capacity = s_record_capacity ? s_record_capacity * 2U : 256U;
        NtRecordLine *grown = (NtRecordLine *)realloc(s_record, (size_t)s_record_capacity * sizeof(NtRecordLine));
        NT_BUILD_ASSERT(grown && "build record: out of memory");
        s_record = grown;
    }
    s_record[s_record_count] = (NtRecordLine){text, mtime, size, s_record_count, kind};
    s_record_count++;
}

/* Stat'ed now: before an input is read, right after an output is published, so
 * a later edit or another builder's overwrite never looks current. */
static void record_file(NtRecordKind kind, const char *path) {
    int64_t mtime = 0;
    int64_t size = 0;
    file_stat(path, &mtime, &size);
    record_push(kind, absolute_path(path), mtime, size);
}

void nt_builder_record_input(const char *path) { record_file(RECORD_INPUT, path); }

void nt_builder_record_output(const char *path) { record_file(RECORD_OUTPUT, path); }

static const char *match_name(const char *match) {
    const char *slash = strrchr(match, '/');
    return slash ? slash + 1 : match;
}

/* "glob <absolute pattern>", then "  <name>" per match. */
static void record_glob(const char *pattern, char **matches, uint32_t match_count) {
    char *abs_pattern = absolute_path(pattern);
    /* glob_list re-lists this absolute pattern on the next run. */
    NT_BUILD_ASSERT(strlen(abs_pattern) < GLOB_PATH_MAX && "glob: absolute pattern too long");
    size_t size = strlen(abs_pattern) + 7;
    for (uint32_t i = 0; i < match_count; i++) {
        size += strlen(match_name(matches[i])) + 3;
    }
    char *block = (char *)malloc(size);
    NT_BUILD_ASSERT(block && "build record: out of memory");
    size_t len = (size_t)snprintf(block, size, "glob %s\n", abs_pattern);
    free(abs_pattern);
    for (uint32_t i = 0; i < match_count; i++) {
        len += (size_t)snprintf(block + len, size - len, "  %s\n", match_name(matches[i]));
    }
    record_push(RECORD_GLOB, block, 0, 0);
}

/* Returns false if glob overflow (too many matches). */
bool nt_builder_glob_iterate(const char *pattern, nt_builder_glob_callback_fn callback, void *user) {
    char **matches = NULL;
    uint32_t match_count = 0;
    bool ok = glob_list(pattern, &matches, &match_count);
    /* The files this glob saw, not a later re-listing: one added while the
     * builder runs must still rebuild the pack next time. An overflowed
     * pattern is recorded empty and never matches its re-listing. */
    record_glob(pattern, matches, match_count);

    /* Invoke callbacks in sorted order */
    for (uint32_t i = 0; i < match_count; i++) {
        callback(matches[i], user);
        free(matches[i]);
    }
    free((void *)matches);
    return ok;
}

/* By kind, then text. Of duplicates, the first read of an input sorts first (an
 * edit between two reads still rebuilds), and the last write of an output. */
static int record_cmp(const void *a, const void *b) {
    const NtRecordLine *la = (const NtRecordLine *)a;
    const NtRecordLine *lb = (const NtRecordLine *)b;
    if (la->kind != lb->kind) {
        return la->kind < lb->kind ? -1 : 1;
    }
    int text_cmp = strcmp(la->text, lb->text);
    if (text_cmp != 0 || la->seq == lb->seq) {
        return text_cmp;
    }
    bool earlier = la->seq < lb->seq;
    return earlier != (la->kind == RECORD_OUTPUT) ? -1 : 1;
}

static void record_path(const char *dir, char *out, size_t out_size) {
    int len = snprintf(out, out_size, "%s/build_record.txt", dir);
    NT_BUILD_ASSERT(len > 0 && (size_t)len < out_size && "build record: path too long");
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void nt_builder_write_record(const char *dir) {
    NT_BUILD_ASSERT(dir && "write_record: dir is required");
    char path[1024];
    record_path(dir, path, sizeof(path));
    /* Per process: builders of two presets can share one pack dir. */
    char tmp_path[sizeof(path) + 24];
#ifdef _WIN32
    unsigned long pid = (unsigned long)GetCurrentProcessId();
#else
    unsigned long pid = (unsigned long)getpid();
#endif
    (void)snprintf(tmp_path, sizeof(tmp_path), "%s.%lu.tmp", path, pid);
    if (s_record_count > 1) {
        qsort(s_record, s_record_count, sizeof(NtRecordLine), record_cmp);
    }
    FILE *file = fopen(tmp_path, "wb");
    NT_BUILD_ASSERT(file && "write_record: cannot open output");
    for (uint32_t i = 0; i < s_record_count; i++) {
        const NtRecordLine *line = &s_record[i];
        if (i > 0 && line->kind == s_record[i - 1].kind && strcmp(line->text, s_record[i - 1].text) == 0) {
            continue;
        }
        if (line->kind == RECORD_GLOB) {
            (void)fputs(line->text, file);
            continue;
        }
        (void)fprintf(file, "%s %lld %lld %s\n", line->kind == RECORD_INPUT ? "input" : "output", (long long)line->mtime, (long long)line->size, line->text);
    }
    bool write_failed = ferror(file) != 0;
    write_failed |= fclose(file) != 0;
    NT_BUILD_ASSERT(!write_failed && "write_record: write failed");
    /* Replace, never rewrite in place: a torn record must not look up to date. */
    bool published = nt_builder_publish_file(tmp_path, path);
    NT_BUILD_ASSERT(published && "write_record: cannot publish");
}

/* The record cannot list the builder itself: a builder linked after the record
 * was written rebuilds. */
static int64_t builder_mtime(void) {
    char exe[4096];
#ifdef _WIN32
    DWORD len = GetModuleFileNameA(NULL, exe, (DWORD)sizeof(exe));
    NT_BUILD_ASSERT(len > 0 && len < sizeof(exe) && "up_to_date: cannot find the builder executable");
#elif defined(__APPLE__)
    uint32_t exe_size = (uint32_t)sizeof(exe);
    int found = _NSGetExecutablePath(exe, &exe_size);
    NT_BUILD_ASSERT(found == 0 && "up_to_date: cannot find the builder executable");
#else
    ssize_t len = readlink("/proc/self/exe", exe, sizeof(exe) - 1);
    NT_BUILD_ASSERT(len > 0 && (size_t)len < sizeof(exe) - 1 && "up_to_date: cannot find the builder executable");
    exe[len] = '\0';
#endif
    int64_t mtime = 0;
    int64_t size = 0;
    file_stat(exe, &mtime, &size);
    /* An unreadable path (non-ANSI on Windows, a replaced exe on Linux) would
     * otherwise pass as "older than any record". */
    NT_BUILD_ASSERT(mtime >= 0 && "up_to_date: cannot stat the builder executable");
    return mtime;
}

/* Re-lists a recorded glob and compares it with the "  <name>" lines at
 * names; returns the line after them. */
static char *glob_unchanged(const char *pattern, char *names, bool *same) {
    char **matches = NULL;
    uint32_t match_count = 0;
    bool ok = glob_list(pattern, &matches, &match_count);
    uint32_t i = 0;
    while (names[0] == ' ' && names[1] == ' ') {
        char *end = strchr(names, '\n');
        if (!end) {
            ok = false;
            break;
        }
        *end = '\0';
        ok = ok && i < match_count && strcmp(match_name(matches[i]), names + 2) == 0;
        i++;
        names = end + 1;
    }
    *same = ok && i == match_count;
    for (uint32_t m = 0; m < match_count; m++) {
        free(matches[m]);
    }
    free((void *)matches);
    return names;
}

bool nt_builder_up_to_date(const char *dir) {
    NT_BUILD_ASSERT(dir && "up_to_date: dir is required");
    char path[1024];
    record_path(dir, path, sizeof(path));
    uint32_t size = 0;
    char *text = nt_builder_read_file(path, &size);
    int64_t record_mtime = 0;
    int64_t record_size = 0;
    file_stat(path, &record_mtime, &record_size);
    bool same = text != NULL && builder_mtime() < record_mtime;
    for (char *line = text; same && *line != '\0';) {
        char *end = strchr(line, '\n');
        if (!end) {
            same = false;
            break;
        }
        *end = '\0';
        if (strncmp(line, "glob ", 5) == 0) {
            line = glob_unchanged(line + 5, end + 1, &same);
            continue;
        }
        /* "<input|output> <mtime> <size> <path>" */
        char *field = strchr(line, ' ');
        if (!field) {
            same = false;
            break;
        }
        long long mtime = strtoll(field + 1, &field, 10);
        long long file_size = strtoll(field, &field, 10);
        int64_t now_mtime = 0;
        int64_t now_size = 0;
        file_stat(field + 1, &now_mtime, &now_size);
        /* A missing input may stay missing; a missing output never is current. */
        bool missing_output = file_size < 0 && strncmp(line, "output ", 7) == 0;
        same = *field == ' ' && now_mtime == mtime && now_size == file_size && !missing_output;
        line = end + 1;
    }
    free(text);
    return same;
}

/* --- Batch callback context --- */

typedef struct {
    NtBuilderContext *ctx;
    uint32_t match_count;
    void *type_data; /* per-type params */
} GlobCallbackData;

typedef struct {
    const nt_mesh_opts_t *opts;
} GlobMeshParams;

typedef struct {
    const nt_tex_opts_t *opts;
} GlobTextureParams;

typedef struct {
    nt_build_shader_stage_t stage;
} GlobShaderParams;

typedef struct {
    const nt_font_opts_t *opts;
} GlobFontParams;

/* --- Callbacks --- */

static void mesh_glob_callback(const char *full_path, void *user) {
    GlobCallbackData *cb = (GlobCallbackData *)user;
    GlobMeshParams *p = (GlobMeshParams *)cb->type_data;
    cb->match_count++;
    nt_builder_add_mesh(cb->ctx, full_path, p->opts);
}

static void texture_glob_callback(const char *full_path, void *user) {
    GlobCallbackData *cb = (GlobCallbackData *)user;
    GlobTextureParams *p = (GlobTextureParams *)cb->type_data;
    cb->match_count++;
    nt_builder_add_texture(cb->ctx, full_path, p ? p->opts : NULL);
}

static void shader_glob_callback(const char *full_path, void *user) {
    GlobCallbackData *cb = (GlobCallbackData *)user;
    GlobShaderParams *p = (GlobShaderParams *)cb->type_data;
    cb->match_count++;
    nt_builder_add_shader(cb->ctx, full_path, p->stage);
}

static void font_glob_callback(const char *full_path, void *user) {
    GlobCallbackData *cb = (GlobCallbackData *)user;
    GlobFontParams *p = (GlobFontParams *)cb->type_data;
    cb->match_count++;
    nt_builder_add_font(cb->ctx, full_path, p->opts);
}

/* --- Public batch API --- */

static void nt_builder_glob_add(NtBuilderContext *ctx, const char *pattern, nt_builder_glob_callback_fn callback, void *type_data) {
    GlobCallbackData cb;
    memset(&cb, 0, sizeof(cb));
    cb.ctx = ctx;
    cb.type_data = type_data;

    NT_BUILD_ASSERT(nt_builder_glob_iterate(pattern, callback, &cb) && "glob overflow");
    NT_BUILD_ASSERT(cb.match_count > 0 && "no files matched pattern");
}

void nt_builder_add_meshes(NtBuilderContext *ctx, const char *pattern, const nt_mesh_opts_t *opts) {
    NT_BUILD_ASSERT(ctx && pattern && opts && "invalid add_meshes args");
    GlobMeshParams p = {opts};
    nt_builder_glob_add(ctx, pattern, mesh_glob_callback, &p);
}

void nt_builder_add_textures(NtBuilderContext *ctx, const char *pattern, const nt_tex_opts_t *opts) {
    NT_BUILD_ASSERT(ctx && pattern && "invalid add_textures args");
    (void)nt_builder_assert_texture_opts(opts);
    GlobTextureParams p = {opts};
    nt_builder_glob_add(ctx, pattern, texture_glob_callback, &p);
}

void nt_builder_add_shaders(NtBuilderContext *ctx, const char *pattern, nt_build_shader_stage_t stage) {
    NT_BUILD_ASSERT(ctx && pattern && "invalid add_shaders args");
    GlobShaderParams p = {stage};
    nt_builder_glob_add(ctx, pattern, shader_glob_callback, &p);
}

void nt_builder_add_fonts(NtBuilderContext *ctx, const char *pattern, const nt_font_opts_t *opts) {
    NT_BUILD_ASSERT(ctx && pattern && opts && "invalid add_fonts args");
    GlobFontParams p = {opts};
    nt_builder_glob_add(ctx, pattern, font_glob_callback, &p);
}
