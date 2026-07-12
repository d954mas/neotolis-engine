#ifndef NT_BENCH_JSON_H
#define NT_BENCH_JSON_H

#include <stdint.h>

/* Timings are machine-bound; density, page, and vertex metrics are portable. */

#define NT_BENCH_MAX_ATLASES 16
#define NT_BENCH_STR_MAX 256

typedef struct {
    char name[NT_BENCH_STR_MAX];
    uint32_t sprites;      /* input sprite count (region_count) */
    uint32_t unique;       /* deduped unique-sprite count */
    uint32_t pages;        /* atlas page count */
    uint32_t region_count; /* NtAtlasHeader.region_count (== sprites) */
    double pack_ms;        /* atlas pipeline wall time, machine-bound */
    double density_fill_frontier;
    double density_fill_texture;
    uint32_t hull_total; /* summed hull vertex count across regions */
    uint32_t hull_min;
    uint32_t hull_max;
    double hull_mean;
} nt_bench_atlas_result_t;

typedef struct {
    /* meta */
    char tool_version[NT_BENCH_STR_MAX];
    int builder_version; /* NT_BUILDER_VERSION */
    char corpus[NT_BENCH_STR_MAX];
    char os[NT_BENCH_STR_MAX];
    char cpu[NT_BENCH_STR_MAX];
    /* meta.atlas_opts */
    char opts_shape[32]; /* "rect" | "convex" | "concave" */
    uint32_t opts_max_size;
    uint32_t opts_padding;
    uint32_t opts_max_vertices;
    int opts_allow_transform; /* bool */
    uint32_t opts_alpha_threshold;
    int opts_power_of_two; /* bool */
    uint64_t cache_hits;
    uint64_t cache_misses;
    /* atlases */
    nt_bench_atlas_result_t atlases[NT_BENCH_MAX_ATLASES];
    uint32_t atlas_count;
} nt_bench_run_t;

/* Write run as a single JSON object to out_path. Returns 0 on success,
 * negative on a NULL arg or file-open/write failure. */
int nt_bench_write_json(const char *out_path, const nt_bench_run_t *run);

#endif /* NT_BENCH_JSON_H */
