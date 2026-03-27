/* clang-format off */
#include "nt_builder_internal.h"
#include "hash/nt_hash.h"
/* clang-format on */

#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <direct.h>
#include <windows.h>
#else
#include <dirent.h>
#include <sys/stat.h>
#endif

/* --- opts_version_hash: serialize kind + type-specific fields + builder version --- */

uint64_t nt_builder_compute_opts_hash(const NtBuildEntry *pe) {
    uint8_t buf[128];
    uint32_t pos = 0;

    /* Builder version -- invalidates all cache on encoder changes (D-03) */
    uint32_t version = NT_BUILDER_VERSION;
    memcpy(buf + pos, &version, sizeof(version));
    pos += (uint32_t)sizeof(version);

    /* Asset kind */
    uint32_t kind = (uint32_t)pe->kind;
    memcpy(buf + pos, &kind, sizeof(kind));
    pos += (uint32_t)sizeof(kind);

    switch (pe->kind) {
    case NT_BUILD_ASSET_TEXTURE: {
        const NtBuildTextureData *td = (const NtBuildTextureData *)pe->data;
        /* format (encode-affecting, same as opts_equal) */
        uint32_t format = (uint32_t)td->opts.format;
        memcpy(buf + pos, &format, sizeof(format));
        pos += (uint32_t)sizeof(format);

        /* compression path */
        uint8_t has_compress = td->has_compress ? 1 : 0;
        memcpy(buf + pos, &has_compress, sizeof(has_compress));
        pos += (uint32_t)sizeof(has_compress);

        if (td->has_compress) {
            uint32_t mode = (uint32_t)td->compress.mode;
            memcpy(buf + pos, &mode, sizeof(mode));
            pos += (uint32_t)sizeof(mode);

            uint32_t quality = td->compress.quality;
            memcpy(buf + pos, &quality, sizeof(quality));
            pos += (uint32_t)sizeof(quality);

            float endpoint_rdo = td->compress.endpoint_rdo_quality;
            memcpy(buf + pos, &endpoint_rdo, sizeof(endpoint_rdo));
            pos += (uint32_t)sizeof(endpoint_rdo);

            float selector_rdo = td->compress.selector_rdo_quality;
            memcpy(buf + pos, &selector_rdo, sizeof(selector_rdo));
            pos += (uint32_t)sizeof(selector_rdo);
        }
        break;
    }
    case NT_BUILD_ASSET_SHADER: {
        const NtBuildShaderData *sd = (const NtBuildShaderData *)pe->data;
        uint32_t stage = (uint32_t)sd->stage;
        memcpy(buf + pos, &stage, sizeof(stage));
        pos += (uint32_t)sizeof(stage);
        break;
    }
    case NT_BUILD_ASSET_MESH:
    case NT_BUILD_ASSET_BLOB:
        /* No additional fields -- kind + version sufficient */
        break;
    }

    NT_BUILD_ASSERT(pos <= sizeof(buf) && "opts_hash: buffer overflow — increase buf size");
    nt_hash64_t h = nt_hash64(buf, pos);
    return h.value;
}

/* --- Cache path construction --- */

void nt_builder_build_cache_path(const char *cache_dir, uint64_t decoded_hash, uint64_t opts_hash, char *out, size_t out_size) {
    (void)snprintf(out, out_size, "%s/%016llx_%016llx.bin", cache_dir, (unsigned long long)decoded_hash, (unsigned long long)opts_hash);
}

/* --- Directory creation --- */

static bool dir_exists(const char *dir) {
#ifdef _WIN32
    DWORD attr = GetFileAttributesA(dir);
    return (attr != INVALID_FILE_ATTRIBUTES) && (attr & FILE_ATTRIBUTE_DIRECTORY);
#else
    struct stat st;
    return stat(dir, &st) == 0 && S_ISDIR(st.st_mode);
#endif
}

void nt_builder_ensure_cache_dir(const char *dir) {
#ifdef _WIN32
    (void)_mkdir(dir);
#else
    (void)mkdir(dir, 0755);
#endif
    NT_BUILD_ASSERT(dir_exists(dir) && "cache dir creation failed — check that parent directory exists");
}

/* --- Cache lookup --- */

nt_cache_status_t nt_builder_cache_lookup(const char *cache_dir, uint64_t decoded_hash, uint64_t opts_hash, uint8_t **out_data, uint32_t *out_size) {
    char path[1024];
    nt_builder_build_cache_path(cache_dir, decoded_hash, opts_hash, path, sizeof(path));

    /* Try exact match first */
    FILE *f = fopen(path, "rb");
    if (f) {
        /* Read file: fseek(END), ftell, fseek(SET), malloc, fread */
        if (fseek(f, 0, SEEK_END) != 0) {
            (void)fclose(f);
            return NT_CACHE_MISS_NEW;
        }
        long flen = ftell(f);
        if (flen <= 0) {
            (void)fclose(f);
            return NT_CACHE_MISS_NEW;
        }
        if (fseek(f, 0, SEEK_SET) != 0) {
            (void)fclose(f);
            return NT_CACHE_MISS_NEW;
        }

        uint32_t size = (uint32_t)flen;
        uint8_t *data = (uint8_t *)malloc(size);
        if (!data) {
            (void)fclose(f);
            return NT_CACHE_MISS_NEW;
        }

        size_t read = fread(data, 1, size, f);
        (void)fclose(f);
        if (read != size) {
            free(data);
            return NT_CACHE_MISS_NEW;
        }

        *out_data = data;
        *out_size = size;
        return NT_CACHE_HIT;
    }

    /* No exact match -- scan for prefix to distinguish miss(new) vs miss(opts) (D-04) */
    char prefix[32];
    (void)snprintf(prefix, sizeof(prefix), "%016llx_", (unsigned long long)decoded_hash);

#ifdef _WIN32
    char scan_pattern[1024];
    (void)snprintf(scan_pattern, sizeof(scan_pattern), "%s/%s*.bin", cache_dir, prefix);
    WIN32_FIND_DATAA fd;
    HANDLE hFind = FindFirstFileA(scan_pattern, &fd);
    if (hFind != INVALID_HANDLE_VALUE) {
        (void)FindClose(hFind);
        return NT_CACHE_MISS_OPTS;
    }
#else
    size_t prefix_len = strlen(prefix);
    DIR *d = opendir(cache_dir);
    if (d) {
        struct dirent *ent;
        while ((ent = readdir(d)) != NULL) { // NOLINT(concurrency-mt-unsafe)
            if (strncmp(ent->d_name, prefix, prefix_len) == 0) {
                (void)closedir(d);
                return NT_CACHE_MISS_OPTS;
            }
        }
        (void)closedir(d);
    }
#endif

    return NT_CACHE_MISS_NEW;
}

/* --- Cache store --- */

bool nt_builder_cache_store(const char *cache_dir, uint64_t decoded_hash, uint64_t opts_hash, const uint8_t *data, uint32_t size) {
    char path[1024];
    nt_builder_build_cache_path(cache_dir, decoded_hash, opts_hash, path, sizeof(path));

    /* Write to temp + rename for crash safety */
    char tmp_path[1040];
    (void)snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", path);

    FILE *f = fopen(tmp_path, "wb");
    if (!f) {
        return false;
    }

    size_t written = fwrite(data, 1, size, f);
    (void)fclose(f);

    if (written != size) {
        (void)remove(tmp_path);
        return false;
    }

    /* Atomic rename (overwrite on POSIX, may need remove on Windows) */
#ifdef _WIN32
    /* Windows rename fails if destination exists; remove first */
    (void)remove(path);
#endif
    if (rename(tmp_path, path) != 0) {
        (void)remove(tmp_path);
        return false;
    }

    return true;
}

/* --- Public API --- */

void nt_builder_set_cache_dir(NtBuilderContext *ctx, const char *dir) {
    NT_BUILD_ASSERT(ctx && dir && "set_cache_dir: both ctx and dir required (D-08)");
    NT_BUILD_ASSERT(strlen(dir) < 900 && "cache_dir too long (max ~900, leaves room for hash filename)");
    free(ctx->cache_dir);
    ctx->cache_dir = strdup(dir);
    nt_builder_ensure_cache_dir(dir);
    NT_LOG_INFO("Cache directory: %s", dir);
}
