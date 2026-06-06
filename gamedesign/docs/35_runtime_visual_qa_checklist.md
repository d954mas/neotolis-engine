# 35. Runtime Visual QA Checklist

Purpose: prove that production art is not only present as raw PNG and packed into an atlas, but is actually bound, drawn in the intended gameplay/UI state, readable at game scale, and not falling back to placeholder rectangles.

This document applies to all production batches:

```text
Batch A = map/world/UI base
Batch B = cards/equipment/icons/first FX
Batch C = future library, not active until feature starts
```

## Evidence Ladder

An asset is accepted for gameplay only after this full evidence chain:

```text
raw PNG -> builder found -> atlas/runtime bind -> drawn in gameplay/UI state -> screenshot QA
```

| Level | Evidence | Proves |
| --- | --- | --- |
| 1. Raw PNG | File exists in `games/turkic-jam-2026/raw/...`; size and alpha match the batch contract | Art can be handed to the builder |
| 2. Builder found | Pack builder reports found, not missing/invalid | Art reached atlas input |
| 3. Atlas/runtime bind | Runtime bind log or code path confirms a sprite/region handle | Game can resolve the sprite |
| 4. Drawn | Sprite is used in the intended gameplay/UI state | Art is not only packed, it is visible |
| 5. Screenshot QA | Screenshot proves readability and no overlap/fallback | Player sees the intended visual |

Level 1-2 means pipeline-ready. It does not mean final art QA.

## Batch A Checks

Batch A must be visible in the normal gameplay screen:

- `ground_sand_base_01` covers the map and does not compete with gameplay objects.
- `decor_*` overlays buildable cells; there are no blank beige cells.
- `road_*` replaces procedural road visuals; straight/corner/entry topology reads clearly.
- `buffer_*` separates no-build edge from buildable field without looking like a wall.
- `aul_ground_2x2`, `aul_yurt_small_*`, `aul_fire_01` read as a starting aul/camp, not a palace.
- `tile_saxaul_01`, `tile_yurt_01`, `tile_tamga_stone_01`, `tile_wolf_track_01` draw as active tile objects over ground/decor.
- `tile_saxaul_01` is a low, wide brushwood shrub, not a tree and not an oasis.
- `hero_wayfarer_*` is visible on the road, bottom anchored, and does not overlap UI.
- `road_current_highlight` makes the current hero cell readable.

## Batch B Checks

Batch B is runtime-visible only after these states are proven:

- Top HUD chips use `icon_*` at 24x24 or near that size; icons remain distinguishable without reading text.
- Bottom hand cards use `ui_card_*`, `card_art_*`, `card_badge_count_32`.
- Selected card state is visually distinct from a normal playable card.
- Placement affordances use the placement icons or a clear fallback.
- Right hero panel uses `equip_slot_*` and first `equip_*` items.
- First FX frames are used in at least one runtime event: dust step, tile placed, tile trigger, gain popup, invalid cell.
- Missing optional Batch B assets keep fallback UI working.

## Batch C Checks

Batch C must not be promoted into the active missing report as a whole.

Batch C gets runtime QA only when its feature starts:

- aul upgrade stages;
- future card/tile families;
- FTUE intro FX;
- death/Last Tamga memory FX;
- hero archetype panels;
- future HUD icons.

Until then:

```text
raw future library = ok
active playable requirement = no
```

## Screenshot Set

Minimum screenshot evidence for a runtime visual pass:

| Screenshot | Must show |
| --- | --- |
| normal gameplay | map, aul, road, buffer, hero, HUD, log, card hand, hero panel |
| selected card | selected card, card art, valid placement slots |
| placement valid | valid cell highlight and clear cursor/target |
| placement invalid | invalid affordance; road/buffer not confused with buildable cells |
| tile trigger | active tile sprite plus event/log/FX |
| hero panel | doll, equipment slots, stats readable on the right |

Later FTUE screenshot set:

```text
dark intro -> aul reveal -> first click -> hero leaves aul -> reaches road
```

## Fail Criteria

Visual pass is rejected if:

- asset is found in atlas but not used in runtime;
- screenshot does not show the sprite, or it is too small/dark/noisy;
- UI card looks like an inventory item instead of a playable spendable card;
- icon is not readable at 24x24;
- FX hides the road, hero, tile, or card state;
- road buffer looks buildable;
- fake-shot/concept image pixels are imported directly as runtime sprites;
- final paint changes filenames/sizes without GDD contract update.

## Reporting Format

Programmer report:

```text
Batch:
Builder:
Bind:
Drawn:
Screenshots:
Fallback kept:
Missing/invalid:
Blocked:
```

Art report:

```text
Created/updated files:
Still placeholder:
Needs external/final paint:
Ready for programmer:
Next art batch:
```

## Current Expectation

- Batch A and Batch B must proceed to Level 5 for the first playable.
- Batch C remains Level 1 future library unless GDD/Code requests specific assets.
- Placeholder art may pass pipeline QA, but it does not pass final art QA.
