# 42. Visual completion board

Status: active tracking board.

Purpose: keep the full visual goal visible while Art and Code work in parallel.

Evidence ladder:

```text
L1 raw PNG exists
L2 builder found
L3 atlas/runtime bind
L4 drawn in gameplay/UI state
L5 screenshot QA/readability accepted
```

`candidate technical art` means the PNG is pipeline-ready and passed local technical checks, but its source is deterministic/script/procedural. It is not production-final unless GDD explicitly accepts it as a technical UI/icon/FX exception after L5.

`candidate generated bitmap art` means the PNG comes from generated bitmap / painted source, then was cleaned, sliced, validated, packed, and still needs L5 screenshot/readability acceptance.

## Current pass status

| Pass | Scope | Art delivery | Technical verification | Runtime QA | Status |
| --- | --- | --- | --- | --- | --- |
| Pass 1 minimum | first UI/card/saxaul/stamina/supplies/hero idle | delivered, 8 files | passed: dimensions, pack, native target | pending L5 | candidate technical art; generated repaint likely for player-facing art |
| Pass 1 continuation | UI surfaces, yurt/tamga/beast card art, wisdom/glory | delivered, 11 files | passed: dimensions, pack, native target | pending L5 | candidate technical art; UI exceptions possible |
| Pass 2 | hero walk/panel, equipment, HUD/card utility icons | delivered, 24 files | passed: dimensions, pack, native target | pending L5 | candidate technical art; hero/equipment may need generated repaint |
| Pass 3 | world map: ground/decor/road/buffer/aul/remaining active tiles | delivered, 30 files | passed: dimensions, pack, native target | pending L5 | candidate technical art; active tile objects need L5/generated review |
| Pass 4 | FX: first playable feedback, FTUE/death/memory | delivered, 47 files | passed: dimensions, alpha, pack, native target | pending L5 | candidate technical art; may remain if motion/readability works |
| Pass 5 | aul upgrade stages and Tamga post | delivered, 6 files | passed: dimensions, alpha, pack registry, generated ids, native target | not started | candidate technical art; generated repaint if player-facing |
| Pass 6 | future tile/card/icon placeholder replacement | delivered, 22 files | passed: dimensions, alpha, pack registry 21/0, generated ids, native target | not started | candidate technical art; card/landmark art should use generated bitmap before final |
| Pass 7 | hero archetype panel dolls and small icon readability fixes | delivered, 5 files | passed: dimensions, alpha, pack registry 3/0, generated ids, native target | QA-only devapi/tree pass; L5 pending | candidate generated bitmap art for hero panels; icons are technical exceptions |
| Pass 8 | generated card art repaint | delivered, 10 card art files | passed: dimensions, RGBA, pack registry, generated ids, desktop L5 | QA-only L5 pass, card layout fixed by Code | candidate generated bitmap art; weak cards still need review |
| Pass 9 | generated active tile repaint | delivered, 8 active tile files | passed: dimensions, RGBA, pack registry, desktop L5 | QA-only L5 pass, post-map-migration normal map screenshot pending | candidate generated bitmap art; cutout halos/detail density remain risks |
| Pass 10 | generated wayfarer and equipment repaint | delivered, 14 files | passed: dimensions, RGBA, pack registry, fresh-pack desktop L5 | QA-only L5 pass, normal gameplay/right-panel review pending | candidate generated bitmap art; first traveler may read too heroic |
| Pass 11 | generated HUD and utility icons repaint | delivered, 18 icon files | passed: dimensions, RGBA, pack registry, desktop L5 | QA-only L5 pass, normal gameplay HUD review pending | candidate generated bitmap art; body/glory/tamga readability remain risks |

## Runtime QA status

| Area | Current evidence | Gap | Owner |
| --- | --- | --- | --- |
| Pack builder | Batch A 44/0, Batch B 47/0, Batch C aul progression 6/0, Batch C hero panels 3/0, Pass 6 future library 21/0, optional 121/0 | screenshot QA still needed | Code/GDD |
| WASM/browser | stale `wasm-debug/index.*` from 2026-06-05 19:25 | cannot use for L5 until rebuilt | Code |
| Native screenshot QA | desktop framebuffer readback exists via `--visual-qa --dump-frame <png> --exit-after-frame` | use fresh desktop capture after each visual/runtime change | Code/GDD |
| Desktop `--visual-qa` | integrated and native build passes; devapi reports scene `visual_qa`, resources ready, Batch A 44/44, Batch B 47/47, Batch C aul 6/6, Batch C hero panels 3/3, Pass 6 future 21/21; L5 PNGs exist for Pass 8/9/10/11 | final user/GDD readability acceptance and post-map-migration gameplay capture still pending | GDD, then Art fixes |
| HUD readability | Pass 11 generated icons visible in QA-only desktop L5 screenshot at HUD scale | normal gameplay HUD screenshot still needed after map migration | GDD, then Art fixes |
| Card hand readability | surfaces/art/badge/placement drawn by previous Code report | screenshot in real hand needed | Code, then Art fixes |
| Right hero panel | equipment and panel drawn by previous Code report | screenshot in real panel needed | Code, then Art fixes |
| Map/world | Batch A wired by previous Code report | Pass 3 screenshot QA | Code, then GDD review |

## Delivered candidate technical art

Pass 1 minimum:

```text
raw/ui/ui_panel_felt_dark_96.png
raw/ui/ui_card_playable_96x128.png
raw/ui/ui_card_selected_96x128.png
raw/cards/card_art_saxaul_64.png
raw/icons/icon_stamina_32.png
raw/icons/icon_supplies_32.png
raw/tiles/tile_saxaul_01.png
raw/hero/hero_wayfarer_idle_s.png
```

Pass 1 continuation:

```text
raw/ui/ui_panel_felt_light_96.png
raw/ui/ui_slot_equipment_64.png
raw/ui/ui_chip_resource_64.png
raw/ui/ui_tooltip_dark_64.png
raw/ui/ui_card_back_96x128.png
raw/ui/ui_button_dark_64.png
raw/cards/card_art_yurt_64.png
raw/cards/card_art_tamga_stone_64.png
raw/cards/card_art_wolf_track_64.png
raw/icons/icon_wisdom_32.png
raw/icons/icon_glory_32.png
```

Pass 2:

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

## Delivered candidate technical art

Pass 3:

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

## Future visual library

Batch C exists as raw future placeholders, but is not active final art yet. Candidate-final replacement is now split into specific passes. Batch C still covers:

- future aul stages; Pass 5 candidate technical PNGs are delivered and registered as Batch C/aul progression, but still need L4/L5 usage proof and generated repaint if player-facing;
- future tile families;
- FTUE/death/memory FX;
- future archetype/panel art; corrected Pass 7 delivered and registered generated bitmap Body/Mind/Spirit panel dolls;
- future icons.

Next future-art step is not another registry pass. Pass 6 future tile/card/icon registry and Pass 7 hero panel registry are complete at L2/L3; both remain inactive in normal gameplay. Map/UI screenshot fixes still take priority if Code provides real L5 evidence with concrete defects.

## Current blockers

```text
WASM output is stale.
Desktop L5 captures exist for generated card art, active tiles, wayfarer/equipment and HUD icons.
Normal gameplay screenshots after the map migration are still missing.
```

These are QA blockers, not art production blockers. Pass 5 and Pass 6 are registered, while Pass 8-11 have generated bitmap candidates and QA-only L5 proof. The next proof must be normal gameplay pixels/readability after the map migration.

## Next actions

1. Wait for the map migration from Clay to sprites to finish.
2. Code/GDD: use desktop/native QA, not stale WASM, to produce a normal gameplay screenshot.
3. GDD: review HUD/card/map/right-panel readability in that normal gameplay screenshot.
4. Art: make targeted generated-bitmap fixes only for weak assets.
5. Code: keep Pass 5/6 future groups in QA/editor/debug state only, not production progression.
