# 87. Pass 14 Beast Trail delivery review

Status: revision requested.

Reviewed delivery:

```text
gamedesign/assets/concept/pass_14_beast_trail/pass_14_beast_trail_source.png
gamedesign/assets/concept/pass_14_beast_trail/pass_14_beast_trail_contact.png
gamedesign/assets/concept/pass_14_beast_trail/pass_14_beast_trail_reuse_map.md
gamedesign/assets/concept/pass_14_beast_trail/proposed_runtime/tiles/tile_wolf_track_01.png
gamedesign/assets/concept/pass_14_beast_trail/proposed_runtime/cards/card_art_wolf_track_64.png
```

## Technical Check

Accepted:

```text
source exists
contact sheet exists
reuse map exists
raw runtime files were not overwritten
proposed tile is 128x128 ARGB
proposed card art is 64x64 ARGB
visible source is generated bitmap, not SVG/vector-looking art
no wolf body, monster, totem, rune, magic glow or real tamga sign
```

## Visual Review

The source sheet direction is good:

```text
low disturbed desert trail
visible animal tracks
scratch/drag marks
muted danger tone
generated painterly bitmap source
```

The current proposed runtime slices are not approved yet.

Issues:

1. `proposed_runtime/cards/card_art_wolf_track_64.png` is too small and soft in the card preview. It reads as a brown spot before it reads as paw/scratch risk.
2. `proposed_runtime/tiles/tile_wolf_track_01.png` is better than the old asset, but the chosen horizontal strip is very thin. It risks becoming a road/buffer smudge at gameplay scale.
3. The source sheet has stronger readable candidates than the current slices, especially the compact lower-left and diagonal lower-right variants.

## Decision

```text
PASS 14 SOURCE DIRECTION ACCEPTED
PASS 14 CURRENT RUNTIME SLICES NOT APPROVED
REVISION REQUESTED
NO CODE INTEGRATION YET
NO RAW OVERWRITE
```

## Required Revision

Use the same generated source sheet if possible. Do not generate a new broad pass.

Revise only:

```text
proposed_runtime/tiles/tile_wolf_track_01.png
proposed_runtime/cards/card_art_wolf_track_64.png
pass_14_beast_trail_contact.png
pass_14_beast_trail_reuse_map.md if crop notes change
```

Tile target:

```text
use a fuller crop from lower-left or diagonal lower-right source variant
keep transparent background
keep 128x128
keep low flat trail
make paw marks and scratches readable at map scale
avoid full baked square sand tile
```

Card target:

```text
use the strongest compact paw/scratch crop
fill more of the 64x64 art area
increase contrast only enough for card readability
avoid tiny centered blob
```

Contact sheet must show:

```text
current runtime tile 128
revised proposed tile 128
current card art 64
revised proposed card art 64
map-context preview
card preview on existing Pass 12 surface
```

## Next Step

Art revises the slices from the existing source. GDD reviews again before Code receives an integration task.
