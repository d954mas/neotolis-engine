# Base Decor Ground Tiles 01 Review

Source image:

```text
gamedesign/assets/concept/base_decor_ground_tiles_01.png
```

## Summary

This sheet is accepted as the first visual reference for free buildable cells.

It supports the GDD rule:

```text
empty buildable cell is never visually empty
cell.tile_id = none still shows cell.base_decor_id
```

Not all tiles are production-ready, but the family split is correct.

## Per-Decor Review

| Decor id | Status | Notes |
| --- | --- | --- |
| `decor_dune_01` | accept as guide | Good quiet ground. Large shape, low gameplay noise. |
| `decor_stones_01` | accept as guide | Reads as passive stones, not `tamga_stone`. Final should reduce largest stone by ~20%. |
| `decor_dry_grass_01` | revise | Too close to active vegetation language. Make grass shorter/sparser so it does not compete with `saxaul`. |
| `decor_tracks_01` | accept as guide | Good old movement signal. It does not read like `wolf_track`. |
| `decor_bones_01` | revise | Too active/story-heavy. Needs small scattered bones, not a clear skeleton scene. |
| `decor_cracks_01` | accept as guide | Strong, flat and readable. Keep lower contrast than danger tiles. |

## Production Rules

Base decor must:

- keep the cell visually alive;
- remain clearly buildable/free;
- be quieter than any `tile_*` active object;
- not imply immediate gameplay risk unless an active tile is placed;
- avoid looking like a card reward or landmark.

Scale limits for `64x64` tile:

| Decor id | Max visual weight |
| --- | --- |
| `decor_dune_01` | one soft ridge, no hard landmark |
| `decor_stones_01` | 3-5 small stones, no monolith |
| `decor_dry_grass_01` | 2-4 short tufts, no bush mass |
| `decor_tracks_01` | faint old tracks, no paw prints |
| `decor_bones_01` | 2-5 small bones, no full rib cage/skull cluster |
| `decor_cracks_01` | flat cracks, no lava/danger glow |

## Baked vs Sprite Decision

For first playable, either model is acceptable:

```text
opaque baked ground tile: decor_dune_01, decor_tracks_01, decor_cracks_01
alpha decor sprite over sand: decor_stones_01_sprite, decor_dry_grass_01_sprite, decor_bones_01_sprite
```

Recommended hybrid:

- Use baked opaque variants for broad ground patterns (`dune`, `tracks`, `cracks`).
- Use small alpha decals for reusable clutter (`stones`, `dry_grass`, `bones`).

## Prompt For Next Decor Pass

```text
Use case: stylized-concept
Asset type: quiet base decor tile correction sheet
Primary request: create 6 square 64x64-style quiet buildable desert decor tiles, no text labels
Subject: decor_dune_01 low sand ridge; decor_stones_01 few small passive stones; decor_dry_grass_01 very sparse short grass tufts, not a bush; decor_tracks_01 faint old cart or foot tracks, not paw danger; decor_bones_01 only 2-5 small scattered bones, no skeleton and no skull focus; decor_cracks_01 flat dry clay cracks
Style/medium: flat readable jam game art, top-down 2D ground tiles, very low texture, quieter than active sprites
Constraints: every tile must still look free/buildable; no active tile object; no empty blank beige cells
Avoid: saxaul bush, oasis, yurt, tamga stone, wolf paw prints, full skeleton, skull focus, lush greenery, photorealism, excessive texture, labels, text, watermark
```
