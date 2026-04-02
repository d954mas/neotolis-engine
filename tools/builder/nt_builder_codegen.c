/* clang-format off */
#include "nt_builder_internal.h"
/* clang-format on */

#include <ctype.h>

/* --- Path-to-identifier conversion --- */

static const char *type_prefix_for_kind(nt_build_asset_kind_t kind) {
    switch (kind) {
    case NT_BUILD_ASSET_MESH:
        return "MESH";
    case NT_BUILD_ASSET_TEXTURE:
        return "TEXTURE";
    case NT_BUILD_ASSET_SHADER:
        return "SHADER";
    case NT_BUILD_ASSET_BLOB:
        return "BLOB";
    case NT_BUILD_ASSET_FONT:
        return "FONT";
    }
    return "UNKNOWN";
}

#ifndef NT_CODEGEN_MAX_IDENTIFIER
#define NT_CODEGEN_MAX_IDENTIFIER 128
#endif

static void path_to_identifier(const char *path, const char *type_prefix, char *out, size_t out_size) {
    int written = snprintf(out, out_size, "ASSET_%s_", type_prefix);
    NT_BUILD_ASSERT(written > 0 && (size_t)written < out_size && "identifier prefix too long");

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
        NT_LOG_ERROR("Codegen: identifier truncated at %zu chars for '%s'", out_size, path);
        NT_BUILD_ASSERT(0 && "identifier too long -- increase NT_CODEGEN_MAX_IDENTIFIER");
    }
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
        char stem[256];
        nt_builder_pack_stem(pack_path, stem, sizeof(stem));
        (void)snprintf(header_path, size, "%s/%s.h", header_dir, stem);
    } else {
        nt_builder_pack_to_header_path(pack_path, header_path, size);
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

/* --- Shared entry type for codegen output --- */

typedef struct {
    const char *path; /* logical path (borrowed) */
    uint64_t resource_id;
    nt_build_asset_kind_t kind;
} CodegenEntry;

/* --- Collision detection --- */

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void check_codegen_collisions(const CodegenEntry *entries, uint32_t count) {
    if (count == 0) {
        return;
    }

    /* Heap-allocate identifier table -- not hot path, avoids large stack frame */
    char(*identifiers)[NT_CODEGEN_MAX_IDENTIFIER] = (char(*)[NT_CODEGEN_MAX_IDENTIFIER])malloc((size_t)count * NT_CODEGEN_MAX_IDENTIFIER);
    if (!identifiers) {
        return;
    }

    for (uint32_t i = 0; i < count; i++) {
        const char *prefix = type_prefix_for_kind(entries[i].kind);
        path_to_identifier(entries[i].path, prefix, identifiers[i], NT_CODEGEN_MAX_IDENTIFIER);
    }

    /* O(n^2) is fine for small asset counts */
    for (uint32_t i = 0; i < count; i++) {
        for (uint32_t j = i + 1; j < count; j++) {
            if (strcmp(identifiers[i], identifiers[j]) == 0) {
                NT_LOG_ERROR("Codegen: identifier collision '%s' between '%s' and '%s'", identifiers[i], entries[i].path, entries[j].path);
                free(identifiers);
                NT_BUILD_ASSERT(0 && "codegen identifier collision -- rename one of the conflicting assets");
            }
        }
    }
    free(identifiers);
}

/* --- Shared: write sorted entries to FILE --- */

static void write_sorted_defines(FILE *f, const CodegenEntry *entries, uint32_t count) {
    /* Build sort index per type group */
    const char *type_order[] = {"MESH", "TEXTURE", "SHADER", "BLOB", "FONT"};

    SortEntry sorted[NT_BUILD_MAX_ASSETS];

    for (int t = 0; t < 5; t++) {
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

    /* Build CodegenEntry array from pending */
    CodegenEntry ce[NT_BUILD_MAX_ASSETS];
    for (uint32_t i = 0; i < ctx->pending_count; i++) {
        const NtBuildEntry *pe = &ctx->pending[i];
        ce[i].path = pe->rename_key ? pe->rename_key : pe->path;
        ce[i].resource_id = pe->resource_id;
        ce[i].kind = pe->kind;
    }

    /* Check for identifier collisions before writing */
    check_codegen_collisions(ce, ctx->pending_count);

    FILE *f = fopen(header_path, "w");
    if (!f) {
        NT_LOG_WARN("Could not write codegen header: %s", header_path);
        return NT_BUILD_ERR_IO;
    }

    /* Preamble */
    (void)fprintf(f, "/* clang-format off */\n");
    (void)fprintf(f, "/* Auto-generated by nt_builder -- do not edit */\n");
    (void)fprintf(f, "#ifndef %s\n", guard);
    (void)fprintf(f, "#define %s\n\n", guard);
    (void)fprintf(f, "#include \"hash/nt_hash.h\"\n\n");

    write_sorted_defines(f, ce, ctx->pending_count);
    write_register_labels(f, func_prefix, ce, ctx->pending_count);

    (void)fprintf(f, "#endif /* %s */\n", guard);
    (void)fclose(f);

    NT_LOG_INFO("Generated header: %s (%u assets)", header_path, ctx->pending_count);
    return NT_BUILD_OK;
}

/* --- Merge per-pack .h headers into a combined header --- */

/* Parse a single #define ASSET_XXX_YYY ((nt_hash64_t){0x...ULL}) line */
typedef struct {
    char identifier[NT_CODEGEN_MAX_IDENTIFIER];
    uint64_t hash;
    char comment_path[256]; /* logical path from comment */
    nt_build_asset_kind_t kind;
} MergeEntry;

static nt_build_asset_kind_t kind_from_identifier(const char *id) {
    if (strstr(id, "ASSET_MESH_") == id) {
        return NT_BUILD_ASSET_MESH;
    }
    if (strstr(id, "ASSET_TEXTURE_") == id) {
        return NT_BUILD_ASSET_TEXTURE;
    }
    if (strstr(id, "ASSET_SHADER_") == id) {
        return NT_BUILD_ASSET_SHADER;
    }
    if (strstr(id, "ASSET_BLOB_") == id) {
        return NT_BUILD_ASSET_BLOB;
    }
    if (strstr(id, "ASSET_FONT_") == id) {
        return NT_BUILD_ASSET_FONT;
    }
    return NT_BUILD_ASSET_BLOB; /* fallback */
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void nt_builder_merge_headers(const char *const *header_paths, uint32_t count, const char *output_path) {
    NT_BUILD_ASSERT(header_paths && count > 0 && output_path && "merge_headers: invalid arguments");

    MergeEntry *entries = (MergeEntry *)calloc(NT_BUILD_MAX_ASSETS, sizeof(MergeEntry));
    NT_BUILD_ASSERT(entries && "merge_headers: alloc failed");
    uint32_t entry_count = 0;

    for (uint32_t hi = 0; hi < count; hi++) {
        FILE *f = fopen(header_paths[hi], "r");
        if (!f) {
            NT_LOG_ERROR("merge_headers: could not open '%s'", header_paths[hi]);
            NT_BUILD_ASSERT(0 && "merge_headers: per-pack header file not found -- run pack builder first");
        }

        char line[1024];
        while (fgets(line, (int)sizeof(line), f) != NULL) {
            /* Look for #define ASSET_... lines */
            if (strncmp(line, "#define ASSET_", 14) != 0) {
                continue;
            }

            /* Parse: #define IDENTIFIER ((nt_hash64_t){0xHEXULL}) / * comment * / */
            char id_buf[NT_CODEGEN_MAX_IDENTIFIER];
            char hex_buf[32];
            memset(id_buf, 0, sizeof(id_buf));
            memset(hex_buf, 0, sizeof(hex_buf));

            const char *p = line + 8; /* skip "#define " */
            /* Read identifier */
            size_t wi = 0;
            while (*p && *p != ' ' && *p != '\t' && wi < sizeof(id_buf) - 1) {
                id_buf[wi++] = *p++;
            }
            id_buf[wi] = '\0';

            /* Find 0x hex value */
            const char *hex_start = strstr(p, "0x");
            if (!hex_start) {
                continue;
            }
            wi = 0;
            const char *hp = hex_start;
            while (*hp && *hp != 'U' && *hp != 'u' && *hp != '}' && wi < sizeof(hex_buf) - 1) {
                hex_buf[wi++] = *hp++;
            }
            hex_buf[wi] = '\0';

            char *end_ptr = NULL;
            uint64_t hash = strtoull(hex_buf, &end_ptr, 16);

            /* Find comment path between slash-star delimiters */
            char comment[256] = {0};
            const char *cs = strstr(p, "/*");
            if (cs) {
                cs += 3; /* skip opening delimiter + space */
                wi = 0;
                while (*cs && *cs != '*' && wi < sizeof(comment) - 1) {
                    comment[wi++] = *cs++;
                }
                /* Trim trailing space */
                while (wi > 0 && comment[wi - 1] == ' ') {
                    wi--;
                }
                comment[wi] = '\0';
            }

            /* Dedup by hash */
            bool found = false;
            for (uint32_t e = 0; e < entry_count; e++) {
                if (entries[e].hash == hash) {
                    found = true;
                    break;
                }
            }
            if (found) {
                continue;
            }
            NT_BUILD_ASSERT(entry_count < NT_BUILD_MAX_ASSETS && "merge_headers: too many unique assets -- increase NT_BUILD_MAX_ASSETS");

            strncpy(entries[entry_count].identifier, id_buf, NT_CODEGEN_MAX_IDENTIFIER - 1);
            entries[entry_count].hash = hash;
            strncpy(entries[entry_count].comment_path, comment, sizeof(entries[entry_count].comment_path) - 1);
            entries[entry_count].kind = kind_from_identifier(id_buf);
            entry_count++;
        }
        (void)fclose(f);
    }

    /* Build CodegenEntry array for shared write functions */
    CodegenEntry *ce = (CodegenEntry *)calloc(entry_count > 0 ? entry_count : 1, sizeof(CodegenEntry));
    NT_BUILD_ASSERT(ce && "merge_headers: alloc failed");
    for (uint32_t i = 0; i < entry_count; i++) {
        ce[i].path = entries[i].comment_path;
        ce[i].resource_id = entries[i].hash;
        ce[i].kind = entries[i].kind;
    }

    char guard[256];
    derive_include_guard(output_path, guard, sizeof(guard));

    char func_prefix[128];
    derive_func_prefix(output_path, func_prefix, sizeof(func_prefix));

    FILE *f = fopen(output_path, "w");
    if (!f) {
        NT_LOG_ERROR("Could not write merged header: %s", output_path);
        free(ce);
        free(entries);
        NT_BUILD_ASSERT(0 && "merge_headers: could not open output file");
    }

    (void)fprintf(f, "/* clang-format off */\n");
    (void)fprintf(f, "/* Auto-generated by nt_builder -- do not edit */\n");
    (void)fprintf(f, "#ifndef %s\n", guard);
    (void)fprintf(f, "#define %s\n\n", guard);
    (void)fprintf(f, "#include \"hash/nt_hash.h\"\n\n");

    write_sorted_defines(f, ce, entry_count);
    write_register_labels(f, func_prefix, ce, entry_count);

    (void)fprintf(f, "#endif /* %s */\n", guard);
    (void)fclose(f);

    NT_LOG_INFO("Generated merged header: %s (%u assets from %u packs)", output_path, entry_count, count);

    free(ce);
    free(entries);
}
