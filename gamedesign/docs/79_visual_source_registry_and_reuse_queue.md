# 79. Visual source registry and reuse queue

Status: active GDD/Art/Code coordination registry.

Purpose: keep generated source systems visible so Art does not regenerate one-off UI/world elements and Code does not integrate unrelated visual styles.

## Production Rule

Current visual production rule:

```text
visible production art = generated bitmap / painted-source bitmap
technical masks/guides = allowed only for alignment, slicing, cleanup or QA proof
UI/world families = reuse source sheets and component systems before generating anything new
SVG/vector/script = never visible final art
UI generation = component family first, individual element never
```

This registry is a companion to:

```text
gamedesign/docs/66_visual_design_bible.md
gamedesign/docs/67_art_generation_and_reuse_protocol.md
gamedesign/docs/72_visual_design_bible_enforcement.md
```

## Approved / Working Source Systems

| Family | Source / Evidence | Runtime Use | Current Status |
| --- | --- | --- | --- |
| UI surfaces | `gamedesign/assets/concept/pass_12_generated_ui_surfaces/pass_12_ui_material_source.png` | panels, buttons, cards, chips, slots, tooltip | reuse before any new UI generation |
| UI contact | `gamedesign/assets/concept/pass_12_generated_ui_surfaces/pass_12_generated_ui_surfaces_contact_sheet.png` | validates exact runtime UI slices | candidate generated bitmap |
| HUD/icons | `gamedesign/assets/concept/pass_11_generated_hud_icons/pass_11_generated_hud_icons_contact_sheet.png` | `raw/icons/*.png` | candidate generated bitmap; normal gameplay review pending |
| Wayfarer/equipment | `gamedesign/assets/concept/pass_10_generated_wayfarer_equipment/pass_10_generated_wayfarer_equipment_contact_sheet.png` | hero panel, map hero, equipment items | candidate generated bitmap; normal gameplay review pending |
| Active tiles | `gamedesign/assets/concept/pass_9_generated_active_tiles/pass_9_active_tiles_runtime_contact_sheet.png` | placed tile objects | dense QA evidence accepted; beast trail identity needs map-context follow-up |
| Card art | `gamedesign/assets/concept/pass_8_generated_bitmap_repaint/pass_8_generated_card_art_runtime_contact_sheet.png` | card illustrations | dense QA evidence accepted; beast trail card may need targeted fix |
| World ground/decor/aul | `gamedesign/assets/concept/pass_13_generated_world_foundation/pass_13_world_foundation_source.png` plus V2 accepted subset | sand/decor/aul core | accepted runtime candidate |
| Road/buffer source | `gamedesign/assets/concept/pass_13_generated_world_foundation/pass_13_road_buffer_source_v3.png` and revised V4 slices | road material and buffer material only | integrated in runtime; L5 accepted for current playable |

## UI Reuse Contract

Do not generate new standalone buttons, panels, cards, slots or chips while Pass 12 can solve the need.

Any new UI request must start with this answer:

```text
Can Pass 12 solve this?
Which existing UI family is reused?
Which state/overlay is runtime-only?
Which source sheet produces the slice?
Why is a new generated source sheet needed, if any?
```

If the answer is "generate another button/card/panel", the request is rejected and must be decomposed.

Runtime UI should keep using:

```text
raw/ui/ui_panel_felt_dark_96.png
raw/ui/ui_panel_felt_light_96.png
raw/ui/ui_button_dark_64.png
raw/ui/ui_card_playable_96x128.png
raw/ui/ui_card_selected_96x128.png
raw/ui/ui_card_back_96x128.png
raw/ui/ui_slot_equipment_64.png
raw/ui/ui_chip_resource_64.png
raw/ui/ui_tooltip_dark_64.png
```

State differences must be runtime tint/opacity/edge treatment on the same material family:

```text
idle
hover
pressed
selected
disabled
invalid
```

Forbidden:

```text
button_blue_depth.png
button_green_depth.png
button_red_depth.png
new one-off button image for each action
new unrelated card frame for one card
new unrelated hero panel frame because a fake-shot looked good
```

## World Reuse Contract

World foundation is split into layers:

```text
ground/base decor
road path
road buffer/no-build edge
active tile objects
hero/equipment/fx
UI overlay
```

Accepted runtime candidates already integrated:

```text
raw/ground/ground_sand_base_01.png
raw/decor/decor_dune_01.png
raw/decor/decor_stones_01.png
raw/decor/decor_dry_grass_01.png
raw/decor/decor_tracks_01.png
raw/decor/decor_cracks_01.png
raw/aul/aul_ground_2x2.png
raw/aul/aul_fire_01.png
```

Road and buffer V4 runtime candidates are integrated and accepted for the current playable:

```text
raw/road/road_straight_ns.png
raw/road/road_straight_ew.png
raw/road/road_corner_ne.png
raw/road/road_corner_es.png
raw/road/road_corner_sw.png
raw/road/road_corner_wn.png
raw/road/buffer_edge_stones_01.png
raw/road/buffer_packed_sand_01.png
```

## Current Runtime Visual Gaps

| Gap | Owner | Evidence Needed |
| --- | --- | --- |
| Cyrillic mojibake | Code | fixed for current playable; `tmp/cyrillic_ui_check.png` accepted |
| Normal gameplay review for Pass 8-12 | GDD/Code | review `tmp/pass13_v4_road_buffer_runtime.png`, then request targeted screenshots if needed |
| Active tile readability | GDD/Art | screenshot-backed review of placed tile objects in normal map context |
| Card hand readability | GDD/Art/Code | empty/back treatment accepted in `tmp/normal_gameplay_visual_qa_dense_empty_card_map_tiles.png`; Beast Trail card identity remains targeted Art issue |
| Hero panel readability | GDD/Art/Code | screenshot-backed review of doll, equipment items, stats hierarchy |
| Future art library acceptance | GDD/Art | actual usage or QA scene screenshots, not raw existence only |
| FX acceptance | GDD/Code | motion/readability proof or explicit technical exception |

## Next Queue

Priority order:

```text
P0 Art: deliver Pass 14 Beast Trail proposal-only candidates.
P0 GDD: review Pass 14 source/contact/reuse before raw overwrite.
P1 Code: integrate only GDD-approved Pass 14 runtime candidates.
P2 GDD/Art: plan missing future asset families after current playable visuals stabilize.
```

Do not start a broad new UI generation pass until the normal gameplay screenshot identifies a concrete UI failure that cannot be solved by Pass 12 component reuse.
