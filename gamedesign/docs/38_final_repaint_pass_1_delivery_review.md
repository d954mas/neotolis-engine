# 38. Final repaint pass 1 delivery review

Цель: зафиксировать фактическую доставку первого repaint-прохода и отделить ее от placeholder-kit статуса.

## Delivered files

Pass 1 minimum delivered 2026-06-06:

| Runtime file | Size | Status |
| --- | ---: | --- |
| `games/turkic-jam-2026/raw/ui/ui_panel_felt_dark_96.png` | 96x96 | replaced |
| `games/turkic-jam-2026/raw/ui/ui_card_playable_96x128.png` | 96x128 | replaced |
| `games/turkic-jam-2026/raw/ui/ui_card_selected_96x128.png` | 96x128 | replaced |
| `games/turkic-jam-2026/raw/cards/card_art_saxaul_64.png` | 64x64 | replaced |
| `games/turkic-jam-2026/raw/icons/icon_stamina_32.png` | 32x32 | replaced |
| `games/turkic-jam-2026/raw/icons/icon_supplies_32.png` | 32x32 | replaced |
| `games/turkic-jam-2026/raw/tiles/tile_saxaul_01.png` | 128x128 | replaced |
| `games/turkic-jam-2026/raw/hero/hero_wayfarer_idle_s.png` | 128x128 | replaced |

Contact sheet:

```text
gamedesign/assets/concept/final_repaint_pass_1/final_repaint_pass_1_contact_sheet.png
```

Pass 1 continuation delivered 2026-06-06:

| Runtime file | Size | Status |
| --- | ---: | --- |
| `games/turkic-jam-2026/raw/ui/ui_panel_felt_light_96.png` | 96x96 | replaced |
| `games/turkic-jam-2026/raw/ui/ui_slot_equipment_64.png` | 64x64 | replaced |
| `games/turkic-jam-2026/raw/ui/ui_chip_resource_64.png` | 64x64 | replaced |
| `games/turkic-jam-2026/raw/ui/ui_tooltip_dark_64.png` | 64x64 | replaced |
| `games/turkic-jam-2026/raw/ui/ui_card_back_96x128.png` | 96x128 | replaced |
| `games/turkic-jam-2026/raw/ui/ui_button_dark_64.png` | 64x64 | replaced |
| `games/turkic-jam-2026/raw/cards/card_art_yurt_64.png` | 64x64 | replaced |
| `games/turkic-jam-2026/raw/cards/card_art_tamga_stone_64.png` | 64x64 | replaced |
| `games/turkic-jam-2026/raw/cards/card_art_wolf_track_64.png` | 64x64 | replaced |
| `games/turkic-jam-2026/raw/icons/icon_wisdom_32.png` | 32x32 | replaced |
| `games/turkic-jam-2026/raw/icons/icon_glory_32.png` | 32x32 | replaced |

Continuation contact sheet:

```text
gamedesign/assets/concept/final_repaint_pass_1/final_repaint_pass_1_continuation_contact_sheet.png
```

Source tool:

```text
gamedesign/tools/final_repaint_pass_1_minimum.py
gamedesign/tools/final_repaint_pass_1_continuation.py
```

## Technical verification

Passed:

```text
PNG size check: all expected sizes
Pixel format: Format32bppArgb

build\games\turkic-jam-2026\native-debug\build_turkic_jam_packs.exe build\games\turkic-jam-2026
Batch A: found 44 / missing 0
Batch B: found 47 / missing 0
Optional production sprites: found 91 / missing 0
Atlas: 95 sprites, one page

cmake --build build/_cmake/native-debug --target turkic_jam
passed
```

This proves:

```text
L1 raw PNG exists
L2 builder found / atlas pack accepted
```

It does not yet prove:

```text
L3 runtime bind after replacement
L4 drawn in the intended UI/gameplay states
L5 screenshot readability
```

## Art lead review

Accepted as first repaint candidate:

- UI surfaces moved away from generic placeholder and toward felt/parchment style.
- Selected card state is visually clear in the contact sheet.
- Saxaul reads as a low brushwood shrub, not an oasis or full tree.
- Hero reads as a simple wayfarer silhouette and is usable for the first playable.
- Continuation UI kit is visually consistent with the minimum pass.
- Yurt and Tamga Stone card art read at contact-sheet scale.

Needs screenshot QA before final acceptance:

- `icon_stamina_32.png` and `icon_supplies_32.png` may be too minimal at 24px HUD scale.
- `icon_wisdom_32.png` and `icon_glory_32.png` also need 24px HUD validation.
- `ui_panel_felt_dark_96.png` must be checked behind real text.
- `card_art_saxaul_64.png` must be checked inside the actual bottom hand card.
- `card_art_wolf_track_64.png` is dark/subtle and may disappear inside the actual hand card.
- `hero_wayfarer_idle_s.png` must be checked on the map, because current runtime may prefer walk-direction sprites.

## Next required evidence

Code/runtime QA should capture:

1. HUD with `icon_stamina_32` and `icon_supplies_32`.
2. HUD with `icon_wisdom_32` and `icon_glory_32`.
3. Bottom hand with playable/selected/back card surfaces and P0 card art.
4. Bottom hand specifically showing `card_art_wolf_track_64`.
5. Map with placed `tile_saxaul_01`.
6. Hero visible on map.
7. Any UI panel using `ui_panel_felt_dark_96` or `ui_panel_felt_light_96`.

Only after screenshots prove readability can this pass move from `candidate final art` to `accepted visible gameplay art`.
