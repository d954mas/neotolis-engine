# 48. Runtime visual QA harness spec

Status: request to Code.

Purpose: create a practical way to prove L4/L5 for production art without waiting for the full game loop to naturally expose every visual state.

This is a QA/debug harness, not production progression mechanics.

## Problem

Art Pass 1-5 is now mostly pipeline-ready:

```text
Pass 1: UI/card/saxaul/stamina/supplies/hero idle
Pass 2: hero walk/panel/equipment/HUD/card utility icons
Pass 3: world map/road/buffer/aul/active tiles
Pass 4: first playable FX and future FTUE/death FX
Pass 5: aul upgrade stages and Tamga post
```

Builder and registry evidence is strong, but final art acceptance is still blocked because L5 screenshots are missing. Native/devapi screenshots are acceptable if WASM remains stale.

## Required mode

Add a QA-only visual mode reachable by one of these low-risk paths:

```text
preferred: --visual-qa
acceptable: --devapi <port> + command to switch to visual QA
acceptable: temporary QA config flag, if not committed as production flow
```

The mode must be clearly reported as QA-only and must not replace production gameplay, progression, balance, or config.

## Required screens

One screen can contain all zones if readable. Tabs/pages are acceptable if easier.

### 1. Gameplay Composition

Show a real-rendered gameplay composition at current first-playable scale:

```text
ground_sand_base_01
decor_dune_01 / decor_stones_01 / decor_dry_grass_01 / decor_tracks_01 / decor_bones_01 / decor_cracks_01
road_straight_* / road_corner_* / road_entry_aul / road_current_highlight
buffer_edge_stones_01 / buffer_packed_sand_01 / buffer_stakes_01 / buffer_cart_marks_01
aul_ground_2x2 / aul_yurt_small_01 / aul_yurt_small_02 / aul_fire_01
hero_wayfarer_idle_s or current actual map hero sprite
```

Must prove:

- road buffer reads as no-build edge, not wall/fence;
- aul reads as starting camp, not palace;
- hero scale is readable;
- current road highlight is visible.

### 2. Active Tiles

Show active tile sprites over actual ground/base decor:

```text
tile_saxaul_01
tile_yurt_01
tile_tamga_stone_01
tile_wolf_track_01
tile_oasis_01
tile_mirage_01
tile_storm_01
tile_last_tamga_01
```

Must prove:

- Saxaul is small/ordinary help, not oasis/tree;
- beast trail is not too dark;
- mirage/storm read at gameplay scale;
- Last Tamga reads as fictional memory marker.

### 3. UI / Cards / Hero Panel

Show actual UI at game scale:

```text
top HUD icons at intended size
bottom card hand with selected card
card_art_saxaul_64 / card_art_yurt_64 / card_art_tamga_stone_64 / card_art_wolf_track_64
card_badge_count_32
card_placement_roadside_32 / card_placement_field_32 / card_placement_special_32
right hero panel with hero_wayfarer_panel
equip_slot_* and equip_* items
```

Must prove:

- icons read at 24px-ish scale;
- selected card is clearly selected;
- card art reads inside the real card surface;
- right panel looks like hero/equipment, not random buttons.

### 4. FX Strip

Show first playable FX as short frame strips or looping previews:

```text
fx_dust_step_00..03
fx_tile_placed_00..03
fx_tile_trigger_00..03
fx_gain_popup_00..02
fx_invalid_cell_00..01
```

Must prove:

- dust is visible on sand;
- tile placed/trigger are transient and not permanent objects;
- gain popup supports text clarity;
- invalid feedback is visible but not a giant warning wall.

Batch C future FX may be shown in a separate "future" section, but must stay labeled future.

### 5. Aul Progression

Show Pass 5 sprites side by side at map/editor scale:

```text
aul_tamga_post_01
aul_stage_01_camp
aul_stage_02_settlement
aul_stage_03_village
aul_stage_04_fortified_aul
aul_stage_05_steppe_capital
```

Must prove:

- progression reads 1 -> 5;
- stage 05 does not look like palace/golden dome fantasy;
- stage 05 central blue/turquoise shape does not read as accidental oasis/water;
- Tamga post scale is usable if clickable later.

## Report format

Code should report:

```text
L2 builder:
L3 bind:
L4 drawn in visual QA mode:
L5 screenshots:
Fallbacks:
Known risks:
Blocked:
```

Screenshots should use fresh native/devapi or fresh WASM only. Stale `wasm-debug/index.*` is not accepted.

## Acceptance

The harness itself is accepted when:

```text
cmake --build build/_cmake/native-debug --target turkic_jam passes
visual QA mode starts without changing production config
all required groups have a drawn state or an explicit missing/fallback label
screenshots are produced or the exact screenshot blocker is reported
```

Final art is still not accepted until GDD reviews the screenshots.

## GDD review checklist

When Code delivers the harness, GDD checks in this order:

1. Entry path:
   - `--visual-qa` or devapi switch exists;
   - normal `start_in_game` / heir select flow remains unchanged;
   - no production config/data shortcut was committed.

2. Build evidence:
   - `build_turkic_jam_packs` reports Batch A 44/0, Batch B 47/0, Batch C aul progression 6/0;
   - `cmake --build build/_cmake/native-debug --target turkic_jam` passes;
   - generated headers still include the Pass 5 aul ids.

3. Drawn-state evidence:
   - every required group has at least one real drawn sprite in the harness;
   - missing/fallback state is visibly labeled, not silently hidden;
   - QA labels do not cover the art being reviewed.

4. Screenshot evidence:
   - screenshot is from fresh native/devapi or fresh WASM only;
   - stale `wasm-debug/index.*` is rejected;
   - screenshot includes enough resolution to judge 24px icons and card art.

5. Art review:
   - map, road, buffer and aul readability;
   - active tile readability;
   - card hand and selected card clarity;
   - hero panel/equipment clarity;
   - FX visibility without overpowering UI;
   - aul stage progression and stage 05 turquoise/oasis risk.
