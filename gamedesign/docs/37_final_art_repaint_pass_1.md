# 37. Final art repaint pass 1

Цель: перевести самый заметный визуал первого playable из `placeholder runtime art` в `candidate final art`, не меняя runtime filenames, sizes и alpha rules.

Этот pass не создает новые gameplay ids. Он заменяет существующие PNG поверх тех же путей.

## Scope

Pass 1 закрывает видимые зоны, которые игрок видит постоянно:

```text
UI surfaces
card hand
HUD icons
hero/wayfarer
first tile: saxaul
aul core
road/buffer readability
```

Не трогать Batch C future library в этом pass, кроме случаев, когда GDD отдельно запускает FTUE/death/aul-upgrade feature.

## Style lock

Reference:

```text
gamedesign/assets/concept/ui_fake_shot_h_approved_style_reference.png
```

Правила:

- warm felt/leather/wood, but lighter than fake-shot F;
- simple readable silhouettes;
- Turkic-nomadic hints through woven trim, felt edge, simple fictional tamga marks;
- no Aladdin/palace/genie/lamp/golden dome/bazaar fantasy;
- no real clan/sacred tamga copies;
- no dense neural texture noise;
- no fake-shot pixels imported directly as runtime sprites.

## Priority files

### P0 UI surfaces

These files are always visible or define the whole game feel:

| Runtime file | Size | Alpha | Slice9 | Acceptance |
| --- | ---: | --- | ---: | --- |
| `raw/ui/ui_panel_felt_dark_96.png` | 96x96 | alpha ok | 24 | dark felt/cloth panel, calm, readable text contrast |
| `raw/ui/ui_panel_felt_light_96.png` | 96x96 | alpha ok | 24 | light parchment/felt card surface, not beige-only |
| `raw/ui/ui_card_playable_96x128.png` | 96x128 | alpha ok | 18 | clearly playable card, not inventory plaque |
| `raw/ui/ui_card_selected_96x128.png` | 96x128 | alpha ok | 18 | selected state obvious at hand scale |
| `raw/ui/ui_slot_equipment_64.png` | 64x64 | alpha ok | 14 | readable RPG slot, not a generic button |
| `raw/ui/ui_chip_resource_64.png` | 64x64 | alpha ok | 16 | small HUD chip, icon/text can sit on it |
| `raw/ui/ui_tooltip_dark_64.png` | 64x64 | alpha ok | 16 | compact dark tooltip backing |
| `raw/ui/ui_card_back_96x128.png` | 96x128 | alpha ok | 18 | back of card, woven/tamga hint, not item plate |
| `raw/ui/ui_button_dark_64.png` | 64x64 | alpha ok | 16 | small command button surface |

### P0 card art

Card art must read at 64x64, because cards are bottom UI, not full illustrations.

| Runtime file | Size | Acceptance |
| --- | ---: | --- |
| `raw/cards/card_art_saxaul_64.png` | 64x64 | low wide shrub/brushwood, ordinary help, not tree/oasis |
| `raw/cards/card_art_yurt_64.png` | 64x64 | small yurt/camp help, not palace |
| `raw/cards/card_art_tamga_stone_64.png` | 64x64 | fictional sign stone, not copied real sacred symbol |
| `raw/cards/card_art_wolf_track_64.png` | 64x64 | beast trail/track risk, not wolf body/totem |
| `raw/cards/card_badge_count_32.png` | 32x32 | readable small count marker |
| `raw/cards/card_placement_roadside_32.png` | 32x32 | roadside placement icon |
| `raw/cards/card_placement_field_32.png` | 32x32 | field placement icon |
| `raw/cards/card_placement_special_32.png` | 32x32 | special/unknown placement icon |

### P0 HUD icons

HUD icons must read at 24x24 in game, even though source files are 32x32.

| Runtime file | Size | Acceptance |
| --- | ---: | --- |
| `raw/icons/icon_stamina_32.png` | 32x32 | body/energy readable |
| `raw/icons/icon_supplies_32.png` | 32x32 | pack/supplies readable |
| `raw/icons/icon_wisdom_32.png` | 32x32 | wisdom/memory readable |
| `raw/icons/icon_glory_32.png` | 32x32 | glory readable |
| `raw/icons/icon_body_32.png` | 32x32 | Body stat |
| `raw/icons/icon_mind_32.png` | 32x32 | Mind stat |
| `raw/icons/icon_spirit_32.png` | 32x32 | Spirit stat |
| `raw/icons/icon_circle_32.png` | 32x32 | loop/circle progress |
| `raw/icons/icon_day_32.png` | 32x32 | day/time |
| `raw/icons/icon_speed_32.png` | 32x32 | speed control |
| `raw/icons/icon_last_tamga_32.png` | 32x32 | last tamga/memory |

### P0 hero and equipment

| Runtime file | Size | Acceptance |
| --- | ---: | --- |
| `raw/hero/hero_wayfarer_idle_s.png` | 128x128 | bottom anchored, readable at map scale |
| `raw/hero/hero_wayfarer_walk_s.png` | 128x128 | same silhouette, south walk |
| `raw/hero/hero_wayfarer_walk_e.png` | 128x128 | same silhouette, east walk |
| `raw/hero/hero_wayfarer_walk_n.png` | 128x128 | same silhouette, north walk |
| `raw/hero/hero_wayfarer_walk_w.png` | 128x128 | same silhouette, west walk |
| `raw/hero/hero_wayfarer_panel.png` | 128x192 | hero doll for right panel |
| `raw/equipment/equip_slot_weapon_01.png` | 64x64 | weapon slot silhouette |
| `raw/equipment/equip_slot_clothes_01.png` | 64x64 | clothes slot silhouette |
| `raw/equipment/equip_slot_tool_01.png` | 64x64 | tool/satchel slot silhouette |
| `raw/equipment/equip_slot_tamga_01.png` | 64x64 | tamga slot silhouette |
| `raw/equipment/equip_weapon_staff_01.png` | 64x64 | starter staff |
| `raw/equipment/equip_clothes_cloak_01.png` | 64x64 | starter cloak |
| `raw/equipment/equip_tool_satchel_01.png` | 64x64 | starter satchel |
| `raw/equipment/equip_tamga_charm_01.png` | 64x64 | fictional tamga charm |

### P0 world readability

| Runtime file | Size | Acceptance |
| --- | ---: | --- |
| `raw/tiles/tile_saxaul_01.png` | 128x128 | low/wide shrub, active object only, transparent |
| `raw/aul/aul_ground_2x2.png` | 256x256 | packed earth home base, not city |
| `raw/aul/aul_yurt_small_01.png` | 128x128 | small yurt |
| `raw/aul/aul_yurt_small_02.png` | 128x128 | variation, same style |
| `raw/aul/aul_fire_01.png` | 128x128 | warm campfire, FTUE center |
| `raw/road/road_straight_ew.png` | 128x128 | road only, transparent |
| `raw/road/road_straight_ns.png` | 128x128 | road only, transparent |
| `raw/road/road_corner_ne.png` | 128x128 | road corner |
| `raw/road/road_corner_es.png` | 128x128 | road corner |
| `raw/road/road_corner_sw.png` | 128x128 | road corner |
| `raw/road/road_corner_wn.png` | 128x128 | road corner |
| `raw/road/buffer_edge_stones_01.png` | 128x128 | no-build edge, not buildable |
| `raw/road/buffer_packed_sand_01.png` | 128x128 | no-build packed edge |

## Delivery rules

Designer must report:

```text
Created/updated files:
Still placeholder:
Needs external/final paint:
Ready for programmer:
Next art batch:
```

Acceptance requires:

- exact filenames unchanged;
- exact dimensions unchanged;
- alpha rules unchanged;
- no direct fake-shot crop/import;
- preview contact sheet for changed files;
- raw PNG files replaced in `games/turkic-jam-2026/raw/...`;
- programmer pack builder returns found, not invalid/missing;
- runtime screenshot QA later confirms readability.

## Rejection criteria

Reject a repaint if:

- it looks like generic dark fantasy inventory;
- it adds Arabic palace/bazaar/genie visual language;
- it changes the gameplay meaning of a tile;
- saxaul reads as tree/oasis;
- card surface reads as equipment slot;
- icon cannot be recognized at 24x24;
- noisy texture makes text harder to read;
- filesize/detail is excessive for jam/WASM use.

## Next pass after this

After Pass 1:

1. Review runtime screenshots.
2. Fix UI/card readability first.
3. Then repaint remaining P0 tiles: yurt, tamga stone, beast trail, oasis, mirage, storm, last tamga.
4. Then start FTUE intro FX and death/Last Tamga memory FX from Batch C.
