# 72. Visual design bible enforcement

Status: mandatory GDD review gate for Art and Code coordination.

Purpose: stop two recurring production mistakes before they reach runtime:

1. delivering SVG/script-looking assets instead of generated bitmap art;
2. generating repeated UI/world elements as isolated one-offs instead of reusing decomposed source systems.

## Decision

The project visual direction is accepted as:

```text
generated bitmap / painted-source bitmap
warm sand, felt, leather, woven trim
practical Turkic-nomadic UI and world materials
readable Loop Hero-like gameplay screen
no SVG-looking candidate-final art
no unrelated one-off UI generations
```

This applies to player-facing UI, cards, map tiles, hero, equipment, icons, world foundation and future visible assets.

## Automatic Rejection

GDD rejects a delivery if any of these are true:

```text
visible asset is pure vector/SVG/script art
visible asset looks like SVG/script art even if exported as PNG
asset was generated as a standalone duplicate instead of from a reusable source sheet
UI element was regenerated instead of reused from an approved UI family
UI buttons/cards/panels use unrelated styles within the same screen
source inventory is missing
reuse map is missing
contact sheet is missing
runtime file names/sizes are unclear
raw runtime files were overwritten before GDD approval
```

Technical masks, chroma cleanup, slicing guides, nine-slice construction and validators are allowed, but the visible material must still come from generated bitmap source.

## Two Hard Stop Errors

These two mistakes are hard stop errors, not minor review notes.

### Error 1: SVG Instead Of Generated Art

Do not solve visible art with SVG, vector drawing, procedural shapes, or script-painted silhouettes.

Allowed:

```text
SVG/vector/script only as technical_mask_only
SVG/vector/script only as slice guide, alignment guide, grid proof or validator
generated bitmap source used as the visible material
```

Rejected:

```text
visible SVG art exported as PNG
flat vector-looking art exported as PNG
procedural shape art presented as final style
generated image replaced by a script redraw
```

If the visible pixels look like SVG or script art, the asset is rejected even when the file extension is `.png`.

### Error 2: Repeated UI Generation Instead Of Decomposition

Do not generate UI pieces one by one when a reusable family can solve them.

Allowed:

```text
one generated UI material source sheet
-> reusable panel family
-> reusable button family
-> reusable card family
-> reusable slot family
-> reusable chip family
-> reusable tooltip family
-> runtime text/icons/states on top
```

Rejected:

```text
new generated button for each command
new generated card frame for each card
new generated panel for each screen
new generated equipment slot for each item type
same UI element generated multiple times with small style drift
```

Before any UI generation, Art must provide a decomposition plan:

```text
component family
base surface
states
overlays
which approved source is reused
which exact runtime files will be produced
```

If the pass cannot show this decomposition plan, it should not start generation.

## Required Art Pass Header

Every Art pass must start with:

```text
component inventory:
- exact component families in this pass

existing generated sources to reuse:
- exact source sheet paths

new generated source needed:
- yes/no and why

runtime files touched:
- exact target filenames and sizes

reuse map:
- source area/material -> runtime files

risks:
- readability, noise, halos, cultural risk, one-off drift
```

If this header is missing, the pass is not ready for generation or slicing.

## Before Generation Checklist

Art must answer this before calling image generation:

```text
1. Is this a visible production-facing asset?
   -> yes: use generated bitmap / painted-source bitmap.
   -> no: SVG/vector/script guide is allowed only as mask, slice guide, validator or layout proof.

2. Does an approved source sheet already cover this family?
   -> yes: reuse and slice it.
   -> no: generate one new family source sheet, not many isolated objects.

3. Is this UI?
   -> yes: generate or reuse component families: panel, button, card, slot, chip, icon.
   -> no: do not invent one-off UI surfaces.

4. Can runtime build the screen from components?
   -> yes: deliver components and reuse map.
   -> no: reject and redesign the source sheet.
```

Every delivery must label each visible file as one of:

```text
generated_bitmap_final_candidate
generated_bitmap_source
generated_bitmap_slice
technical_mask_only
contact_sheet_only
rejected_do_not_integrate
```

`technical_mask_only` and `contact_sheet_only` files must never be presented as final visual style.

## UI Decomposition Rule

UI is a reusable material system, not a pile of generated buttons.

Correct:

```text
pass_12_ui_material_source.png
-> dark felt panel
-> dark button
-> tooltip
-> parchment card
-> selected card edge
-> equipment slot
-> resource chip
```

Incorrect:

```text
generate one button
generate another button
generate a card frame from another style
generate a third panel from another style
```

States are made from the same family:

```text
idle       same material, normal brightness
hover      same material, lighter edge
active     same material, pressed center
selected   same material, teal/gold edge
disabled   same material, lower saturation
```

Approved UI source families must be reused before generating anything new:

```text
ui_panel_family
ui_button_family
ui_card_family
ui_slot_family
ui_resource_chip_family
ui_tooltip_family
ui_icon_family
```

Wrong delivery pattern:

```text
button_start_01 generated separately
button_export_01 generated separately
button_reset_01 generated separately
card_saxaul_frame generated separately
hero_panel_frame generated separately
```

Correct delivery pattern:

```text
one generated UI material source sheet
-> one button 9-slice / state family
-> one card 9-slice / selected family
-> one panel 9-slice family
-> one slot/chip/icon family
```

Text labels, numbers and button captions are runtime content. They are not baked into generated UI bitmaps unless GDD explicitly requests a title/logo illustration.

## World Decomposition Rule

World foundation uses one coherent generated source sheet, then reuse/slicing:

```text
one world material source
-> ground sand base
-> quiet decor overlays
-> road material
-> road buffer/no-build edge
-> aul core components
```

Do not generate a separate unrelated image for every road corner or decor tile. Road corners, straight road and buffer pieces should share the same generated material language.

## Pass 13 Review Gate

Pass 13 source is accepted only as source material. Runtime slicing is not approved until Art provides:

```text
gamedesign/assets/concept/pass_13_generated_world_foundation/pass_13_world_foundation_reuse_map.md
gamedesign/assets/concept/pass_13_generated_world_foundation/pass_13_world_foundation_slicing_proposal_contact.png
```

The slicing proposal contact must compare:

```text
current raw asset at runtime size | proposed generated candidate at same runtime size | notes/risk
```

No `games/turkic-jam-2026/raw/*` overwrite before GDD review.

## Code Coordination Rule

Code receives exact runtime PNGs only after GDD approves the slicing/contact/reuse evidence.

Code should not create new UI ids or layout exceptions because Art generated an unrelated one-off visual. If a new visual does not fit the reusable UI/material system, the art pass goes back to Art/GDD.

## Current Source References

Use these before generating anything new:

```text
gamedesign/docs/66_visual_design_bible.md
gamedesign/docs/67_art_generation_and_reuse_protocol.md
gamedesign/assets/concept/pass_12_generated_ui_surfaces/pass_12_ui_material_source.png
gamedesign/assets/concept/pass_12_generated_ui_surfaces/pass_12_generated_ui_surfaces_contact_sheet.png
gamedesign/assets/concept/pass_13_generated_world_foundation/pass_13_world_foundation_source.png
```
