# Pass 15 UI Asset Kit Inventory

Status: inventory only. No new UI generation. No raw overwrite.

## Production Inventory Header

Can Pass 12 solve this?

Yes for the current UI kit inventory. Pass 12 already provides the visible generated-bitmap UI surface family used by the current runtime UI files.

Which existing UI family is reused?

Pass 12 generated UI surfaces:

```text
gamedesign/assets/concept/pass_12_generated_ui_surfaces/pass_12_ui_material_source.png
gamedesign/assets/concept/pass_12_generated_ui_surfaces/pass_12_generated_ui_surfaces_contact_sheet.png
```

Which state/overlay is runtime-only?

Text labels, numbers, selected card lift, card dimming, hover/selection state, valid-cell glow placement, disabled/empty state logic, icon + number layout and tooltip text are runtime-only. They must not be baked into generated UI bitmaps.

Which source sheet produces the slice?

`pass_12_ui_material_source.png` is the active generated-bitmap source sheet for UI surfaces until GDD approves a replacement family source.

Why is a new generated source sheet needed, if any?

No new source sheet is needed for this inventory step. A future source sheet is justified only if screenshot QA proves Pass 12 cannot solve a reusable family, not an individual button/card/panel.

## Existing Runtime UI Inventory

| Runtime file | Size | Label | Family | Current role |
| --- | --- | --- | --- | --- |
| `games/turkic-jam-2026/raw/ui/ui_panel_felt_dark_96.png` | 96x96 | `generated_bitmap_slice` | `panel_dark` | dark 9-slice panel base |
| `games/turkic-jam-2026/raw/ui/ui_panel_felt_light_96.png` | 96x96 | `generated_bitmap_slice` | `panel_light` | light note/summary panel base |
| `games/turkic-jam-2026/raw/ui/ui_button_dark_64.png` | 64x64 | `generated_bitmap_slice` | `button` | compact command button base |
| `games/turkic-jam-2026/raw/ui/ui_card_playable_96x128.png` | 96x128 | `generated_bitmap_slice` | `card_face` | playable tile card face |
| `games/turkic-jam-2026/raw/ui/ui_card_selected_96x128.png` | 96x128 | `generated_bitmap_slice` | `card_face_selected` | selected playable card surface |
| `games/turkic-jam-2026/raw/ui/ui_card_back_96x128.png` | 96x128 | `generated_bitmap_slice` | `card_back` | empty/back/hidden card surface |
| `games/turkic-jam-2026/raw/ui/ui_slot_equipment_64.png` | 64x64 | `generated_bitmap_slice` | `equipment_slot` | shared equipment slot surface |
| `games/turkic-jam-2026/raw/ui/ui_chip_resource_64.png` | 64x64 | `generated_bitmap_slice` | `resource_chip` | top HUD chip base |
| `games/turkic-jam-2026/raw/ui/ui_tooltip_dark_64.png` | 64x64 | `generated_bitmap_slice` | `tooltip` | compact tooltip / hint base |

## Gameplay UI Zones

### Top HUD Resource Chips

Can Pass 12 solve this?

Yes.

Which existing UI family is reused?

`resource_chip` via `ui_chip_resource_64.png`.

Which state/overlay is runtime-only?

Resource icon, number text, warning color/tint, day/speed multiplier text and spacing.

Which source sheet produces the slice?

`pass_12_ui_material_source.png` -> `ui_chip_resource_64.png`.

Why is a new generated source sheet needed, if any?

Not needed. If chips fail screenshot readability, adjust runtime layout/icon contrast first; only regenerate the `resource_chip` family if the material itself is the blocker.

### Left Chat / Combat Log

Can Pass 12 solve this?

Mostly yes.

Which existing UI family is reused?

`panel_dark` via `ui_panel_felt_dark_96.png`; optional marker style can reuse icon/tamga marker family if later approved.

Which state/overlay is runtime-only?

Log text, newest-event highlight, event color categories, scroll/fade, row spacing and marker placement.

Which source sheet produces the slice?

`pass_12_ui_material_source.png` -> `ui_panel_felt_dark_96.png`.

Why is a new generated source sheet needed, if any?

No new surface sheet. A future `log_row_marker_32` source may be needed if GDD wants a dedicated generated marker family; this should be a reusable marker family, not one marker per event.

### Bottom Card Hand: Playable, Selected, Empty / Back

Can Pass 12 solve this?

Yes.

Which existing UI family is reused?

`card_face` via `ui_card_playable_96x128.png`, selected state via `ui_card_selected_96x128.png`, empty/back via `ui_card_back_96x128.png`.

Which state/overlay is runtime-only?

Card title, count badge number, card art, card dimming, hover lift, selected lift, disabled/empty label and placement target state.

Which source sheet produces the slice?

`pass_12_ui_material_source.png` -> card face/back/selected slices.

Why is a new generated source sheet needed, if any?

Not needed unless gameplay screenshots show the card face still reads like inventory plaques instead of spendable cards. If that happens, regenerate one `card_surface` family source sheet, not individual card frames.

### Right Hero / Equipment / Stats Panel

Can Pass 12 solve this?

Yes for the panel surface.

Which existing UI family is reused?

`panel_dark` via `ui_panel_felt_dark_96.png`; `equipment_slot` via `ui_slot_equipment_64.png`.

Which state/overlay is runtime-only?

Hero doll, item sprites, stat labels/numbers, current cell line, current check summary, locked/hidden future systems.

Which source sheet produces the slice?

`pass_12_ui_material_source.png` -> `ui_panel_felt_dark_96.png` and `ui_slot_equipment_64.png`.

Why is a new generated source sheet needed, if any?

No new panel sheet now. If right-panel identity is too plain, generate one reusable `panel_header_trim` or `panel_accent_overlay` family, not a new full hero panel.

### Equipment Slots

Can Pass 12 solve this?

Yes.

Which existing UI family is reused?

`equipment_slot` via `ui_slot_equipment_64.png`.

Which state/overlay is runtime-only?

Slot icon marker, equipped item sprite, empty/locked state, hover outline and rarity/tint.

Which source sheet produces the slice?

`pass_12_ui_material_source.png` -> `ui_slot_equipment_64.png`.

Why is a new generated source sheet needed, if any?

Not needed. The user specifically rejected four unrelated generated slot cells; all equipment slots must reuse one slot surface plus runtime icon/item overlay.

### Tooltip / Selected-Card Hint

Can Pass 12 solve this?

Yes.

Which existing UI family is reused?

`tooltip` via `ui_tooltip_dark_64.png`; optionally `panel_dark` for larger contextual hints.

Which state/overlay is runtime-only?

Tooltip text, placement preview text, hover target, fade/position and compact top-center placement.

Which source sheet produces the slice?

`pass_12_ui_material_source.png` -> `ui_tooltip_dark_64.png`.

Why is a new generated source sheet needed, if any?

Not needed. If tooltip feels too large, solve with layout and nine-slice sizing before art regeneration.

### Valid-Cell Overlay

Can Pass 12 solve this?

Partially. Pass 12 covers UI surfaces, but valid-cell overlay is a map interaction overlay rather than a panel/card surface.

Which existing UI family is reused?

Use the Pass 12 teal/selection accent language if a visual overlay source is needed; keep it separate from panels/cards.

Which state/overlay is runtime-only?

Valid/invalid placement state, hover cell, selected tile target, alpha pulse and color tint.

Which source sheet produces the slice?

Currently no approved runtime `ui_valid_cell_overlay_128.png` exists in raw UI. If needed, it should come from a reusable selection overlay family source, not a one-off cell image.

Why is a new generated source sheet needed, if any?

Possibly needed later. Screenshot QA must first prove current Code overlay cannot be solved by opacity/color/shape adjustment. If art is needed, generate one reusable `selection_overlay` source family for valid cell, invalid cell and selected card accents.

## New Generation Gate

Do not generate UI yet. Future UI generation is allowed only if the next GDD task identifies a reusable missing family:

```text
panel/backplate family
card surface family
button family
slot family
chip family
tooltip/log-row family
selection overlay family
icon/tamga marker family
```

Never generate a standalone button, card, panel, slot or chip for one immediate use.
