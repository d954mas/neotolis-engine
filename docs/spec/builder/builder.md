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
- **Content-dependent failures of sprite images inside the ATLAS builder** are recoverable and route to a graceful error channel instead of aborting: undecodable/oversized/zero-dimension sprite images, transparent-after-trim sprites, oversized slice9 borders, contour-vertex overflow, trim-offset overflow, duplicate region names, size/page limits, and unfittable sprites. `ATLAS_DEGENERATE_HULL` and `ATLAS_HULL_INFEASIBLE` remain formatted legacy kinds no longer produced: with `max_vertices >= 4` the trim-rect candidate makes geometry selection total, so an invalid selection is a programmer invariant (assert), not content.

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
opts.max_added_area_percent = 10.0F;
opts.shape = NT_ATLAS_SHAPE_CONCAVE_CONTOUR;

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

1. **alpha_trim** — resolve the per-sprite `alpha_threshold` (inherit unless `has_alpha_threshold`), extract the alpha plane, and find the tight retained-pixel bbox (rejects fully transparent inputs unless the effective threshold is 0).
2. **cache_check** — compute atlas-level cache key (per-sprite pixel hash, source dimensions, origins, raw overrides, pack-affecting opts, and version), then try loading cached placement and pages. The key is add-order-sensitive because cached placements reference sprite indices. Post-pack fields are excluded because the texture encode stage has its own cache.
3. **dedup** — sprites are bucketed by the D4-invariant post-trim dimension pair `(min(w,h), max(w,h))`, keyed inside each bucket by a canonical **post-trim** content hash (the minimum of the eight orientation hashes), and confirmed by an exact post-trim byte compare **through a relative transform**; duplicates share the original's placement rectangle. Canvas size and art position therefore no longer keep identical art apart, and neither does orientation: a sprite joins a group with relative transform `rel` when `rel` is in that sprite's own effective `allowed_transforms` mask and its pixels match the root's under `rel` — the lowest such `rel` wins, and `rel = identity` is simply the exact case, so there is one search and no separate exact stage. `rel` means `root_bitmap == rel(alias_bitmap)`, which is exactly what the alias's region `transform` then stores. A sprite whose resolved `dedup` is off is excluded from the search entirely, so it is neither an alias nor an alias target. The root of each group is its lowest-add-index **eligible** member — a `DEDUP_OFF` sprite at the head of a run is skipped, and the next member becomes root — so repacks are byte-identical. The canonical hash itself is only computed for dimension buckets holding two or more sprites, and only when some sprite could actually fold: a singleton keeps hash `0`, and an atlas whose effective masks are all identity-only hashes the identity orientation alone. That last narrowing is deliberately per atlas and never per sprite, because minimum-over-orbit is only orientation-invariant when the orientation set is a subgroup, and a mask such as `{identity, rot90}` is not closed. What is shared here is the placement rectangle and nothing else: every sprite still owns its own vertex/index block, and whether two of those blocks end up sharing one byte range in the blob is decided separately, by byte equality, in `serialize`.
4. **geometry** — for each unique sprite, build the binary mask from the same effective alpha threshold and select the shape-specific geometry path. `RECT` emits the trim AABB. `CONVEX_HULL` starts from the opaque-pixel convex hull. `CONCAVE_CONTOUR` performs morphological closing when needed, traces the boundary, and evaluates concave simplifiers. Polygon candidates share full retained-cell coverage, topology, triangulation, area-budget, vertex-budget, and deterministic-selection checks described below.
5. **pipeline_validate** — non-mutating pre-pack checks that report every bad sprite in one pass: empty-page fit, duplicate region names, region-count cap, and per-sprite trim-dimension limits. It still runs on surviving sprites after earlier content errors.
6. **tile_pack** — call `vector_pack` (NFP packer, see below) to assign each unique sprite to a page and (x, y) position.
7. **compose** — blit trimmed pixels onto page buffers, run AABB edge-extrude only when packing uses rectangles; in polygon mode, require `extrude=0` and rely on `padding`.
8. **serialize** — build the atlas blob in transaction-owned storage. Every sprite's vertex/index block is emitted from its own geometry and its own region transform; byte-identical blocks then share one range, first writer in add order wins. Sprites on different pages can therefore share a block — the page comes from `NtAtlasRegion.page_index`, not from the vertex data. Block identity additionally requires equal `trim_offset_x/y` even though neither is part of the block: the runtime bakes the trim offset into `cached_pos[]`, which it indexes by `vertex_start`, so two regions on one byte range would overwrite each other's precomputed positions and the later region would win. Post-trim dedup is what made differing trim offsets reachable inside one group, so the two rules ship together.
9. **cache_write/debug_png** — persist optional successful-build artifacts after all recoverable work has succeeded.
10. **publish** — register the atlas blob, page textures, metadata, and region codegen in the pack. Capacity, allocation, and resource-ID failures assert and terminate the build; they are not recoverable rollback paths.

Any content error collected during trim, geometry, or validation prevents packing and publication, but surviving sprites still pass through the non-mutating validation stages so the transaction reports related errors together. A failed transaction appends those errors to the pack and publishes nothing. Cache hits skip packing and compose, but still serialize and publish the same output. Before a polygon candidate is accepted, its triangle union must continuously cover the full unit-square area of every retained pixel cell; centre-only coverage is not sufficient.

### Dedup statistics

`const nt_atlas_stats_t *nt_builder_get_atlas_stats(ctx, &count)` returns one record per successfully committed atlas, in commit order: `sprites`, `placements`, `folds_exact`, `folds_d4`, `area_saved_px`, `vertex_blocks_shared`. `folds_exact + folds_d4 + placements == sprites` holds for every record. Like the content-error channel this is a borrowed, read-only view owned by the context and valid only until `nt_builder_free_pack`; it is deliberately **not** reachable through the `NtAtlasBuild` handle, which commit frees. Only `placements` is recoverable from the packed atlas, and only by counting distinct per-page UV **rings** (rotation- and reversal-insensitive) — not by `vertex_start`, which a D4 alias no longer shares, and not by the UV bbox, which two disjoint concave sprites can share. The exact/D4 stage split, the saved area and the shared-block count exist nowhere else. The same numbers are appended to the `BENCH` log line as `folds_exact`, `folds_d4`, `area_saved` and `blocks_shared`.

### Vector packer

The packer is **NFP/Minkowski-based** (`nt_builder_atlas_vpack.c`). For each candidate position the incoming polygon is tested against the union of No-Fit Polygons of all already-placed sprites. Properties:

- **Sub-pixel exact** — no quantization to a tile grid.
- **Concave-aware** — Clipper2 `MinkowskiSum + Union(NonZero)` produces multi-ring NFPs for concave inputs; rings are forbidden zones.
- **D4 orientations** — flipH, flipV, diagonal flip and combinations, gated by the effective `allowed_transforms` mask. A zero per-sprite mask inherits the atlas mask; a non-zero mask replaces it, so a sprite may narrow or widen the atlas default. Identity is always permitted. Transform-identical sprites fold onto one placement when each member's own mask permits the relative transform it needs. A shared placement is oriented once for the whole dedup group, and a member's region transform is then `d4_compose(placement, its relative)` — the alias's local space maps onto the root's, and the root's onto the page. So the mask handed to the packer is the **intersection**, over every member, of the placements whose product with that member's relative still lands inside the member's own mask, floored at identity. D4 is not closed under composition, which is exactly why this is evaluated per placement candidate instead of assumed; the identity placement always survives, because a member's own relative is by construction already in its mask. A nine-patch anywhere in a group resolves to identity-only and therefore pins that placement to identity, with no special case. Groups whose relatives are all identity reduce to the plain intersection of member masks. (Full packer spec rewrite tracked in Phase 83.)
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
    uint8_t max_vertices;                   /* max polygon vertices per region (default 8, hard cap 16) */
    float max_added_area_percent;           /* simplification-added area budget (default 10%) */
    nt_atlas_shape_t shape;                 /* silhouette mode (default NT_ATLAS_SHAPE_CONCAVE_CONTOUR) */
    uint8_t allowed_transforms;             /* D4 transform mask (NT_ATLAS_TRANSFORM_* bits); 0xFF = all (default), 0x01 = identity only. Identity is the implicit floor. */
    bool power_of_two;                      /* round atlas dims to POT (default true) */
    bool debug_png;                         /* write debug atlas page PNGs (default false) */
    bool premultiplied;                     /* premultiply RGB by alpha during texture encode (default true) */
    float pixels_per_unit;                  /* source pixels per world unit (default 1.0F) */
    nt_texture_default_filter_t filter_min; /* default LINEAR_MIPMAP_LINEAR */
    nt_texture_default_filter_t filter_mag; /* default LINEAR */
    nt_texture_default_wrap_t wrap_u;       /* default REPEAT */
    nt_texture_default_wrap_t wrap_v;       /* default REPEAT */
    bool gen_mipmaps;                       /* RAW only; default true */
    bool dedup;                             /* fold identical content onto one placement (default true; a zero-init struct means OFF) */
} nt_atlas_opts_t;
```

Named mask presets are exact bit sets: `NT_ATLAS_TRANSFORMS_IDENTITY`,
`NT_ATLAS_TRANSFORMS_IDENTITY_ROT90`, `NT_ATLAS_TRANSFORMS_ROTATIONS`,
`NT_ATLAS_TRANSFORMS_FLIPS`, and `NT_ATLAS_TRANSFORMS_ALL`.

**Silhouette modes (`nt_atlas_shape_t`):**

- `NT_ATLAS_SHAPE_RECT` — 4-vertex AABB of the trim rect. No contour tracing, no hull, no RDP. Fastest geometry stage; lowest pack density because the packer cannot slot concave notches between sprites. The only mode where `extrude > 0` is legal.
- `NT_ATLAS_SHAPE_CONVEX_HULL` — convex hull of opaque pixels via `binary_build_convex_polygon`, simplified by a bounded frontier of convex reduction, covering simplification, and the trim AABB. With enough area allowance, the selector may therefore choose the 4-vertex AABB. Skips morphological closing, contour tracing, RDP, and the concave 4-strategy pipeline entirely. Good compromise when sprites are roughly convex: noticeably denser than `RECT` without paying the full concave cost.
- `NT_ATLAS_SHAPE_CONCAVE_CONTOUR` (default) — traces the concave alpha boundary, runs a deterministic multi-strategy frontier, and selects by the area-budget contract below. It falls back to a covering RECT/convex frontier when the traced contour is unusable (logged as a warning), so selection always succeeds. Densest packing, highest cost.

**Threshold, area budget, and selected-geometry proof:**

Every frontier adopts the trim-rect candidate, so with `max_vertices >= 4` geometry selection is provably total: there is no content-dependent geometry failure. An invalid selection for an in-range trim is a builder bug and asserts.

- `alpha_threshold` defines retained pixels for trimming, geometry, and page composition. A pixel is retained and copied into the atlas page when `alpha >= effective_alpha_threshold`; below-threshold pixels inside the trim rectangle compose as transparent. `0` is legal and means "retain every pixel": no trim, a fully transparent sprite still publishes (transparent-after-trim cannot fire), and the RGB of zero-alpha pixels composes into the page (observable with `premultiplied = false`). Per-sprite overrides use `has_alpha_threshold` so an explicit `0` remains representable; zero-initialized sprite options inherit the atlas value. Anti-aliased fringe pixels participate when their alpha reaches the configured threshold.
- `max_vertices` is a hard ceiling for serialized region polygons. The atlas default is 8; the range is 4..16 — a trim-clamped triangle cannot cover any mask that touches all four sides of its (tight) trim rect, so 3 is rejected at the API boundary. Per-sprite `0` inherits the atlas value.
- `max_added_area_percent` is the user-facing simplification tolerance. It is finite and non-negative; the atlas default is 10%. Per-sprite overrides use `has_max_added_area_percent` so an explicit `0%` remains representable, while zero-initialized sprite options still inherit the atlas value.
- Geometry uses one retained-cell set after effective alpha resolution and trim. `Aopaque` is the total retained unit-cell area. For every vertex count populated by the deterministic candidate generators, the builder keeps the tightest proven candidate. `Abase` is the smallest exact area among those feasible candidates. A candidate is eligible when `(Acandidate - Abase) / Aopaque * 100 <= max_added_area_percent`; the selector then chooses the fewest vertices, then smaller exact area, then stable generator/coordinate ties.
- `0%` means "no simplification-added area beyond the exact baseline", not "zero transparent area". Example: if `Aopaque=100`, `Abase=110`, and `max_added_area_percent=10`, the selected candidate may have `Aselected<=120`. The unavoidable 10 area of base overdraw is reported separately from the allowed simplification-added area.
- Donut-style transparent holes are not subtracted as "lost" pixels. The retained pixels around the hole must be covered; the simple polygon may also cover the transparent interior, and that contributes to base/total overdraw.
- Candidate generators may temporarily produce points outside the trim rectangle while searching for a covering simplification. Before selection/publish, every candidate is clamped back to trim-local bounds and re-proved; if the clamp breaks retained-cell coverage or topology, that candidate is rejected.
- Corner-cut candidates are nested as depth increases, so the builder bisects for a deepest cut under the combined coverage-and-validity predicate instead of testing every pixel depth. Coverage is monotone in depth; polygon validity can break at degenerate depths (e.g. two cuts meeting), so the bisection may settle on a slightly shallower — still valid and deterministic — cut. This keeps a tight candidate without making cost linear in sprite dimensions.
- Candidate and serialized geometry both pass the same selected-geometry proof: inputs valid, opaque area valid, base/selected bounds, topology, full retained-cell coverage, triangulation, metric order, allowance, and ceiling. Corrupt area, allowance, ceiling, polygon, or triangle data fails the proof.
- These controls affect builder geometry, composed page pixels, and atlas cache identity. They do not change the runtime API or the on-disk atlas blob format.

**Premultiplied alpha (default):** atlas pages are encoded through the regular texture pipeline with `premultiplied = true`, which writes `RGB' = (RGB * A + 127) / 255` into the page before `strip_channels` (RAW path) or `nt_basisu_encode` (BASIS path). The resulting texture sets `NT_TEXTURE_FLAG_PREMULTIPLIED` in `NtTextureAssetHeader.flags`, and the runtime must draw with `(ONE, ONE_MINUS_SRC_ALPHA)` blending. This is what keeps NFP-packed sprites free of dark fringes at sub-pixel clearance: `(0,0,0,0)` gap pixels are the identity for premultiplied blending, so bilinear filtering at sprite edges stays correct. Setting `premultiplied = false` logs a warning and is only valid for NEAREST-filtered or fully-opaque atlases; setting `premultiplied = true` with a non-RGBA8 `format` is a hard assert.

**Hard limits:**
- `0 <= extrude <= max_size`; non-zero atlas extrude requires `NT_ATLAS_SHAPE_RECT`.
- `max_added_area_percent` must be finite and non-negative at atlas and per-sprite boundaries. NaN, infinity, and negative values are caller bugs and assert; signed zero is canonicalized to positive zero.
- `4 ≤ max_vertices ≤ 16`. NFP buffers are stack-sized for `nA + nB ≤ 32`; a trim-clamped triangle cannot cover a full-perimeter mask, so `3` asserts. The per-sprite override is `0` (atlas default) or `4..16`.
- Per-region `index_count` is `uint8_t` → ≤ 255 indices per region. With `max_vertices ≤ 16` the validated CDT triangulation produces at most `(16 - 2) * 3 = 42` indices, so one byte is sufficient.
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
    uint8_t shape;              /* 0 = atlas default, 1 = RECT, 2 = CONVEX, 3 = CONCAVE */
    uint8_t allowed_transforms; /* 0 = inherit atlas mask; non-zero replaces it (identity floor applies) */
    uint8_t max_vertices;       /* 0 = atlas default, else 4..16 */
    uint8_t margin;       /* 0 = atlas default; raise-only (a below-atlas value clamps up) */
    uint8_t extrude;      /* 0 = inherit atlas default; non-zero sets this sprite's edge bleed (RECT only), smaller or larger than atlas extrude */
    uint8_t dedup;        /* 0 = inherit atlas dedup, NT_ATLAS_SPRITE_DEDUP_ON = 1, NT_ATLAS_SPRITE_DEDUP_OFF = 2 */
    float max_added_area_percent;    /* finite and non-negative; used only when presence is true */
    uint8_t alpha_threshold;         /* used only when presence is true; 0 retains every pixel */
    bool has_max_added_area_percent; /* false = inherit atlas value; true preserves explicit 0% */
    bool has_alpha_threshold;        /* false = inherit atlas value; true preserves an explicit 0 */
} nt_atlas_sprite_opts_t;
```

Per-sprite options keep a positional-initializer contract (source-recompile
compatibility, not a binary-ABI promise for stale object files), and every
zero-initialized control preserves inherit semantics — but the field order is
not append-only across versions. The `dedup` override was inserted inside the
override block, between `extrude` and `max_added_area_percent`, so a positional
initializer written before that change lands its area-budget and threshold
values one slot early. Atlas-level `nt_atlas_opts_t` carries no positional
contract at all — use designated initializers or `nt_atlas_opts_defaults()`.
When defaults matter, start from the defaults helper: a zero-initialized atlas
options struct means `max_added_area_percent = 0%` and `dedup = false`, not the
public 10% and dedup-on defaults it returns.

**Slice9 transform semantics:** non-zero slice9 borders auto-force `shape = RECT` and an identity-only effective transform mask. `allowed_transforms` of `0` (inherit) or `IDENTITY` are accepted and canonicalized to the stored `IDENTITY` override before cache-key generation; explicitly requesting any non-identity mask bit on a slice9 sprite is a caller bug and asserts. Dedup itself never reads the mask — a nine-patch's identity-only effective mask reaches the packer through the group intersection instead.

**Margin vs. extrude override semantics:**
- `margin` is **raise-only**: it only feeds the packing footprint, so a per-sprite value below the atlas margin is clamped up to the atlas value. An `UNFITTABLE` record reports the *effective* (clamped-up) margin the packer actually used, not a below-atlas request.
- `extrude`: `0` **inherits** the atlas default. A non-zero override sets **this sprite's edge bleed** (RECT only) and may be smaller OR larger than the atlas extrude — but a *zero* bleed cannot be expressed per-sprite (0 means inherit). Effective extrude, whether inherited or overridden, requires the effective sprite shape to be RECT. Compose/serialize apply the raw override. The packing footprint, however, reserves room for `max(this sprite's extrude, atlas extrude)`, so an `UNFITTABLE` record reports that effective (max) extrude — the space that actually caused the fit failure, distinct from the per-sprite bleed written into the page.

**Measured area-budget guidance (`mixed_aa`, native release):**

The current deterministic frontier evidence uses six public area-budget values. Each row is reconstructed from real production packs and repeat-verified through the selected-geometry proof. Tracked proofs use `portable-v1`: platform, machine CPU, and timing fields are removed before hashing. The recorded builder binary hash identifies the publisher but is not a consumer-side byte-identity gate, so the portable proof remains consumable across supported platforms. The frontier also records a deterministic hash of the production atlas-geometry sources — the single list in `scripts/lib/hull_geometry_sources.sh`, which includes the Clipper2 bridge and the vendored Clipper2 sources; visual acceptance rejects evidence after any of those sources changes, and the CI guard prints a non-fatal staleness warning. Canonical publication is explicit (`scripts/bench_hull_tolerance.sh --publish`), requires `native-release`, one builder thread, and the exact `mixed_aa` 0/2/5/10/15/25 sweep, and refuses a dirty worktree. Ordinary local sweeps only write under their output directory.

Visual acceptance is black-box: every polygon displayed in its 6/7/8 selected-count evidence was chosen by the public production selector, serialized into a real pack, reconstructed, and re-proved. Internal frontier slots cannot be forced into acceptance output. `visual_input_sha256` hashes the actual decoded RGBA plus resolved row controls used by the builder.

| Added area budget | Hull vertices total | Mean | Frontier fill | Representative total overdraw | Sweep SHA-256 |
| ---: | ---: | ---: | ---: | ---: | --- |
| 0% | 896 | 7.0000 | 0.5465 | 0.15990160% | `cdcc56c66377ed8428ad9b743e9a204344bc98f0ee4ccc6718dbc9716b27967f` |
| 2% | 752 | 5.8750 | 0.5857 | 0.76260763% | `39339f1b0dccb65d2c7ca9a4c43f017464e3bf2b9a79837770799791a1e4e471` |
| 5% | 716 | 5.5938 | 0.5766 | 0.76260763% | `8e27e8bf34027deed6587898121a83b6ac9f84ef8b9dd77a1abf6b3dd1537165` |
| 10% | 666 | 5.2031 | 0.5547 | 0.76260763% | `ae40d10f58d65ddcd4448d7e14e69cc506307689e3746525d22990715c8dce0c` |
| 15% | 642 | 5.0156 | 0.5814 | 0.76260763% | `84e4ef521f145c7c44216be2687c2f2b9124f78aff19df6a304949e810aa0ff1` |
| 25% | 598 | 4.6719 | 0.5565 | 0.76260763% | `d93d867da5b8d40344e2cd75172b12f6d90fd7738cdcb922f644f99e51000d77` |

**Pivot semantics:**
- Normalized over the **source image** dimensions (not the trimmed rect). Default `(0.5, 0.5)` = image centre.
- **Values outside `[0, 1]` are allowed** — pivots may lie outside the frame for weapons, effects, or motion-stabilised sprites. Must be finite (`isfinite()` asserted; NaN/inf is caller bug).
- Source-space (not trim-space) is chosen so frame-by-frame animations with varying per-frame trim bounds have stable pivots across frames. A walk cycle with `origin_y = 0.9375` sits on the same source-image pixel row regardless of how much whitespace the alpha trim removed from each frame.

**Glob rule:** `nt_atlas_add_glob` asserts `opts->name == NULL` — a single name cannot apply to N matched files without hash collisions. Each matched file derives its own name from its path, and the `origin_x/y` fields propagate to all of them. For per-file name overrides within a glob, call `nt_builder_glob_iterate` with a custom callback that calls `nt_atlas_add` per match.

**Dedup + different pivots:** adding the same pixel-identical sprite twice with different `origin_x/y` produces **two separate regions** that share one placement, and — because their blocks come out byte-identical — one `vertex_start` / `index_start` range in the blob. The dedup pass matches on the canonical post-trim content hash, a trim-local byte compare, and the **resolved** packing controls: effective shape, max vertices, alpha threshold, max-added-area percent, margin and extrude — so two spellings of the same resolved value (e.g. an inherited margin and an explicit one equal to the atlas margin) dedup together. Origin is not considered, so each logical region stores its own pivot while sharing geometry/pixels. Neither the slice9 borders nor the transform mask take part: borders are per-region metadata that never reaches the packed pixels (RECT, untrimmed, identity), so two nine-patches with the same art and different borders share one placement and each keeps its own borders; and the mask is admission policy rather than content identity, so a sprite allowing all eight orientations may alias onto one restricted to identity. This is the cheap path for "same sprite, different anchor" (e.g. icon referenced with centre pivot in menu vs bottom-centre in HUD).

An alias whose relative transform is **not** identity does not come out byte-identical: its local↔UV pairing differs, so it gets its own vertex/index block and its own `region.transform` holding `d4_compose(placement, relative)`. Its geometry is the exact integer D4 pre-image of the root's hull, re-triangulated through the same path a standalone pack uses, so `NtAtlasRegion.flags` (the `QUAD_*` render hint) does not depend on which orientation an alias happens to carry. The runtime needs no change for any of it — orientation is baked into the per-vertex `atlas_u/v` at serialize time, and `NtAtlasRegion.transform` stays exporter metadata.

**Dedup off-switch:** atlas-level `nt_atlas_opts_t.dedup` is `true` in `nt_atlas_opts_defaults()`, so dedup is opt-out; a zero-initialized options struct therefore means dedup OFF, the same trade-off `allowed_transforms` makes. Per sprite the override is tri-state: `0` inherits the atlas flag, `NT_ATLAS_SPRITE_DEDUP_ON` forces sharing on, `NT_ATLAS_SPRITE_DEDUP_OFF` forces it off. OFF is **bidirectional** — such a sprite never aliases onto another sprite and never becomes an alias target, including through the plain identity case — so it always ends up with a private placement rectangle and its own page pixels. That guarantee is what runtime replace / draw-into-region needs: writing into one region must not disturb any other. Both the atlas flag and the per-sprite override participate in the atlas cache key, so flipping either produces a fresh pack rather than a stale cached one.

**Zero-init footgun:** C99 designated-initialiser compound literals (`&(nt_atlas_sprite_opts_t){.origin_y = 1.0F}`) zero-init unset fields — so `origin_x` becomes `0.0`, not the default `0.5`. Always start from `nt_atlas_sprite_opts_defaults()` for partial overrides, or set every field explicitly in the literal.

### Atlas cache

Separate from the per-asset [builder cache](#builder-cache) because atlas placement is a global decision over the whole sprite set.

**Cache key:** `xxh64(per_sprite(decoded_hash + source_width + source_height + origin_x + origin_y + raw_overrides) + pack_opts + ATLAS_CACHE_KEY_VERSION)`. `ATLAS_CACHE_KEY_VERSION` is currently 22. Pack options include atlas `alpha_threshold`, `max_vertices`, `max_added_area_percent`, the `allowed_transforms` mask, and the atlas `dedup` flag; per-sprite identity includes threshold, max-vertices, the transform-mask override, the tri-state dedup override, and the explicit max-added-area and alpha-threshold presence bits even when they currently resolve to inherited values. Slice9 is the transform-mask exception: its accepted `0` and `IDENTITY` inputs are canonicalized to the same stored `IDENTITY` override because they have identical forced behavior. Each overridable payload participates only when its presence bit is true; otherwise the unused value is canonicalized to zero. This keeps cache and dedup behavior safe if atlas defaults or resolution rules change. Signed zero is canonicalized before storage and hashing. Per-sprite data is hashed in add order because cached placements reference sprites by index. Source dimensions are part of identity because the same flat RGBA bytes can describe different image shapes. Only pack/compose-affecting options are included; post-pack fields are handled by the texture encode cache.

**Storage:** one `atlas_<key>.bin` file per cache entry, containing the placement table and composed page pixels. On hit, the pipeline skips pack, compose, and cache write; debug output, serialization, and publish still run.

**Invalidation:** any change to source pixels, raw pack-affecting overrides, pack opts, or `ATLAS_CACHE_KEY_VERSION` produces a fresh key. The version constant is bumped when the packer's behavior changes in a way that would silently produce different output.

**Failure mode:** atomic temp+rename writes; read/write failures fall through to a fresh build, never break it.
