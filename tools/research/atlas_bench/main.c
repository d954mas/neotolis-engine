/*
 * atlas_bench — AUDIT-01 measurement engine.
 *
 * Packs one corpus glob into a single-atlas .ntpack with a chosen atlas-opts
 * profile, times the uncached atlas pipeline, then parses the produced pack
 * and writes one JSON
 * file of per-atlas density / page / pack_ms / hull-vertex metrics.
 *
 * Usage:
 *   atlas_bench <out_json> <corpus_glob> <atlas_name> <shape> <max_size> [max_sprites]
 *     shape       = rect | convex | concave
 *     max_size    = max atlas page dimension (e.g. 2048)
 *     max_sprites = optional cap on sprites added (0 / omitted = all)
 *
 * The output directory of <out_json> (and the sibling .ntpack it writes) must
 * already exist. Run from the project root so corpus globs resolve.
 */

#define NT_BUILD_MAX_ASSETS 16384 /* bigatlas is 4813 files */
#define GLOB_MAX_MATCHES 8192
#include "nt_builder.h"

#include "bench_json.h"
#include "ntpack_parse.h"
#include "time/nt_time.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Per-corpus atlas-opts matrix (Claude discretion, D-03 "atlas opts" meta):
 * start from nt_atlas_opts_defaults() and override only shape + max_size from
 * argv. max_vertices stays at the builder default (8) — the same silhouette
 * budget the atlas example ships, so bench numbers track real packs. padding,
 * alpha_threshold, allow_transform, power_of_two keep their defaults (2, 1,
 * true, true). Raw RGBA pages (compress == NULL) so the parser can read page
 * pixel dims from the texture asset header. */
#define BENCH_MAX_VERTICES 8

typedef struct {
    NtBuilderContext *ctx;
    uint32_t count;
    uint32_t limit; /* 0 = unlimited */
} bench_add_data_t;

static void bench_add_callback(const char *path, void *user) {
    bench_add_data_t *d = (bench_add_data_t *)user;
    if (d->limit > 0 && d->count >= d->limit) {
        return;
    }
    nt_builder_atlas_add(d->ctx, path, NULL); /* NULL = default centre pivot, name from path */
    d->count++;
}

static const char *shape_to_name(nt_atlas_shape_t shape) {
    switch (shape) {
    case NT_ATLAS_SHAPE_RECT:
        return "rect";
    case NT_ATLAS_SHAPE_CONVEX_HULL:
        return "convex";
    case NT_ATLAS_SHAPE_CONCAVE_CONTOUR:
        return "concave";
    default:
        return "unknown";
    }
}

int main(int argc, char *argv[]) {
    if (argc < 6) {
        (void)fprintf(stderr, "Usage: atlas_bench <out_json> <corpus_glob> <atlas_name> <shape rect|convex|concave> <max_size> [max_sprites]\n");
        return 1;
    }
    const char *out_json = argv[1];
    const char *corpus_glob = argv[2];
    const char *atlas_name = argv[3];
    const char *shape_arg = argv[4];
    const uint32_t max_size = (uint32_t)strtoul(argv[5], NULL, 10);
    const uint32_t max_sprites = (argc >= 7) ? (uint32_t)strtoul(argv[6], NULL, 10) : 0U;

    nt_atlas_shape_t shape = NT_ATLAS_SHAPE_CONCAVE_CONTOUR;
    if (strcmp(shape_arg, "rect") == 0) {
        shape = NT_ATLAS_SHAPE_RECT;
    } else if (strcmp(shape_arg, "convex") == 0) {
        shape = NT_ATLAS_SHAPE_CONVEX_HULL;
    } else if (strcmp(shape_arg, "concave") == 0) {
        shape = NT_ATLAS_SHAPE_CONCAVE_CONTOUR;
    } else {
        (void)fprintf(stderr, "atlas_bench: bad shape '%s' (want rect|convex|concave)\n", shape_arg);
        return 1;
    }
    if (max_size == 0) {
        (void)fprintf(stderr, "atlas_bench: max_size must be > 0\n");
        return 1;
    }

    /* Sibling .ntpack next to the JSON; the produced pack is what we parse. */
    char pack_path[1024];
    (void)snprintf(pack_path, sizeof(pack_path), "%s.ntpack", out_json);

    NtBuilderContext *ctx = nt_builder_start_pack(pack_path);
    if (ctx == NULL) {
        (void)fprintf(stderr, "atlas_bench: failed to start pack at %s\n", pack_path);
        return 1;
    }

    // NOLINTNEXTLINE(concurrency-mt-unsafe) — single-threaded CLI, getenv is fine
    const char *threads_env = getenv("NT_BUILDER_THREADS");
    if (threads_env != NULL && threads_env[0] != '\0') {
        uint32_t threads = (uint32_t)strtoul(threads_env, NULL, 10);
        if (threads > 0) {
            nt_builder_set_threads(ctx, threads);
        } else {
            nt_builder_set_threads_auto(ctx);
        }
    } else {
        nt_builder_set_threads_auto(ctx);
    }
    /* Deliberately NO nt_builder_set_cache_dir — a cache hit skips packing and
     * would report a garbage (near-zero) pack_ms (Pitfall 2). */

    nt_atlas_opts_t opts = nt_atlas_opts_defaults();
    opts.shape = shape;
    opts.max_size = max_size;
    opts.max_vertices = BENCH_MAX_VERTICES;

    nt_builder_begin_atlas(ctx, atlas_name, &opts);
    bench_add_data_t add = {ctx, 0, max_sprites};
    (void)nt_builder_glob_iterate(corpus_glob, bench_add_callback, &add);
    if (add.count == 0) {
        /* Fail loud (Pitfall 5): an empty glob means missing LFS assets or a
         * bad path — a zero-sprite pack would silently produce meaningless
         * metrics. */
        (void)fprintf(stderr, "atlas_bench: corpus glob '%s' matched 0 sprites (LFS pulled? path correct?)\n", corpus_glob);
        nt_builder_free_pack(ctx);
        return 1;
    }
    const double t0 = nt_time_now();
    nt_builder_end_atlas(ctx);
    const double pack_ms = (nt_time_now() - t0) * 1000.0;
    const nt_build_result_t r = nt_builder_finish_pack(ctx);
    nt_builder_free_pack(ctx);

    if (r != NT_BUILD_OK) {
        (void)fprintf(stderr, "atlas_bench: finish_pack failed (%d)\n", r);
        return 1;
    }

    nt_bench_atlas_metrics_t m;
    const int prc = nt_bench_parse_ntpack(pack_path, &m);
    if (prc != 0) {
        (void)fprintf(stderr, "atlas_bench: failed to parse produced pack %s (%d)\n", pack_path, prc);
        return 1;
    }

    nt_bench_run_t run;
    memset(&run, 0, sizeof(run));
    (void)snprintf(run.tool_version, sizeof(run.tool_version), "1.0.0");
    run.builder_version = NT_BUILDER_VERSION;
    (void)snprintf(run.corpus, sizeof(run.corpus), "%s", corpus_glob);
#ifdef _WIN32
    (void)snprintf(run.os, sizeof(run.os), "windows");
#elif defined(__linux__)
    (void)snprintf(run.os, sizeof(run.os), "linux");
#elif defined(__APPLE__)
    (void)snprintf(run.os, sizeof(run.os), "macos");
#else
    (void)snprintf(run.os, sizeof(run.os), "unknown");
#endif
#ifdef _WIN32
    // NOLINTNEXTLINE(concurrency-mt-unsafe) — single-threaded CLI, getenv is fine
    const char *cpu_env = getenv("PROCESSOR_IDENTIFIER");
#else
    const char *cpu_env = NULL;
#endif
    (void)snprintf(run.cpu, sizeof(run.cpu), "%s", (cpu_env != NULL && cpu_env[0] != '\0') ? cpu_env : "unknown");
    (void)snprintf(run.opts_shape, sizeof(run.opts_shape), "%s", shape_to_name(opts.shape));
    run.opts_max_size = opts.max_size;
    run.opts_padding = opts.padding;
    run.opts_max_vertices = opts.max_vertices;
    run.opts_allow_transform = opts.allow_transform ? 1 : 0;
    run.opts_alpha_threshold = opts.alpha_threshold;
    run.opts_power_of_two = opts.power_of_two ? 1 : 0;
    /* cache_hits/misses and unique are reserved for the Phase 83 builder stats
     * API (DIAG-01); Phase 78 reads only the produced pack (D-02), which does
     * not encode the dedup-unique or cache counters. Left at 0. */
    run.cache_hits = 0;
    run.cache_misses = 0;

    run.atlas_count = 1;
    nt_bench_atlas_result_t *a = &run.atlases[0];
    (void)snprintf(a->name, sizeof(a->name), "%s", atlas_name);
    a->sprites = m.region_count;
    a->unique = 0; /* not recoverable from the pack (see cache note above) */
    a->pages = m.page_count;
    a->region_count = m.region_count;
    a->pack_ms = pack_ms;
    a->density_fill_frontier = m.density_fill_frontier;
    a->density_fill_texture = m.density_fill_texture;
    a->hull_total = m.hull_vert_total;
    a->hull_min = m.hull_vert_min;
    a->hull_max = m.hull_vert_max;
    a->hull_mean = (m.region_count > 0) ? ((double)m.hull_vert_total / (double)m.region_count) : 0.0;

    const int wrc = nt_bench_write_json(out_json, &run);
    if (wrc != 0) {
        (void)fprintf(stderr, "atlas_bench: failed to write JSON %s (%d)\n", out_json, wrc);
        return 1;
    }

    (void)printf("atlas_bench: %s shape=%s pages=%u sprites=%u pack_ms=%.2f fill_texture=%.4f fill_frontier=%.4f -> %s\n", atlas_name, shape_to_name(opts.shape), m.page_count, m.region_count, pack_ms,
                 m.density_fill_texture, m.density_fill_frontier, out_json);
    return 0;
}
