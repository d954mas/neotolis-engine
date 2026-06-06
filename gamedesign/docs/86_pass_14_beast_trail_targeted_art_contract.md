# 86. Pass 14 beast trail targeted art contract

Status: active targeted Art task.

## Purpose

Fix only the weak P0 risk tile/card pair confirmed by dense map-context QA:

```text
Звериная тропа / wolf_track
```

Do not start a broad art pass. Do not repaint the full tile set. Do not generate UI.

## Source Evidence

Problem confirmed in:

```text
tmp/normal_gameplay_visual_qa_dense_empty_card_map_tiles.png
gamedesign/docs/85_dense_visual_qa_followup_review.md
```

The issue is not runtime wiring. The asset exists and draws. The problem is player-facing identity/readability.

## Runtime Targets

Art must prepare proposal-only candidates for:

```text
games/turkic-jam-2026/raw/tiles/tile_wolf_track_01.png       128x128 RGBA transparent
games/turkic-jam-2026/raw/cards/card_art_wolf_track_64.png   64x64 RGBA transparent or card-readable crop
```

Do not overwrite these raw files before GDD approval.

## Visual Direction

Player-facing concept:

```text
Звериная тропа
```

Must read as:

```text
risk
roadside/field danger
disturbed desert path
tracks discovered near the road
```

Composition:

```text
low flat desert trail, not a tall object
2-3 readable paw/track marks
slightly darker disturbed sand path
one or two scratch / dragged marks
subtle muted danger accent, not bright UI red
transparent background for runtime tile sprite
card art crop must remain readable at 64x64
```

Do not depict:

```text
wolf body
monster
totem
sacred wolf symbol
magic rune
glowing spell mark
real clan/tamga sign
Arabic fantasy
SVG/vector/script-looking art
```

## Required Deliverables

```text
gamedesign/assets/concept/pass_14_beast_trail/pass_14_beast_trail_source.png
gamedesign/assets/concept/pass_14_beast_trail/pass_14_beast_trail_contact.png
gamedesign/assets/concept/pass_14_beast_trail/pass_14_beast_trail_reuse_map.md
gamedesign/assets/concept/pass_14_beast_trail/proposed_runtime/tiles/tile_wolf_track_01.png
gamedesign/assets/concept/pass_14_beast_trail/proposed_runtime/cards/card_art_wolf_track_64.png
```

The source must be generated bitmap / painted-source bitmap.

The contact sheet must compare:

```text
current runtime tile at 128x128
proposed tile at 128x128
current card art at 64x64
proposed card art at 64x64
small map-context preview over sand/decor
small card preview on existing Pass 12 card surface
```

## Reuse / Decomposition

This pass is not a new UI pass. Use existing UI surfaces for preview only:

```text
ui_card_playable_96x128
ui_card_selected_96x128
```

If technical masks or chroma cleanup are used, label them:

```text
technical_mask_only
```

Visible production candidate pixels must come from generated bitmap source.

## Acceptance Criteria

```text
tile is readable at gameplay map scale
card art is readable at hand scale
tile reads as risk, not generic decor
no wolf body/totem/rune
no SVG-looking final art
exact target dimensions and RGBA
non-empty alpha
reuse map included
raw files not overwritten before GDD approval
```

## Prompt Seed

Use this as the generation direction, not as final copy text:

```text
top-down 2D generated bitmap game asset source, Turkic nomadic desert roguelite, Beast Trail risk tile, low flat disturbed sand trail, 2-3 readable paw tracks and scratch marks, compact dark ochre marks, subtle muted danger accent, warm desert palette, readable at 128x128 and 64x64, transparent sprite candidate, hand-painted bitmap style, no text, no symbols, no wolf body, no totem, no rune, no magic glow, no Arabic fantasy, no palace, no genie, no flying carpet, no SVG, no vector, no procedural shapes, no watermark
```
