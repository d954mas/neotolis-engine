# Pass 14 Beast Trail Reuse Map

Status: targeted proposal only. No raw runtime overwrite.

## Production Inventory

- visible production-facing asset: yes, Beast Trail tile/card candidate
- existing UI family reused: `ui_card_playable_96x128.png` for preview only
- new generated source sheet needed: yes, current wolf_track source fails map/card readability
- source sheet: `pass_14_beast_trail_source.png` labeled `generated_bitmap_source`
- runtime files proposed: `tile_wolf_track_01.png`, `card_art_wolf_track_64.png`
- runtime text/state overlays: none
- technical masks/guides: chroma cleanup and fit/crop only, labeled `technical_mask_only`

## Proposed Runtime Slices

### `games/turkic-jam-2026/raw/tiles/tile_wolf_track_01.png`

- proposed export: `proposed_runtime/tiles/tile_wolf_track_01.png`
- label: `generated_bitmap_slice`
- size/mode: `128x128 RGBA`
- source crop: diagonal lower-right Beast Trail variant from generated source
- intent: low flat disturbed sand trail, 2-3 paw marks, scratch/drag marks, muted danger accent
- raw overwrite: no

### `games/turkic-jam-2026/raw/cards/card_art_wolf_track_64.png`

- proposed export: `proposed_runtime/cards/card_art_wolf_track_64.png`
- label: `generated_bitmap_slice`
- size/mode: `64x64 RGBA`
- source crop: compact lower-left paw/scratch Beast Trail crop from generated source
- intent: paw and scratch marks remain readable at card scale
- raw overwrite: no

## Screenshot Failure Fixed

- Dense QA showed current `wolf_track` reads like generic sand/decor.
- Proposal increases risk identity through darker disturbed path, paw marks and scratch marks.
- No broad UI/art pass is started.

## Avoided Motifs

- no wolf body, monster, totem, sacred wolf symbol, magic rune, glowing spell mark, real tamga/clan sign, Arabic fantasy