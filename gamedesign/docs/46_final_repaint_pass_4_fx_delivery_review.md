# 46. Final Repaint Pass 4 FX Delivery Review

Status: candidate final art, waiting for runtime screenshot/short visual QA.

Date: 2026-06-06.

## Scope

Pass 4 replaced all `raw/fx/*.png` runtime FX frames from `44_final_repaint_pass_4_fx_contract.md`.

This pass covers:

- Batch B first playable feedback FX;
- Batch C future FTUE/death/memory FX.

Only `games/turkic-jam-2026/raw/fx/` was changed.

## Delivered Files

Batch B first playable FX:

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

Batch C future FX:

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

Contact sheet:

```text
gamedesign/assets/concept/final_repaint_pass_4_fx/final_repaint_pass_4_fx_contact_sheet.png
```

## Technical Verification

GDD-side verification passed:

```text
py -3.12 tmp/validate_final_repaint_pass_4_fx.py
OK final repaint pass 4 FX: 47 frames

build\games\turkic-jam-2026\native-debug\build_turkic_jam_packs.exe build\games\turkic-jam-2026
Batch A found 44 / missing 0
Batch B found 47 / missing 0
Batch B FX found 17 / missing 0
Optional production sprites found 91 / missing 0
Atlas 95 sprites, one page
CRC32 0x63D12428

cmake --build build/_cmake/native-debug --target turkic_jam
passed
```

## Art Lead Review

Accepted as candidate final art for the current style direction.

Strong points:

- first playable FX are compact and do not look like generic neon magic;
- tile placement and trigger FX use the established teal/Tamga accent;
- invalid placement is clear without becoming a giant warning wall;
- fire glow supports the aul/home feeling, not a magic portal;
- Last Tamga spawn reads as fictional memory marker;
- all frames keep transparent backgrounds and visible alpha.

Known risks waiting for runtime QA:

- `fx_dust_step_*` may be too subtle on bright sand at gameplay scale;
- `fx_storm_veil_*` may cover too much map if drawn fully opaque or too long;
- `fx_near_death_*` is very red and must not fight with HUD/log readability;
- `fx_gain_popup_*` must support text popup, not replace clear reward text;
- `fx_tile_trigger_*` must be visible over active tile art without looking like a permanent object.

## Runtime QA Request

Code should include Batch B FX in the visual QA ladder:

```text
raw PNG -> builder found -> atlas/runtime bind -> drawn state -> screenshot/short visual QA
```

Priority hooks:

1. `fx_dust_step_*` on hero movement.
2. `fx_tile_placed_*` on successful tile placement.
3. `fx_tile_trigger_*` when a placed tile effect resolves.
4. `fx_gain_popup_*` when resources/cards are gained.
5. `fx_invalid_cell_*` on invalid placement.

Batch C future FX should stay inactive until matching features start:

- FTUE intro;
- fire glow;
- Last Tamga death/memory;
- storm veil;
- card reward reveal;
- near-death pulse.

Do not call this pass final accepted art until L5 screenshot/readability or short visual evidence is approved.
