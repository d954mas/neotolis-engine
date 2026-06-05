# Playable Art Integration Brief

Purpose: map the current playable prototype visuals to the approved first-pass art assets.

Reference screenshot:

```text
C:/Users/ROG/YandexDisk/Скриншоты/2026-06-06_00-43-07.png
```

Current art direction status:

```text
flat_readable_jam_style = approved
layer model = approved
active tile objects = transparent sprites over ground/base decor
```

## What The Screenshot Already Gets Right

| Area | Keep |
| --- | --- |
| Layout | Top HUD, left log, right storyteller/hero panel, bottom card hand, center map. |
| Map priority | Map remains the main object and is not buried under UI. |
| Color function | Road, aul, hero/current marker and placed tiles are distinct. |
| Card hand | `saxaul` as first card reads correctly as the first interaction. |
| Loop structure | Road around aul is visible and understandable. |

## Main Art Replacement Goal

Do not redesign the screen first.

Replace placeholder geometry in this order:

```text
colored road blocks -> road_path ground tiles
empty dark build cells -> sand_base + base_decor
no-build edge -> road_buffer decals/ground tiles
colored active blocks -> transparent active tile sprites
yellow hero dot -> wayfarer sprite + current cell highlight
aul rectangle -> aul_ground_2x2 + yurt/fire/tamga sprites
```

## Current Prototype To Asset Mapping

| Current visual | Asset replacement | Notes |
| --- | --- | --- |
| tan road segments | `road_straight_ns`, `road_straight_ew`, `road_corner_*` | Keep darker than sand. Continuous path must remain readable. |
| central `Аул` rectangle | `aul_ground_2x2` plus `aul_yurt_small_01`, `aul_fire_01`, `aul_tamga_post_01` | Make it warmer and higher contrast than field. |
| dark gray empty cells | `sand_base` plus `decor_*` | No blank dark squares. Every buildable free cell gets decor. |
| green/red/blue tile blocks | `tile_saxaul_01`, `tile_yurt_01`, `tile_tamga_stone_01`, `tile_wolf_track_01`, etc. | Transparent sprites, not full baked sand tiles. |
| yellow hero marker | `hero_wayfarer_walk_*` plus `road_current_highlight` | Keep hero small and leave FX/bubble air. |
| missing road edge | `buffer_packed_sand_01`, `buffer_edge_stones_01`, `buffer_stakes_01`, `buffer_cart_marks_01` | P0: visually explain no-build zone. |

## Readability Priorities

1. `road_path` must stay readable before any decorative detail.
2. `road_buffer` must be visibly no-build but not a wall.
3. `aul_core` must be the warm visual anchor.
4. Active tile sprites must be stronger than base decor.
5. `saxaul` must be weaker than `oasis`.
6. Free buildable cells must be alive but quiet.

## First Playable Art Pass

### P0

| Task | Target |
| --- | --- |
| Replace road blocks | Use simple packed-earth road tiles. |
| Add road buffer | Broken low stones/stakes/packed sand around road. |
| Replace dark free cells | Use `base_decor_id` visuals: `dune`, `stones`, `dry_grass`, `tracks`, `bones`, `cracks`. |
| Add active tile sprites | At minimum `saxaul`, `yurt`, `tamga_stone`, `wolf_track`. |

### P1

| Task | Target |
| --- | --- |
| Replace aul block | 2x2 ground plus 2 yurts, fire, tamga post. |
| Replace hero dot | Tiny wayfarer with 4-direction walk. |
| Add current cell highlight | Warm dust ring under hero/current road cell. |
| Add placement highlight | Valid build cells glow softly; road/buffer do not. |

### P2

| Task | Target |
| --- | --- |
| Add `oasis`, `mirage`, `storm`, `last_tamga` | Use transparent sprites from active tile sheet after cleanup. |
| Add FX | `fx_dust_step`, `fx_tile_placed`, `fx_tile_trigger`, `fx_last_tamga_spawn`. |
| Add UI icons | Replace resource text-only chips when stable. |

## Asset Candidates Already Created

| Purpose | File |
| --- | --- |
| Active tile concept sheet | `gamedesign/assets/concept/active_tiles_transparent_sheet_02.png` |
| Active tile downscale preview | `gamedesign/assets/concept/active_tiles_transparent_sheet_02_512_preview.png` |
| Saxaul candidate | `gamedesign/assets/concept/tile_saxaul_01_candidate.png` |
| Road/buffer concept sheet | `gamedesign/assets/concept/road_buffer_ground_tiles_01.png` |
| Base decor concept sheet | `gamedesign/assets/concept/base_decor_ground_tiles_01.png` |

## Notes For Runtime Integration

The art layer should preserve the current prototype's gameplay clarity.

Do:

- Keep the same map layout and UI layout while swapping visuals.
- Use transparent active sprites over ground tiles.
- Keep tile cells stable and fixed-size.
- Keep hover/placement overlays separate from art sprites.

Do not:

- Bake active objects into sand tiles.
- Turn `road_buffer` into a wall/fence.
- Make base decor so strong that empty cells look occupied.
- Add ornate UI frames before gameplay readability is stable.
