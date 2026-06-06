# 88. UI asset kit contract

Status: active Art/UI production rule.

Owner: GDD / Art direction.

Audience: Art thread, Code thread.

## Problem

The designer keeps making two expensive mistakes:

1. visible production art is sometimes delivered as SVG/vector/script-looking output instead of generated bitmap art;
2. UI elements are generated as separate one-off images instead of one coherent reusable kit.

This breaks visual quality and makes the game look inconsistent.

## Decision

For UI, the next accepted deliverable is not "more mockups".

The next accepted deliverable is a reusable generated-bitmap UI kit:

```text
generated bitmap source sheet
-> reusable UI component families
-> runtime slices / nine-slice surfaces
-> contact sheet
-> reuse map
-> desktop gameplay proof
```

SVG/vector/script-looking UI is not accepted as visible production art.

## Visual Target

Use the approved direction from `ui_style_iteration_02_turkic_identity.md`:

```text
Turkic nomadic fairy-tale survival UI
felt, leather, woven trim, carved tags, warm fire, ochre dust, teal tamga accent
readable gameplay first, ornament second
```

The target is not generic fantasy UI and not a web dashboard.

The UI should feel like handmade game material:

```text
soft felt surfaces
slightly worn leather holders
restrained woven geometric trim
carved wood/bone small badges
warm parchment/felt card faces
dark felt card backs
small fictional tamga-like pictograms
```

## Hard Rules

### No Visible SVG Production Art

Forbidden for final/candidate-final visible art:

```text
SVG-looking shapes
flat vector panels
procedural rectangles as finished UI
generic icon-pack style
one-color web buttons
```

Allowed only as technical helpers:

```text
mask
slicing guide
nine-slice safe guide
layout overlay
debug preview
alpha cleanup
```

If a technical helper is used, it must be labeled `technical_only`.

### Reuse Before Generate

Do not generate a new button, card, panel, or slot for every use.

Correct:

```text
one generated card face material -> all playable cards
one generated card back material -> empty/back cards
one generated panel material -> hero panel/log/tooltips by nine-slice
one generated slot material -> equipment/tamga/relic slots
one generated chip material -> all top resource chips
```

Wrong:

```text
generate one attack button
generate one reset button
generate one export button
generate one card frame for each card
generate one unrelated panel for every screen area
```

## Required Component Families

The UI kit must provide these families first:

| Family | Runtime Use | Notes |
| --- | --- | --- |
| `panel_dark` | log, right hero panel, modal bodies | felt/leather, readable dark base |
| `panel_light` | notes, selected summaries | parchment/felt, not website white |
| `card_face` | playable tile cards | light, spendable, clear title/art/effect |
| `card_back` | empty hand slots / hidden cards | dark felt, one fictional tamga mark |
| `button` | compact commands only | same base, state via edge/brightness |
| `resource_chip` | top HUD resources, day, loop | shared chip, icon + number |
| `equipment_slot` | weapon/clothes/tamga slots | one slot family, not one-off plaques |
| `tooltip` | hover/selection hints | compact, same dark material |
| `log_row` | event log rows | marker + text, no giant parchment |
| `selection_overlay` | selected card / valid cell | edge/glow overlay, not new full asset |

## Required Runtime Slices

Art proposes exact runtime candidates, but Code decides integration after GDD approval.

Minimum proposal set:

```text
ui_panel_dark_96.png
ui_panel_light_96.png
ui_card_playable_96x128.png
ui_card_selected_96x128.png
ui_card_back_96x128.png
ui_button_dark_64.png
ui_chip_resource_64.png
ui_slot_equipment_64.png
ui_tooltip_dark_64.png
ui_log_row_marker_32.png
ui_valid_cell_overlay_128.png
```

If existing runtime filenames already exist, preserve them unless Code explicitly approves a rename.

## Decomposition Map

Every UI kit delivery must include:

```text
source material -> runtime file -> used by -> notes
```

Example:

```text
dark felt material -> ui_panel_dark_96.png -> log/right panel/tooltips -> nine-slice/tile reuse
light felt material -> ui_card_playable_96x128.png -> all playable cards -> title/art/effect changes only
woven trim strip -> selected card edge/resource chip edge -> reused as thin accent
leather holder material -> ui_slot_equipment_64.png -> weapon/clothes/tamga slots -> same slot family
```

If a delivery lacks this map, it is not ready for integration.

## Mockup Requirement

The contact sheet must show the kit inside the real gameplay layout:

```text
top HUD resource chips
left chat/combat log
center map
bottom card hand
right hero doll/equipment/stats panel
selected card + valid placement state
```

Do not submit isolated pretty UI parts without gameplay context.

## Acceptance Criteria

The UI kit is accepted only if:

```text
visible pixels are generated bitmap / painterly raster
component families are reused instead of one-off regenerated
cards read as playable tile cards, not inventory plaques
right panel reads as hero/equipment/stats, not unexplained storage
log is present but does not steal the screen
top HUD is compact and readable
map remains the largest visual object
Turkic nomadic identity is visible but restrained
no Arabian fantasy language
no real sacred or clan marks copied
runtime filenames and dimensions are explicit
desktop screenshot proof exists
```

## Next Art Task

After Pass 14 Beast Trail is reviewed, Art should prepare:

```text
gamedesign/assets/concept/pass_15_ui_asset_kit/pass_15_ui_asset_kit_source.png
gamedesign/assets/concept/pass_15_ui_asset_kit/pass_15_ui_asset_kit_contact.png
gamedesign/assets/concept/pass_15_ui_asset_kit/pass_15_ui_asset_kit_reuse_map.md
gamedesign/assets/concept/pass_15_ui_asset_kit/proposed_runtime/ui/...
```

This is a proposal only. Do not overwrite runtime files before GDD approval.
