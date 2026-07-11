#include "bench_json.h"

#include <stdio.h>

/* Emit a JSON string literal with minimal escaping (quote + backslash + the
 * control chars a Windows path or corpus glob can carry). Enough for the
 * numbers-and-paths shape this tool writes — not a general JSON encoder. */
static void json_str(FILE *f, const char *s) {
    (void)fputc('"', f);
    for (const char *p = s; *p != '\0'; p++) {
        switch (*p) {
        case '"':
            (void)fputs("\\\"", f);
            break;
        case '\\':
            (void)fputs("\\\\", f);
            break;
        case '\n':
            (void)fputs("\\n", f);
            break;
        case '\r':
            (void)fputs("\\r", f);
            break;
        case '\t':
            (void)fputs("\\t", f);
            break;
        default:
            (void)fputc(*p, f);
            break;
        }
    }
    (void)fputc('"', f);
}

int nt_bench_write_json(const char *out_path, const nt_bench_run_t *run) {
    if (out_path == NULL || run == NULL) {
        return -1;
    }

    FILE *f = fopen(out_path, "wb");
    if (f == NULL) {
        return -2;
    }

    (void)fputs("{\n", f);

    /* meta block */
    (void)fputs("  \"meta\": {\n", f);
    (void)fputs("    \"tool_version\": ", f);
    json_str(f, run->tool_version);
    (void)fputs(",\n", f);
    (void)fprintf(f, "    \"builder_version\": %d,\n", run->builder_version);
    (void)fputs("    \"corpus\": ", f);
    json_str(f, run->corpus);
    (void)fputs(",\n", f);
    (void)fputs("    \"os\": ", f);
    json_str(f, run->os);
    (void)fputs(",\n", f);
    (void)fputs("    \"cpu\": ", f);
    json_str(f, run->cpu);
    (void)fputs(",\n", f);
    (void)fputs("    \"atlas_opts\": {\n", f);
    (void)fputs("      \"shape\": ", f);
    json_str(f, run->opts_shape);
    (void)fputs(",\n", f);
    (void)fprintf(f, "      \"max_size\": %u,\n", run->opts_max_size);
    (void)fprintf(f, "      \"padding\": %u,\n", run->opts_padding);
    (void)fprintf(f, "      \"max_vertices\": %u,\n", run->opts_max_vertices);
    (void)fprintf(f, "      \"allow_transform\": %s,\n", run->opts_allow_transform ? "true" : "false");
    (void)fprintf(f, "      \"alpha_threshold\": %u,\n", run->opts_alpha_threshold);
    (void)fprintf(f, "      \"power_of_two\": %s\n", run->opts_power_of_two ? "true" : "false");
    (void)fputs("    },\n", f);
    (void)fprintf(f, "    \"cache_hits\": %llu,\n", (unsigned long long)run->cache_hits);
    (void)fprintf(f, "    \"cache_misses\": %llu\n", (unsigned long long)run->cache_misses);
    (void)fputs("  },\n", f);

    /* atlases array */
    (void)fputs("  \"atlases\": [\n", f);
    for (uint32_t i = 0; i < run->atlas_count; i++) {
        const nt_bench_atlas_result_t *a = &run->atlases[i];
        (void)fputs("    {\n", f);
        (void)fputs("      \"name\": ", f);
        json_str(f, a->name);
        (void)fputs(",\n", f);
        (void)fprintf(f, "      \"sprites\": %u,\n", a->sprites);
        (void)fprintf(f, "      \"unique\": %u,\n", a->unique);
        (void)fprintf(f, "      \"pages\": %u,\n", a->pages);
        (void)fprintf(f, "      \"region_count\": %u,\n", a->region_count);
        (void)fprintf(f, "      \"pack_ms\": %.4f,\n", a->pack_ms);
        (void)fprintf(f, "      \"density_fill_frontier\": %.4f,\n", a->density_fill_frontier);
        (void)fprintf(f, "      \"density_fill_texture\": %.4f,\n", a->density_fill_texture);
        (void)fputs("      \"hull_verts\": {\n", f);
        (void)fprintf(f, "        \"total\": %u,\n", a->hull_total);
        (void)fprintf(f, "        \"min\": %u,\n", a->hull_min);
        (void)fprintf(f, "        \"max\": %u,\n", a->hull_max);
        (void)fprintf(f, "        \"mean\": %.4f\n", a->hull_mean);
        (void)fputs("      }\n", f);
        (void)fputs(i + 1 < run->atlas_count ? "    },\n" : "    }\n", f);
    }
    (void)fputs("  ]\n", f);

    (void)fputs("}\n", f);

    const int err = ferror(f);
    if (fclose(f) != 0 || err != 0) {
        return -3;
    }
    return 0;
}
