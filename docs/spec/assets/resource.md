# Resource System

Two-level resource registry: `NtAssetMeta` per pack asset, `NtResourceSlot` per
requested resource_id, generational `nt_resource_t` handles, and priority-stacked
packs with target/published winner resolve. Covers resolve callbacks, blob
pinning (PIN_BLOB), virtual packs, asset types with the NT_ASSET_FONT and
NT_ASSET_ATLAS binary formats, placeholder policy, and `nt_hash` identity hashing.

Related: [Async Loading](async-loading.md), [Pack Format](ntpack.md), [Builder Architecture](../builder/builder.md)

## Core concepts

- `publication_epoch`: monotonic counter that changes when the published view of any slot changes

- `resource_id`: 64-bit xxHash of asset path (`nt_hash64_t`) — stable resource identity
- `NtAssetMeta`: per-asset metadata entry (one per asset per pack)
- `NtResourceSlot`: per unique resource requested by game code — holds resolved handle and optional user_data
- `nt_resource_t`: generational handle to a slot — what game code holds and passes around

Two-level system:
- **Assets** (MAX_ASSETS): metadata from all packs. Same resource_id can appear in multiple packs.
- **Slots** (MAX_SLOTS): unique resources requested by game. One slot per resource_id, holds the published handle plus any per-slot auxiliary state.

Each slot tracks two winner notions:
- **target winner**: highest-priority READY asset for this resource_id
- **published winner**: highest-priority asset that is usable right now

For simple runtime-handle asset types (texture, mesh, blob), target and published winners usually match. Asset types that derive persistent auxiliary state from pack bytes (atlas, future similar types) may defer publication until `user_data` has been synchronized to the target winner.

## ResourceId

`resource_id` is a `uint64_t` xxHash (XXH64) of the asset path, wrapped in `nt_hash64_t` for type safety. Game code obtains it via `nt_hash64_str("path")`. The `nt_hash` module provides centralized hashing for both builder and runtime. The registry uses resource_id to match assets across packs and resolve priority.

## Generational handles

Game code receives `nt_resource_t` — a 32-bit handle encoding slot index (lower 16 bits) and generation (upper 16 bits). Generation detects stale handles within a single init/shutdown lifecycle. After shutdown, all handles are invalid — game code must re-request resources after reinit. Access functions (`nt_resource_get`, `nt_resource_is_ready`) validate generation before returning data. `nt_resource_get()` returns the currently published winner handle. `nt_resource_is_ready()` means "published winner is fully usable", not merely "some runtime handle exists somewhere in the stack."

Typed wrappers (MeshHandle, TextureHandle) live outside nt_resource — game code or future phases.

`nt_resource_publication_epoch()` exposes a monotonic change counter for systems that want to skip work when published slot data has not changed.

## AssetMeta stability

**Unmount** removes asset entries (resource_id = 0) — slots are recycled for new packs. **Unload** (Phase 25) clears runtime handle/state but preserves metadata — enables fast reload without re-parsing.

`NT_BLOB_AUTO` eviction clears only the pack blob bytes. Already-activated assets keep `state == READY` and their `runtime_handle`. Whether a slot can stay published after eviction depends on asset type:
- simple assets stay usable from the runtime handle alone
- aux-backed assets stay published only if their existing `user_data` already belongs to the published winner

## NtAssetMeta

```c
typedef struct {
    uint64_t resource_id;    /* nt_hash64 value */
    uint32_t offset;         /* byte offset in pack blob */
    uint32_t size;           /* asset data size */
    uint32_t runtime_handle; /* resolved handle, 0 = none */
    uint16_t format_version; /* per-type binary format version */
    uint16_t pack_index;     /* index into packs[] */
    uint8_t asset_type;      /* nt_asset_type_t */
    uint8_t state;           /* nt_asset_state_t */
    uint8_t is_dedup;        /* 1 = shares data with another asset in same pack */
    uint8_t _pad;
    uint32_t meta_offset; /* byte offset into pack's resident meta_data buffer (NT_NO_METADATA = no meta) */
} NtAssetMeta;
```

## NtResourceSlot

Persistent per-slot state — survives across frames:

```c
typedef struct {
    uint64_t resource_id;            /* nt_hash64 value */
    uint32_t runtime_handle;         /* published winner's runtime handle (what game sees) */
    uint16_t generation;             /* stale-handle detection; incremented on slot reuse */
    int16_t resolve_prio;            /* priority of currently published winner */
    uint32_t resolve_seq;            /* mount_seq of published winner (tiebreak) */
    uint16_t resolve_asset_idx;      /* index into assets[] of published winner */
    uint16_t prev_resolve_asset_idx; /* previous published winner (change detection) */
    uint16_t user_data_asset_idx;    /* asset idx last used to build user_data (aux sync check) */
    uint32_t prev_runtime_handle;    /* previous published handle (detect re-activation) */
    uint8_t asset_type;              /* nt_asset_type_t */
    uint8_t state;                   /* nt_asset_state_t visible to game code */
    void *user_data;                 /* per-slot auxiliary data (on_resolve/on_cleanup) */
} NtResourceSlot;
```

Transient per-pass state — allocated at the start of each resolve pass, freed at the end. Lives in a separate array to keep NtResourceSlot small for the common case (resolve runs only when `needs_resolve` is true):

```c
typedef struct {
    uint32_t target_runtime_handle;    /* best READY asset handle, even if blob is evicted */
    uint32_t candidate_runtime_handle; /* best READY asset handle that is publishable now */
    int16_t target_prio;               /* priority of target winner */
    int16_t candidate_prio;            /* priority of publishable candidate */
    uint32_t target_seq;               /* mount_seq of target winner */
    uint32_t candidate_seq;            /* mount_seq of publishable candidate */
    uint16_t target_asset_idx;         /* assets[] index of target winner */
    uint16_t candidate_asset_idx;      /* assets[] index of publishable candidate */
    uint8_t scan_state;                /* best nt_asset_state_t seen among all matching assets */
    uint8_t resolve_pending;           /* on_resolve fired for the published winner */
    uint8_t post_resolve_pending;      /* on_post_resolve should fire after the pass */
} NtResolveTemp;
```

The resolve pass computes both the target winner and the published winner for each slot:
- the target winner is purely priority/sequence-based over READY assets
- the published winner is the best asset that is usable now

For aux-backed asset types, "usable now" means one of two things:
- `user_data` was already built from this exact asset (`user_data_asset_idx == asset_idx`)
- or the winner's blob is currently resident, so `on_resolve` can rebuild `user_data` immediately

If a higher-priority target winner is not yet publishable, the slot keeps the best lower-priority usable fallback published. If no usable fallback exists, the slot reports `LOADING` until publication can complete. `nt_resource_get_state()` and `nt_resource_is_ready()` always report the published state, not the raw target winner state.

### Resolve callbacks (on_resolve / on_cleanup / on_post_resolve)

Per-asset-type callbacks for auxiliary data that persists across pack stacking. Registered separately from activate/deactivate — asset types that don't use them pay nothing.

```c
typedef void (*nt_resolve_fn)(const uint8_t *data, uint32_t size, uint32_t runtime_handle, void **user_data);
typedef void (*nt_cleanup_fn)(void *user_data);
typedef void (*nt_post_resolve_fn)(const uint8_t *data, uint32_t size, nt_resource_t handle, uint32_t runtime_handle, void *user_data);

nt_resource_set_resolve_callbacks(asset_type, on_resolve, on_cleanup);
nt_resource_set_post_resolve_callback(asset_type, on_post_resolve);
nt_resource_set_behavior_flags(asset_type, flags);
const void *nt_resource_peek_user_data(handle);
```

Behavior flags:
- `NT_RESOURCE_BEHAVIOR_AUX_BACKED`: published winner is deferred until `user_data` is synchronized to the winning asset. If the target winner requires aux data but its file-pack blob is currently missing, `resource_step()` schedules that pack for immediate re-download.
- `NT_RESOURCE_BEHAVIOR_PIN_BLOB`: the published winner PINs its pack blob so a zero-copy consumer can read the live bytes at any time (see [Blob pinning](#blob-pinning-phase-72-addition)). Mutually independent of `AUX_BACKED` — a copy-out consumer never needs it, a zero-copy consumer always does.

**on_resolve** fires in Phase D for the published winner when:
- published asset identity changes
- published `runtime_handle` changes (re-activation / invalidate / context loss)
- or an aux-backed asset is being published but its `user_data` has not yet been synchronized to that asset

`on_resolve` only runs when the published winner is usable now. For aux-backed assets this means the callback either already owns matching `user_data`, or the winner's blob is resident and can be parsed immediately. For simple runtime-handle asset types, `data` may still be NULL (virtual pack, placeholder-style handle substitution, or evicted blob) because publication can proceed from the runtime handle alone. The data pointer is valid only for the duration of the call — callbacks must copy if needed.

**on_cleanup** fires when a slot loses its published real winner (no publishable real candidate remains) and during shutdown for remaining non-NULL user_data. `on_resolve` requires `on_cleanup` — registering resolve without cleanup is an assert.

**on_post_resolve** fires after the resolve iteration finishes. It may call `request` / `find` / `get` style resource accessors, but must not recurse into `mount` / `unmount` / `step` / `load` / `parse`. Typical use: materialize dependent resource slots from ids that were copied in `on_resolve`.

Publication change detection uses three pieces of state: published asset identity (`resolve_asset_idx`), published `runtime_handle`, and aux synchronization (`user_data_asset_idx`). Placeholder substitution does not trigger `on_resolve` — placeholders are visual fallbacks, not real winners.

`resource_step()` may run more than one resolve pass in the same frame when `on_post_resolve` work creates new slots that need resolution. The pass count is bounded.

### Blob pinning (Phase 72 addition)

Two consumption models exist for asset types that derive state from pack bytes:

- **Copy-out** (`NT_RESOURCE_BEHAVIOR_AUX_BACKED`, e.g. atlas): `on_resolve` copies the bytes it needs into a self-contained `user_data`. Once built, `user_data` never touches the blob again, so the pack blob can be evicted freely and the consumer keeps working. Copy-out consumers do **not** pin.
- **Zero-copy** (`NT_RESOURCE_BEHAVIOR_PIN_BLOB`, e.g. font): the consumer reads the *live* pack blob on demand (glyph decode at cache-miss). Its `user_data` is only a `{blob, size}` view, so the blob must stay resident for as long as it is the published winner. Zero-copy consumers **pin** the blob.

**Pin count (`NtPackMeta.blob_pins`).** Each pack carries a `uint32_t blob_pins`, the aggregate count of published winners (across all slots) pinning that pack's blob. The **resolve pass owns the count and rebuilds it from scratch each pass**: it resets every pack's `blob_pins` to 0 at the top of the pass, then increments the winning pack once per published `PIN_BLOB` winner as it publishes them. Because the count is *derived* from the current published winners rather than transferred on winner-change, it self-heals — there is no per-slot pin identity to keep in sync, so `packs[]` index reuse (same-step unmount+remount), sequence wrap, or a winner dropping cannot corrupt it. Consumers never pin/unpin themselves. Phase-C eviction reads the previous pass's rebuilt count via a real `if (blob_pins > 0)` gate (an assert would be compiled out in shipping builds).

**Eviction vs. the pin (`NT_BLOB_AUTO`).**
- **Timer-freeze (D-06):** while `blob_pins > 0`, Phase-C eviction is skipped *and* `blob_last_access_ms` is refreshed each step. Zero-copy reads never bump last-access, so freezing the clock means a fresh full TTL grace begins only once the pin drops to 0.
- **AUTO-as-KEEP (D-07):** a pinned `NT_BLOB_AUTO` pack behaves as `NT_BLOB_KEEP`. This is not an error; it is reported once via an edge-triggered log (re-armed when `blob_pins` returns to 0), never per frame.
- **Plain assets (copy-out) recover via invalidate:** for a plain asset (texture/mesh) the GPU `runtime_handle` is self-contained after activation, so rendering continues with `blob == NULL`. Recovery after GPU context loss is game-driven: the game calls `nt_resource_invalidate(asset_type)` (contract: "game must re-create resources" on `context_restored`), which deactivates + marks assets back to `REGISTERED` (Pass 1) and, for any pack whose `AUTO`-evicted blob is now `NULL`, resets `pack_state` to re-issue the download (Pass 2) — so the next `resource_step()` re-downloads and re-activates. `AUTO` is therefore recoverable for plain assets; no source is permanently lost as long as the game invalidates on context restore. With explicit pack-level lifetime, `AUTO` for plain assets is mostly a memory optimization (unmount already bounds the blob).

**Unmount override (D-08).** Explicit `nt_resource_unmount` overrides the pin: it proceeds (developer intent wins), emits a one-shot error log if `blob_pins > 0`, and preserves the deactivate-before-free ordering. Teardown zeroes `blob_pins`; the next resolve rebuilds it from the current winners, and the unmounted pack (no longer a winner) is simply not counted — no stale pin, no double-free. The zero-copy consumer loses its provider and degrades to its fallback (a font renders tofu, then clears metrics once no provider remains). Invariant: **every blob-freeing path is reconciled with the pin — eviction respects it (skip), unmount overrides it (proceed + log).**

The per-asset pin (the published winner of a pinning slot) is exposed for diagnostics as `nt_resource_asset_info_t.blob_pins` and surfaced in the devapi `resource.list` group.

## Virtual packs

Game code can create virtual packs to register runtime-created resources (procedural textures, generated meshes) into the registry. Virtual pack assets participate in priority stacking identically to file pack assets. Unmount clears registry entries but does not destroy resources — game owns them.

```c
nt_resource_create_pack(pack_id, priority);
nt_resource_register(pack_id, resource_id, asset_type, runtime_handle);
nt_resource_unregister(pack_id, resource_id);
```

## Asset types

```c
typedef enum {
        NT_ASSET_MESH = 1,
        NT_ASSET_TEXTURE = 2,
        NT_ASSET_SHADER_CODE = 3,
        NT_ASSET_BLOB = 4,  /* generic binary data (game-defined) */
        NT_ASSET_FONT = 5,  /* font glyph data (Slug format) */
        NT_ASSET_ATLAS = 6, /* atlas region metadata (vertices + UVs + origin) */
    } nt_asset_type_t;
```

Additional types (material, audio) will be added as needed.

### NT_ASSET_FONT binary format

Builder produces font assets from TTF/OTF sources. Binary layout:

```
NtFontAssetHeader (24 bytes)
  magic:               u32   (0x544E4F46 "FONT")
  version:             u16   (5)
  glyph_count:         u16
  units_per_em:        u16
  ascent:              i16
  descent:             i16   (negative)
  line_gap:            i16
  underline_position:  i16   (post.underlinePosition, font units)
  underline_thickness: i16   (post.underlineThickness, font units)
  strikeout_position:  i16   (OS/2.yStrikeoutPosition, font units)
  strikeout_size:      i16   (OS/2.yStrikeoutSize, font units)

NtFontGlyphEntry[glyph_count] (24 bytes each, sorted by codepoint for bsearch)
  codepoint:    u32
  data_offset:  u32   (byte offset from header start)
  advance:      i16
  bbox:         i16 x4 (x0, y0, x1, y1)
  curve_count:  u16
  kern_count:   u16
  _reserved:    u8 x2

Per-glyph data (at data_offset):
  NtFontKernEntry[kern_count] (4 bytes each, sorted by right_glyph_index)
    right_glyph_index: u16
    value:             i16
  Contour data (delta-encoded int16 coordinates, line/quadratic bitmask)
```

Runtime does not parse TTF. Glyph contours are delta-encoded quadratic Bezier curves (lines promoted to degenerate quadratics). At lookup time, contours are decoded into float control points, decomposed into horizontal bands, and uploaded to GPU textures for Slug-style vector rendering. Glyphs are cached with LRU eviction — not immutable once loaded.

**v4 → v5 ADDITION (DECO-04, spec addition per AGENTS.md).** The header grew from 16 to 24 bytes with four `int16` decoration-metric fields (`underline_position`, `underline_thickness`, `strikeout_position`, `strikeout_size`) and `NT_FONT_VERSION` bumped 4 → 5. The builder reads these raw from the source font's `post` (`underlinePosition`@8, `underlineThickness`@10) and `OS/2` (`yStrikeoutSize`@26, `yStrikeoutPosition`@28) tables (big-endian, UPM-rescaled with the other metrics); when a table is absent it bakes a metric-correct heuristic (underline just below baseline, strike near mid x-height) so the runtime never sees garbage. This keeps decoration metrics in the builder — the runtime stays a parser-free safety net. The runtime version guard rejects stale v4 packs to tofu, so all font `.ntpack` assets must be rebuilt.

### NT_ASSET_ATLAS binary format

Builder produces atlas assets from a set of sprite PNGs (or raw RGBA buffers). One atlas yields **two kinds of pack entries**: a single `NT_ASSET_ATLAS` blob with region metadata, plus N `NT_ASSET_TEXTURE` page entries (named `<atlas>/tex0`, `<atlas>/tex1`, …). Runtime keeps a 1:N relationship — one metadata blob references N textures.

Binary layout (`shared/include/nt_atlas_format.h`, packed, **v7**):

```
NtAtlasHeader (28 bytes)
  magic:               u32  (0x534C5441 "ATLS")
  version:             u16  (7)
  region_count:        u16  (one entry per source sprite)
  page_count:          u16  (number of texture pages)
  _pad:                u16
  vertex_offset:       u32  (byte offset from header start)
  total_vertex_count:  u32
  index_offset:        u32  (byte offset from header start)
  total_index_count:   u32

texture_resource_ids[page_count]: u64
  Each entry is nt_hash64_str("<atlas_name>/tex<N>") matching the
  page texture's resource_id in the same pack.

NtAtlasRegion[region_count] (48 bytes each, v6+)
  name_hash:      u64   (xxh64 of region name)
  source_w:       u16   (original image width in pixels, pre-trim)
  source_h:       u16   (original image height in pixels, pre-trim)
  trim_offset_x:  i16   (pixels stripped from the left edge during alpha trim)
  trim_offset_y:  i16   (pixels stripped from the BOTTOM edge in y-up source space.
                         v5 — was top edge in v4. Builder converts at write time:
                         trim_offset_y = source_h - trim_y_png - trim_h.)
  origin_x:       f32   (pivot X, normalized over source_w — 0.5 = centre, 1.0 = right edge.
                         Values outside [0, 1] are allowed for off-frame pivots. Source-space
                         NOT trim-space — stable across animation frames with varying trim bounds.)
  origin_y:       f32   (pivot Y, normalized over source_h, in y-up source space.
                         v5 — 0.0 = bottom edge, 1.0 = top edge. Builder converts at write
                         time: origin_y = 1 - origin_y_png.)
  vertex_start:   u32   (index into vertex array — u32 in v3, was u16 in v2.
                         Regions may share a span only when their trim_offset_x/y also
                         match — v7, because the runtime bakes cached_pos per span.)
  index_start:    u32   (index into the index array — u32 in v3, was u16 in v2)
  vertex_count:   u8    (vertices for this region; ≤ max_vertices)
  page_index:     u8    (which texture page)
  transform:      u8    (D4 element VALUE 0..7, encoded flipH=bit0, flipV=bit1,
                         diagonal=bit2 — not a mask; the mask domain is
                         allowed_transforms, where mask bit i permits stored VALUE i.
                         Exporter metadata only — UVs are already baked. v7 stores
                         compose(placement, relative), so two regions sharing one
                         placement may carry different values; v6 stored the
                         placement orientation alone.)
  index_count:    u8    (triangle indices for this region; ≤ 255)
  flags:          u8    (builder-authored render hints, e.g. NT_ATLAS_REGION_FLAG_QUAD_*;
                         bit 3 reserved. The ring is stored rotated to its
                         lexicographically smallest vertex, so the same image yields
                         the same hint however it was produced.)
  _pad0:          u8    (alignment padding for uint16 slice9_lrtb)
  slice9_lrtb[4]: u16   (slice9 borders [left, right, top, bottom] in pixels;
                         all zero = no slice9. Non-zero values signal 9-cell
                         stretching at runtime — no separate flag bit needed.
                         Slice9 sprites are never alpha-trimmed and always use
                         RECT shape.)
  _reserved2[2]:  u8    (must be zero)

NtAtlasVertex[total_vertex_count] (8 bytes each, at vertex_offset)
  local_x:   i16  (corner X in trim-rect local space, 0..trim_w.
                   Polygon vertices use corner coordinates, not pixel centres.
                   Source-image pos: local_x + trim_offset_x
                   Pivot-relative:   (local_x + trim_offset_x) - origin_x * source_w)
  local_y:   i16  (corner Y in trim-rect local space, y-up — 0 = bottom of trim,
                   trim_h = top. v5 — was y-down in v4. Symmetric to local_x:
                   pivot_relative_y = (local_y + trim_offset_y) - origin_y * source_h.
                   Runtime cached_pos applies this directly with no Y-flip.)
  atlas_u:   u16  (normalized 0..65535 over atlas page width)
  atlas_v:   u16  (normalized 0..65535 over atlas page height. UV.v stays y-down because
                   atlas page texture pixel data is uploaded top-row-first; UV.v=0 maps to
                   PNG top, which is what the y-up sprite sees at its high-y vertex.)

uint16[total_index_count] (at index_offset)
  Triangle list, indices local per region (0 .. vertex_count-1).
  Builder pre-swaps each triangle's last two indices at pack time (a,b,c)→(a,c,b)
  so the in-blob winding is world-CCW after y-up vertices are read directly. This
  lets sprite materials use cull_mode = BACK without per-game opt-outs.
  Runtime offsets indices by vertex_start when building GPU buffers.
```

Runtime keeps an owned atlas snapshot in slot `user_data`, not a raw mmap view. On first publication the atlas module validates the blob, copies region metadata, vertex data, index data, and page resource ids into owned buffers, then builds an open-addressing hash table for O(1) region lookup. UVs are pre-normalized and triangles are pre-built by the builder using validated Clipper2 CDT; incomplete triangulation fails closed and the candidate is not published.

Subsequent publications merge by `name_hash` to preserve stable region indices across pack stacking:
- common regions update metadata in place and rewrite their copied vertex/index payload
- new regions append to the end
- removed regions are marked dead in place (`vertex_count = index_count = 0`) but KEEP their `name_hash` and stay in the hash table, so a later merge that re-adds the name revives the SAME index — a resolved region index is therefore stable for the atlas lifetime
- the hash table is rebuilt from all named regions (live + dead) after each merge

Page texture resource ids are copied during `on_resolve`. The actual `nt_resource_t` page handles are materialized in `on_post_resolve` and cached in the atlas snapshot, so `nt_atlas_get_page_resource()` remains O(1).

Atlas registers `NT_RESOURCE_BEHAVIOR_AUX_BACKED`. A higher-priority atlas whose blob is currently missing becomes the target winner, but it is not published until its metadata snapshot has been rebuilt. If a lower-priority usable atlas is already published, it stays active until the target blob is reloaded and resolved.

## Placeholder policy

Texture-only placeholder: if a texture slot has no publishable READY asset, `nt_resource_step()` may publish the placeholder resource's handle for rendering. Non-texture resources never publish placeholder handles and return handle 0 when not ready.

```c
// Placeholder is a regular resource (e.g. from a virtual pack or base pack)
nt_resource_set_placeholder_texture(nt_hash64_str("textures/placeholder.png"));
```

The function automatically requests a slot for the placeholder resource_id if one does not exist. Placeholder participates in the same resolve system — if the placeholder resource itself has no publishable READY winner, no substitution occurs. Publishing a placeholder handle does not make the slot READY: `nt_resource_is_ready()` remains false and `nt_resource_get_state()` continues to report the non-ready state.

## nt_hash -- Identity Hashing

`nt_hash` provides xxHash (XXH32/XXH64) hashing in 32-bit and 64-bit widths. Used for resource identity (64-bit) and attribute/pack naming (32-bit). Both builder and runtime link this module -- single source of truth for hash computation. xxHash chosen over FNV-1a for superior avalanche properties (critical for open-addressing hash maps) and higher throughput on WASM.

Type-safe wrappers prevent accidental mixing of raw integers with hash values:

```c
typedef struct {
    uint32_t value;
} nt_hash32_t;
typedef struct {
    uint64_t value;
} nt_hash64_t;
```

API:

```c
nt_hash32_t nt_hash32(const void *data, uint32_t size);
nt_hash64_t nt_hash64(const void *data, uint32_t size);

static inline nt_hash32_t nt_hash32_str(const char *s);
static inline nt_hash64_t nt_hash64_str(const char *s);
```

Debug label system for hash-to-string reverse lookup (compile-time toggle `NT_HASH_LABELS`):

```c
void nt_hash_register_label64(nt_hash64_t hash, const char *label);
const char *nt_hash64_label(nt_hash64_t hash);
```

CRC32 remains in `shared/` for pack data checksum -- different purpose (error detection vs identity hashing).
