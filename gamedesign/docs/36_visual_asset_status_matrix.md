# 36. Visual asset status matrix

Цель: держать весь visual scope в одном управляемом статусе: что уже лежит в runtime raw, что подключено в builder/runtime, что все еще placeholder, и что должно быть следующим production pass.

Статусные уровни берутся из `35_runtime_visual_qa_checklist.md`:

```text
L1 raw PNG
L2 builder found
L3 atlas/runtime bind
L4 drawn in gameplay/UI state
L5 screenshot QA
```

## Current raw inventory

Фактический raw inventory на 2026-06-06:

| Folder | PNG count | Notes |
| --- | ---: | --- |
| `raw/aul` | 11 | Batch A aul + Batch C aul stages |
| `raw/cards` | 15 | Batch B first card art + Batch C future card art |
| `raw/decor` | 7 | Batch A decor plus folder marker/extra file context |
| `raw/equipment` | 9 | Batch B equipment kit |
| `raw/fx` | 48 | Batch B first FX + Batch C future FX |
| `raw/ground` | 2 | Batch A sand base plus folder context |
| `raw/hero` | 10 | Batch A wayfarer + Batch C archetypes |
| `raw/icons` | 19 | Batch B HUD icons + Batch C future icons |
| `raw/road` | 13 | Batch A road and buffer |
| `raw/tiles` | 18 | Batch A P0 tiles + Batch C future tiles |
| `raw/ui` | 12 | Batch A/B UI kit plus existing UI PNGs |

Total current runtime PNG files:

```text
154 PNG files in games/turkic-jam-2026/raw
Batch A: 44 runtime files, including 10 Pass 1 repaint candidates
Batch B: 47 runtime files, including 9 Pass 1 repaint candidates
Batch C: 60 future placeholder runtime files
Other/existing UI files: 3
```

Pass 1 repaint candidates delivered 2026-06-06:

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
raw/cards/card_art_saxaul_64.png
raw/cards/card_art_yurt_64.png
raw/cards/card_art_tamga_stone_64.png
raw/cards/card_art_wolf_track_64.png
raw/icons/icon_stamina_32.png
raw/icons/icon_supplies_32.png
raw/icons/icon_wisdom_32.png
raw/icons/icon_glory_32.png
raw/tiles/tile_saxaul_01.png
raw/hero/hero_wayfarer_idle_s.png
```

These 19 files are `candidate technical art`, not `final accepted art`. Player-facing card/tile/hero art should receive generated bitmap repaint before final acceptance unless GDD explicitly accepts a technical exception after L5.

Pass 2 repaint scope opened 2026-06-06:

```text
raw/hero/hero_wayfarer_walk_s.png
raw/hero/hero_wayfarer_walk_e.png
raw/hero/hero_wayfarer_walk_n.png
raw/hero/hero_wayfarer_walk_w.png
raw/hero/hero_wayfarer_panel.png
raw/equipment/equip_slot_weapon_01.png
raw/equipment/equip_slot_clothes_01.png
raw/equipment/equip_slot_tool_01.png
raw/equipment/equip_slot_tamga_01.png
raw/equipment/equip_weapon_staff_01.png
raw/equipment/equip_clothes_cloak_01.png
raw/equipment/equip_tool_satchel_01.png
raw/equipment/equip_tamga_charm_01.png
raw/icons/icon_body_32.png
raw/icons/icon_mind_32.png
raw/icons/icon_spirit_32.png
raw/icons/icon_circle_32.png
raw/icons/icon_day_32.png
raw/icons/icon_speed_32.png
raw/icons/icon_last_tamga_32.png
raw/cards/card_badge_count_32.png
raw/cards/card_placement_roadside_32.png
raw/cards/card_placement_field_32.png
raw/cards/card_placement_special_32.png
```

These 24 files were delivered as `candidate technical art` in `40_final_repaint_pass_2_delivery_review.md`. They are not `final accepted art`; hero/equipment and icons were later covered by generated bitmap Pass 10 and Pass 11 candidates.

Pass 3 world/map repaint scope opened 2026-06-06:

```text
raw/ground/ground_sand_base_01.png
raw/decor/decor_dune_01.png
raw/decor/decor_stones_01.png
raw/decor/decor_dry_grass_01.png
raw/decor/decor_tracks_01.png
raw/decor/decor_bones_01.png
raw/decor/decor_cracks_01.png
raw/road/road_straight_ns.png
raw/road/road_straight_ew.png
raw/road/road_corner_ne.png
raw/road/road_corner_es.png
raw/road/road_corner_sw.png
raw/road/road_corner_wn.png
raw/road/road_entry_aul.png
raw/road/road_current_highlight.png
raw/road/buffer_edge_stones_01.png
raw/road/buffer_packed_sand_01.png
raw/road/buffer_stakes_01.png
raw/road/buffer_cart_marks_01.png
raw/aul/aul_ground_2x2.png
raw/aul/aul_yurt_small_01.png
raw/aul/aul_yurt_small_02.png
raw/aul/aul_fire_01.png
raw/tiles/tile_yurt_01.png
raw/tiles/tile_tamga_stone_01.png
raw/tiles/tile_wolf_track_01.png
raw/tiles/tile_oasis_01.png
raw/tiles/tile_mirage_01.png
raw/tiles/tile_storm_01.png
raw/tiles/tile_last_tamga_01.png
```

These 30 files were delivered as `candidate technical art` in `43_final_repaint_pass_3_world_map_delivery_review.md`. They are not `final accepted art`; active tile objects and player-facing landmarks should be reviewed for generated bitmap repaint after L5.

## Batch status

| Batch | Scope | Raw | Builder | Runtime | Final art | Current decision |
| --- | --- | --- | --- | --- | --- | --- |
| A | map/world/base UI | L1 yes, 44 files | L2 yes, found 44/0 | L3/L4 partial: map, road, aul, tiles, hero wired by Code report | partial: 10 Pass 1 candidates; 5 Pass 2 hero candidates; 30 Pass 3 map/world candidates; not L5 accepted | Push repaint candidates to L5 screenshots |
| B | cards/equipment/icons/first FX | L1 yes, 47 files | L2 yes, found 47/0 | L3 pass by devapi: 47/47; L4 QA draw groups reachable | Pass 8 card art, Pass 10 equipment, Pass 11 icons and Pass 12 UI surfaces are candidate generated bitmap art with QA-only L5 proof; FX still technical candidates | Finish normal gameplay screenshot/readability QA |
| C | future visual library | L1 yes, 60 files | L2 pass: aul progression 6/0, Pass 7 hero panels 3/0, Pass 6 future group 21/0 | partial: aul progression 6/6, Pass 7 hero panels 3/3, Pass 6 future 21/21 by desktop devapi; visual QA harness reaches groups | mixed: Pass 5/6 are candidate technical art; corrected Pass 7 hero panels are candidate generated bitmap art | Prove through desktop screenshot/readability QA |

## Gameplay screen status

| Screen area | Needed visual | Current asset source | Runtime status | Next owner |
| --- | --- | --- | --- | --- |
| Map ground | sand base, quiet ground | Batch A `ground_sand_base_01`; Pass 3 candidate technical art | reported wired | Code: screenshot QA |
| Empty cells | dune/stones/dry grass/tracks/bones/cracks | Batch A `decor_*`; Pass 3 candidate technical art | reported wired | Code: screenshot QA |
| Road | straight/corner/entry/current highlight | Batch A `road_*`; Pass 3 candidate technical art | reported wired | Code: screenshot QA |
| No-build buffer | packed sand/stones/stakes/cart marks | Batch A `buffer_*` in `raw/road`; Pass 3 candidate technical art | reported wired | Code: screenshot QA |
| Aul core | ground/yurts/fire | Batch A `aul_*`; Pass 3 candidate technical art | reported wired | Code: screenshot QA |
| Hero on map | wayfarer idle/walk directions | Batch A `hero_wayfarer_*`; Pass 10 generated bitmap repaint delivered for all wayfarer map sprites | QA-only L5 visible; normal post-map-migration gameplay screenshot pending | GDD: review tone/readability; Code only after map migration |
| Active tile objects | saxaul/yurt/tamga stone/beast trail/oasis/mirage/storm/last tamga | Batch A `tile_*`; Pass 9 generated bitmap repaint delivered for 8 active tile objects | QA-only L5 visible; normal post-map-migration gameplay screenshot pending | GDD: review cutout halos/detail density after map migration |
| Top HUD | resource/stat/day/circle/speed icons | Batch B and Pass 6 `icon_*`; Pass 11 generated bitmap repaint delivered for all 18 HUD/utility/future icons | QA-only L5 visible; normal gameplay HUD screenshot pending | GDD: review body/glory/tamga readability after map migration |
| Card hand | card surfaces, art, badge, placement icons | Batch A/B `ui_card_*`, `card_art_*`, `card_badge_*`, `card_placement_*`; Pass 8 generated bitmap card art and Pass 12 generated UI surfaces delivered | QA-only L5 visible after card-layout fix; normal gameplay screenshot pending | GDD: review weak cards and card surface noise; Art: targeted fixes only |
| Right hero panel | doll, equipment slots/items, stats | Batch A hero panel + Batch B equipment; Pass 10 generated bitmap repaint delivered for panel, items and slots | QA-only L5 visible; normal gameplay/right-panel screenshot pending | GDD: review if first traveler reads too heroic |
| Combat/chat log | panel/text styling, not heavy art | UI kit; Pass 12 generated dark/light panel surfaces available | procedural layout acceptable; generated surfaces QA-only L5 visible | Code: keep readable, no oversized art |
| First FX | dust, tile placed, trigger, gain, invalid | Batch B `fx_*`; Pass 4 candidate technical art | not proven drawn yet | Code: hook selected FX after delivery |
| FTUE intro | sand/fire/aul reveal | Batch C future FX; Pass 4 candidate technical art | future only | GDD/Code: feature start before registry |
| Death/memory | Last Tamga FX, near death | Batch C future FX; Pass 4 candidate technical art | future only | Future Code hook |
| Aul upgrades | stage 01-05 visuals, Tamga post | Batch C `aul_stage_*`, `aul_tamga_post_01`; Pass 5 candidate technical art delivered and registry-found 6/0 | future/QA only | Code: draw in QA harness; GDD: screenshot QA |
| Future tile/card library | well/watchtower/pack/camps/hunting trail/vision/false path/buried spring, matching card art and utility icons | Pass 6 candidate technical art delivered; Pass 8 generated bitmap card art covers 10 card files; Pass 11 generated bitmap icons cover future utility icons | L2 registry pass: 21 future files found 21/0; not active in gameplay by design; QA-only L5 visible for icons/cards | GDD: review future tile art later; keep inactive in normal gameplay |
| Hero archetype panels | Body/Mind/Spirit panel dolls for heir/archetype UI | Corrected Pass 7 generated bitmap art delivered for `hero_body_panel`, `hero_mind_panel`, `hero_spirit_panel`; icon readability fixes delivered for `icon_aul_upgrade_32`, `icon_settings_32` | L2/L3 pass: Batch C hero archetype panels found 3/0, desktop devapi `batch_c_hero_panels` 3/3; QA-only visual harness, not production heir UI | GDD/Code: desktop screenshot/readability proof |

## Immediate acceptance targets

### Target 1: Batch A visible gameplay screenshot

Required screenshot:

```text
normal gameplay with aul, road, buffer, decor cells, hero, at least one placed saxaul
```

Pass only if:

- map is mostly visual assets, not procedural rectangles;
- road and buffer are distinguishable;
- aul reads as small camp/starting home;
- hero is visible on road;
- saxaul is low/wide shrub, not oasis/tree.

### Target 2: Batch B UI screenshot

Required screenshot:

```text
HUD icons + card hand with art + selected card + right hero panel with equipment slots
```

Pass only if:

- card hand has enough room and does not collide with log;
- card art is readable at gameplay scale;
- icons work at 24x24;
- hero panel is readable and not bloated;
- fallback still works if optional regions are missing.

### Target 3: Art delivery pass

Designer must deliver:

```text
Created/updated files
Still placeholder
Needs external/final paint
Ready for programmer
Next art batch
```

No new broad fake-shot pass is needed unless GDD explicitly asks.

## What is not complete

The full user goal is not complete yet because:

- Pass 1-6 technical repaint passes still cover FX/ground/future scopes, while Pass 7/8/9/10/11/12 have generated bitmap source and QA-only L5 evidence;
- Batch A/B are not fully proven in normal gameplay after the map migration;
- Batch B runtime pass is still in progress, especially FX L4 hooks;
- Batch C is partially runtime-integrated for aul progression, future tile/card/icon library, and hero archetype panels; source-policy acceptance still depends on generated bitmap source or explicit technical exception plus L5;
- final art replacements have not been accepted by user/GDD as final production art.

## Next decisions

1. Wait for the map migration from Clay to sprites to finish.
2. Capture one normal desktop gameplay screenshot with map, HUD, card hand, chat log and right hero panel.
3. Review Pass 8-11 generated bitmap candidates in that real layout.
4. Hook and QA Pass 4 first playable FX after the main layout is stable.
5. If a repaint candidate is weak in runtime, send a targeted fix list to Art instead of repainting blindly.

Current active repaint contract:

```text
68_pass_13_world_foundation_contract.md
```
