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

**Unmount** removes asset entries (resource_id = 0) — slots are recycled for new packs. **Unload** clears runtime handle/state but preserves metadata — enables fast reload without re-parsing.

`NT_BLOB_AUTO` eviction clears only the pack blob bytes. Already-activated assets keep `state == READY` and their `runtime_handle`. Whether a slot can stay published after eviction depends on asset type:
- simple assets stay usable from the runtime handle alone
- aux-backed assets stay published only if their existing `user_data` already belongs to the published winner

## Registry state split

Layouts live in `engine/resource/nt_resource_internal.h` (not public API — do not
mirror them here). The contract is the SPLIT, not the fields:

- **`NtAssetMeta`** — one record per asset per mounted pack: identity
  (`resource_id`), where the bytes are (`pack_index`, `offset`, `size`,
  `meta_offset`), the per-type `format_version` the runtime checks, and the
  asset's own load `state`. `is_dedup` marks assets sharing one byte range
  inside a pack, so activation must not free it twice.
- **`NtResourceSlot`** — one persistent record per unique resource_id the game
  asked for. Holds what the game currently sees (`runtime_handle`, `state`,
  `generation` for stale-handle detection), the published winner's identity and
  rank (`resolve_prio`/`resolve_seq`/`resolve_asset_idx`), the previous winner
  for change detection, and the `user_data` built by `on_resolve`.
- **`NtResolveTemp`** — per-slot scratch valid only inside one resolve pass, in
  a separate array so the persistent slot stays small (resolve runs only when
  `needs_resolve` is set). It carries the TARGET winner (best READY asset even
  if its blob is evicted) alongside the publishable CANDIDATE, plus the
  callback-pending flags the pass consumes.

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
- `NT_RESOURCE_BEHAVIOR_PIN_BLOB`: the published winner PINs its pack blob so a zero-copy consumer can read the live bytes at any time (see [Blob pinning](#blob-pinning)). Mutually independent of `AUX_BACKED` — a copy-out consumer never needs it, a zero-copy consumer always does.

**on_resolve** fires in Phase D for the published winner when:
- published asset identity changes
- published `runtime_handle` changes (re-activation / invalidate / context loss)
- or an aux-backed asset is being published but its `user_data` has not yet been synchronized to that asset

`on_resolve` only runs when the published winner is usable now. For aux-backed assets this means the callback either already owns matching `user_data`, or the winner's blob is resident and can be parsed immediately. For simple runtime-handle asset types, `data` may still be NULL (virtual pack, placeholder-style handle substitution, or evicted blob) because publication can proceed from the runtime handle alone. The data pointer is valid only for the duration of the call — callbacks must copy if needed.

**on_cleanup** fires when a slot loses its published real winner (no publishable real candidate remains) and during shutdown for remaining non-NULL user_data. `on_resolve` requires `on_cleanup` — registering resolve without cleanup is an assert.

**on_post_resolve** fires after the resolve iteration finishes. It may call `request` / `find` / `get` style resource accessors, but must not recurse into `mount` / `unmount` / `step` / `load` / `parse`. Typical use: materialize dependent resource slots from ids that were copied in `on_resolve`.

Publication change detection uses three pieces of state: published asset identity (`resolve_asset_idx`), published `runtime_handle`, and aux synchronization (`user_data_asset_idx`). Placeholder substitution does not trigger `on_resolve` — placeholders are visual fallbacks, not real winners.

`resource_step()` may run more than one resolve pass in the same frame when `on_post_resolve` work creates new slots that need resolution. The pass count is bounded.

### Blob pinning

Two consumption models exist for asset types that derive state from pack bytes:

- **Copy-out** (`NT_RESOURCE_BEHAVIOR_AUX_BACKED`, e.g. atlas): `on_resolve` copies the bytes it needs into a self-contained `user_data`. Once built, `user_data` never touches the blob again, so the pack blob can be evicted freely and the consumer keeps working. Copy-out consumers do **not** pin.
- **Zero-copy** (`NT_RESOURCE_BEHAVIOR_PIN_BLOB`, e.g. font): the consumer reads the *live* pack blob on demand (glyph decode at cache-miss). Its `user_data` is only a `{blob, size}` view, so the blob must stay resident for as long as it is the published winner. Zero-copy consumers **pin** the blob.

**Pin count (`NtPackMeta.blob_pins`).** Each pack carries a `uint32_t blob_pins`, the aggregate count of published winners (across all slots) pinning that pack's blob. The **resolve pass owns the count and rebuilds it from scratch each pass**: it resets every pack's `blob_pins` to 0 at the top of the pass, then increments the winning pack once per published `PIN_BLOB` winner as it publishes them. Because the count is *derived* from the current published winners rather than transferred on winner-change, it self-heals — there is no per-slot pin identity to keep in sync, so `packs[]` index reuse (same-step unmount+remount), sequence wrap, or a winner dropping cannot corrupt it. Consumers never pin/unpin themselves. Phase-C eviction reads the previous pass's rebuilt count via a real `if (blob_pins > 0)` gate (an assert would be compiled out in shipping builds).

**Eviction vs. the pin (`NT_BLOB_AUTO`).**
- **Timer-freeze (D-06):** while `blob_pins > 0`, Phase-C eviction is skipped *and* `blob_last_access_ms` is refreshed each step. Zero-copy reads never bump last-access, so freezing the clock means a fresh full TTL grace begins only once the pin drops to 0.
- **AUTO-as-KEEP (D-07):** a pinned `NT_BLOB_AUTO` pack behaves as `NT_BLOB_KEEP`. This is not an error; it is reported once via an edge-triggered log (re-armed when `blob_pins` returns to 0), never per frame.
- **Plain assets (copy-out) recover via invalidate:** for a plain asset (texture/mesh) the GPU `runtime_handle` is self-contained after activation, so rendering continues with `blob == NULL`. Recovery after GPU context loss is game-driven: the game calls `nt_resource_invalidate(asset_type)` (contract: "game must re-create resources" on `context_restored`), which deactivates + marks assets back to `REGISTERED` (Pass 1) and, for any pack whose `AUTO`-evicted blob is now `NULL`, resets `pack_state` to re-issue the download (Pass 2) — so the next `resource_step()` re-downloads and re-activates. `AUTO` is therefore recoverable for plain assets; no source is permanently lost as long as the game invalidates on context restore. With explicit pack-level lifetime, `AUTO` for plain assets is mostly a memory optimization (unmount already bounds the blob).

**Unmount override (D-08).** Explicit `nt_resource_unmount` overrides the pin: it proceeds (developer intent wins), emits a one-shot error log if `blob_pins > 0`, and preserves the deactivate-before-free ordering. Teardown zeroes `blob_pins`; the next resolve rebuilds it from the current winners, and the unmounted pack (no longer a winner) is simply not counted — no stale pin, no double-free. The zero-copy consumer loses its provider **synchronously, before the blob is freed**: unmount walks the `PIN_BLOB` slots whose published winner resolves to this pack and runs `on_cleanup` + clears `user_data` first — otherwise a font read between the unmount and the next resolve pass would dereference freed memory. Copy-out (`AUX_BACKED`) consumers are **not** severed — their `user_data` is self-contained. The severed consumer degrades to its fallback (a font renders tofu, then clears metrics once no provider remains). Invariant: **every blob-freeing path is reconciled with the pin — eviction respects it (skip), unmount overrides it (proceed + log).**

The per-asset pin (the published winner of a pinning slot) is exposed for diagnostics as `nt_resource_asset_info_t.blob_pins` and surfaced in the devapi `resource.list` group.

## GPU context loss recovery

`nt_gfx_begin_frame()` detects a restored context and sets
`g_nt_gfx.context_restored` for that frame. Resource readiness, resolved runtime
handles, and render items computed before that call still describe the previous
GPU context. The game must discard them and skip dependent draws for the restored
frame.

When `context_restored` is true, the game does three things in the restored
frame, all of them before it submits anything:

- discards render decisions and draw lists prepared before
  `nt_gfx_begin_frame()`, and draws nothing for this frame. The draw entry points
  assert on it rather than leaving the rule to prose: clearing through
  `nt_gfx_begin_pass` stays legal, submitting geometry does not
- destroys its own `nt_program_t` handles and sets each handle variable to
  `NT_PROGRAM_INVALID`. A stale non-zero handle asserts on the next destroy --
  the one mistake single ownership cannot absorb
- calls the restore entry point of every active renderer, and destroys and
  recreates its own GPU objects, then calls `nt_resource_invalidate()` for the
  shader-code asset type and every other file-backed GPU asset type it uses

Their order among themselves does not matter, because nothing draws between
them. Destroying a program clears the borrow record of every pipeline built on
it, destroying a pipeline never consults its program, and no restore entry point
flushes. What is load-bearing is that all three precede the frame's first draw.

No step needs pool headroom over the steady state: every rebuild destroys before
it recreates, whether it is a renderer relinking inside its own restore entry
point or `nt_program_ref_update` reclaiming a dead handle before linking again.

Restore is asked of ACTIVE renderers only, and every restore entry point makes
that safe rather than assuming it: one whose module was never initialized returns
without touching anything, so a game may call all of them unconditionally instead
of tracking which modules it turned off. Without that, restoring an unused
self-owned renderer would take program and pipeline slots the game sized for
itself.

Two kinds of renderer answer that third bullet differently. One that owns its
own program -- `nt_shape_renderer`, `nt_postfx_blur`, both linking from embedded
sources -- relinks inside its restore entry point and needs nothing from the
game. One that borrows a game material's program -- `nt_mesh_renderer`,
`nt_sprite_renderer`, `nt_text_renderer` -- drops its queued commands and its
pipeline cache there and waits for the game to relink.

Materials are not part of the teardown. A material keeps the handle it was
holding; destroying the program bumps that pool slot's generation, so
`nt_gfx_program_ready(info->program)` reports false for it and every renderer
skips it. Material handles never change across a restore, so ECS components, the
UI context, and game-side structures need no re-binding.

Objects that own GPU state split the same way, and the split decides what a game
must redo. A font keeps its sources -- the `nt_font_add` list is plain resource
handles -- while its curve and band textures die; `nt_font_step` rebuilds those
itself once the asset re-activates, so re-adding a source after a restore is not
just unnecessary but asserts on the duplicate. An atlas keeps its parsed regions
and needs its page texture resolved again. The rule generalises: re-create what
lived on the GPU, keep what merely referenced it.

Programs come back over the following frames, not in the restored frame: the
shader stages re-activate from `NT_ASSET_SHADER_CODE` through the resource step's
activation budget, and the frame's `nt_resource_step()` has already run by the
time `context_restored` is seen. The game links a new program once both stages
resolve -- never relinking an existing handle, which no API supports -- and
assigns it with `nt_material_set_program`. Because assigning the same handle
changes nothing, that gate is written to run every frame and carries
no "already assigned" latch; a game whose materials sit on several programs gates
each material on its own program. A blob-resident pack (the default,
`NT_BLOB_KEEP`) re-activates on the next step; an evicted one re-downloads first,
and the same gate simply waits longer.

Both ECS `draw_list` paths skip a material whose program is not ready and warn
once until a pipeline is built again. The skip is normal runtime state, not a
caller error. The immediate-mode
`nt_sprite_renderer_set_material` / `nt_text_renderer_set_material` entry points
assert only that a program was assigned, not that it is live, so they survive the
frame the context dies on; a game still stops feeding them once its own gate goes
false. That includes the UI walk: once a widget is declared, `nt_ui` calls those
setters unconditionally, so the gate belongs around the declaration, not around
the draw.

`nt_resource_invalidate()` skips virtual packs. A game-owned GPU object published
through a virtual pack must be destroyed, recreated from game-owned source data,
and published again with `nt_resource_register()`.

The frame's `nt_resource_step()` has already run before
`nt_gfx_begin_frame()` discovers the restore. File-backed assets therefore
reactivate and republish no earlier than a later resource step. The game rebuilds
resource-dependent render state after that publication instead of reusing the
discarded list. Render targets are recreated by `nt_gfx` from retained
descriptors, but their pixel contents must be redrawn.

## Pack lifetime (mount / unmount)

Asset lifetime is **explicit pack-level mount/unmount, owned by the developer** —
there is **no refcounting by design** (declined: it hides lifetime instead of
expressing it; see api-contracts "Do not introduce refcounting"). Group assets by
lifetime: level assets go in level packs, mounted on enter and unmounted on
leave. `blob_pins` above is a *pin* (a derived residency requirement), not a
refcount — nothing counts consumers, and unmount always wins.

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

Runtime keeps an owned atlas snapshot in slot `user_data`, not a raw mmap view. On first publication the atlas module validates the blob, copies region metadata, vertex data, index data, and page resource ids into owned buffers, then builds an open-addressing hash table for O(1) region lookup. Validation is integer bounds checking only (magic, version, canonical section offsets, per-region vertex/index spans, and every region's `page_index` referencing a declared page — a region-bearing blob with no pages is rejected before publication); it runs once per blob change at activate and resolve, never per frame. UVs are pre-normalized and triangles are pre-built by the builder using validated Clipper2 CDT; incomplete triangulation fails closed and the candidate is not published.

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

Hash values are single-field structs (`nt_hash32_t` / `nt_hash64_t`), not raw
integers, so a raw int cannot be passed where an identity is expected. A
compile-time label registry (`NT_HASH_LABELS`) gives hash-to-string reverse
lookup for debugging. Signatures: `engine/hash/nt_hash.h`.

CRC32 remains in `shared/` for pack data checksum -- different purpose (error detection vs identity hashing).
