# 57. Pass 8 generated bitmap repaint readiness

Status: prepared, not activated.

Owner: Art thread, accepted by GDD as readiness report.

## Decision

Do not overwrite runtime PNGs before L5 desktop screenshot/readback review. Pass 8 should be a targeted generated bitmap repaint based on real runtime defects, not a broad fake-shot or blind replacement pass.

## Highest priority after L5

### P0 card art

Activate generated bitmap repaint for these if the L5 screenshot shows they read as technical icons rather than appealing painted card art:

```text
raw/cards/card_art_saxaul_64.png
raw/cards/card_art_yurt_64.png
raw/cards/card_art_tamga_stone_64.png
raw/cards/card_art_wolf_track_64.png
raw/cards/card_art_oasis_64.png
raw/cards/card_art_mirage_64.png
raw/cards/card_art_storm_64.png
raw/cards/card_art_last_tamga_64.png
raw/cards/card_art_well_64.png
raw/cards/card_art_watchtower_64.png
```

### P0/P1 active tile identity

Activate generated bitmap repaint for these if the L5 screenshot shows weak identity, procedural look, poor contrast on sand, or unclear gameplay meaning:

```text
raw/tiles/tile_saxaul_01.png
raw/tiles/tile_yurt_01.png
raw/tiles/tile_tamga_stone_01.png
raw/tiles/tile_wolf_track_01.png
raw/tiles/tile_oasis_01.png
raw/tiles/tile_mirage_01.png
raw/tiles/tile_storm_01.png
raw/tiles/tile_last_tamga_01.png
raw/tiles/tile_well_01.png
raw/tiles/tile_watchtower_01.png
```

### P1 hero, equipment, aul progression

Activate generated bitmap repaint for these if they become player-facing and still look too procedural:

```text
raw/hero/hero_wayfarer_panel.png
raw/equipment/equip_weapon_staff_01.png
raw/equipment/equip_clothes_cloak_01.png
raw/equipment/equip_tool_satchel_01.png
raw/equipment/equip_tamga_charm_01.png
raw/aul/aul_stage_01_camp.png
raw/aul/aul_stage_02_settlement.png
raw/aul/aul_stage_03_village.png
raw/aul/aul_stage_04_fortified_aul.png
raw/aul/aul_stage_05_steppe_capital.png
```

## Likely technical/UI exceptions

These may remain deterministic/scripted if L5 proves readability:

```text
raw/ui/ui_panel_felt_dark_96.png
raw/ui/ui_panel_felt_light_96.png
raw/ui/ui_card_playable_96x128.png
raw/ui/ui_card_selected_96x128.png
raw/ui/ui_card_back_96x128.png
raw/ui/ui_slot_equipment_64.png
raw/ui/ui_chip_resource_64.png
raw/ui/ui_tooltip_dark_64.png
raw/ui/ui_button_dark_64.png
```

Pass 4 FX may remain technical if motion/readability works in runtime.

Tiny HUD/utility icons may remain technical if readable at 24px:

```text
raw/icons/icon_stamina_32.png
raw/icons/icon_supplies_32.png
raw/icons/icon_body_32.png
raw/icons/icon_circle_32.png
raw/icons/icon_day_32.png
raw/icons/icon_warning_32.png
```

## L5 watchlist

Pay special attention to:

```text
raw/icons/icon_settings_32.png
raw/icons/icon_mind_32.png
raw/icons/icon_speed_32.png
raw/cards/card_placement_field_32.png
raw/cards/card_placement_special_32.png
raw/tiles/tile_mirage_01.png
raw/tiles/tile_storm_01.png
raw/tiles/tile_wolf_track_01.png
raw/aul/aul_stage_05_steppe_capital.png
```

Known risk:

```text
aul_stage_05_steppe_capital turquoise center may read as water/oasis.
```

## Activation rule

Pass 8 activates only after GDD reviews a valid desktop L5 screenshot/readback artifact and writes concrete defects.

Until then:

```text
No broad runtime overwrite.
No new fake-shot.
Keep exact filenames and ids.
Prepare source sheets only if needed, clearly marked not runtime replacements.
```
