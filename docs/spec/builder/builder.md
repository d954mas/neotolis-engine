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
NtBuilderContext *ctx = nt_builder_start_pack("build/base.ntpack");
nt_builder_add_shaders(ctx, "assets/shaders/*.vert", NT_BUILD_SHADER_VERTEX);
nt_builder_add_textures(ctx, "assets/textures/ui/*.png", &tex_opts);
nt_builder_add_meshes(ctx, "assets/meshes/common/*.glb", &mesh_opts);
nt_build_result_t r = nt_builder_finish_pack(ctx);
nt_builder_free_pack(ctx);
```

Working references: `examples/*/build_packs.c`.

## Why code-based builder

Explicit control, no DSL needed, powerful grouping logic, easy custom per-project rules, aligns with engine philosophy.

## Builder module layers

```text
tools/builder/
    main.c                     CLI entry
    nt_builder.c               context, pack write, error channel
    nt_builder_texture.c       nt_builder_mesh.c        nt_builder_shader.c
    nt_builder_font.c          nt_builder_blob.c        nt_builder_scene.c
    nt_builder_atlas.c         nt_builder_atlas_geometry.c  nt_builder_atlas_vpack.c
    nt_builder_cache.c         nt_builder_codegen.c     nt_builder_dump.c
    nt_builder_glob.c          nt_builder_hash.c        nt_builder_include.c
    nt_builder_tangent.c
```

## Core builder API

Signatures are canonical in `tools/builder/nt_builder.h`; this list names the
surface an asset pipeline uses (no material or audio adds exist — those formats
are not built yet).

```c
NtBuilderContext *nt_builder_start_pack(const char *output_path);
nt_build_result_t nt_builder_finish_pack(NtBuilderContext *ctx);
void nt_builder_free_pack(NtBuilderContext *ctx);

nt_builder_add_mesh / add_texture / add_shader / add_font   /* one source file */
nt_builder_add_meshes / add_textures / add_shaders / add_fonts   /* glob pattern */
nt_builder_add_texture_from_memory / add_texture_raw   /* in-memory pixels */
nt_builder_add_scene_mesh   /* one primitive out of a parsed GLB scene */
nt_builder_add_blob         /* opaque bytes under a resource id */
nt_builder_add_asset_root   /* convention-based tree import */
/* Font opts: charset (required), name override, target_units_per_em. */

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

**Mesh stream layouts** (both `add_mesh` and the scene API share one validator): stream count and per-stream component count ranges, valid stream type, `normalized` only on integer types, a required POSITION stream, non-NULL unique `engine_name`s — compared by the 32-bit name hash the pack actually carries, so hash collisions are rejected too — unique `gltf_name`s (one source attribute feeds one stream), and the WebGL2 alignment rule — every attribute's byte offset and the vertex stride must be multiples of that attribute's type size. Desktop GL tolerates misaligned layouts, so without this check a bad layout draws in native runs and fails only in the browser; the builder rejects it offline with the offending stream named.

**Component narrowing.** A layout entry may declare `source_components` (default 0): the source accessor must then carry exactly that many components and the leading `count` are packed — e.g. a VEC4 `COLOR_0` narrowed to RGB, or a computed tangent's handedness dropped. Narrowing is declared explicitly so a genuine source/layout width mismatch still fails the build; `count` greater than the source width (widening/padding) is rejected. Narrowing affects only the pack layout, never builder computations: streams are extracted at source width, MikkTSpace runs on full-width data, and compaction to the declared counts happens last. So a mesh can pack a narrowed POSITION and computed tangents at once. Computed (MikkTSpace) tangents count as a 4-component source; their POSITION/NORMAL/UV0 inputs must be VEC3/VEC3/VEC2 **sources** — automatic for conformant glTF.

**Mesh wire encode.** The shared mesh-buffer builder (`nt_builder_build_mesh_buffer`,
one choke point for `add_mesh` and the scene API) emits the wire form of
[runtime-formats](../assets/runtime-formats.md#mesh-wire-layout-format-v3)
automatically — no knob. Vertices are always written as SOA planes. Indices
are encoded with the meshopt index codec when `index_type != 0`,
`index_count > 0` and `index_count % 3 == 0`, and the encoded stream is kept
only if it is smaller than RAW. The draw count (indices, else vertices) must be a
multiple of 3 — GL draws only GL_TRIANGLES, so a partial trailing triangle is
rejected (`NT_BUILD_ERR_VALIDATION`), never packed. Before encoding, every
index is validated
`< vertex_count` (`NT_BUILD_ERR_VALIDATION` otherwise — the codec sizes its
encode buffer from `vertex_count`, and this also closes the RAW path's silent
out-of-range hole). When the encoded stream wins, the encoder decodes it back and stores the
decoded (canonicalized) order as the pack ground truth -- a decode-back
failure is `NT_BUILD_ASSERT` (broken codec, never a silent RAW fallback).
When RAW wins (tiny meshes), the SOURCE index order ships unchanged,
provoking vertex included. MikkTSpace, AABB and vertex data are
computed before canonicalization and are invariant under it.

**Tangent model.** The layout expresses tangent presence (a TANGENT stream or not); `tangent_mode` only says where the data comes from: `AUTO` (glTF, else MikkTSpace), `COMPUTE` (always MikkTSpace), `REQUIRE` (glTF or build error). A mesh without normal mapping simply omits the TANGENT stream. Tangent computation exists only in the scene API; `add_mesh` reads TANGENT from the glTF and asserts on any mode other than `AUTO`.

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
  Exception: `ATLAS_REGION` constants hash the region name alone (not the `atlas/name` path) — the runtime
  looks regions up per-atlas by name hash (`nt_atlas_find_region`, see `engine/atlas/nt_atlas.h`), while the
  identifier and comment still carry the full path for readability.
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

**Scope:** the cache pays off where encode is expensive at `finish_pack` time
(textures/basis). Meshes are OUT of its scope by construction: `add_mesh`
builds the final blob (extraction, tangents, wire encode) eagerly at add time,
so the entry's decoded bytes already ARE the encoded result and repeat builds
redo that work regardless of the cache. Mesh wire encode is milliseconds per
mesh; if it ever grows expensive, the fix is moving it behind the cache, not
widening this contract.

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
3. **dedup** — fold identical art onto one placement rectangle. Canvas size, art
   position and orientation do not keep identical art apart.
   - **Search.** Bucket by the D4-invariant post-trim dimension pair
     `(min(w,h), max(w,h))`; key inside a bucket by a canonical **post-trim**
     content hash (the minimum of the eight orientation hashes); confirm by an
     exact post-trim byte compare **through a relative transform**.
   - **Joining a group.** A sprite joins under relative transform `rel` when `rel`
     is in that sprite's own effective `allowed_transforms` mask and its pixels
     match the root's under `rel` (`rel` means `root_bitmap == rel(alias_bitmap)`);
     the lowest such `rel` wins. `rel = identity` is simply the exact case, so
     there is one search and no separate exact stage. The alias's region
     `transform` stores `compose(placement, rel)` — equal to `rel` alone only when
     the group packs at an identity placement.
   - **Roots and exclusion.** A group's root is its lowest-add-index **eligible**
     member, so repacks are byte-identical. A sprite whose resolved `dedup` is off
     keeps hash `0`, sorts into a separate run, and is skipped as both member and
     target — never an alias, never an alias target.
   - **Multiple groups per run.** Only roots are fold targets, so a member whose
     own mask forbids the relative it would need to reach the earlier root stays
     unfolded and becomes a second root. Placement count is therefore add-order
     sensitive for such mixed-mask runs, deliberately: widening the search to a
     relative outside the member's mask (legal whenever some group placement makes
     the product land back inside it) is a density trade the packer does not take.
   - **When the hash is computed.** Only for buckets holding two or more sprites,
     and only for members that could actually fold — a singleton keeps hash `0`,
     so does a `DEDUP_OFF` sprite. A bucket whose members all resolve to
     identity-only masks hashes the identity orientation alone. That narrowing is
     per **bucket**, never per sprite: minimum-over-orbit is orientation-invariant
     only when the orientation set is a subgroup, and a mask such as
     `{identity, rot90}` is not closed. A bucket is the widest set whose keys are
     ever compared (a run never crosses a dimension bucket), so one orientation
     set per bucket is exactly as strict as one per atlas.
   - **What is shared.** The placement rectangle and nothing else: every sprite
     still owns its vertex/index block, and whether two blocks share one byte
     range in the blob is decided separately, by byte equality, in `serialize`.
4. **geometry** — for each unique sprite, build the binary mask from the same effective alpha threshold and select the shape-specific geometry path. `RECT` emits the trim AABB. `CONVEX_HULL` starts from the opaque-pixel convex hull. `CONCAVE_CONTOUR` performs morphological closing when needed, traces the boundary, and evaluates concave simplifiers. Polygon candidates share full retained-cell coverage, topology, triangulation, area-budget, vertex-budget, and deterministic-selection checks described below.
5. **pipeline_validate** — non-mutating pre-pack checks that report every bad sprite in one pass: empty-page fit, duplicate region names, region-count cap, and per-sprite trim-dimension limits. It still runs on surviving sprites after earlier content errors.
6. **tile_pack** — call `vector_pack` (NFP packer, see below) to assign each unique sprite to a page and (x, y) position.
7. **compose** — blit trimmed pixels onto page buffers, run AABB edge-extrude only when packing uses rectangles; in polygon mode, require `extrude=0` and rely on `padding`.
8. **serialize** — build the atlas blob in transaction-owned storage.
   - Every sprite's vertex/index block is emitted from its own geometry and its own
     region transform; byte-identical blocks then share one range, first writer in
     add order wins.
   - Sprites on different pages can share a block — the page comes from
     `NtAtlasRegion.page_index`, not from the vertex data.
   - Block identity additionally requires equal `trim_offset_x/y`, neither of which
     is part of the block: the runtime bakes the trim offset into `cached_pos[]`,
     indexed by `vertex_start`, so two regions on one byte range would overwrite
     each other's precomputed positions and the later region would win. Post-trim
     dedup is what made differing trim offsets reachable inside one group, so the
     two rules ship together.
9. **cache_write/debug_png** — persist optional successful-build artifacts after all recoverable work has succeeded.
10. **publish** — register the atlas blob, page textures, metadata, and region codegen in the pack. Capacity, allocation, and resource-ID failures assert and terminate the build; they are not recoverable rollback paths.

Any content error collected during trim, geometry, or validation prevents packing and publication, but surviving sprites still pass through the non-mutating validation stages so the transaction reports related errors together. A failed transaction appends those errors to the pack and publishes nothing. Cache hits skip packing and compose, but still serialize and publish the same output. Before a polygon candidate is accepted, its triangle union must continuously cover the full unit-square area of every retained pixel cell; centre-only coverage is not sufficient.

### Dedup statistics

The fixed stats storage holds `NT_BUILD_MAX_ATLASES` records (defaults to `NT_BUILD_MAX_ASSETS`, so observability never imposes a lower atlas limit even when a consumer raises the asset ceiling).

`const nt_atlas_stats_t *nt_builder_get_atlas_stats(ctx, &count)` returns one
record per successfully committed atlas, in commit order: `sprites`, `placements`,
`folds_exact`, `folds_d4`, `area_saved_px`, `vertex_blocks_shared`, `cache_hit`.

- **Invariant:** `folds_exact + folds_d4 + placements == sprites` holds for every
  record, cache hits included — dedup, geometry and serialize run on every commit,
  only pack and compose are skipped. That is why `cache_hit` is the one observable
  distinguishing a replay from a rebuild.
- **Lifetime:** like the content-error channel, a borrowed read-only view owned by
  the context and valid only until `nt_builder_free_pack`. Deliberately **not**
  reachable through the `NtAtlasBuild` handle, which commit frees.
- **Why it must be observed here:** only `placements` is recoverable from the
  packed atlas, and only by counting distinct per-page UV **rings** (rotation- and
  reversal-insensitive) — not by `vertex_start`, which a D4 alias no longer shares,
  and not by the UV bbox, which two disjoint concave sprites can share. The
  exact/D4 split, the saved area and the shared-block count exist nowhere else.
- The same numbers are appended to the `BENCH` log line as `folds_exact`,
  `folds_d4`, `area_saved` and `blocks_shared`.

### Vector packer

The packer is **NFP/Minkowski-based** (`nt_builder_atlas_vpack.c`). For each candidate position the incoming polygon is tested against the union of No-Fit Polygons of all already-placed sprites. Properties:

- **Sub-pixel exact** — no quantization to a tile grid.
- **Concave-aware** — Clipper2 `MinkowskiSum + Union(NonZero)` produces multi-ring NFPs for concave inputs; rings are forbidden zones.
- **D4 orientations** — flipH, flipV, diagonal flip and combinations, gated by the
  effective `allowed_transforms` mask. A zero per-sprite mask inherits the atlas
  mask; a non-zero mask replaces it, so a sprite may narrow or widen the atlas
  default. Identity is always permitted.
  - A shared placement is oriented once for the whole dedup group, and a member's
    region transform is `d4_compose(placement, its relative)` — the alias's local
    space maps onto the root's, the root's onto the page.
  - So the mask handed to the packer is the **intersection**, over every member, of
    the placements whose product with that member's relative still lands inside
    that member's own mask, floored at identity.
  - It is evaluated per placement candidate rather than assumed, because an
    allowed-transform mask need not be closed under D4 composition (D4 is a group;
    an arbitrary subset of it is not). The identity placement always survives — a
    member's own relative is by construction already in its mask.
  - A nine-patch anywhere in a group resolves to identity-only and therefore pins
    that placement to identity, with no special case. Groups whose relatives are
    all identity reduce to the plain intersection of member masks.
- **NFP cache** — 8-way set-associative seqlock cache keyed by `(placed_shape_hash, incoming_shape_hash)`. Lock-free reads via version counter, CAS writes. Same shape pair across different sprites reuses the cached NFP.
- **Parallel build** — when `nt_builder_set_threads(ctx, N)` is called, NFP construction and candidate scanning run on a thread pool. Per-thread stat accumulators merge into global stats deterministically.
- **Page growth** — sprites that don't fit allocate a new page (up to `NT_ATLAS_MAX_PAGES = 8`, the shared format cap — the runtime preallocates that many page slots per atlas, so packing more would produce an unloadable atlas); new pages start with the same dimensions as the first.

### Atlas options

Fields and defaults are in `nt_atlas_opts_t` / `nt_atlas_shape_t`
(`tools/builder/nt_builder.h`). Contracts that the struct cannot state:

- A zero-initialized struct means `dedup` OFF and `shape` RECT — both differ from
  the documented "default" of a filled struct. Fill the struct explicitly.
- `extrude > 0` is legal only with `shape == NT_ATLAS_SHAPE_RECT`.
- `allowed_transforms` has identity as an implicit floor: a mask of 0 inherits the
  atlas mask, never "nothing allowed".

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

Fields are in `nt_atlas_sprite_opts_t` (`tools/builder/nt_builder.h`). Two
inherit conventions run through it: `0` means "inherit the atlas value" for
`shape` / `allowed_transforms` / `max_vertices` / `margin` / `extrude` / `dedup`,
while `max_added_area_percent` and `alpha_threshold` carry an explicit
`has_*` presence flag — without it an intentional 0 would be indistinguishable
from inherit.

New per-sprite controls are **appended** after the existing field list, so legal
positional initializers keep their field mapping on recompile (a
source-recompile promise, not a binary-ABI one for stale objects), and
zero-initialized appended controls preserve inherit semantics. Atlas-level
`nt_atlas_opts_t` makes no such promise — use designated initializers or
`nt_atlas_opts_defaults()`.

**Slice9 transform semantics:** non-zero slice9 borders auto-force `shape = RECT` and an identity-only effective transform mask. `allowed_transforms` of `0` (inherit) or `IDENTITY` are accepted and canonicalized to the stored `IDENTITY` override before cache-key generation; explicitly requesting any non-identity mask bit on a slice9 sprite is a caller bug and asserts. Dedup's *content* comparison never reads the mask — grouping is by pixels alone — but *admission* does: a nine-patch's identity-only effective mask both restricts the relative transforms it may join with and reaches the packer through the group intersection.

**Margin vs. extrude override semantics:**
- `margin` is **raise-only**: it only feeds the packing footprint, so a per-sprite value below the atlas margin is clamped up to the atlas value. An `UNFITTABLE` record reports the *effective* (clamped-up) margin the packer actually used, not a below-atlas request.
- `extrude`: `0` **inherits** the atlas default. A non-zero override sets **this sprite's edge bleed** (RECT only) and may be smaller OR larger than the atlas extrude — but a *zero* bleed cannot be expressed per-sprite (0 means inherit). Effective extrude, whether inherited or overridden, requires the effective sprite shape to be RECT. Compose/serialize apply the raw override. The packing footprint, however, reserves room for `max(this sprite's extrude, atlas extrude)`, so an `UNFITTABLE` record reports that effective (max) extrude — the space that actually caused the fit failure, distinct from the per-sprite bleed written into the page.

**Measured area-budget guidance (`mixed_aa`, native release):**

The current deterministic frontier evidence uses six public area-budget values. Each row is reconstructed from real production packs and repeat-verified through the selected-geometry proof. Tracked proofs use `portable-v1`: platform, machine CPU, and timing fields are removed before hashing. The recorded builder binary hash identifies the publisher but is not a consumer-side byte-identity gate, so the portable proof remains consumable across supported platforms. The frontier also records a deterministic hash of the production atlas-geometry sources — the single list in `scripts/lib/hull_geometry_sources.sh`, which includes the Clipper2 bridge and the vendored Clipper2 sources; visual acceptance rejects evidence after any of those sources changes, and the CI guard prints a non-fatal staleness warning. Canonical publication is explicit (`scripts/bench_hull_tolerance.sh --publish`), requires `native-release`, one builder thread, and the exact `mixed_aa` 0/2/5/10/15/25 sweep, and refuses a dirty worktree. Ordinary local sweeps only write under their output directory.

Visual acceptance is black-box: every polygon displayed in its 6/7/8 selected-count evidence was chosen by the public production selector, serialized into a real pack, reconstructed, and re-proved. Internal frontier slots cannot be forced into acceptance output. `visual_input_sha256` hashes the actual decoded RGBA plus resolved row controls used by the builder.

The measured numbers themselves (per-budget vertex counts, frontier fill,
overdraw, and the sweep/pack SHA-256s) live in the published artifact
`tools/research/atlas_bench/hull_area_frontier.json` — that file is the
authority; never hand-copy rows out of it.

**Pivot semantics:**
- Normalized over the **source image** dimensions (not the trimmed rect). Default `(0.5, 0.5)` = image centre.
- **Values outside `[0, 1]` are allowed** — pivots may lie outside the frame for weapons, effects, or motion-stabilised sprites. Must be finite (`isfinite()` asserted; NaN/inf is caller bug).
- Source-space (not trim-space) is chosen so frame-by-frame animations with varying per-frame trim bounds have stable pivots across frames. A walk cycle with `origin_y = 0.9375` sits on the same source-image pixel row regardless of how much whitespace the alpha trim removed from each frame.

**Glob rule:** `nt_atlas_add_glob` asserts `opts->name == NULL` — a single name cannot apply to N matched files without hash collisions. Each matched file derives its own name from its path, and the `origin_x/y` fields propagate to all of them. For per-file name overrides within a glob, call `nt_builder_glob_iterate` with a custom callback that calls `nt_atlas_add` per match.

**Dedup + different pivots:** adding the same pixel-identical sprite twice with
different `origin_x/y` produces **two separate regions** sharing one placement and
— because their blocks come out byte-identical — one `vertex_start` /
`index_start` range in the blob. The cheap path for "same sprite, different
anchor" (an icon centre-pivoted in a menu, bottom-centre in a HUD).

What the match considers:

- **Yes:** the canonical post-trim content hash, a trim-local byte compare, and the
  **resolved** packing controls — effective shape, max vertices, alpha threshold,
  max-added-area percent, margin, extrude. Two spellings of the same resolved value
  (an inherited margin and an explicit one equal to the atlas margin) dedup
  together.
- **No — origin:** each logical region stores its own pivot while sharing
  geometry/pixels.
- **No — slice9 borders:** per-region metadata that never reaches the packed pixels
  (RECT, untrimmed, identity), so two nine-patches with the same art and different
  borders share one placement and each keeps its own borders.
- **No — the transform mask:** it is admission policy, not content identity, so a
  sprite allowing all eight orientations may alias onto one restricted to identity.

An alias whose relative transform is **not** identity does not come out byte-identical: its local↔UV pairing differs, so it gets its own vertex/index block and its own `region.transform` holding `d4_compose(placement, relative)`. Its geometry is the exact integer D4 pre-image of the root's hull, canonicalized and re-triangulated through the same path a standalone pack uses, so `NtAtlasRegion.flags` (the `QUAD_*` render hint) does not depend on which orientation an alias happens to carry.

**Canonical ring rotation.** Every emitted polygon ring is rotated to start at its lexicographically smallest `(x, y)` vertex. Contour tracing and the D4 pull-back reach the same ring by different routes and would otherwise start at different vertices — and `Clipper2` triangulates in array order, so the same bitmap would get different index bytes, a different `QUAD_*` hint, and no chance of byte-identical block sharing. With the rule applied on both routes an alias's vertex and index bytes are identical to a standalone pack of the same image, which is what makes the hint orientation-independent. The `QUAD_*` match itself is taken up to triangle rotation and triangle order, because the start vertex inside each triangle belongs to `Clipper2`, not to us. The runtime needs no change for any of it — orientation is baked into the per-vertex `atlas_u/v` at serialize time, and `NtAtlasRegion.transform` stays exporter metadata.

**Dedup off-switch:** atlas-level `nt_atlas_opts_t.dedup` is `true` in `nt_atlas_opts_defaults()`, so dedup is opt-out; a zero-initialized options struct therefore means dedup OFF, the same trade-off `allowed_transforms` makes. Per sprite the override is tri-state: `0` inherits the atlas flag, `NT_ATLAS_SPRITE_DEDUP_ON` forces sharing on, `NT_ATLAS_SPRITE_DEDUP_OFF` forces it off. OFF is **bidirectional** — such a sprite never aliases onto another sprite and never becomes an alias target, including through the plain identity case — so it always ends up with a private placement rectangle and its own page pixels. That is the exact scope of the guarantee — pixels and placement, not blob bytes: the serialize-stage block sharing is decided by byte equality alone and does not consult the off-switch, so an OFF sprite may still share a read-only vertex/index range with another region. Both the atlas flag and the per-sprite override participate in the atlas cache key, so flipping either produces a fresh pack rather than a stale cached one.

**Zero-init footgun:** C99 designated-initialiser compound literals (`&(nt_atlas_sprite_opts_t){.origin_y = 1.0F}`) zero-init unset fields — so `origin_x` becomes `0.0`, not the default `0.5`. Always start from `nt_atlas_sprite_opts_defaults()` for partial overrides, or set every field explicitly in the literal.

### Atlas cache

Separate from the per-asset [builder cache](#builder-cache) because atlas placement is a global decision over the whole sprite set.

**Cache key:** `xxh64(per_sprite(decoded_hash + source_width + source_height + origin_x + origin_y + raw_overrides) + pack_opts + ATLAS_CACHE_KEY_VERSION)`. `ATLAS_CACHE_KEY_VERSION` is currently 22. Pack options include atlas `alpha_threshold`, `max_vertices`, `max_added_area_percent`, the `allowed_transforms` mask, and the atlas `dedup` flag; per-sprite identity includes threshold, max-vertices, the transform-mask override, the tri-state dedup override, and the explicit max-added-area and alpha-threshold presence bits even when they currently resolve to inherited values. Slice9 is the transform-mask exception: its accepted `0` and `IDENTITY` inputs are canonicalized to the same stored `IDENTITY` override because they have identical forced behavior. Each overridable payload participates only when its presence bit is true; otherwise the unused value is canonicalized to zero. This keeps cache and dedup behavior safe if atlas defaults or resolution rules change. Signed zero is canonicalized before storage and hashing. Per-sprite data is hashed in add order because cached placements reference sprites by index. Source dimensions are part of identity because the same flat RGBA bytes can describe different image shapes. Only pack/compose-affecting options are included; post-pack fields are handled by the texture encode cache.

**Storage:** one `atlas_<key>.bin` file per cache entry, containing the placement table and composed page pixels. On hit, the pipeline skips pack, compose, and cache write; debug output, serialization, and publish still run.

**Invalidation:** any change to source pixels, raw pack-affecting overrides, pack opts, or `ATLAS_CACHE_KEY_VERSION` produces a fresh key. The version constant is bumped when the packer's behavior changes in a way that would silently produce different output.

**Failure mode:** atomic temp+rename writes; read/write failures fall through to a fresh build, never break it.
