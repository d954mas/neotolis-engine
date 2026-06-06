# 97. Release Visual Art Production Brief

Status: active GDD brief for Designer and Code.

Purpose: move the current playable from proof-quality visuals to release-ready runtime art without letting GDD generate or integrate production assets directly.

## Ownership

```text
GDD owns: target, priorities, constraints, acceptance and rejection.
Designer owns: generated bitmap sources, source cleanup, runtime candidates, contact sheets and reuse maps.
Code owns: raw asset copy, pack integration, runtime ids, screenshots and technical checks.
```

GDD must not treat its own quick mockups or generated scratch files as approved production art. If a GDD scratch file exists, Designer may use it only as a negative/rough reference unless a later GDD entry explicitly approves it.

## Current Release Visual Target

The release frame is the normal gameplay screen, not a visual QA board:

```text
tmp/normal_gameplay_placement_state.png
```

The layout is accepted as the current base:

```text
top HUD chips
left log
center world map
right hero/equipment panel
bottom card hand
```

The current blocking visual gap is placement feedback:

```text
valid cells still read like technical debug fill
invalid placement is log-only instead of map-local
hover/selected map feedback is not yet a reusable production art family
bottom hint copy says "near the road" while the accepted placement model is beyond the road buffer
```

## P0 Designer Task

Create a production candidate for one reusable map placement feedback family:

```text
ui_valid_cell_overlay_128.png
ui_invalid_cell_overlay_128.png
ui_hover_cell_overlay_128.png
```

These are overlays, not tile art.

Required visual behavior:

```text
transparent center
edge/corner language only
readable at actual map-cell size in 1280x720 gameplay
valid = dusty teal / quiet permission signal
invalid = warm red dust / short rejection signal
hover = warm ochre/gold dust / focus signal
no neon fill
no icons, text, arrows, warning signs or real sacred marks
no full-cell repaint that hides ground/decor/tile art
```

Designer must use the existing production rules:

```text
gamedesign/docs/66_visual_design_bible.md
gamedesign/docs/67_art_generation_and_reuse_protocol.md
gamedesign/docs/88_ui_asset_kit_contract.md
gamedesign/docs/95_selection_overlay_family_contract_draft.md
gamedesign/docs/96_ui_design_bible_component_families.md
```

Deliver under a Designer-owned concept pass folder:

```text
gamedesign/assets/concept/<designer_selection_overlay_pass>/
```

Required delivery:

```text
component inventory
source sheet
exact 128x128 RGBA runtime candidate PNGs
contact sheet over tmp/normal_gameplay_placement_state.png
reuse map
risks / known readability tradeoffs
```

Designer must not copy files into:

```text
games/turkic-jam-2026/raw/
```

Only Code does raw integration after GDD approval.

## P0 Code Task

Until GDD approves a Designer delivery:

```text
do not integrate GDD scratch images
do not copy proposal PNGs into raw/ui
do not create new UI styles or one-off placement states
keep the accepted gameplay layout
```

Immediate Code fix:

```text
replace "у дороги" / "near the road" placement copy
with "за кромкой дороги" / "в подсвеченную клетку за дорогой"
```

After GDD approval of Designer assets, Code integrates only the approved filenames and produces a normal gameplay proof with:

```text
selected card in hand
valid buildable cells visible
hovered buildable cell visible
invalid click on road/no-build visible on map
left log still readable
top HUD/right panel/bottom hand still readable
```

## Acceptance Gates

GDD accepts the pass only if all gates pass:

| Gate | Required Evidence |
| --- | --- |
| Style | Generated/painterly bitmap source, not SVG/procedural final pixels. |
| Reuse | One overlay family reused for all placement states. |
| Gameplay scale | Contact sheet and runtime screenshot prove readability at actual map-cell size. |
| Layout | Current normal gameplay layout remains intact. |
| Feedback | Invalid placement is map-local, not log-only. |
| Text | Placement copy matches road-buffer model. |
| Engine | No runtime parser, no new monolithic subsystem, no hidden hot-path allocation. |
| Pack | Approved assets appear through the normal pack pipeline. |

## Rejection Rules

Reject and return to Designer if:

```text
overlay covers the tile center
valid/hover/invalid are not distinguishable in one glance
asset looks like debug fill or flat SVG
asset introduces unrelated UI style
asset uses real sacred marks, Arabic fantasy, palace/bazaar language or generic magic runes
delivery lacks source sheet, contact sheet or reuse map
```

Reject and return to Code if:

```text
raw files are copied before GDD approval
runtime uses a log-only invalid state
copy still says "near the road" for road-buffer placement
layout changes are introduced only to fit the overlay art
```
