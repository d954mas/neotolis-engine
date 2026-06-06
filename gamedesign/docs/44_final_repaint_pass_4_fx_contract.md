# 44. Final Repaint Pass 4 FX Contract

Status: open for Art delivery.

Date: 2026-06-06.

## Purpose

Replace all runtime FX placeholder PNGs with candidate final art.

This pass covers both:

- first playable feedback FX from Batch B;
- future FTUE/death/memory FX from Batch C.

This is not a fake-shot pass. The output must be exact runtime PNG replacements in `games/turkic-jam-2026/raw/fx/`.

## Rules

1. Replace only the listed `raw/fx/*.png` files.
2. Do not rename files.
3. Do not add new ids.
4. Do not create a broad concept image as the deliverable.
5. Keep exact dimensions and RGBA alpha.
6. Every frame must contain visible pixels; fully transparent FX frames are invalid for the builder.
7. Create a contact sheet in `gamedesign/assets/concept/final_repaint_pass_4_fx/`.
8. Report exact changed files, dimensions, what remains placeholder, ready-for-programmer status, and screenshot QA needs.

Do not repaint in this pass:

```text
raw/aul/*
raw/cards/*
raw/decor/*
raw/equipment/*
raw/ground/*
raw/hero/*
raw/icons/*
raw/road/*
raw/tiles/*
raw/ui/*
```

## Required Files

### First Playable Feedback FX

These are Batch B FX. They should be subtle, fast and readable over the gameplay map.

```text
raw/fx/fx_dust_step_00.png       64x64 RGBA
raw/fx/fx_dust_step_01.png       64x64 RGBA
raw/fx/fx_dust_step_02.png       64x64 RGBA
raw/fx/fx_dust_step_03.png       64x64 RGBA
raw/fx/fx_tile_placed_00.png     64x64 RGBA
raw/fx/fx_tile_placed_01.png     64x64 RGBA
raw/fx/fx_tile_placed_02.png     64x64 RGBA
raw/fx/fx_tile_placed_03.png     64x64 RGBA
raw/fx/fx_tile_trigger_00.png    64x64 RGBA
raw/fx/fx_tile_trigger_01.png    64x64 RGBA
raw/fx/fx_tile_trigger_02.png    64x64 RGBA
raw/fx/fx_tile_trigger_03.png    64x64 RGBA
raw/fx/fx_gain_popup_00.png      64x64 RGBA
raw/fx/fx_gain_popup_01.png      64x64 RGBA
raw/fx/fx_gain_popup_02.png      64x64 RGBA
raw/fx/fx_invalid_cell_00.png    64x64 RGBA
raw/fx/fx_invalid_cell_01.png    64x64 RGBA
```

### FTUE / Death / Memory FX

These are Batch C future FX. They are raw future library until Code starts the matching feature, but the art can be prepared now.

```text
raw/fx/fx_intro_sand_00.png          128x128 RGBA
raw/fx/fx_intro_sand_01.png          128x128 RGBA
raw/fx/fx_intro_sand_02.png          128x128 RGBA
raw/fx/fx_intro_sand_03.png          128x128 RGBA
raw/fx/fx_intro_sand_04.png          128x128 RGBA
raw/fx/fx_intro_sand_05.png          128x128 RGBA
raw/fx/fx_fire_glow_00.png           128x128 RGBA
raw/fx/fx_fire_glow_01.png           128x128 RGBA
raw/fx/fx_fire_glow_02.png           128x128 RGBA
raw/fx/fx_fire_glow_03.png           128x128 RGBA
raw/fx/fx_last_tamga_spawn_00.png    128x128 RGBA
raw/fx/fx_last_tamga_spawn_01.png    128x128 RGBA
raw/fx/fx_last_tamga_spawn_02.png    128x128 RGBA
raw/fx/fx_last_tamga_spawn_03.png    128x128 RGBA
raw/fx/fx_last_tamga_spawn_04.png    128x128 RGBA
raw/fx/fx_last_tamga_spawn_05.png    128x128 RGBA
raw/fx/fx_storm_veil_00.png          128x128 RGBA
raw/fx/fx_storm_veil_01.png          128x128 RGBA
raw/fx/fx_storm_veil_02.png          128x128 RGBA
raw/fx/fx_storm_veil_03.png          128x128 RGBA
raw/fx/fx_storm_veil_04.png          128x128 RGBA
raw/fx/fx_storm_veil_05.png          128x128 RGBA
raw/fx/fx_card_reward_00.png         128x128 RGBA
raw/fx/fx_card_reward_01.png         128x128 RGBA
raw/fx/fx_card_reward_02.png         128x128 RGBA
raw/fx/fx_card_reward_03.png         128x128 RGBA
raw/fx/fx_near_death_00.png          128x128 RGBA
raw/fx/fx_near_death_01.png          128x128 RGBA
raw/fx/fx_near_death_02.png          128x128 RGBA
raw/fx/fx_near_death_03.png          128x128 RGBA
```

## Art Direction

FX must match the approved readable Turkic nomadic UI/map direction:

- warm sand, felt, teal/tamga accent and fire colors;
- large flat readable shapes, not particle noise;
- transparent background;
- clear frame-to-frame progression;
- subtle enough to preserve gameplay readability;
- no imported broad fake-shot crops.

Avoid:

- generic neon magic;
- heavy fantasy spell circles;
- Arabic palace/genie/lamp/bazaar/flying-carpet motifs;
- copied real sacred tamgas;
- opaque square backgrounds;
- FX that hides the hero, road, cards or active tile.

## Per-FX Intent

| FX | Intent |
| --- | --- |
| `fx_dust_step_*` | Small sand puff under/behind walking hero. Must not look like smoke cloud combat magic. |
| `fx_tile_placed_*` | Short placement confirmation: sand ring, small teal/tamga accent, quick settle. |
| `fx_tile_trigger_*` | Tile effect pulse: readable but quiet, used when Saxaul/Yurt/etc. triggers. |
| `fx_gain_popup_*` | Tiny resource sparkle/mark that supports text popup without replacing text. |
| `fx_invalid_cell_*` | Clear invalid placement feedback: restrained red/amber slash or shake mark, not a warning wall. |
| `fx_intro_sand_*` | Dark-screen sand reveal texture for the opening, gradual and atmospheric. |
| `fx_fire_glow_*` | Warm campfire glow for aul/FTUE, home feeling, not magical portal. |
| `fx_last_tamga_spawn_*` | Death/memory marker reveal, fictional tamga energy, quiet and solemn. |
| `fx_storm_veil_*` | Sand veil for road rebuild/lap transition, must not fully obscure map for long. |
| `fx_card_reward_*` | Card reward reveal, playable card feedback, not loot chest fantasy. |
| `fx_near_death_*` | Low-stamina danger pulse, readable but not horror UI. |

## Acceptance Criteria

1. All 47 PNGs exist at exact dimensions and RGBA mode.
2. Every frame has non-zero alpha pixels.
3. Batch B FX pack as optional sprites without invalid/missing report.
4. Batch C FX remain raw future library until feature starts.
5. Contact sheet clearly shows sequence progression for each FX.
6. First playable FX do not hide road, hero, active tile or card hand.
7. FTUE/death FX feel like game-saga/fairy-tale atmosphere, not generic magic.

## Code Request After Delivery

After Art delivery:

1. Rerun pack builder and report Batch B FX found/missing.
2. Keep Batch C out of the active missing report unless a matching feature starts.
3. Hook only Batch B first playable FX first:
   - dust step;
   - tile placed;
   - tile trigger;
   - gain popup;
   - invalid cell.
4. Capture screenshot or short visual evidence when each FX is drawn.
5. Do not call FX final accepted until L5 screenshot/readability evidence exists.
