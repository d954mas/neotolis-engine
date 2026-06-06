# 84. Dense visual QA screenshot review

Status: GDD screenshot review complete, technical Code report still pending.

## Reviewed Evidence

```text
tmp/normal_gameplay_visual_qa_dense.png
```

This is a desktop/native `SCENE_VISUAL_QA` evidence screenshot. It is not production gameplay/progression, but it uses the runtime renderer and current raw assets. It is valid for art/readability review of connected assets.

## Decision

```text
DENSE VISUAL QA EVIDENCE ACCEPTED FOR ART REVIEW
NOT FINAL PRODUCTION SCREEN ACCEPTANCE
NO BROAD NEW UI GENERATION
NO BROAD NEW ART PASS
TARGETED FIXES ONLY
```

The screenshot proves that current runtime can draw:

```text
map composition with road/buffer/aul/hero
active tile objects over ground/decor
HUD icon chips
5-card hand preview including selected card and empty/back state
hero panel with doll and 4 equipment items
first playable FX strips
future asset library QA groups
Russian log text in UTF-8
```

## Pass / Keep

These areas are good enough to keep for the next playable review:

```text
Pass 12 panel/card/chip/slot material direction
Pass 13 road/buffer and ground/decor/aul foundation
top HUD chip structure
hero doll and equipment kit visibility
selected Saxaul card structure
Yurt and Tamga card art readability
Russian log panel readability after UTF-8 check
```

## Issues

### P0: Card Back / Empty State Readability

The empty card back is visually noisy and the `пусто` label has low contrast. In the screenshot it competes with playable card art instead of clearly reading as inactive/empty.

Preferred fix:

```text
Code/UI: make empty state darker/desaturated and increase label contrast
Reuse existing ui_card_back_96x128
Do not generate a new standalone empty card
```

Only request Art if runtime tint/label treatment cannot solve it.

### P0: QA Labels Are Debug English

The QA scene uses English labels like:

```text
saxaul
yurt
tamga
trail
weapon
clothes
tool
```

This is acceptable for debug evidence, but it cannot be treated as final production text proof. The actual production UI should display Russian player-facing names.

Preferred fix:

```text
Code: keep debug labels if needed, but add a Russian-label QA variant or confirm production screen uses config names
No Art task
```

### P1: Beast Trail Identity

The `trail` / beast trail card and active tile read more like a generic sand patch than a risky placed tile. It needs stronger readable identity at gameplay scale.

Preferred Art target:

```text
runtime ids unchanged:
raw/tiles/tile_wolf_track_01.png
raw/cards/card_art_wolf_track_64.png

player-facing concept:
Звериная тропа

visual direction:
low desert path with 2-3 paw/track marks, darker disturbed sand, subtle danger accent
not a wolf monster, not a sacred wolf, not a magic rune
```

This is a targeted generated-bitmap fix, not a new tile family.

### P1: Active Tile Map-Context Needs Production Screenshot

The active tile objects are shown in a QA section over ground/decor, but not all six are placed inside the actual map loop composition. This is enough for art review, not enough for final map gameplay acceptance.

Preferred next Code evidence:

```text
one normal gameplay or QA-gameplay map screenshot with all P0 active tile objects placed on the actual map area
```

No new art until that screenshot shows a concrete failure.

### P2: Hero Reads Strong, But Acceptable For Current Pass

The hero panel doll and equipment are readable, but the first traveler still reads quite heroic/armed. This is not a blocker for the current visual pass.

Future direction:

```text
first archetype = Путник
less warrior, more traveler/survivor
keep equipment readable
```

Do not repaint now unless production gameplay screenshot makes the tone problem obvious.

## Explicit Non-Tasks

Do not do these now:

```text
generate a new broad UI kit
generate new standalone card frames
generate new standalone buttons/panels/slots
replace road/buffer again
repaint every active tile
turn QA-only future assets into production progression
```

## Next Actions

1. Code: finish current dense QA task report with commands, checks and final screenshot path.
2. Code: if cheap, add a production-like map screenshot with all P0 active tiles placed in the actual map area.
3. Code: improve empty card/back label readability through runtime treatment before requesting art.
4. Art: prepare only one targeted generated-bitmap fix proposal for `Звериная тропа` if GDD confirms after the map placement screenshot.
