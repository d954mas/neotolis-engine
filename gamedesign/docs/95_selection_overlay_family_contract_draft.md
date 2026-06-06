# 95. Selection Overlay Family Contract

Статус: active proposal contract. Генерация авторизована только для proposal-ассетов.

```text
AUTHORIZED FOR PROPOSAL GENERATION
NO RAW OVERWRITE
NO RUNTIME INTEGRATION BEFORE GDD REVIEW
```

Этот документ разрешает Art создать proposal PNG под `gamedesign/assets/concept/pass_selection_overlay/`.
Он не разрешает менять `games/turkic-jam-2026/raw/*`, добавлять runtime id или интегрировать family без отдельного GDD-review.

## Purpose

`selection_overlay` нужен, потому что normal gameplay placement-state proof показал: текущая runtime-подсветка выбранной карты и valid/invalid клеток недостаточно production-ready на реальной карте.

Назначение:

```text
selected card valid placement feedback on actual map cells
invalid/no-build placement feedback
hover preview for buildable cells
short placed/accepted pulse, if needed
```

Это feedback layer поверх карты, а не новый арт тайла.

## Component Family

Family id:

```text
selection_overlay
```

Тип:

```text
generated_bitmap_source -> reusable overlay slices -> runtime state tint/alpha/pulse
```

Важно:

```text
selection_overlay is not full tile art
selection_overlay is not per-card art
selection_overlay is not terrain/decor replacement
selection_overlay is not a UI panel/card/button
```

## Possible Runtime Candidates

Авторизованные proposal filenames:

```text
games/turkic-jam-2026/raw/ui/ui_valid_cell_overlay_128.png
games/turkic-jam-2026/raw/ui/ui_invalid_cell_overlay_128.png
games/turkic-jam-2026/raw/ui/ui_hover_cell_overlay_128.png
```

Размер и формат:

```text
128x128 RGBA transparent
non-empty alpha
transparent center preferred
edge/dust/mark overlay only
```

Возможный будущий дополнительный slice, если placement animation станет нужна:

```text
games/turkic-jam-2026/raw/ui/ui_placed_cell_pulse_128.png
```

Этот файл не включать без отдельного GDD approval.

## Visual Language

Видимые пиксели должны быть generated bitmap / painterly raster.

Материалы и мотивы:

```text
painted dust edge
soft sand disturbed rim
small teal tamga-like accent for valid
warm ochre/fire accent for selected target
muted clan red/amber accent for invalid
subtle woven/felt identity only as tiny edge rhythm
```

Композиция:

```text
thin readable cell-edge treatment
transparent center, so tile art remains visible
soft corner emphasis, not a hard square
small directional dust marks or short painted strokes
readable at gameplay map scale
```

Valid state:

```text
teal / warm green edge
soft dust glow
quiet positive placement cue
```

Invalid state:

```text
muted red / amber broken edge
small blocked dust mark
no giant warning wall
```

Hover state:

```text
low-contrast warm edge
less intense than valid selected state
```

Selected target state:

```text
slightly stronger edge and corner accents
may combine with runtime pulse/tint
```

Placed pulse:

```text
brief dust ring / soft edge flash
should reuse same family language
```

## States

Required state model, if generated:

| State | Runtime use | Suggested file | Runtime-only behavior |
| --- | --- | --- | --- |
| `hover` | cell under cursor / focus | `ui_hover_cell_overlay_128.png` | alpha, fade, position |
| `valid` | selected card can be placed | `ui_valid_cell_overlay_128.png` | pulse, tint, stacking over map |
| `invalid` | no-build / road buffer / blocked | `ui_invalid_cell_overlay_128.png` | short deny flash, alpha |
| `selected_target` | chosen target before placement | reuse `valid` or runtime tint | lift/pulse only |
| `placed_pulse` | card placed successfully | optional future `ui_placed_cell_pulse_128.png` | animation timing |

Text, tooltip copy, placement preview and card-specific state remain runtime-only.

## Reuse

The family must be reused across placement feedback:

```text
all buildable cells
roadside valid cells
field valid cells
no-build road buffer feedback
blocked/invalid feedback
hover preview
selected-card target feedback
```

Do not create separate overlays for:

```text
saxaul placement
yurt placement
wolf_track placement
oasis placement
each individual card
each map biome/decor cell
```

Card identity belongs to card art and active tile sprite. Placement legality belongs to `selection_overlay`.

## Forbidden

Rejected visual directions:

```text
full baked tile
opaque square overlay
one-off overlay per card
flat SVG rectangle
procedural ImageDraw-looking outline as visible art
generic neon magic glow
MMO spell circle
large rune
real clan/tamga sign
Arabic fantasy ornament
wall/fence/no-build icon as full tile art
bright UI red covering the map
```

Technical masks are allowed only as:

```text
technical_only mask
technical_only slicing guide
technical_only alpha cleanup
technical_only contact/grid proof
```

Visible pixels must come from generated bitmap / painterly raster source.

## Source / Reuse Map Requirement

If generation is later authorized, deliver:

```text
gamedesign/assets/concept/pass_selection_overlay/selection_overlay_source.png
gamedesign/assets/concept/pass_selection_overlay/selection_overlay_contact.png
gamedesign/assets/concept/pass_selection_overlay/selection_overlay_reuse_map.md
gamedesign/assets/concept/pass_selection_overlay/proposed_runtime/ui/ui_valid_cell_overlay_128.png
gamedesign/assets/concept/pass_selection_overlay/proposed_runtime/ui/ui_invalid_cell_overlay_128.png
gamedesign/assets/concept/pass_selection_overlay/proposed_runtime/ui/ui_hover_cell_overlay_128.png
```

Required reuse map shape:

```text
source component -> runtime slice -> used by -> notes
```

Example:

```text
teal painted dust edge -> ui_valid_cell_overlay_128.png -> all valid buildable cells -> runtime alpha/pulse
muted red broken dust edge -> ui_invalid_cell_overlay_128.png -> no-build/blocked feedback -> runtime flash only
warm low edge -> ui_hover_cell_overlay_128.png -> hover/focus preview -> lower alpha than selected
```

## Prompt Draft

Prompt draft:

```text
Use case: stylized-concept
Asset type: generated bitmap UI/map overlay source sheet for a 2D roguelite loop-builder
Primary request: Create one reusable source sheet for selection_overlay placement feedback cells. The sheet should include three transparent-overlay components for a 128x128 map cell: valid placement edge, invalid/no-build edge, and hover/focus edge. The style is Turkic nomadic fairy-tale survival UI: painted sand dust, felt/woven restraint, warm ochre dust, teal tamga accent for valid, muted amber/red accent for invalid. Transparent center so map/tile art remains visible. Painterly raster bitmap, not SVG/vector.
Scene/backdrop: flat #ff00ff chroma-key background for alpha removal, no shadows or gradients in the background.
Composition/framing: each overlay centered in its own 128x128 cell, generous padding, edge/corner treatment only, no full opaque square.
Style/medium: hand-painted generated bitmap game UI overlay, readable at gameplay map scale, soft dust edge, subtle corner accents.
Constraints: no text, no real tamga sign, no magic rune, no spell circle, no full baked tile, no one-off per card, no bright neon, no watermark.
```

Negative prompt:

```text
SVG, vector, procedural rectangle, flat outline, black web stroke, neon magic, MMO spell circle, rune, real sacred/clan tamga, Arabic fantasy, palace, bazaar, genie lamp, flying carpet, full carpet texture, opaque tile, wall, fence, warning sign, text, watermark, photorealism
```

## Acceptance Checklist

Before GDD can approve generation/integration:

```text
normal gameplay screenshot proves a real placement-readability gap
GDD explicitly authorizes selection_overlay generation
one generated bitmap source sheet is used for the family
no one-off overlays per card
visible pixels do not look SVG/vector/procedural
runtime filenames and dimensions are exact
transparent center preserves map/tile art
valid/invalid/hover states are distinguishable at map scale
invalid state does not dominate the screen
valid state does not look like magic spell UI
all states reuse one visual family
contact sheet shows overlays in real map context
reuse map exists
raw files are not overwritten before GDD approval
desktop runtime proof follows Code integration
```

## Current Decision

```text
PROPOSAL GENERATION AUTHORIZED
NORMAL GAMEPLAY PLACEMENT-STATE PROOF SHOWED A READABILITY GAP
NO RAW OVERWRITE BEFORE GDD REVIEW
```
