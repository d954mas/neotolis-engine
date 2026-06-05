# Active Tiles Transparent Sheet 02 Review

Source images:

```text
gamedesign/assets/concept/active_tiles_transparent_sheet_02_chromakey.png
gamedesign/assets/concept/active_tiles_transparent_sheet_02.png
gamedesign/assets/concept/active_tiles_transparent_sheet_02_512_preview.png
```

## Summary

This is a better transparent active-tile concept sheet than pass 01.

It is still concept art, not a final runtime atlas, but it is usable as the current production guide for:

```text
tile_yurt_01
tile_tamga_stone_01
tile_wolf_track_01
tile_storm_01
tile_last_tamga_01
```

`tile_saxaul_01`, `tile_oasis_01` and `tile_mirage_01` need another pass.

## Technical Status

```text
alpha background: yes
chroma-key source retained: yes
downscale preview: yes
```

## Per-Asset Review

| Asset | Status | Notes |
| --- | --- | --- |
| `tile_saxaul_01` | revise | Reads as dry brush, but still too tall and too visually valuable. Needs lower/wider silhouette and fewer branches. |
| `tile_oasis_01` | revise | Clear rare help tile. Good no-palm rule. But it is too close to `saxaul` in footprint/complexity; keep it rarer and more compact. |
| `tile_yurt_01` | accept as guide | Good silhouette and clan trim. Final runtime version should be slightly smaller than aul yurts. |
| `tile_tamga_stone_01` | accept as guide | Strong memory read. Teal mark works. Final version can be shorter if one-cell scale feels too tall. |
| `tile_wolf_track_01` | accept as guide | Excellent danger symbol without showing a wolf body. Keep scratches plus paw marks. |
| `tile_mirage_01` | revise | Too water-like. Needs gray-blue heat shimmer/horizon distortion, not a second oasis. |
| `tile_storm_01` | accept as guide | Reads as dust spiral. Good field hazard candidate. |
| `tile_last_tamga_01` | accept as guide | Good small memory relic. Keep lower value than `tamga_stone`. |

## Scale Corrections

| Asset | Correction |
| --- | --- |
| `tile_saxaul_01` | Reduce height by 30-45%, reduce branch count, keep 2-4 brushwood sticks. |
| `tile_oasis_01` | Keep water visible but reduce greenery height, no lush look. |
| `tile_yurt_01` | Field yurt must be smaller than `aul_yurt_small_01`; do not imply full aul center. |
| `tile_tamga_stone_01` | Keep teal mark large enough for 32px read. |
| `tile_mirage_01` | Remove blue water center; use horizontal heat-wave bands and dusty shimmer. |

## Production Guide

Use this sheet as the current visual reference, but redraw into fixed cells:

```text
object_sprites_1x.png
cell_size = 64x64
padding = 2px
trim = disabled
background = transparent
```

If extracted from this concept sheet, every sprite needs manual cleanup and scale normalization before runtime use.

## Next Prompt: Saxaul/Oasis/Mirage Fix Sheet

```text
Use case: stylized-concept
Asset type: focused transparent sprite correction sheet for active tile objects
Primary request: create 6 top-down active tile object sprite variants on a perfectly flat solid #ff00ff chroma-key background, no text labels
Scene/backdrop: perfectly flat uniform #ff00ff background only
Subject: two variants of tile_saxaul_01 as very low wide dry shrub with 2-4 brushwood sticks, 45-60 percent tile width and 25-40 percent tile height; two variants of tile_oasis_01 as compact rare shallow water patch with low desert greenery and no palm; two variants of tile_mirage_01 as dusty gray-blue horizontal heat shimmer and warped horizon, no water pool
Style/medium: flat readable jam game art, simple bold silhouettes, low detail, clean 2D sprite, atlas-friendly, less painterly
Composition/framing: 3 by 2 grid, each object visually contained in its own 64x64 cell with generous padding, consistent scale, no labels
Constraints: background must be one perfectly uniform #ff00ff chroma key; no baked sand tile background; no text; no watermark
Avoid: beige sand background, full tile cards, tall tree, lush tropical greenery, palm tree, second oasis for mirage, arabian palace, genie lamp, flying carpet, ornate bazaar, golden dome, aladdin, photorealism, 3D render, excessive texture, labels, text, watermark
```
