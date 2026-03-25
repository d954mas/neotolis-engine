/* clang-format off */
#include "nt_builder_internal.h"
/* clang-format on */

#include <ctype.h>

/* --- Path-to-identifier conversion --- */

static const char *type_prefix_for_kind(nt_build_asset_kind_t kind) {
    switch (kind) {
    case NT_BUILD_ASSET_MESH:
    case NT_BUILD_ASSET_SCENE_MESH:
        return "MESH";
    case NT_BUILD_ASSET_TEXTURE:
    case NT_BUILD_ASSET_TEXTURE_MEM:
    case NT_BUILD_ASSET_TEXTURE_RAW:
    case NT_BUILD_ASSET_TEXTURE_MEM_COMPRESSED:
        return "TEXTURE";
    case NT_BUILD_ASSET_SHADER:
        return "SHADER";
    case NT_BUILD_ASSET_BLOB:
        return "BLOB";
    default:
        return "UNKNOWN";
    }
}

#ifndef NT_CODEGEN_MAX_IDENTIFIER
#define NT_CODEGEN_MAX_IDENTIFIER 128
#endif

static bool path_to_identifier(const char *path, const char *type_prefix, char *out, size_t out_size) {
    int written = snprintf(out, out_size, "ASSET_%s_", type_prefix);
    if (written < 0 || (size_t)written >= out_size) {
        return false;
    }

    /* Convert full path including extension -- extension is part of identity
     * (e.g., sponza_alpha.vert vs sponza_alpha.frag must produce different identifiers) */
    const char *end = path + strlen(path);
    const char *p = path;
    for (; p < end && (size_t)written < out_size - 1; p++) {
        char c = *p;
        if (c == '/' || c == '\\' || c == '.' || c == '-' || c == ' ') {
            out[written++] = '_';
        } else {
            out[written++] = (char)toupper((unsigned char)c);
        }
    }
    out[written] = '\0';

    /* Detect truncation: path had more chars to convert */
    if (p < end) {
        NT_LOG_WARN("Codegen: identifier truncated at %zu chars for '%s' (increase NT_CODEGEN_MAX_IDENTIFIER)", out_size, path);
        return false;
    }
    return true;
}

/* --- Sorted index for deterministic, diffable output --- */

typedef struct {
    uint32_t index;       /* original index into pending[] or registry entries[] */
    const char *sort_key; /* logical path for sorting */
} SortEntry;

static int sort_entry_cmp(const void *a, const void *b) { return strcmp(((const SortEntry *)a)->sort_key, ((const SortEntry *)b)->sort_key); }

/* --- Header path derivation --- */

static void derive_header_path(const char *pack_path, const char *header_dir, char *header_path, size_t size) {
    if (header_dir) {
        /* Extract pack filename stem, put .h in header_dir */
        const char *slash = strrchr(pack_path, '/');
        const char *bslash = strrchr(pack_path, '\\');
        const char *filename = pack_path;
        if (slash && slash > filename) {
            filename = slash + 1;
        }
        if (bslash && bslash + 1 > filename) {
            filename = bslash + 1;
        }
        /* Copy filename, replace extension with .h */
        char stem[256];
        strncpy(stem, filename, sizeof(stem) - 1);
        stem[sizeof(stem) - 1] = '\0';
        char *dot = strrchr(stem, '.');
        if (dot) {
            *dot = '\0';
        }
        (void)snprintf(header_path, size, "%s/%s.h", header_dir, stem);
    } else {
        /* Default: .h next to .ntpack */
        strncpy(header_path, pack_path, size - 1);
        header_path[size - 1] = '\0';
        char *dot = strrchr(header_path, '.');
        if (dot && (size_t)(dot - header_path) < size - 3) {
            strcpy(dot, ".h"); /* NOLINT(clang-analyzer-security.insecureAPI.strcpy) */
        } else {
            size_t cur_len = strlen(header_path);
            if (cur_len + 2 < size) {
                strncat(header_path, ".h", size - cur_len - 1);
            }
        }
    }
}

/* --- Include guard derivation --- */

static void derive_include_guard(const char *header_path, char *guard, size_t guard_size) {
    /* Extract filename from path */
    const char *last_slash = strrchr(header_path, '/');
    const char *last_bslash = strrchr(header_path, '\\');
    const char *filename = header_path;
    if (last_slash && last_slash > filename) {
        filename = last_slash + 1;
    }
    if (last_bslash && last_bslash + 1 > filename) {
        filename = last_bslash + 1;
    }

    size_t wi = 0;
    for (const char *p = filename; *p && wi < guard_size - 11; p++) {
        char c = *p;
        if (isalnum((unsigned char)c)) {
            guard[wi++] = (char)toupper((unsigned char)c);
        } else {
            guard[wi++] = '_';
        }
    }
    /* Append _GENERATED */
    const char *suffix = "_GENERATED";
    size_t slen = strlen(suffix);
    if (wi + slen < guard_size) {
        memcpy(guard + wi, suffix, slen);
        wi += slen;
    }
    guard[wi] = '\0';
}

/* --- Function prefix derivation --- */

static void derive_func_prefix(const char *header_path, char *prefix, size_t prefix_size) {
    /* Extract filename stem (no extension, no directory) */
    const char *last_slash = strrchr(header_path, '/');
    const char *last_bslash = strrchr(header_path, '\\');
    const char *filename = header_path;
    if (last_slash && last_slash > filename) {
        filename = last_slash + 1;
    }
    if (last_bslash && last_bslash + 1 > filename) {
        filename = last_bslash + 1;
    }

    size_t wi = 0;
    for (const char *p = filename; *p && *p != '.' && wi < prefix_size - 1; p++) {
        char c = *p;
        if (isalnum((unsigned char)c)) {
            prefix[wi++] = (char)tolower((unsigned char)c);
        } else {
            prefix[wi++] = '_';
        }
    }
    prefix[wi] = '\0';
}

/* --- Collision detection --- */

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void check_identifier_collisions(const NtBuilderContext *ctx) {
    uint32_t count = ctx->pending_count;
    if (count == 0) {
        return;
    }

    /* Heap-allocate identifier table -- not hot path, avoids large stack frame */
    char(*identifiers)[NT_CODEGEN_MAX_IDENTIFIER] = (char(*)[NT_CODEGEN_MAX_IDENTIFIER])malloc((size_t)count * NT_CODEGEN_MAX_IDENTIFIER);
    if (!identifiers) {
        return;
    }

    for (uint32_t i = 0; i < count; i++) {
        const NtBuildEntry *pe = &ctx->pending[i];
        const char *prefix = type_prefix_for_kind(pe->kind);
        const char *logical_path = pe->rename_key ? pe->rename_key : pe->path;
        path_to_identifier(logical_path, prefix, identifiers[i], NT_CODEGEN_MAX_IDENTIFIER);
    }

    /* O(n^2) is fine for small asset counts */
    for (uint32_t i = 0; i < count; i++) {
        for (uint32_t j = i + 1; j < count; j++) {
            if (strcmp(identifiers[i], identifiers[j]) == 0) {
                NT_LOG_ERROR("Codegen: identifier collision '%s' between '%s' and '%s'", identifiers[i], ctx->pending[i].rename_key ? ctx->pending[i].rename_key : ctx->pending[i].path,
                             ctx->pending[j].rename_key ? ctx->pending[j].rename_key : ctx->pending[j].path);
                free(identifiers);
                NT_BUILD_ASSERT(0 && "codegen identifier collision -- rename one of the conflicting assets");
            }
        }
    }
    free(identifiers);
}

/* --- Shared: write sorted entries to FILE --- */

typedef struct {
    const char *path; /* logical path (borrowed) */
    uint64_t resource_id;
    nt_build_asset_kind_t kind;
} CodegenEntry;

static void write_sorted_defines(FILE *f, const CodegenEntry *entries, uint32_t count) {
    /* Build sort index per type group */
    const char *type_order[] = {"MESH", "TEXTURE", "SHADER", "BLOB"};

    SortEntry sorted[NT_BUILD_MAX_ASSETS];

    for (int t = 0; t < 4; t++) {
        uint32_t group_count = 0;
        for (uint32_t i = 0; i < count; i++) {
            const char *prefix = type_prefix_for_kind(entries[i].kind);
            if (strcmp(prefix, type_order[t]) != 0) {
                continue;
            }
            sorted[group_count].index = i;
            sorted[group_count].sort_key = entries[i].path;
            group_count++;
        }
        if (group_count == 0) {
            continue;
        }

        qsort(sorted, group_count, sizeof(SortEntry), sort_entry_cmp);

        (void)fprintf(f, "/* --- %s --- */\n", type_order[t]);
        for (uint32_t s = 0; s < group_count; s++) {
            const CodegenEntry *e = &entries[sorted[s].index];
            const char *prefix = type_prefix_for_kind(e->kind);
            char identifier[NT_CODEGEN_MAX_IDENTIFIER];
            path_to_identifier(e->path, prefix, identifier, sizeof(identifier));
            (void)fprintf(f, "#define %s ((nt_hash64_t){0x%016llXULL}) /* %s */\n", identifier, (unsigned long long)e->resource_id, e->path);
        }
        (void)fprintf(f, "\n");
    }
}

static void write_register_labels(FILE *f, const char *func_prefix, const CodegenEntry *entries, uint32_t count) {
    /* Sort all entries by path for deterministic output */
    SortEntry sorted[NT_BUILD_MAX_ASSETS];
    for (uint32_t i = 0; i < count; i++) {
        sorted[i].index = i;
        sorted[i].sort_key = entries[i].path;
    }
    qsort(sorted, count, sizeof(SortEntry), sort_entry_cmp);

    (void)fprintf(f, "#if NT_HASH_LABELS\n");
    (void)fprintf(f, "static inline void %s_register_labels(void) {\n", func_prefix);
    for (uint32_t i = 0; i < count; i++) {
        (void)fprintf(f, "    (void)nt_hash64_str(\"%s\");\n", entries[sorted[i].index].path);
    }
    (void)fprintf(f, "}\n");
    (void)fprintf(f, "#endif\n\n");
}

/* --- Per-pack codegen (called from finish_pack) --- */

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
nt_build_result_t nt_builder_generate_header(const NtBuilderContext *ctx) {
    char header_path[512];
    derive_header_path(ctx->output_path, ctx->header_dir, header_path, sizeof(header_path));

    char guard[256];
    derive_include_guard(header_path, guard, sizeof(guard));

    char func_prefix[128];
    derive_func_prefix(header_path, func_prefix, sizeof(func_prefix));

    /* Check for identifier collisions before writing */
    check_identifier_collisions(ctx);

    FILE *f = fopen(header_path, "w");
    if (!f) {
        NT_LOG_WARN("Could not write codegen header: %s", header_path);
        return NT_BUILD_ERR_IO;
    }

    /* Preamble */
    (void)fprintf(f, "/* Auto-generated by nt_builder -- do not edit */\n");
    (void)fprintf(f, "#ifndef %s\n", guard);
    (void)fprintf(f, "#define %s\n\n", guard);
    (void)fprintf(f, "#include \"hash/nt_hash.h\"\n\n");

    /* Build CodegenEntry array from pending */
    CodegenEntry ce[NT_BUILD_MAX_ASSETS];
    for (uint32_t i = 0; i < ctx->pending_count; i++) {
        const NtBuildEntry *pe = &ctx->pending[i];
        ce[i].path = pe->rename_key ? pe->rename_key : pe->path;
        ce[i].resource_id = pe->resource_id;
        ce[i].kind = pe->kind;
    }

    write_sorted_defines(f, ce, ctx->pending_count);
    write_register_labels(f, func_prefix, ce, ctx->pending_count);

    (void)fprintf(f, "#endif /* %s */\n", guard);
    (void)fclose(f);

    NT_LOG_INFO("Generated header: %s (%u assets)", header_path, ctx->pending_count);
    return NT_BUILD_OK;
}

/* --- Registry: accumulate assets from finish_pack --- */

void nt_builder_register_to_registry(const NtBuilderContext *ctx) {
    NtBuilderRegistry *reg = ctx->registry;
    if (!reg) {
        return;
    }

    for (uint32_t i = 0; i < ctx->pending_count; i++) {
        const NtBuildEntry *pe = &ctx->pending[i];
        const char *logical_path = pe->rename_key ? pe->rename_key : pe->path;
        uint64_t hash = pe->resource_id;

        /* Dedup: skip if hash already registered */
        bool found = false;
        for (uint32_t r = 0; r < reg->count; r++) {
            if (reg->entries[r].hash == hash) {
                found = true;
                break;
            }
        }
        if (found) {
            continue;
        }

        if (reg->count >= NT_BUILD_MAX_ASSETS) {
            NT_LOG_WARN("Registry full (%d max), skipping '%s'", NT_BUILD_MAX_ASSETS, logical_path);
            break;
        }

        reg->entries[reg->count].hash = hash;
        reg->entries[reg->count].path = strdup(logical_path);
        reg->entries[reg->count].kind = pe->kind;
        reg->count++;
    }
}

/* --- Registry: generate combined header --- */

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
nt_build_result_t nt_builder_generate_registry_header(const NtBuilderRegistry *reg, const char *output_path) {
    if (!reg || !output_path) {
        return NT_BUILD_ERR_VALIDATION;
    }

    char guard[256];
    derive_include_guard(output_path, guard, sizeof(guard));

    char func_prefix[128];
    derive_func_prefix(output_path, func_prefix, sizeof(func_prefix));

    FILE *f = fopen(output_path, "w");
    if (!f) {
        NT_LOG_ERROR("Could not write registry header: %s", output_path);
        return NT_BUILD_ERR_IO;
    }

    (void)fprintf(f, "/* Auto-generated by nt_builder -- do not edit */\n");
    (void)fprintf(f, "#ifndef %s\n", guard);
    (void)fprintf(f, "#define %s\n\n", guard);
    (void)fprintf(f, "#include \"hash/nt_hash.h\"\n\n");

    /* Build CodegenEntry array from registry */
    CodegenEntry ce[NT_BUILD_MAX_ASSETS];
    for (uint32_t i = 0; i < reg->count; i++) {
        ce[i].path = reg->entries[i].path;
        ce[i].resource_id = reg->entries[i].hash;
        ce[i].kind = reg->entries[i].kind;
    }

    write_sorted_defines(f, ce, reg->count);
    write_register_labels(f, func_prefix, ce, reg->count);

    (void)fprintf(f, "#endif /* %s */\n", guard);
    (void)fclose(f);

    NT_LOG_INFO("Generated registry header: %s (%u assets)", output_path, reg->count);
    return NT_BUILD_OK;
}
