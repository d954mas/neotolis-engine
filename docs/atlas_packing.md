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
│ Step 5: Compose      │  Blit pixels, write PNG debug, encode to NEOPAK
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
