# 33. Asset Production Batch B

Purpose: production contract for playable cards, HUD icons, hero-panel equipment, and first feedback FX.

Batch A covers the map/world and base UI surfaces. Batch B covers the gameplay UI content that sits on top of those surfaces.

Approved style reference:

```text
gamedesign/assets/concept/ui_fake_shot_h_approved_style_reference.png
gamedesign/docs/ui_design_system_v0_1.md
gamedesign/docs/31_visual_production_master_plan.md
```

## Status

Status: placeholder generation contract.

The expected generator is:

```text
gamedesign/tools/generate_batch_b_assets.py
```

Output preview:

```text
gamedesign/assets/concept/production_batch_b/batch_b_runtime_preview.png
```

Output manifest:

```text
gamedesign/assets/concept/production_batch_b/batch_b_runtime_manifest.md
```

Like Batch A, generated PNGs are not final art. They are runtime-ready placeholders. The art task is to replace them with polished versions using the same filenames, sizes, alpha rules, and target folders.

## Runtime Target Folders

```text
games/turkic-jam-2026/raw/ui/
games/turkic-jam-2026/raw/cards/
games/turkic-jam-2026/raw/equipment/
games/turkic-jam-2026/raw/icons/
games/turkic-jam-2026/raw/fx/
```

## File List

### UI Additions

| File | Target folder | Size | Background | Slice9 | Role |
| --- | --- | --- | --- | --- | --- |
| `ui_card_back_96x128.png` | `raw/ui/` | `96x128` | alpha | `18 px` | hidden/empty card |
| `ui_button_dark_64.png` | `raw/ui/` | `64x64` | alpha | `16 px` | pause/speed/settings button |

### Card Badges And Placement Icons

| File | Target folder | Size | Background | Role |
| --- | --- | --- | --- | --- |
| `card_badge_count_32.png` | `raw/cards/` | `32x32` | alpha | count/charge badge |
| `card_placement_roadside_32.png` | `raw/cards/` | `32x32` | alpha | roadside placement icon |
| `card_placement_field_32.png` | `raw/cards/` | `32x32` | alpha | field placement icon |
| `card_placement_special_32.png` | `raw/cards/` | `32x32` | alpha | special/locked placement icon |

### Card Art P0

Cards can reuse active tile sprites at runtime, but Batch B adds smaller card-read art variants for the hand.

| File | Target folder | Size | Background | Role |
| --- | --- | --- | --- | --- |
| `card_art_saxaul_64.png` | `raw/cards/` | `64x64` | alpha | card art for Саксаул |
| `card_art_yurt_64.png` | `raw/cards/` | `64x64` | alpha | card art for Юрта |
| `card_art_tamga_stone_64.png` | `raw/cards/` | `64x64` | alpha | card art for Камень Тамги |
| `card_art_wolf_track_64.png` | `raw/cards/` | `64x64` | alpha | card art for Звериная тропа |

### Equipment And Hero Panel

| File | Target folder | Size | Background | Role |
| --- | --- | --- | --- | --- |
| `equip_slot_weapon_01.png` | `raw/equipment/` | `64x64` | alpha | weapon slot silhouette |
| `equip_slot_clothes_01.png` | `raw/equipment/` | `64x64` | alpha | clothes slot silhouette |
| `equip_slot_tamga_01.png` | `raw/equipment/` | `64x64` | alpha | tamga slot silhouette |
| `equip_slot_tool_01.png` | `raw/equipment/` | `64x64` | alpha | tool slot silhouette |
| `equip_weapon_staff_01.png` | `raw/equipment/` | `64x64` | alpha | first weapon item |
| `equip_clothes_cloak_01.png` | `raw/equipment/` | `64x64` | alpha | first clothes item |
| `equip_tamga_charm_01.png` | `raw/equipment/` | `64x64` | alpha | first memory item |
| `equip_tool_satchel_01.png` | `raw/equipment/` | `64x64` | alpha | first supplies/tool item |

### HUD And Stat Icons

| File | Target folder | Size | Background | Role |
| --- | --- | --- | --- | --- |
| `icon_stamina_32.png` | `raw/icons/` | `32x32` | alpha | stamina |
| `icon_supplies_32.png` | `raw/icons/` | `32x32` | alpha | supplies |
| `icon_wisdom_32.png` | `raw/icons/` | `32x32` | alpha | wisdom |
| `icon_glory_32.png` | `raw/icons/` | `32x32` | alpha | glory |
| `icon_circle_32.png` | `raw/icons/` | `32x32` | alpha | loop/circle |
| `icon_day_32.png` | `raw/icons/` | `32x32` | alpha | day/time |
| `icon_body_32.png` | `raw/icons/` | `32x32` | alpha | body stat |
| `icon_mind_32.png` | `raw/icons/` | `32x32` | alpha | mind stat |
| `icon_spirit_32.png` | `raw/icons/` | `32x32` | alpha | spirit stat |
| `icon_last_tamga_32.png` | `raw/icons/` | `32x32` | alpha | last tamga |
| `icon_settings_32.png` | `raw/icons/` | `32x32` | alpha | settings |
| `icon_speed_32.png` | `raw/icons/` | `32x32` | alpha | speed |

### FX Frames

Use individual frame files, not runtime-parsed sheets.

| File pattern | Target folder | Size | Background | Frames | Role |
| --- | --- | --- | --- | --- | --- |
| `fx_dust_step_00.png` ... `fx_dust_step_03.png` | `raw/fx/` | `64x64` | alpha | 4 | hero movement dust |
| `fx_tile_placed_00.png` ... `fx_tile_placed_03.png` | `raw/fx/` | `64x64` | alpha | 4 | card placed |
| `fx_tile_trigger_00.png` ... `fx_tile_trigger_03.png` | `raw/fx/` | `64x64` | alpha | 4 | tile effect trigger |
| `fx_gain_popup_00.png` ... `fx_gain_popup_02.png` | `raw/fx/` | `64x64` | alpha | 3 | small gain popup |
| `fx_invalid_cell_00.png` ... `fx_invalid_cell_01.png` | `raw/fx/` | `64x64` | alpha | 2 | invalid placement |

## Acceptance Criteria

1. All files pack as individual sprites; no runtime crop parser is required.
2. Russian card titles can fit on top of these assets when composed in UI.
3. Cards read as playable cards, not inventory plaques.
4. Equipment slots read as slots; equipment items read as items.
5. Icons are recognizable at `24x24` in top HUD chips.
6. FX are subtle and do not hide the road, hero, or placed tile.
7. All gameplay/content sprites use transparent backgrounds.
8. No forbidden visual motifs: Arabic palace, genie, lamp, flying carpet, bazaar, heavy gold fantasy.

## Code Request

After placeholder PNGs exist, Code should promote Batch B ids into the optional registry as a separate category set:

```text
ui
cards
equipment
icons
fx
```

Batch B missing/found report should be separate or clearly grouped so Batch A map status remains readable.

