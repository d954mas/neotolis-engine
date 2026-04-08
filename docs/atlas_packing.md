# Atlas Sprite Packing — Architecture & Performance

Reference implementation inspired by [JCash/atlaspacker](https://github.com/JCash/atlaspacker).

## Pipeline Overview

```
PNG files
    │
    ▼
┌──────────────────────┐
│ Step 1: Alpha trim   │  Extract alpha plane, find tight bounding box
└──────────┬───────────┘
           ▼
┌──────────────────────┐
│ Step 2: Dedup        │  Hash-based duplicate detection, pixel-level verify
└──────────┬───────────┘
           ▼
┌──────────────────────┐
│ Step 3: Geometry     │  Convex hull → simplify → inflate → per-sprite tile mask
└──────────┬───────────┘
           ▼
┌──────────────────────┐
│ Step 4: tile_pack    │  Place sprites onto atlas pages (the expensive part)
└──────────┬───────────┘
           ▼
┌──────────────────────┐
│ Step 5: Compose      │  Blit pixels, dilate extrude zone, write debug PNG
└──────────────────────┘
```

## Tile Grid (TileGrid)

All packing operates on a **tile grid** — a 2D bitset where each bit represents a
`tile_size × tile_size` pixel cell. Smaller `tile_size` = finer packing = larger grid.

```
tile_size=4, atlas 4096px → grid 1024×1024 tiles,  row = 16 uint64 words
tile_size=2, atlas 4096px → grid 2048×2048 tiles,  row = 32 uint64 words
```

A tile grid bit is 1 if that cell is occupied. Collision detection between
a sprite grid and the atlas grid uses word-level AND:

```c
uint64_t hit = atlas_row[word] & (sprite_word << bit_offset);
```

One AND checks 64 tiles at once. A full collision test for a 55×55-tile
sprite is ~55 row iterations × 1 word AND per row ≈ 55 operations.

## Polygon Mode vs Rect Mode

**Rect mode**: sprite mask = full bounding box rectangle. Simple, fast.

**Polygon mode** (default): sprite mask = convex hull of opaque pixels,
rasterized onto the tile grid via SAT triangle-rect tests. Polygons have
irregular edges → sprites can interlock → denser packing.

```
Rect mask:          Polygon mask:
██████████          ·····█████
██████████          ···████████
██████████          ·██████████
██████████          ██████████·
██████████          ████████···
```

Polygon mode makes the search harder: more candidate positions pass
initial checks but fail on detailed collision, and the tile grid has
a "swiss cheese" pattern at high fill levels.

## Pixel Format & Premultiplied Alpha

Atlas pages are encoded through the regular texture pipeline and default
to **premultiplied alpha** (`nt_atlas_opts_t.premultiplied = true`). The
`NtTextureAssetHeader.flags` byte carries `NT_TEXTURE_FLAG_PREMULTIPLIED`
so the runtime (and material blend state) can identify the format.

### Why premultiplied

NFP packing places sprites with sub-pixel clearance — gaps between sprites
are `(0, 0, 0, 0)` after `calloc`. When the GPU bilinearly filters a UV
close to a sprite edge, it blends opaque color with the gap. In straight
alpha this produces **dark fringes**: the RGB of the opaque pixel gets
averaged with (0,0,0), making edges visibly darker. In premultiplied
space, `(0,0,0,0)` is the identity element for `(ONE, ONE_MINUS_SRC_ALPHA)`
blending — bilinear interpolation stays correct across the alpha boundary
and there are no fringes.

### Pipeline position

Premultiplication happens **once**, inside the texture encoder
(`nt_builder_texture.c`), immediately before `strip_channels` (RAW path)
or `nt_basisu_encode` (BASIS path). Doing it before Basis is important:
block compression is lossy and perceptually tuned, so feeding it straight
alpha wastes bits encoding "invisible" RGB in transparent pixels and can
introduce artifacts after decode.

`blit_sprite` stays a plain copy — no math on the hot composition path.
The atlas pipeline just sets `td->opts.premultiplied = true` in
`pipeline_register` and the encoder does the rest.

### Formula

```
RGB' = (RGB * A + 127) / 255
A'   = A
```

Integer round-to-nearest. Lossless when `A == 255`, fully zeroes RGB when
`A == 0`. See `premultiply_rgba_copy` in `nt_builder_texture.c`.

### Rendering contract

Atlas pages and any `nt_tex_opts_t` texture with `premultiplied = true`
**must** be drawn with blend mode `(ONE, ONE_MINUS_SRC_ALPHA)`. Other blend
modes will not match the stored pixel layout.

### Overriding the default

Setting `nt_atlas_opts_t.premultiplied = false` is supported but logs a
warning from `end_atlas`. Valid only for:

- NEAREST-filtered atlases (no bilinear → no fringes)
- Fully opaque sprite sets where the alpha channel is unused

Mixing `premultiplied = true` with a non-RGBA8 pixel format is a hard
error — `begin_atlas` asserts. Alpha must exist for premultiplication to
mean anything.

## Edge Extrude

After each sprite is blitted into its atlas page, `extrude_edges`
duplicates the trimmed sprite rectangle's outermost rows/columns outward
by `opts.extrude` pixels. This is classic AABB edge duplication: only the
trim rect border is copied, and transparent pixels inside the trim rect
stay untouched.

When `polygon_mode=true`, `extrude` must be `0` and the builder asserts if
it is not. Tight polygon packing reserves space for the inflated hull, not
for the whole trim AABB band, so AABB extrude could write outside the
reserved area. For polygon packing, use `padding` to keep filtered samples
away from neighbors.

### Why extrude matters

When the GPU bilinearly filters a sprite edge, it blends four neighboring
texels. Without extrude, one of those texels may be transparent
`(0, 0, 0, 0)` — the sprite's edge visually fades into the gap over half
a pixel. With extrude, the "phantom" texels outside the sprite carry the
sprite's own edge color, so bilinear sampling at the exact boundary
returns the full edge color instead of a half-faded average.

Extrude also covers UV rounding: floating-point UV computation on some
GPUs can drift by a fraction of a pixel. A 2px extrude band tolerates up
to 2px of drift with no visible seam.

### AABB edge duplication

The implementation is intentionally simple and rectangle-based:

```
1. Copy the top trim row upward and the bottom trim row downward.
2. Copy the left trim column leftward and the right trim column rightward.
3. Repeat up to N = opts.extrude pixels.
```

This matches the common "extrude borders" behavior used by mainstream
atlas workflows. It is predictable and safe to disable by setting
`opts.extrude = 0`.

Tradeoff: anti-aliased corners and concave silhouette notches are not
filled unless they already touch the trim rect border. If that extra
coverage is ever needed, it should be introduced as a separate mode
instead of changing the default AABB contract.

### Spacing and safety

`margin` only affects the page border. Distance between neighboring
sprites comes from `padding`.

With `padding = 0`, two neighboring extrude bands may visually touch in
the debug page. That does not mean the page edge is wrong; it means the
atlas was packed with no requested gap between sprites. If a visible gap
is desired, raise `padding` (for example `padding = 2` with `extrude = 2`).

### Cost

AABB edge duplication is cheap: O((w + h) × extrude) per sprite. The
compose pass remains small compared to NFP packing, and atlas-level
caching still skips it entirely on cache hits.

## Per-Sprite Tile Grid

Each sprite gets its own tile grid from its inflated convex hull.
The grid has an **origin offset** (ox, oy) in tiles — the hull can
extend beyond (0,0) due to inflation.

When placing sprite at atlas position (tx, ty), the actual tile
footprint starts at (tx+ox, ty+oy).

## Packing Algorithm (tile_pack)

1. **Sort** sprites by area descending (largest first)
2. **Initial atlas size** from `sqrt(total_area) * 0.8`, clamped to `max_size`
3. For each sprite, try placement in order:
   a. Scan existing pages (limited to used extent + sprite size)
   b. Grow last page (double smaller dimension) and rescan
   c. Create new page

## scan_region — The Hot Path

The inner scan tries every (ty, tx) position in the atlas grid region,
with several acceleration layers:

```
for each rotation (0°, 90°, 180°, 270°):
  ┌─ Precompute by0_has_room[] ────────────────────────────┐
  │  For each block-row band: is there a free-run of       │
  │  block columns ≥ sprite width?  O(bh × bw × band_h)   │
  └────────────────────────────────────────────────────────┘
  for each ty:
    ┌─ Y-skip (by0_has_room) ──────────────────────────────┐
    │  If the band for this by0 has no wide enough run,    │
    │  skip ty to next block boundary. ~8 tiles at a time. │
    └──────────────────────────────────────────────────────┘
    ┌─ x4 OR-mask pre-check ───────────────────────────────┐
    │  Compute OR-mask on x4 atlas (14 rows × 8 words).   │
    │  If ALL x4 columns occupied → skip ty entirely.      │
    └──────────────────────────────────────────────────────┘
    ┌─ x1 OR-mask ─────────────────────────────────────────┐
    │  OR all atlas rows in sprite's height band into one  │
    │  mask word-array. Cost: sprite_h × row_words.        │
    │  This is the DOMINANT COST at high fill.             │
    └──────────────────────────────────────────────────────┘
    for each tx:
      ┌─ X-skip (run_from) ───────────────────────────────┐
      │  Block-column free-run check. If consecutive      │
      │  free block columns < sprite width → jump ahead.  │
      └──────────────────────────────────────────────────┘
      ┌─ OR-mask column check ────────────────────────────┐
      │  Single-bit test on OR-mask. If first sprite      │
      │  column occupied → skip to next free column.      │
      └──────────────────────────────────────────────────┘
      ┌─ tgrid_test (full collision) ─────────────────────┐
      │  Word-level AND per sprite row. Returns skip      │
      │  distance on collision (via ctz of hit pattern).  │
      └──────────────────────────────────────────────────┘
      → If no collision + bounds OK → PLACED!
```

## Acceleration Structures

### CoarseGrid (block grid, 8×8 tiles per block)

Flat 2D array: each block is EMPTY / MIXED / FULL.
Per-row `row_nonfull` count enables O(1) "is row entirely full?" check.

Used to build:
- **col_free[bx]**: is there any non-FULL block in this column across the sprite band?
- **run_from[bx]**: consecutive free columns starting at bx
- **by0_has_room[by0]**: does this block-row band have a wide enough free-run?

### x4 Downsampled TileGrid (LOD)

OR-downsample of atlas: each x4 bit = 1 if ANY tile in the 4×4 source block
is occupied. Maintained alongside the full atlas grid.

Used for cheap OR-mask: 14 rows × 8 words instead of 55 rows × 32 words.
The x4 OR-mask "all occupied" check can skip entire ty values.

## Performance Characteristics

At `tile_size=4` (grid 1024²): fast. 2236 unique sprites pack in <1s (release).

At `tile_size=2` (grid 2048²): **problematic at high fill**. The grid is 4× larger
in each dimension, making every operation ~4× more expensive. Combined with
polygon mode's irregular masks creating many MIXED blocks, acceleration
structures become less effective.

### Bottleneck Profile (ts=2, 944 unique sprites, 4096×4096 atlas)

| Phase        | Sprites | Fill | Time  | Note                       |
|-------------|---------|------|-------|----------------------------|
| 0–100       | large   | 0–50%| <1s   | Plenty of space, fast      |
| 100–300     | medium  | 50–73%| ~5s  | Acceleration helps         |
| 300+        | small   | >73% | >>60s | Swiss cheese, scans stall  |

The cliff happens when the atlas becomes a "swiss cheese" pattern:
blocks are MIXED (not FULL), so block-level skips don't fire, but
tile-level gaps are too small/scattered for the sprite to fit easily.

## Possible Improvements

1. **Full x4 scan phase**: scan at x4 resolution first (512×512), only fall
   back to x1 when x4 finds nothing. 64× fewer positions to check.

2. **Skyline / height map**: track per-column height, jump directly to
   promising ty instead of linear scan.

3. **Free interval lists per row**: intersect across sprite height to find
   candidate tx ranges. O(gaps) instead of O(width).

4. **Larger tile_size as default**: ts=4 is 100× faster than ts=2 with
   minimal visual quality loss for most use cases.
