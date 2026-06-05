# Active Tiles Transparent Sheet 01 Review

Source images:

```text
gamedesign/assets/concept/active_tiles_transparent_sheet_01_chromakey.png
gamedesign/assets/concept/active_tiles_transparent_sheet_01.png
```

Technical status:

```text
PNG mode: RGBA
size: 1254x1254
transparent corners: yes
transparent pixels: 1206728 / 1572516
```

## Summary

This sheet validates the confirmed layer model:

```text
ground/base decor below
active tile object sprites above
```

It is not production-ready. It is a concept reference for object identity and relative theme.

## What Works

| Asset | Keep |
| --- | --- |
| `tile_saxaul_01` | Dry brushwood language works. It reads as practical help/fuel. |
| `tile_yurt_01` | Felt, red trim and clan identity read clearly. |
| `tile_tamga_stone_01` | Teal carved mark strongly communicates memory/tamga. |
| `tile_wolf_track_01` | Paw tracks + scratch marks read as danger without showing a monster. |
| Sheet | Chroma-key to alpha workflow works for opaque sprites. |

## Problems

| Issue | Fix |
| --- | --- |
| Too painterly | Redraw flatter for `flat_readable_jam_style`. |
| Objects too large | Fit each sprite into a fixed `64x64` visual budget. This sheet reads closer to high-res concept art. |
| `saxaul` too tall | Must be `45-60%` tile width and `25-40%` tile height. Current version is still too high and thorny. |
| `yurt` too dominant | For `tile_yurt_01`, use a small field tile object. The large aul yurt belongs to `aul_core`, not a regular field tile. |
| `tamga_stone` too tall | Good for landmark, but field tile version should be shorter/wider or clearly occupy one tile. |
| Sheet size not atlas-grid exact | Next pass should target exact `512x512` sheet with 8 fixed `64x64` cells or final `128x128` source cells. |

## Scale Rules For Next Pass

| Asset | Target inside `64x64` cell |
| --- | --- |
| `tile_saxaul_01` | `29-38w x 16-26h`, bottom-centered |
| `tile_yurt_01` | `42-50w x 34-44h`, center-bottom |
| `tile_tamga_stone_01` | `28-36w x 38-48h`, center-bottom |
| `tile_wolf_track_01` | `44-54w x 28-36h`, low/flat |

## Prompt For Next Transparent Sheet

```text
Use case: stylized-concept
Asset type: exact-scale active tile object sprite sheet for a 2D roguelite loop-builder game
Primary request: create 8 fixed-cell top-down active tile object sprites on a perfectly flat solid #ff00ff chroma-key background, no text labels
Scene/backdrop: perfectly flat uniform #ff00ff background only, no shadows on the background, no gradients, no texture, no floor plane
Subject: eight separate atlas-friendly 64x64-style game object sprites in a clean 4 by 2 grid: tile_saxaul_01 low wide dry shrub 45-60 percent tile width and 25-40 percent tile height; tile_oasis_01 small rare water patch with low greenery, no palm; tile_yurt_01 small field yurt; tile_tamga_stone_01 one-tile memory stone with teal carved tamga; tile_wolf_track_01 flat paw tracks and scratch marks; tile_mirage_01 low wavy horizon shimmer; tile_storm_01 compact dust spiral; tile_last_tamga_01 small bone or stone with teal mark
Style/medium: flat readable jam game art, simple bold silhouettes, limited texture, clean 2D sprite, atlas-friendly, less painterly
Composition/framing: exact 4 by 2 grid, each sprite visually contained in its own 64x64 cell with generous padding, consistent scale, no labels
Lighting/mood: practical readable gameplay icons, Turkic nomadic desert tone
Color palette: dark brown wood/twigs, off-white felt, muted clan red, teal tamga accent, dark danger marks, dusty blue-gray storm; do not use #ff00ff in any subject
Constraints: background must be one perfectly uniform #ff00ff chroma key; keep subjects fully separated from background with crisp edges; no baked sand tile background; no text; no watermark
Avoid: beige sand background, full tile cards, tall tree, huge yurt, big palm oasis, lush tropical greenery, arabian palace, genie lamp, flying carpet, ornate bazaar, golden dome, aladdin, ornate magic UI, photorealism, 3D render, excessive texture, labels, text, watermark
```

## Production Decision

Keep as concept reference:

```text
gamedesign/assets/concept/active_tiles_transparent_sheet_01.png
```

Do not use directly as runtime atlas without a redraw/downscale pass.
