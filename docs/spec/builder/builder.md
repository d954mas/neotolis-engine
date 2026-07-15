# Builder Architecture

The builder is a standalone native C17 binary; packing rules are code, not a
DSL. Covers the core API (typed add_* calls plus the begin/add/commit atlas transaction),
build stages and validation, asset-ID header codegen and combined headers, the
content-addressed encode cache, and the NFP-based atlas builder with its
pipeline, vector packer, options, per-sprite overrides, and atlas cache.

Related: [Pack Format](../assets/ntpack.md), [Runtime Formats](../assets/runtime-formats.md), [Resource System](../assets/resource.md)

## Builder model

Builder is a standalone native binary (C17, with vendored C++ for Basis Universal encoder behind extern "C"). Rules are written in code.

```c
start_pack("base");
add_shaders("assets/shaders/*.shader");
add_textures("assets/textures/ui/*.png");
add_materials("assets/materials/ui/*.mat");
add_meshes("assets/meshes/common/*.glb");
add_audio("assets/sfx/*.wav");
add_audio("assets/music/*.ogg");
finish_pack();
```

## Why code-based builder

Explicit control, no DSL needed, powerful grouping logic, easy custom per-project rules, aligns with engine philosophy.

## Builder module layers

```text
builder/
    main_builder.c
    builder_pack.c
    builder_manifest.c
    builder_import_mesh.c
    builder_import_texture.c
    builder_import_shader.c
    builder_import_material.c
    builder_import_audio.c
    builder_project.c
```

## Core builder API

```c
start_pack(const char *name);
finish_pack(void);

add_mesh(const char *path);
add_texture(const char *path);
add_shader(const char *path);
add_material(const char *path);
add_audio(const char *path);
add_font(const char *path, const nt_font_opts_t *opts);   /* opts: charset (required), name override, target_units_per_em */

add_meshes(const char *pattern);
add_textures(const char *pattern);
add_shaders(const char *pattern);
add_materials(const char *pattern);
add_audios(const char *pattern);
add_fonts(const char *pattern, const nt_font_opts_t *opts);

/* Atlas: groups N source sprites into 1 metadata blob + M texture pages.
 * Per-sprite opts carry the name override and the pivot point (NULL = defaults). */
NtAtlasBuild *nt_atlas_begin(NtBuilderContext *ctx, const char *name, const nt_atlas_opts_t *opts);
void nt_atlas_add(NtAtlasBuild *atlas, const char *path, const nt_atlas_sprite_opts_t *opts);
void nt_atlas_add_raw(NtAtlasBuild *atlas, const uint8_t *rgba, uint32_t w, uint32_t h, const nt_atlas_sprite_opts_t *opts);
void nt_atlas_add_glob(NtAtlasBuild *atlas, const char *pattern, const nt_atlas_sprite_opts_t *opts);
nt_build_result_t nt_atlas_commit(NtAtlasBuild *atlas);
```

`nt_atlas_add_raw` borrows `rgba` only for the call and deep-copies valid RGBA8 input. The pointer is required only when the dimensions describe a valid non-zero byte span; zero or oversized dimensions ignore it and report the corresponding graceful content error.

Prefer typed wildcard functions over one untyped `add_files()`. Atlas uses a typed transaction because packing requires the complete sprite set. The handle owns atlas-local inputs and errors until the terminal `nt_atlas_commit`; it is invalid after commit.

**Font UPM normalization.** `nt_font_opts_t.target_units_per_em` rescales a font's metrics, glyph contours, and kern offsets to a target units-per-em value offline (0 = keep the source UPM). This is the contract that lets fallback fonts of different native UPM be mixed in one font: set every member of a fallback set to the same `target_units_per_em` (the max member UPM) so their metrics share one coordinate space and merge without tripping the runtime shared-metrics assert. Normalization is done in the builder so the runtime stays a simple safety net.

## Builder stages

1. source assets
2. import
3. validation
4. conversion to runtime format
5. pack placement with alignment
6. manifest generation (embedded in pack header)
7. write NTPACK binary

## Builder validation

Builder must check: references between assets, resource types, mesh/material/shader compatibility, required attributes, runtime format generation correctness, audio format validity.

## Asserts vs. graceful content errors

The builder distinguishes two failure classes:

- **Programmer invariants, unexpected states, OOM, and missing/unreadable files** assert (`NT_BUILD_ASSERT`) — these are bugs or a broken environment, and crashing surfaces them instantly. The single-asset APIs (`nt_builder_add_texture`, `nt_builder_add_mesh`, `nt_builder_add_font`) also assert on a decode/parse failure of their input.
- **Content-dependent failures of sprite images inside the ATLAS builder** are recoverable and route to a graceful error channel instead of aborting: undecodable/oversized/zero-dimension sprite images, transparent-after-trim sprites, oversized slice9 borders, degenerate hulls, contour-vertex overflow, trim-offset overflow, duplicate region names, size/page limits, and unfittable sprites.

`NT_BUILD_ASSERT` is terminal: normal execution ends with `abort()`. Tests may install a non-local-jump hook to verify that an assertion fires, but the interrupted builder context is not reusable and has no rollback guarantee.

The graceful channel is scoped to the atlas builder — it lets a batch of sprites survive one bad member. Content errors stay local to `NtAtlasBuild` while it is collecting. One bad sprite does not stop validation of the remaining sprites in that transaction, so related errors are reported together in stable add order. A failed commit publishes no atlas blob, page texture, metadata, or codegen region; it appends its errors to the pack accumulator and marks the final pack invalid. The accumulator holds up to `NT_BUILD_MAX_ERRORS` (256) content errors; beyond that the tail is dropped and `nt_builder_errors_truncated()` returns true. Atlas-specific kinds use the `NT_BUILD_ERR_KIND_ATLAS_*` prefix; generic image-content kinds remain `NT_BUILD_ERR_KIND_CORRUPT_IMAGE`, `NT_BUILD_ERR_KIND_ZERO_DIM`, and `NT_BUILD_ERR_KIND_IMAGE_TOO_LARGE`. A single-dimension-oversized image file is reported as `IMAGE_TOO_LARGE`, not `CORRUPT_IMAGE`. Callers read committed errors before or after `finish_pack`:

- `nt_build_error_t` — pure-data record (kind, atlas/sprite names, dims, limits). Names longer than the fixed record fields retain both ends plus a stable hash instead of being silently truncated.
- `const nt_build_error_t *nt_builder_get_errors(ctx, &count)` — borrowed, read-only view valid until `nt_builder_free_pack`.
- `nt_build_error_format(err, buf, len)` — renders one actionable line on demand.

This keeps the atlas builder graceful so a GUI frontend survives bad sprites with actionable messages; a command-line frontend can fail fast on a non-`NT_BUILD_OK` result. Before a failed commit returns, it attempts to remove any pre-existing `.ntpack` and generated header; an invalidation failure returns `NT_BUILD_ERR_IO` while the committed diagnostics remain readable. `nt_builder_finish_pack` repeats this invalidation idempotently for aggregate failure. Single-asset adds and missing files still crash early.

The "report bad sprites together" guarantee is **per transaction**. A later atlas transaction always runs, even after an earlier failed commit. Its own commit returns its own result, so it may return `NT_BUILD_OK`; `nt_builder_finish_pack` still reports the aggregate pack failure and writes no `.ntpack`. Errors from multiple failed transactions are appended in commit order, with add order preserved inside each transaction.

Nested atlas transactions assert. Committing an empty transaction asserts. `nt_builder_finish_pack` with an open transaction asserts because its resources have not been validated or published; `nt_builder_free_pack` safely discards an unfinished transaction. There is no public abort operation: failed commit is the rollback path and also preserves diagnostics. Cache directory and worker count are pack configuration and cannot change while a transaction is open. Ordinary non-atlas `add_*` calls may interleave; their publication order is immediate, while the atlas batch appears at commit. `nt_atlas_begin` validates atlas scalar, shape, and page-texture options. Per-sprite options, missing files, glob patterns, and cross-field rules are validated on every real transaction, including when the pack is already failed.

## Asset ID codegen

`finish_pack` generates a `.h` header alongside each `.ntpack` with typed compile-time constants for every asset:

```c
/* Auto-generated by nt_builder -- do not edit */
#define ASSET_MESH_MESHES_CUBE_GLB ((nt_hash64_t){0x...ULL})     /* meshes/cube.glb */
#define ASSET_SHADER_SHADERS_MESH_VERT ((nt_hash64_t){0x...ULL}) /* shaders/mesh.vert */
```

Rules:
- Constants are typed compound literals `((nt_hash64_t){...})` -- work with `nt_resource_request` without casts.
- Hash values match `nt_hash64_str(normalized_path)` at runtime. The header is the single source of truth.
- Identifier format: `ASSET_{TYPE}_{
    PATH
}` where path includes file extension (`.vert`, `.frag`, `.glb` etc.) to avoid collisions between same-stem files. Slashes, dots, dashes become underscores, uppercased.
- Entries sorted alphabetically within each type group (MESH, TEXTURE, SHADER, BLOB) for deterministic, diffable output.
- `register_labels()` function under `#if NT_HASH_LABELS` registers all paths for debug hash lookup.
- Identifier collisions (two assets producing the same `#define` name) are a fatal builder error.

## Combined headers (multi-pack projects)

Projects with multiple packs merge per-pack headers into one combined header after all packs are built:

```c
/* Each finish_pack generates a per-pack .h (e.g. core.h, textures.h) */
nt_builder_set_header_dir(ctx, "examples/myproject/generated");
nt_builder_finish_pack(ctx);

/* After all packs: merge into one combined header */
const char *headers[] = {"generated/core.h", "generated/textures.h"};
nt_builder_merge_headers(headers, 2, "generated/assets.h");
```

Rules:
- `merge_headers` reads per-pack `.h` files, deduplicates by hash, sorts, writes one combined header.
- No runtime state needed during pack building — merge operates on already-generated files.
- Game code includes the single combined header, not per-pack headers.
- Per-pack headers are still generated for diagnostics and per-pack diffing.
- `set_header_dir` controls where headers are written. Convention: `examples/{project}/generated/` in source tree so headers are visible in IDE and version control.

## Generated headers in version control

Generated asset headers are committed to git. This is intentional:

- Output is deterministic (sorted by name, stable hashes). File only changes when assets are added, removed, or renamed.
- Git diff on the header shows exactly which assets changed between commits -- acts as an asset changelog.
- New contributors can build and run without first running the builder.
- Header files are small (one line per asset) and compress well in git.

## Builder cache

Content-addressed encode cache. Opt-in via `nt_builder_set_cache_dir(ctx, path)`. Skips re-encoding unchanged assets on repeat builds.

**Cache key:** `decoded_hash` (xxHash64 of decoded source bytes) × `opts_hash` (hash of encode-affecting options + `NT_BUILDER_VERSION`).

**Storage:** flat directory of `.bin` files named `{decoded_hash}_{opts_hash}.bin`. No index file, no subdirectories. Cached data is raw encoded asset bytes (post-encode, pre-pack-header).

**Pipeline order:** early dedup → cache lookup → encode → cache store. Dedup runs first so duplicates never hit cache. Cache stores only unique encoded results.

**Invalidation:**
- Source data changes → different `decoded_hash` → automatic miss.
- Encode options change (format, compression, quality) → different `opts_hash` → automatic miss.
- Encoder logic changes → bump `NT_BUILDER_VERSION` → all `opts_hash` values change → full cache miss.
- Manual: delete cache directory contents.

**Safety:** write-to-temp + atomic rename. Cache failures (read/write) fall through to normal encode — never break the build.

**Build summary** reports per-asset cache status (cached / miss-new / miss-opts) and aggregate hit/miss counts.

## Atlas builder

The atlas builder packs a set of sprite images into one or more atlas pages and emits compact runtime metadata (`NT_ASSET_ATLAS`, see [Resource System — asset types](../assets/resource.md)). It is the only "grouping" producer in the builder — every other importer is single-asset.

**API shape (typed transaction):**

```c
nt_atlas_opts_t opts = nt_atlas_opts_defaults();  /* atlas-level: packer, format, etc. */
opts.max_size = 2048;
opts.max_vertices = 8;
opts.shape = NT_ATLAS_SHAPE_CONCAVE_CONTOUR;
opts.tracer_tolerance = 2.0F; /* measured starting point for anti-aliased art */

NtAtlasBuild *atlas = nt_atlas_begin(ctx, "hero", &opts);

/* Idle frames use the default centre pivot (0.5, 0.5). */
nt_atlas_add_glob(atlas, "assets/sprites/hero/idle_*.png", NULL);

/* Walk cycle: override the pivot to bottom-centre for every matched frame. */
nt_atlas_sprite_opts_t walk = nt_atlas_sprite_opts_defaults();
walk.origin_y = 1.0F; /* feet at bottom edge */
nt_atlas_add_glob(atlas, "assets/sprites/hero/walk_*.png", &walk);

/* Single sprite with a custom name override. */
nt_atlas_add(atlas, "assets/sprites/hero/portrait.png",
             &(nt_atlas_sprite_opts_t){
                 .name = "hero_portrait",
                 .origin_x = 0.5F,
                 .origin_y = 0.5F,
             });

nt_build_result_t atlas_result = nt_atlas_commit(atlas);
```

`nt_atlas_begin` returns an opaque atlas transaction. Subsequent `nt_atlas_add*` calls feed sprites into that handle. `nt_atlas_commit` runs the full pipeline and publishes the atlas batch only after all recoverable validation has succeeded; after it returns, the handle is invalid. Nested atlas transactions are not allowed.

### Pipeline

`nt_atlas_commit` runs these stages in order:

1. **alpha_trim** — resolve the per-sprite `alpha_threshold` (a zero override inherits the atlas value), extract the alpha plane, and find the tight retained-pixel bbox (rejects fully transparent inputs).
2. **cache_check** — compute atlas-level cache key (per-sprite pixel hash, source dimensions, origins, raw overrides, pack-affecting opts, and version), then try loading cached placement and pages. The key is add-order-sensitive because cached placements reference sprite indices. Post-pack fields are excluded because the texture encode stage has its own cache.
3. **dedup** — hash + byte-level compare to find identical sprites; duplicates share `vertex_start`/`index_start` in the final blob.
4. **geometry** — for each unique sprite, build the binary mask from the same effective alpha threshold and select the shape-specific geometry path. `RECT` emits the trim AABB. `CONVEX_HULL` starts from the opaque-pixel convex hull. `CONCAVE_CONTOUR` performs morphological closing when needed, traces the boundary, and evaluates concave simplifiers. Polygon candidates share coverage, fidelity, vertex-budget, and deterministic-selection checks described below.
5. **pipeline_validate** — non-mutating pre-pack checks that report every bad sprite in one pass: empty-page fit, duplicate region names, region-count cap, and per-sprite trim-dimension limits. It still runs on surviving sprites after earlier content errors.
6. **tile_pack** — call `vector_pack` (NFP packer, see below) to assign each unique sprite to a page and (x, y) position.
7. **compose** — blit trimmed pixels onto page buffers, run AABB edge-extrude only when packing uses rectangles; in polygon mode, require `extrude=0` and rely on `padding`.
8. **serialize** — build the atlas blob in transaction-owned storage.
9. **cache_write/debug_png** — persist optional successful-build artifacts after all recoverable work has succeeded.
10. **publish** — register the atlas blob, page textures, metadata, and region codegen in the pack. Capacity, allocation, and resource-ID failures assert and terminate the build; they are not recoverable rollback paths.

Any content error collected during trim, geometry, or validation prevents packing and publication, but surviving sprites still pass through the non-mutating validation stages so the transaction reports related errors together. A failed transaction appends those errors to the pack and publishes nothing. Cache hits skip packing and compose, but still serialize and publish the same output. Before a polygon candidate is accepted, every retained pixel centre must be inside or on its boundary; simplification may never trade coverage for fewer vertices.

### Vector packer

The packer is **NFP/Minkowski-based** (`nt_builder_atlas_vpack.c`). For each candidate position the incoming polygon is tested against the union of No-Fit Polygons of all already-placed sprites. Properties:

- **Sub-pixel exact** — no quantization to a tile grid.
- **Concave-aware** — Clipper2 `MinkowskiSum + Union(NonZero)` produces multi-ring NFPs for concave inputs; rings are forbidden zones.
- **8 D4 orientations** — flipH, flipV, diagonal flip and combinations. Identity-equivalent orientations are deduplicated.
- **NFP cache** — 8-way set-associative seqlock cache keyed by `(placed_shape_hash, incoming_shape_hash)`. Lock-free reads via version counter, CAS writes. Same shape pair across different sprites reuses the cached NFP.
- **Parallel build** — when `nt_builder_set_threads(ctx, N)` is called, NFP construction and candidate scanning run on a thread pool. Per-thread stat accumulators merge into global stats deterministically.
- **Page growth** — sprites that don't fit allocate a new page (up to `ATLAS_MAX_PAGES = 64`); new pages start with the same dimensions as the first.

### Atlas options

```c
/* Silhouette mode for atlas packing. Ordered by cost and density. */
typedef enum {
    NT_ATLAS_SHAPE_RECT = 0,            /* AABB trim rect — fastest, worst pack density */
    NT_ATLAS_SHAPE_CONVEX_HULL = 1,     /* convex hull of opaque pixels — no contour trace */
    NT_ATLAS_SHAPE_CONCAVE_CONTOUR = 2, /* concave contour + multi-strategy — densest, slowest */
} nt_atlas_shape_t;

typedef struct {
    const nt_tex_compress_opts_t *compress; /* NULL = raw RGBA */
    nt_texture_pixel_format_t format;       /* 0 = RGBA8 default */
    uint32_t max_size;                      /* max atlas page dimension (default 2048) */
    uint32_t padding;                       /* extra spacing between sprites after extrude (default 2) */
    uint32_t margin;                        /* atlas edge margin (default 0) */
    uint32_t extrude;                       /* AABB edge duplication (default 0; <= max_size; RECT only when non-zero) */
    uint8_t alpha_threshold;                /* alpha >= threshold = opaque (default 1) */
    float tracer_tolerance;                 /* pixel-domain fidelity tolerance (default 0.0F = legacy; RECT ignores it) */
    uint8_t max_vertices;                   /* max polygon vertices per region (default 8, hard cap 16) */
    nt_atlas_shape_t shape;                 /* silhouette mode (default NT_ATLAS_SHAPE_CONCAVE_CONTOUR) */
    bool allow_transform;                   /* try 8 D4 orientations (4 rotations × 2 flips; default true) */
    bool power_of_two;                      /* round atlas dims to POT (default true) */
    bool debug_png;                         /* write debug atlas page PNGs (default false) */
    bool premultiplied;                     /* premultiply RGB by alpha during texture encode (default true) */
} nt_atlas_opts_t;
```

**Silhouette modes (`nt_atlas_shape_t`):**

- `NT_ATLAS_SHAPE_RECT` — 4-vertex AABB of the trim rect. No contour tracing, no hull, no RDP. Fastest geometry stage; lowest pack density because the packer cannot slot concave notches between sprites. The only mode where `extrude > 0` is legal.
- `NT_ATLAS_SHAPE_CONVEX_HULL` — convex hull of opaque pixels via `binary_build_convex_polygon`, simplified to `max_vertices`. Skips morphological closing, contour tracing, RDP, and the 4-strategy pipeline entirely. Good compromise when sprites are roughly convex: noticeably denser than `RECT` without paying the full concave cost.
- `NT_ATLAS_SHAPE_CONCAVE_CONTOUR` (default) — traces the concave alpha boundary, runs RDP plus a multi-strategy simplification (RDP / perpendicular distance / bbox / convex hull), Clipper2-inflates the chosen polygon, and post-verifies pixel coverage. Internally falls back to `binary_build_convex_polygon` for degenerate inputs (disjoint components that morphological closing cannot merge, degenerate contours, Clipper2 inflate failure). Densest packing, highest cost.

**Threshold, tolerance, and compatibility contract:**

- `alpha_threshold` defines retained pixels for both trimming and geometry. A pixel is retained when `alpha >= effective_alpha_threshold`; per-sprite `0` inherits the atlas value. Anti-aliased fringe pixels therefore participate when their alpha reaches the configured threshold.
- `tracer_tolerance` is a non-negative pixel-domain upper bound on boundary error. Per-sprite `0.0F` inherits the atlas value. `RECT` ignores tolerance but still uses the effective threshold for its trim AABB.
- At the default `0.0F`, the established zero-tolerance path remains byte-identical. Concave candidates keep the legacy area-first strategy order. Convex hulls keep the legacy reduction unless that result is proven to lose retained-pixel coverage, in which case only the covering reducer correction is used.
- At positive tolerance, convex and concave modes evaluate feasible vertex counts from 3 through `max_vertices`. Feasibility requires exact retained-pixel coverage, boundary error no greater than the tolerance, a valid simple polygon, and the vertex budget. Candidate selection is deterministic and count-first, then final polygon area, generator order, and stable vertex coordinates/order.
- Convex and concave modes keep separate source geometry and simplifiers: convex starts from the opaque-pixel convex hull, while concave starts from its cleaned traced boundary. Both use the same final coverage, fidelity, and deterministic-selection contract. When no lower-count candidate is feasible, the max-budget covering path is retained rather than accepting pixel loss.
- These controls affect builder geometry and atlas cache identity only. They do not change the runtime API or the on-disk atlas format.

**Premultiplied alpha (default):** atlas pages are encoded through the regular texture pipeline with `premultiplied = true`, which writes `RGB' = (RGB * A + 127) / 255` into the page before `strip_channels` (RAW path) or `nt_basisu_encode` (BASIS path). The resulting texture sets `NT_TEXTURE_FLAG_PREMULTIPLIED` in `NtTextureAssetHeader.flags`, and the runtime must draw with `(ONE, ONE_MINUS_SRC_ALPHA)` blending. This is what keeps NFP-packed sprites free of dark fringes at sub-pixel clearance: `(0,0,0,0)` gap pixels are the identity for premultiplied blending, so bilinear filtering at sprite edges stays correct. Setting `premultiplied = false` logs a warning and is only valid for NEAREST-filtered or fully-opaque atlases; setting `premultiplied = true` with a non-RGBA8 `format` is a hard assert.

**Hard limits:**
- `0 <= extrude <= max_size`; non-zero atlas extrude requires `NT_ATLAS_SHAPE_RECT`.
- `tracer_tolerance` must be finite and non-negative at atlas and per-sprite boundaries. NaN, infinity, and negative values are caller bugs and assert; signed zero is canonicalized to positive zero.
- `3 ≤ max_vertices ≤ 16`. NFP buffers are stack-sized for `nA + nB ≤ 32`; below 3 the simplified hull degenerates to a line/point. The per-sprite override is `0` (atlas default) or `3..16`.
- Per-region `index_count` is `uint8_t` → ≤ 255 indices per region. With `max_vertices ≤ 16` an ear-clipped/fan triangulation produces at most `(16 - 2) * 3 = 42` indices, so one byte is sufficient.
- Per-atlas `vertex_start` / `index_start` are `uint32_t`.

### Per-sprite options (`nt_atlas_sprite_opts_t`)

Each `nt_atlas_add` / `nt_atlas_add_raw` / `nt_atlas_add_glob` call accepts an optional `nt_atlas_sprite_opts_t*`. `NULL` picks the defaults (centre pivot, name derived from path).

```c
typedef struct {
    const char *name;     /* NULL = derive from path (add/glob); required for add_raw */
    float origin_x;       /* pivot X, normalized over source_w (default 0.5) */
    float origin_y;       /* pivot Y, normalized over source_h (default 0.5) */
    uint16_t slice9_left; /* slice9 borders in source pixels (0 = no slice9) */
    uint16_t slice9_right;
    uint16_t slice9_top;
    uint16_t slice9_bottom;
    float tracer_tolerance;  /* 0 = atlas default; finite, non-negative; RECT ignores it */
    uint8_t alpha_threshold; /* 0 = atlas default */
    uint8_t shape;        /* 0 = atlas default, 1 = RECT, 2 = CONVEX, 3 = CONCAVE */
    uint8_t allow_rotate; /* 0 = atlas default, 1 = NO */
    uint8_t max_vertices; /* 0 = atlas default, max 16 */
    uint8_t margin;       /* 0 = atlas default; raise-only (a below-atlas value clamps up) */
    uint8_t extrude;      /* 0 = inherit atlas default; non-zero sets this sprite's edge bleed (RECT only), smaller or larger than atlas extrude */
} nt_atlas_sprite_opts_t;
```

**Margin vs. extrude override semantics:**
- `margin` is **raise-only**: it only feeds the packing footprint, so a per-sprite value below the atlas margin is clamped up to the atlas value. An `UNFITTABLE` record reports the *effective* (clamped-up) margin the packer actually used, not a below-atlas request.
- `extrude`: `0` **inherits** the atlas default. A non-zero override sets **this sprite's edge bleed** (RECT only) and may be smaller OR larger than the atlas extrude — but a *zero* bleed cannot be expressed per-sprite (0 means inherit). Effective extrude, whether inherited or overridden, requires the effective sprite shape to be RECT. Compose/serialize apply the raw override. The packing footprint, however, reserves room for `max(this sprite's extrude, atlas extrude)`, so an `UNFITTABLE` record reports that effective (max) extrude — the space that actually caused the fit failure, distinct from the per-sprite bleed written into the page.

**Measured tolerance guidance (`mixed_aa`, native release):**

The benchmark corpus contains 4,812 anti-aliased sprites. Every row below produced one page and an exact repeat pack hash. `pack_ms` is recorded for diagnostics only: it is wall-clock timing from one local run and is not an acceptance threshold.

| Tolerance (px) | Hull vertices total | Mean | Texture fill | Pack bytes | Pack ms | Pack SHA-256 |
| ---: | ---: | ---: | ---: | ---: | ---: | --- |
| 0.00 | 37,514 | 7.7959 | 0.6304 | 67,557,296 | 26,630.61 | `d10a415f7e46dc45b6555b685199d05c4d3cdf1e0dcb978ea69e90eb79cb387d` |
| 0.50 | 35,856 | 7.4514 | 0.6610 | 67,545,688 | 38,271.51 | `1bea6f863a8d73597fcc49e4c135fc61c01dddf11507cd0de1d2a86a17a889cf` |
| 1.00 | 30,574 | 6.3537 | 0.6608 | 67,512,468 | 28,343.48 | `a03eb0af151f46693ca61a5dcd3fb115ac9666ae218d9f89e2b50972b8fdaeff` |
| 1.25 | 30,530 | 6.3446 | 0.6606 | 67,512,244 | 28,586.01 | `965308910685dc0b3fc8d4a5b8c4d6f393c0cb87415390ded6802a3e301f951c` |
| 1.50 | 29,778 | 6.1883 | 0.6577 | 67,508,464 | 29,275.34 | `7056d3b1809625140186316ff437cd462bd388ac18e633fd5b62d31651b42c65` |
| 1.75 | 28,602 | 5.9439 | 0.6577 | 67,501,128 | 28,740.19 | `b182f1878db103b278af0f98da7ca08876499b8394262f7f0043c729ebf6e803` |
| 2.00 | 26,208 | 5.4464 | 0.6578 | 67,485,656 | 24,405.38 | `f6f9343f695e0d8f1f336c5b3797a5d2cba7ba3e72d4e8cbb957a6ee217d2c8a` |
| 3.00 | 25,014 | 5.1983 | 0.6571 | 67,477,384 | 22,003.98 | `857859bb6848354e4cc5c00cf113222f1c4fa52501d0bb3369e17e675487b1c2` |

For similar anti-aliased art, `2.0F` is the measured practical starting point: versus zero it reduces mean vertices by 30.1% and raises texture fill by 0.0274 absolute. `1.0F` to `1.5F` preserves a tighter silhouette when fidelity matters more; `3.0F` reduces mean vertices only another 4.6% from `2.0F` and slightly lowers fill. The API default remains `0.0F` for byte-compatible existing builds.

**Pivot semantics:**
- Normalized over the **source image** dimensions (not the trimmed rect). Default `(0.5, 0.5)` = image centre.
- **Values outside `[0, 1]` are allowed** — pivots may lie outside the frame for weapons, effects, or motion-stabilised sprites. Must be finite (`isfinite()` asserted; NaN/inf is caller bug).
- Source-space (not trim-space) is chosen so frame-by-frame animations with varying per-frame trim bounds have stable pivots across frames. A walk cycle with `origin_y = 0.9375` sits on the same source-image pixel row regardless of how much whitespace the alpha trim removed from each frame.

**Glob rule:** `nt_atlas_add_glob` asserts `opts->name == NULL` — a single name cannot apply to N matched files without hash collisions. Each matched file derives its own name from its path, and the `origin_x/y` fields propagate to all of them. For per-file name overrides within a glob, call `nt_builder_glob_iterate` with a custom callback that calls `nt_atlas_add` per match.

**Dedup + different pivots:** adding the same pixel-identical sprite twice with different `origin_x/y` produces **two separate regions** that **share** `vertex_start` and `index_start` in the blob. The dedup pass matches on pixel hash + byte-level pixel compare (origin is not considered), so the geometry/pixel data is stored once; each logical region stores its own pivot. This is the cheap path for "same sprite, different anchor" (e.g. icon referenced with centre pivot in menu vs bottom-centre in HUD).

**Zero-init footgun:** C99 designated-initialiser compound literals (`&(nt_atlas_sprite_opts_t){.origin_y = 1.0F}`) zero-init unset fields — so `origin_x` becomes `0.0`, not the default `0.5`. Always start from `nt_atlas_sprite_opts_defaults()` for partial overrides, or set every field explicitly in the literal.

### Atlas cache

Separate from the per-asset [builder cache](#builder-cache) because atlas placement is a global decision over the whole sprite set.

**Cache key:** `xxh64(per_sprite(decoded_hash + source_width + source_height + origin_x + origin_y + raw_overrides) + pack_opts + ATLAS_CACHE_KEY_VERSION)`. `ATLAS_CACHE_KEY_VERSION` is currently 13. Pack options include atlas `alpha_threshold` and `tracer_tolerance`; raw per-sprite identity includes both overrides even when they currently resolve to an inherited value. This keeps cache and dedup behavior safe if atlas defaults or resolution rules change. Signed zero is canonicalized before storage and hashing. Per-sprite data is hashed in add order because cached placements reference sprites by index. Source dimensions are part of identity because the same flat RGBA bytes can describe different image shapes. Only pack/compose-affecting options are included; post-pack fields are handled by the texture encode cache.

**Storage:** one `atlas_<key>.bin` file per cache entry, containing the placement table and composed page pixels. On hit, the pipeline skips pack, compose, and cache write; debug output, serialization, and publish still run.

**Invalidation:** any change to source pixels, raw pack-affecting overrides, pack opts, or `ATLAS_CACHE_KEY_VERSION` produces a fresh key. The version constant is bumped when the packer's behavior changes in a way that would silently produce different output. Leaving every new control at its default preserves the established zero-tolerance bytes.

**Failure mode:** atomic temp+rename writes; read/write failures fall through to a fresh build, never break it.
