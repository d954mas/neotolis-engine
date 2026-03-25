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

static void path_to_identifier(const char *path, const char *type_prefix, char *out, size_t out_size) {
    int written = snprintf(out, out_size, "ASSET_%s_", type_prefix);
    if (written < 0 || (size_t)written >= out_size) {
        return;
    }

    /* Find extension to strip: last '.' that is after the last '/' */
    const char *ext = strrchr(path, '.');
    const char *last_slash = strrchr(path, '/');
    if (ext && (!last_slash || ext > last_slash)) {
        /* ext is in the filename portion -- strip it */
    } else {
        ext = path + strlen(path); /* no extension to strip */
    }

    for (const char *p = path; p < ext && (size_t)written < out_size - 1; p++) {
        char c = *p;
        if (c == '/' || c == '.' || c == '-' || c == ' ') {
            out[written++] = '_';
        } else {
            out[written++] = (char)toupper((unsigned char)c);
        }
    }
    out[written] = '\0';
}

/* --- Header path derivation --- */

static void derive_header_path(const char *pack_path, char *header_path, size_t size) {
    strncpy(header_path, pack_path, size - 1);
    header_path[size - 1] = '\0';
    char *dot = strrchr(header_path, '.');
    if (dot && (size_t)(dot - header_path) < size - 3) {
        strcpy(dot, ".h"); /* NOLINT(clang-analyzer-security.insecureAPI.strcpy) */
    } else {
        strncat(header_path, ".h", size - strlen(header_path) - 1);
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

static void check_identifier_collisions(const NtBuilderContext *ctx) {
    char identifiers[NT_BUILD_MAX_ASSETS][512];
    uint32_t count = ctx->pending_count;

    for (uint32_t i = 0; i < count; i++) {
        const NtBuildEntry *pe = &ctx->pending[i];
        const char *prefix = type_prefix_for_kind(pe->kind);
        const char *logical_path = pe->rename_key ? pe->rename_key : pe->path;
        path_to_identifier(logical_path, prefix, identifiers[i], sizeof(identifiers[i]));
    }

    /* O(n^2) is fine for small asset counts */
    for (uint32_t i = 0; i < count; i++) {
        for (uint32_t j = i + 1; j < count; j++) {
            if (strcmp(identifiers[i], identifiers[j]) == 0) {
                NT_LOG_WARN("Codegen: identifier collision '%s' between '%s' and '%s'", identifiers[i], ctx->pending[i].rename_key ? ctx->pending[i].rename_key : ctx->pending[i].path,
                            ctx->pending[j].rename_key ? ctx->pending[j].rename_key : ctx->pending[j].path);
            }
        }
    }
}

/* --- Main codegen function --- */

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
nt_build_result_t nt_builder_generate_header(const NtBuilderContext *ctx) {
    char header_path[512];
    derive_header_path(ctx->output_path, header_path, sizeof(header_path));

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

    /* Group entries by type for organized output */
    const char *type_order[] = {"MESH", "TEXTURE", "SHADER", "BLOB"};
    for (int t = 0; t < 4; t++) {
        bool header_printed = false;
        for (uint32_t i = 0; i < ctx->pending_count; i++) {
            const NtBuildEntry *pe = &ctx->pending[i];
            const char *prefix = type_prefix_for_kind(pe->kind);
            if (strcmp(prefix, type_order[t]) != 0) {
                continue;
            }

            if (!header_printed) {
                (void)fprintf(f, "/* --- %s --- */\n", type_order[t]);
                header_printed = true;
            }

            const char *logical_path = pe->rename_key ? pe->rename_key : pe->path;

            char identifier[512];
            path_to_identifier(logical_path, prefix, identifier, sizeof(identifier));

            (void)fprintf(f, "#define %s ((nt_hash64_t){0x%016llXULL}) /* %s */\n", identifier, (unsigned long long)pe->resource_id, logical_path);
        }
        if (header_printed) {
            (void)fprintf(f, "\n");
        }
    }

    /* register_labels function */
    (void)fprintf(f, "#if NT_HASH_LABELS\n");
    (void)fprintf(f, "static inline void %s_register_labels(void) {\n", func_prefix);
    for (uint32_t i = 0; i < ctx->pending_count; i++) {
        const NtBuildEntry *pe = &ctx->pending[i];
        const char *logical_path = pe->rename_key ? pe->rename_key : pe->path;
        (void)fprintf(f, "    (void)nt_hash64_str(\"%s\");\n", logical_path);
    }
    (void)fprintf(f, "}\n");
    (void)fprintf(f, "#endif\n\n");

    (void)fprintf(f, "#endif /* %s */\n", guard);
    (void)fclose(f);

    NT_LOG_INFO("Generated header: %s (%u assets)", header_path, ctx->pending_count);
    return NT_BUILD_OK;
}
