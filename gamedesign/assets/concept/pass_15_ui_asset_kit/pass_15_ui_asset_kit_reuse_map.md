# Pass 15 UI Asset Kit Reuse Map

Status: reuse map only. No new UI generation. No raw overwrite.

## Source System

| Source / family | Label | Runtime slice | Used by | Notes |
| --- | --- | --- | --- | --- |
| `pass_12_ui_material_source.png` dark felt material | `generated_bitmap_source` | `ui_panel_felt_dark_96.png` | left log, right hero panel, modal bodies, large dark surfaces | 9-slice / tiling; text and headers are runtime |
| `pass_12_ui_material_source.png` light felt/parchment material | `generated_bitmap_source` | `ui_panel_felt_light_96.png` | notes, selected summaries, small light panels | do not use as website-white panel |
| `pass_12_ui_material_source.png` dark felt / button material | `generated_bitmap_source` | `ui_button_dark_64.png` | compact commands only | button text/icon/state is runtime |
| `pass_12_ui_material_source.png` card face material | `generated_bitmap_source` | `ui_card_playable_96x128.png` | playable hand cards | title, card art, count and effect text are runtime |
| `pass_12_ui_material_source.png` card selected accent | `generated_bitmap_source` | `ui_card_selected_96x128.png` | selected card in hand | selected lift/dimming/placement state is runtime |
| `pass_12_ui_material_source.png` dark card back material | `generated_bitmap_source` | `ui_card_back_96x128.png` | empty/back/hidden card slots | empty label and unavailable state are runtime |
| `pass_12_ui_material_source.png` equipment slot material | `generated_bitmap_source` | `ui_slot_equipment_64.png` | all equipment slots | one slot surface reused for weapon/clothes/tool/tamga |
| `pass_12_ui_material_source.png` chip material | `generated_bitmap_source` | `ui_chip_resource_64.png` | HUD chips: supplies, wisdom, glory, day, speed, loop | icon + number are runtime |
| `pass_12_ui_material_source.png` tooltip material | `generated_bitmap_source` | `ui_tooltip_dark_64.png` | hover tooltip, selected-card hint | tooltip text and position are runtime |

## Runtime-Only Overlays

| Overlay / state | Runtime-only responsibility | Art source needed now? |
| --- | --- | --- |
| selected card lift | transform / z-order / shadow / dimming of other cards | no |
| card hover | transform / brightness tint / tooltip placement | no |
| empty card label | text and disabled styling over `ui_card_back_96x128.png` | no |
| HUD numbers | text rendering over `ui_chip_resource_64.png` | no |
| resource icons | icon sprites over shared chip surface | no new UI surface |
| log latest event | text color and small marker tint | no new UI surface |
| hero stats | icon/text layout over dark panel | no new UI surface |
| equipment item contents | item sprites over shared slot | no new slot surface |
| valid cell overlay | map interaction overlay | maybe later; screenshot-backed only |
| invalid cell overlay | map interaction overlay | maybe later; reuse selection overlay family if generated |

## Gameplay Zone Decomposition

### Top HUD Resource Chips

```text
ui_chip_resource_64.png
-> nine-slice/tile chip background
-> runtime icon + number + optional tint
-> supplies / wisdom / glory / day / speed / loop
```

Pass 12 can solve this. No new source sheet.

### Left Chat / Combat Log

```text
ui_panel_felt_dark_96.png
-> large 9-slice log panel
-> runtime header text + event rows + marker dots
```

Pass 12 can solve the surface. A future reusable `log_row_marker_32` family may be generated only if GDD wants painterly event markers.

### Bottom Card Hand

```text
ui_card_playable_96x128.png
-> playable card face
-> runtime title + art + count + effect

ui_card_selected_96x128.png
-> selected card face/accent
-> runtime selected lift and valid cell state

ui_card_back_96x128.png
-> empty/back/hidden card surface
-> runtime "empty" label / disabled dim
```

Pass 12 can solve this. If card readability fails, regenerate one `card_surface` family, not one card per tile.

### Right Hero / Equipment / Stats Panel

```text
ui_panel_felt_dark_96.png
-> hero/status panel body
-> runtime hero doll + stats + current cell/check

ui_slot_equipment_64.png
-> all equipment/tamga/tool slots
-> runtime item sprites and slot markers
```

Pass 12 can solve the surface. No new hero-panel one-off.

### Equipment Slots

```text
ui_slot_equipment_64.png
-> weapon slot
-> clothes slot
-> tool slot
-> tamga slot
```

One shared slot. Semantic slot type is runtime icon/item overlay, not separate generated plaques.

### Tooltip / Selected-Card Hint

```text
ui_tooltip_dark_64.png
-> hover tooltip
-> selected-card hint
-> compact top-center or above-card hint
```

Pass 12 can solve this. No permanent bottom instruction panel asset.

### Valid-Cell Overlay

Current status:

```text
No raw UI file in current runtime UI inventory.
This is a map overlay, not a panel/card/button.
```

If screenshot QA proves art is needed:

```text
selection_overlay source family
-> ui_valid_cell_overlay_128.png
-> ui_invalid_cell_overlay_128.png
-> selected-card edge/target accent reuse
```

This would be a reusable overlay family, not a one-off green square.

## Missing Family Queue

Do not generate these now. Queue only for screenshot-backed GDD activation:

| Candidate family | Reason it might be needed | Trigger |
| --- | --- | --- |
| `log_row_marker_32` | event rows need painterly tamga/diamond markers | log screenshot reads too plain or generic |
| `selection_overlay_128` | valid/invalid cells need art-owned overlay language | Code color/alpha adjustments cannot fix build-slot overlay |
| `panel_header_trim` | right/log panels need more Turkic identity | panels read too generic after layout proof |
| `card_badge_frame` | count/cost badge needs reusable surface | card count badge reads like raw text only |

## Rejection Rules

Reject any next UI request that says:

```text
generate another button
generate another card frame for one card
generate a unique hero panel
generate four equipment slots separately
generate a one-off resource chip
```

Correct response:

```text
reuse Pass 12 family first, or generate one reusable source family after GDD proves the family is missing.
```
