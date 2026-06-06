# 90. Pass 14 Beast Trail revision approval

Status: approved for Code integration.

Reviewed revision:

```text
gamedesign/assets/concept/pass_14_beast_trail/pass_14_beast_trail_contact.png
gamedesign/assets/concept/pass_14_beast_trail/pass_14_beast_trail_reuse_map.md
gamedesign/assets/concept/pass_14_beast_trail/proposed_runtime/tiles/tile_wolf_track_01.png
gamedesign/assets/concept/pass_14_beast_trail/proposed_runtime/cards/card_art_wolf_track_64.png
```

## Decision

```text
PASS 14 REVISION APPROVED FOR CODE INTEGRATION
RAW OVERWRITE ALLOWED BY CODE TASK ONLY
DESKTOP QA REQUIRED AFTER INTEGRATION
```

## Why Approved

The revision fixed the issues from `87_pass_14_beast_trail_delivery_review.md`:

```text
tile uses fuller diagonal lower-right crop
tile reads as a placed Beast Trail risk object, not just a thin smudge
card uses compact lower-left paw/scratch crop
card art fills more of 64x64 and reads better on existing Pass 12 card surface
visible pixels still come from generated bitmap source
no wolf body, monster, totem, rune, magic glow, real tamga or Arabic fantasy
```

## Code Integration Targets

Copy:

```text
gamedesign/assets/concept/pass_14_beast_trail/proposed_runtime/tiles/tile_wolf_track_01.png
-> games/turkic-jam-2026/raw/tiles/tile_wolf_track_01.png

gamedesign/assets/concept/pass_14_beast_trail/proposed_runtime/cards/card_art_wolf_track_64.png
-> games/turkic-jam-2026/raw/cards/card_art_wolf_track_64.png
```

Preserve runtime ids and filenames.

## Required Code QA

After integration, Code must provide:

```text
desktop build / affected target result
desktop visual QA screenshot with Beast Trail on map and in card hand
confirmation that tile/card draw from runtime raw/packed assets
no unrelated UI/art/layout changes
```

The screenshot must show:

```text
map-context Beast Trail tile
card hand Beast Trail art
other P0 cards still present
HUD/log/right panel unchanged
```
