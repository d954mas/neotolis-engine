# 85. Dense visual QA follow-up review

Status: GDD screenshot review complete, Code final command report pending.

## Reviewed Evidence

```text
tmp/normal_gameplay_visual_qa_dense_empty_card_map_tiles.png
```

This is a desktop/native QA-harness screenshot, not a production gameplay/progression screen. It uses runtime renderer and current raw assets.

## Decision

```text
EMPTY/BACK CARD RUNTIME TREATMENT ACCEPTED FOR QA EVIDENCE
MAP-CONTEXT ACTIVE TILE EVIDENCE ACCEPTED FOR ART REVIEW
BEAST TRAIL TARGETED ART FIX CONFIRMED
NO BROAD UI OR ART PASS
```

## Accepted Improvements

### Empty / Back Card

The empty card now reads as inactive:

```text
existing ui_card_back_96x128 reused
card is darker/desaturated enough
пусто label is readable
no new UI art was created
```

This resolves the P0 empty/back readability issue for the current QA evidence.

### Active Tiles In Map Context

The map composition now includes P0 active tile objects directly in the map area:

```text
saxaul
yurt
tamga_stone
wolf_track / beast trail
mirage
storm
```

This is sufficient for art review. It is still a QA composition, not final production tile placement.

## Confirmed Art Issue

### Beast Trail / `wolf_track`

`Звериная тропа` remains the weakest P0 tile in map context.

Problem:

```text
on the map it reads too close to generic sand/decor
as a card it reads more like a soft sand pattern than a risk tile
the player may not understand why this is danger/risk/glory
```

GDD confirms a targeted Art pass is now justified.

Required target:

```text
runtime ids unchanged:
raw/tiles/tile_wolf_track_01.png
raw/cards/card_art_wolf_track_64.png

player-facing name:
Звериная тропа

visual direction:
low desert path
2-3 readable paw/track marks
darker disturbed sand
subtle danger accent
no wolf body
no sacred wolf/totem
no magic rune
no SVG/vector/script-looking art
```

## Other Notes

```text
Mirage and storm are readable enough for the current pass.
Yurt, tamga stone and saxaul remain readable.
Hero/equipment panel remains acceptable for this pass.
HUD chips remain acceptable.
Future asset panels remain QA-only and should not become production progression yet.
```

## Next Actions

1. Code: finish final report with commands/checks and confirm touched file list.
2. Art: create targeted generated-bitmap proposal for Beast Trail only.
3. GDD: review Beast Trail proposal before any raw overwrite.
4. Code: integrate only GDD-approved Beast Trail runtime candidates.
