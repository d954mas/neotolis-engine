# 68. Pass 13 Generated World Foundation Contract

Status: active art contract, source-first.

Purpose: replace the remaining player-facing technical world foundation art with generated bitmap / painted-source bitmap assets while preserving existing runtime filenames and pack ids.

This pass must follow:

```text
66_visual_design_bible.md
67_art_generation_and_reuse_protocol.md
```

## Decision

Pass 13 is the next visual-production priority.

Reason:

- UI surfaces, card art, active tile objects, wayfarer/equipment and HUD icons already have generated bitmap candidate passes.
- The largest remaining visible technical-art area is the world foundation: sand ground, empty decor cells, road, road buffer and starting aul.
- Map migration may still be in progress, so this pass starts as a generated source/contact/reuse contract before runtime overwrite.

## Hard Rules

```text
no SVG-looking art
no procedural/script-looking candidate-final art
no isolated repeated generation
one generated source sheet -> reusable world material families -> exact PNG exports
preserve existing runtime filenames and dimensions
```

## Component Inventory

Component families:

| Family | Role | Runtime intent |
| --- | --- | --- |
| `ground_sand` | base world surface | quiet readable desert/steppe sand |
| `decor_overlay` | empty buildable cell life | dune, stones, dry grass, tracks, bones, cracks |
| `road_material` | hero loop path | straight, corner, entry, current highlight support |
| `road_buffer` | no-build edge | packed sand, stones, stakes, cart marks |
| `aul_core` | start/home | packed aul ground, small yurts, fire |

Existing generated sources to reuse:

```text
pass_12_ui_material_source.png -> UI only, not world ground
pass_9 active tile objects -> placed active tiles only
pass_8 card art -> card art only
```

New generated source needed:

```text
yes: one world foundation material/source sheet
```

## Runtime Files Touched Later

Do not touch these until the generated source sheet, contact sheet and reuse map are reviewed.

```text
games/turkic-jam-2026/raw/ground/ground_sand_base_01.png
games/turkic-jam-2026/raw/decor/decor_dune_01.png
games/turkic-jam-2026/raw/decor/decor_stones_01.png
games/turkic-jam-2026/raw/decor/decor_dry_grass_01.png
games/turkic-jam-2026/raw/decor/decor_tracks_01.png
games/turkic-jam-2026/raw/decor/decor_bones_01.png
games/turkic-jam-2026/raw/decor/decor_cracks_01.png
games/turkic-jam-2026/raw/road/road_straight_ns.png
games/turkic-jam-2026/raw/road/road_straight_ew.png
games/turkic-jam-2026/raw/road/road_corner_ne.png
games/turkic-jam-2026/raw/road/road_corner_es.png
games/turkic-jam-2026/raw/road/road_corner_sw.png
games/turkic-jam-2026/raw/road/road_corner_wn.png
games/turkic-jam-2026/raw/road/road_entry_aul.png
games/turkic-jam-2026/raw/road/road_current_highlight.png
games/turkic-jam-2026/raw/road/buffer_edge_stones_01.png
games/turkic-jam-2026/raw/road/buffer_packed_sand_01.png
games/turkic-jam-2026/raw/road/buffer_stakes_01.png
games/turkic-jam-2026/raw/road/buffer_cart_marks_01.png
games/turkic-jam-2026/raw/aul/aul_ground_2x2.png
games/turkic-jam-2026/raw/aul/aul_yurt_small_01.png
games/turkic-jam-2026/raw/aul/aul_yurt_small_02.png
games/turkic-jam-2026/raw/aul/aul_fire_01.png
```

## Source Sheet Brief

Target folder:

```text
gamedesign/assets/concept/pass_13_generated_world_foundation/
```

Expected files:

```text
pass_13_world_foundation_source.png
pass_13_world_foundation_contact_sheet.png
pass_13_world_foundation_reuse_map.md
```

The source sheet should include reusable world materials, not baked full gameplay screenshots:

- sand base patches;
- packed road strips and turn material;
- road-buffer/no-build edge samples;
- quiet empty-cell decor overlays;
- aul ground, small yurt fragments, fire source;
- enough margin and separation for slicing.

## Art Direction

Readable top-down / slight 3-quarter 2D loop-builder map.

The world should feel like warm steppe/desert, not blank beige:

- sand has subtle value shifts and wind marks;
- road is packed earth and tracks, visibly walkable;
- road buffer is readable as "you cannot build here" without becoming a wall;
- empty buildable cells have quiet decor but leave room for active tiles;
- aul begins as a small practical camp, not a palace or city.

Avoid:

```text
Aladdin, palace, bazaar, genie, flying carpet, golden dome
generic fantasy map ornaments
real sacred tamgas
heavy noise that hides gameplay cells
decor that looks like active tile objects
```

## Generation Prompt Baseline

```text
Use case: stylized-concept
Asset type: generated bitmap source sheet for a 2D loop-builder world map
Primary request: Create one coherent reusable world foundation source sheet for a warm Turkic-nomadic steppe/desert roguelite map. The sheet will be sliced into ground, decor overlays, road, road buffer and small aul camp assets.
Scene/backdrop: clean neutral background, separated asset patches with generous padding, no gameplay UI, no text labels.
Subject: sand base patches, quiet wind marks, small dunes, stones, dry grass, tracks, cracks, bones, packed loop road strips, road corner material, no-build road buffer edge with stones/stakes/cart marks, packed aul ground, two small yurt pieces, small campfire glow source.
Style/medium: polished generated bitmap / hand-painted 2D game asset sheet, readable at 64x64 and 128x128, warm sand/ochre/clay palette, practical nomadic world materials, not SVG, not vector, not procedural rectangles.
Composition/framing: source sheet, asset families separated in rows, each patch clearly sliceable, top-down with slight 3-quarter object readability.
Constraints: no text, no UI, no complete screenshot, no cast shadows outside asset patches, no copied real sacred marks, no ornate palace fantasy.
Avoid: Aladdin, bazaar, genie, flying carpet, golden dome, palace, photorealism, noisy texture, magical runes, real tribal tamgas, icons, inventory items, unrelated buttons.
```

## Acceptance Gate

Before runtime overwrite:

1. Art provides component inventory and reuse map.
2. GDD reviews the source sheet for style, readability and cultural guardrails.
3. Sliced candidates preserve exact runtime filenames and dimensions.
4. Contact sheet compares current technical art vs generated Pass 13 candidates.
5. Pack/L5 desktop proof is captured after overwrite.

## Risks

| Risk | Mitigation |
| --- | --- |
| Ground becomes too noisy | Keep empty cells quiet; active tiles must remain the focus. |
| Buffer looks like wall | Use stones/stakes/packed edge, not fortification. |
| Aul looks too advanced | Start as small camp/standing place: yurts, fire, packed earth. |
| Decor reads as active tile | Decor overlays must stay low contrast and non-interactive. |
| Runtime overwrite conflicts with map migration | Keep first delivery source/contact only until map migration status is clear. |
