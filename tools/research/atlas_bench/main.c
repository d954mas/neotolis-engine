/* Times an uncached atlas pipeline and reads metrics from the produced pack. */

#define NT_BUILD_MAX_ASSETS 16384 /* bigatlas is 4813 files */
#define GLOB_MAX_MATCHES 8192
#include "nt_builder.h"

#include "atlas_bench_cli.h"
#include "bench_json.h"
#include "nt_builder_atlas_geometry.h"
#include "ntpack_parse.h"
#include "time/nt_time.h"

#include "stb_image.h"

#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Keep production defaults; corpus profiles vary only shape and page size. */
#define BENCH_MAX_VERTICES 8

/* The standalone CLI mirrors the builder's transform presets (atlas_bench_cli.h
 * has no builder dependency); pin them to the single source so they cannot drift. */
_Static_assert(ATLAS_BENCH_TRANSFORMS_ALL == NT_ATLAS_TRANSFORMS_ALL, "CLI ALL preset drifted");
_Static_assert(ATLAS_BENCH_TRANSFORMS_IDENTITY == NT_ATLAS_TRANSFORMS_IDENTITY, "CLI IDENTITY preset drifted");
_Static_assert(ATLAS_BENCH_TRANSFORMS_IDENTITY_ROT90 == NT_ATLAS_TRANSFORMS_IDENTITY_ROT90, "CLI IDENTITY_ROT90 preset drifted");
_Static_assert(ATLAS_BENCH_TRANSFORMS_ROTATIONS == NT_ATLAS_TRANSFORMS_ROTATIONS, "CLI ROTATIONS preset drifted");
_Static_assert(ATLAS_BENCH_TRANSFORMS_FLIPS == NT_ATLAS_TRANSFORMS_FLIPS, "CLI FLIPS preset drifted");

typedef struct {
    NtAtlasBuild *atlas;
    uint32_t count;
    uint32_t limit; /* 0 = unlimited */
    char first_path[1024];
} bench_add_data_t;

static void bench_add_callback(const char *path, void *user) {
    bench_add_data_t *d = (bench_add_data_t *)user;
    if (d->limit > 0 && d->count >= d->limit) {
        return;
    }
    if (d->count == 0U) {
        (void)snprintf(d->first_path, sizeof(d->first_path), "%s", path);
    }
    nt_atlas_add(d->atlas, path, NULL); /* NULL = default centre pivot, name from path */
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

static bool parse_added_area_percent(const char *text, float *out_value) {
    if (text == NULL || text[0] == '\0' || out_value == NULL) {
        return false;
    }
    errno = 0;
    char *end = NULL;
    float value = strtof(text, &end);
    if (errno == ERANGE || end == text || *end != '\0' || !isfinite(value) || value < 0.0F) {
        return false;
    }
    *out_value = (value == 0.0F) ? 0.0F : value;
    return true;
}

typedef struct {
    uint32_t sprite_count;
    double pack_ms;
    char first_path[1024];
} bench_pack_result_t;

static bool build_production_pack(const char *pack_path, const char *corpus_glob, const char *atlas_name, nt_atlas_shape_t shape, uint32_t max_size, uint32_t max_sprites, float percent,
                                  uint8_t allowed_transforms, bench_pack_result_t *out) {
    NtBuilderContext *ctx = nt_builder_start_pack(pack_path);
    if (ctx == NULL) {
        return false;
    }
    // NOLINTNEXTLINE(concurrency-mt-unsafe) — single-threaded CLI reads configuration before workers start
    const char *threads_env = getenv("NT_BUILDER_THREADS");
    if (threads_env != NULL && threads_env[0] != '\0') {
        uint32_t threads = 0U;
        if (!atlas_bench_parse_u32_strict(threads_env, &threads)) {
            (void)fprintf(stderr, "atlas_bench: NT_BUILDER_THREADS must be an integer in 0..%u\n", UINT32_MAX);
            nt_builder_free_pack(ctx);
            return false;
        }
        if (threads > 0U) {
            nt_builder_set_threads(ctx, threads);
        } else {
            nt_builder_set_threads_auto(ctx);
        }
    } else {
        nt_builder_set_threads_auto(ctx);
    }
    nt_atlas_opts_t opts = nt_atlas_opts_defaults();
    opts.shape = shape;
    opts.max_size = max_size;
    opts.max_vertices = BENCH_MAX_VERTICES;
    opts.max_added_area_percent = percent;
    opts.allowed_transforms = allowed_transforms;
    NtAtlasBuild *atlas = nt_atlas_begin(ctx, atlas_name, &opts);
    bench_add_data_t add = {.atlas = atlas, .limit = max_sprites};
    (void)nt_builder_glob_iterate(corpus_glob, bench_add_callback, &add);
    if (add.count == 0U) {
        nt_builder_free_pack(ctx);
        return false;
    }
    const double start = nt_time_now();
    const nt_build_result_t commit = nt_atlas_commit(atlas);
    const double pack_ms = (nt_time_now() - start) * 1000.0;
    const nt_build_result_t finish = nt_builder_finish_pack(ctx);
    if (commit != NT_BUILD_OK || finish != NT_BUILD_OK) {
        uint32_t error_count = 0U;
        const nt_build_error_t *errors = nt_builder_get_errors(ctx, &error_count);
        if (error_count > 0U) {
            (void)fprintf(stderr, "atlas_bench: production pack failed kind=%u sprite=%s detail=%u/%u\n", (uint32_t)errors[0].kind, errors[0].sprite, errors[0].detail_a, errors[0].detail_b);
        }
        nt_builder_free_pack(ctx);
        return false;
    }
    nt_builder_free_pack(ctx);
    memset(out, 0, sizeof(*out));
    out->sprite_count = add.count;
    out->pack_ms = pack_ms;
    (void)snprintf(out->first_path, sizeof(out->first_path), "%s", add.first_path);
    return true;
}

static uint8_t *build_retained_mask(const char *path, uint8_t threshold, uint32_t *out_w, uint32_t *out_h, uint32_t *out_retained) {
    FILE *file = fopen(path, "rb");
    if (file == NULL || fseek(file, 0, SEEK_END) != 0) {
        if (file != NULL) {
            (void)fclose(file);
        }
        return NULL;
    }
    const long length = ftell(file);
    if (length <= 0 || fseek(file, 0, SEEK_SET) != 0) {
        (void)fclose(file);
        return NULL;
    }
    uint8_t *encoded = (uint8_t *)malloc((size_t)length);
    if (encoded == NULL || fread(encoded, 1, (size_t)length, file) != (size_t)length) {
        free(encoded);
        (void)fclose(file);
        return NULL;
    }
    (void)fclose(file);
    int source_w = 0;
    int source_h = 0;
    int channels = 0;
    uint8_t *rgba = stbi_load_from_memory(encoded, (int)length, &source_w, &source_h, &channels, 4);
    free(encoded);
    if (rgba == NULL || source_w <= 0 || source_h <= 0) {
        stbi_image_free(rgba);
        return NULL;
    }
    uint8_t *alpha = alpha_plane_extract(rgba, (uint32_t)source_w, (uint32_t)source_h);
    stbi_image_free(rgba);
    uint32_t trim_x = 0U;
    uint32_t trim_y = 0U;
    uint32_t trim_w = 0U;
    uint32_t trim_h = 0U;
    if (alpha == NULL || !alpha_trim(alpha, (uint32_t)source_w, (uint32_t)source_h, threshold, &trim_x, &trim_y, &trim_w, &trim_h)) {
        free(alpha);
        return NULL;
    }
    uint8_t *mask = (uint8_t *)malloc((size_t)trim_w * trim_h);
    if (mask == NULL) {
        free(alpha);
        return NULL;
    }
    uint32_t retained = 0U;
    for (uint32_t y = 0; y < trim_h; y++) {
        for (uint32_t x = 0; x < trim_w; x++) {
            const uint8_t value = alpha[((trim_y + y) * (uint32_t)source_w) + trim_x + x] >= threshold ? 1U : 0U;
            mask[(y * trim_w) + x] = value;
            retained += value;
        }
    }
    free(alpha);
    *out_w = trim_w;
    *out_h = trim_h;
    *out_retained = retained;
    return mask;
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity) -- CLI validation and one cleanup path stay linear.
int main(int argc, char *argv[]) {
    if (argc < 6) {
        (void)fprintf(stderr, "Usage: atlas_bench <out_json> <corpus_glob> <atlas_name> <shape rect|convex|concave> <max_size> [max_sprites] [--max-added-area-percent <percent>] [--transforms "
                              "<hex|all|identity|identity-rot90|rotations|flips>]\n");
        return 1;
    }
    const char *out_json = argv[1];
    const char *corpus_glob = argv[2];
    const char *atlas_name = argv[3];
    const char *shape_arg = argv[4];
    uint32_t max_size = 0U;
    if (!atlas_bench_parse_max_size(argv[5], &max_size)) {
        (void)fprintf(stderr, "atlas_bench: max_size must be an integer in 1..%u\n", ATLAS_BENCH_MAX_ATLAS_SIZE);
        return 1;
    }
    uint32_t max_sprites = 0U;
    int next_arg = 6;
    if (next_arg < argc && strncmp(argv[next_arg], "--", 2) != 0) {
        if (!atlas_bench_parse_u32_strict(argv[next_arg], &max_sprites)) {
            (void)fprintf(stderr, "atlas_bench: max_sprites must be an integer in 0..%u\n", UINT32_MAX);
            return 1;
        }
        next_arg++;
    }
    nt_atlas_opts_t default_opts = nt_atlas_opts_defaults();
    float max_added_area_percent = default_opts.max_added_area_percent;
    uint8_t allowed_transforms = default_opts.allowed_transforms;
    bool percent_seen = false;
    bool transforms_seen = false;
    while (next_arg < argc) {
        if (strcmp(argv[next_arg], "--max-added-area-percent") == 0) {
            if (percent_seen || next_arg + 1 >= argc || !parse_added_area_percent(argv[next_arg + 1], &max_added_area_percent)) {
                (void)fprintf(stderr, "atlas_bench: bad argument near '%s'\n", argv[next_arg]);
                return 1;
            }
            percent_seen = true;
            next_arg += 2;
        } else if (strcmp(argv[next_arg], "--transforms") == 0) {
            if (transforms_seen || next_arg + 1 >= argc || !atlas_bench_parse_transforms(argv[next_arg + 1], &allowed_transforms)) {
                (void)fprintf(stderr, "atlas_bench: bad argument near '%s'\n", argv[next_arg]);
                return 1;
            }
            transforms_seen = true;
            next_arg += 2;
        } else {
            (void)fprintf(stderr, "atlas_bench: bad argument near '%s'\n", argv[next_arg]);
            return 1;
        }
    }

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
    /* The 0% pack is the authoritative baseline; the requested pack differs only by percent. */
    char pack_path[1024];
    char baseline_pack_path[1024];
    if (!atlas_bench_derive_pack_paths(out_json, pack_path, sizeof(pack_path), baseline_pack_path, sizeof(baseline_pack_path))) {
        (void)fprintf(stderr, "atlas_bench: output path is too long\n");
        return 1;
    }
    bench_pack_result_t baseline_run = {0};
    bench_pack_result_t selected_run = {0};
    nt_bench_atlas_metrics_t m = {0};
    uint8_t *mask = NULL;
    nt_bench_selected_geometry_t baseline_geometry = {0};
    nt_bench_selected_geometry_t selected_geometry = {0};
    int exit_code = 1;
    bool keep_selected_pack = false;
    bool json_write_started = false;
    if (!build_production_pack(baseline_pack_path, corpus_glob, atlas_name, shape, max_size, max_sprites, 0.0F, allowed_transforms, &baseline_run) ||
        !build_production_pack(pack_path, corpus_glob, atlas_name, shape, max_size, max_sprites, max_added_area_percent, allowed_transforms, &selected_run) ||
        baseline_run.sprite_count != selected_run.sprite_count || strcmp(baseline_run.first_path, selected_run.first_path) != 0) {
        (void)fprintf(stderr, "atlas_bench: failed to build matching production baseline/selected packs\n");
        goto cleanup;
    }

    const int prc = nt_bench_parse_ntpack(pack_path, &m);
    if (prc != 0) {
        (void)fprintf(stderr, "atlas_bench: failed to parse produced pack %s (%d)\n", pack_path, prc);
        goto cleanup;
    }

    /* Self-check: every emitted region transform must lie inside the requested mask. */
    uint32_t bad_region = 0U;
    uint8_t bad_transform = 0U;
    const int vrc = nt_bench_verify_region_transforms(pack_path, allowed_transforms, &bad_region, &bad_transform);
    if (vrc != 0) {
        if (vrc > 0) {
            (void)fprintf(stderr, "atlas_bench: region %u emitted transform %u outside requested mask 0x%02X\n", bad_region, (unsigned)bad_transform, (unsigned)allowed_transforms);
        } else {
            (void)fprintf(stderr, "atlas_bench: failed to verify emitted transforms in %s (%d)\n", pack_path, vrc);
        }
        goto cleanup;
    }

    nt_atlas_opts_t opts = nt_atlas_opts_defaults();
    opts.shape = shape;
    opts.max_size = max_size;
    opts.max_vertices = BENCH_MAX_VERTICES;
    opts.max_added_area_percent = max_added_area_percent;
    opts.allowed_transforms = allowed_transforms;
    uint32_t mask_w = 0U;
    uint32_t mask_h = 0U;
    uint32_t retained = 0U;
    mask = build_retained_mask(selected_run.first_path, opts.alpha_threshold, &mask_w, &mask_h, &retained);
    if (mask == NULL || nt_bench_parse_selected_geometry(baseline_pack_path, 0U, mask_w, mask_h, &baseline_geometry) != 0 ||
        nt_bench_parse_selected_geometry(pack_path, 0U, mask_w, mask_h, &selected_geometry) != 0) {
        (void)fprintf(stderr, "atlas_bench: failed to reconstruct selected geometry from production packs\n");
        goto cleanup;
    }
    const uint64_t opaque_area2 = (uint64_t)retained * 2U;
    const uint64_t base_area2 = polygon_abs_twice_area(baseline_geometry.polygon, baseline_geometry.vertex_count);
    const uint64_t selected_area2 = polygon_abs_twice_area(selected_geometry.polygon, selected_geometry.vertex_count);
    const nt_selected_geometry_proof_t reconstructed =
        nt_selected_geometry_validate(mask, mask_w, mask_h, opaque_area2, baseline_geometry.polygon, baseline_geometry.vertex_count, base_area2, baseline_geometry.triangle_indices,
                                      baseline_geometry.triangle_index_count, selected_geometry.polygon, selected_geometry.vertex_count, selected_area2, selected_geometry.triangle_indices,
                                      selected_geometry.triangle_index_count, max_added_area_percent, BENCH_MAX_VERTICES);
    const nt_polygon_feasibility_t selected_feasibility = nt_polygon_feasibility(selected_geometry.polygon, selected_geometry.vertex_count, mask, mask_w, mask_h, BENCH_MAX_VERTICES);
    if (!reconstructed.valid) {
        (void)fprintf(stderr, "atlas_bench: reconstructed production proof failed\n");
        goto cleanup;
    }

    nt_bench_run_t run;
    memset(&run, 0, sizeof(run));
    (void)snprintf(run.tool_version, sizeof(run.tool_version), "2.0.0");
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
    run.opts_allowed_transforms = opts.allowed_transforms;
    run.opts_alpha_threshold = opts.alpha_threshold;
    run.opts_max_added_area_percent = opts.max_added_area_percent;
    run.opts_power_of_two = opts.power_of_two ? 1 : 0;
    /* The pack format does not expose dedup or cache counters. */
    run.cache_hits = 0;
    run.cache_misses = 0;

    run.atlas_count = 1;
    nt_bench_atlas_result_t *a = &run.atlases[0];
    (void)snprintf(a->name, sizeof(a->name), "%s", atlas_name);
    a->sprites = m.region_count;
    a->unique = 0; /* not recoverable from the pack (see cache note above) */
    a->pages = m.page_count;
    a->region_count = m.region_count;
    a->pack_ms = selected_run.pack_ms;
    a->density_fill_frontier = m.density_fill_frontier;
    a->density_fill_texture = m.density_fill_texture;
    a->hull_total = m.hull_vert_total;
    a->hull_min = m.hull_vert_min;
    a->hull_max = m.hull_vert_max;
    a->hull_mean = (m.region_count > 0) ? ((double)m.hull_vert_total / (double)m.region_count) : 0.0;

    nt_bench_geometry_proof_t *proof = &run.proof;
    (void)snprintf(proof->source, sizeof(proof->source), "%s", selected_run.first_path);
    if (nt_bench_file_sha256_hex(baseline_pack_path, proof->baseline_pack_sha256) != 0 || nt_bench_file_sha256_hex(pack_path, proof->selected_pack_sha256) != 0) {
        (void)fprintf(stderr, "atlas_bench: failed to hash production packs\n");
        goto cleanup;
    }
    proof->valid = reconstructed.valid;
    proof->full_cell_coverage = reconstructed.base_coverage_valid && reconstructed.selected_coverage_valid;
    proof->topology_valid = reconstructed.base_topology_valid && reconstructed.selected_topology_valid;
    proof->triangulation_valid = reconstructed.base_triangulation_valid && reconstructed.selected_triangulation_valid;
    proof->allowance_valid = reconstructed.allowance_valid;
    proof->ceiling_valid = reconstructed.ceiling_valid;
    proof->retained_cell_count = reconstructed.retained_cell_count;
    proof->base_vertex_count = reconstructed.base_vertex_count;
    proof->selected_vertex_count = reconstructed.selected_vertex_count;
    proof->opaque_area2 = reconstructed.opaque_area2;
    proof->base_area2 = reconstructed.base_area2;
    proof->selected_area2 = reconstructed.selected_area2;
    proof->added_area2 = reconstructed.added_area2;
    proof->lost_area2 = selected_feasibility.lost_area2;
    proof->base_overdraw_percent = opaque_area2 > 0U ? ((double)(base_area2 - opaque_area2) * 100.0) / (double)opaque_area2 : 0.0;
    proof->added_area_percent = opaque_area2 > 0U ? ((double)reconstructed.added_area2 * 100.0) / (double)opaque_area2 : 0.0;
    proof->total_overdraw_percent = opaque_area2 > 0U ? ((double)(selected_area2 - opaque_area2) * 100.0) / (double)opaque_area2 : 0.0;

    json_write_started = true;
    const int wrc = nt_bench_write_json(out_json, &run);
    if (wrc != 0) {
        (void)fprintf(stderr, "atlas_bench: failed to write JSON %s (%d)\n", out_json, wrc);
        goto cleanup;
    }

    exit_code = 0;
    keep_selected_pack = true;

cleanup:
    nt_bench_selected_geometry_destroy(&selected_geometry);
    nt_bench_selected_geometry_destroy(&baseline_geometry);
    free(mask);
    atlas_bench_cleanup_outputs(out_json, baseline_pack_path, pack_path, keep_selected_pack, json_write_started && exit_code != 0);

    if (exit_code == 0) {
        (void)printf("atlas_bench: %s shape=%s percent=%.3g pages=%u sprites=%u pack_ms=%.2f fill_texture=%.4f fill_frontier=%.4f -> %s\n", atlas_name, shape_to_name(opts.shape),
                     (double)max_added_area_percent, m.page_count, m.region_count, selected_run.pack_ms, m.density_fill_texture, m.density_fill_frontier, out_json);
    }
    return exit_code;
}
