# 93. Pass 15 UI contact review

Status: accepted as decomposition proof; runtime proof requested.

Reviewed:

```text
gamedesign/assets/concept/pass_15_ui_asset_kit/pass_15_ui_asset_kit_contact.png
gamedesign/assets/concept/pass_15_ui_asset_kit/pass_15_ui_asset_kit_contact_notes.md
```

## Decision

```text
PASS 15 CONTACT ACCEPTED AS UI REUSE / DECOMPOSITION PROOF
NOT FINAL GAMEPLAY UI ACCEPTANCE
NO NEW UI GENERATION AUTHORIZED
NEXT: CODE RUNTIME UI REUSE PROOF
```

## What Works

The contact sheet proves the main design-system rule:

```text
same chip surface reused across top HUD resources
same dark panel surface reused across log/right/hand context
same card face family reused across playable cards
same selected card surface reused for selected state
same card back reused for empty/back card
same slot surface reused for all equipment slots
same tooltip surface reused for selected-card hint
valid-cell overlay stays runtime-only placeholder, not generated art
```

This directly addresses the repeated problem:

```text
do not generate one button/card/panel/slot at a time
```

## Important Limits

This is accepted only as contact/decomposition evidence.

It is not final runtime UI approval because:

```text
the map area is a contact placeholder, not actual runtime map composition
the valid-cell block is a placeholder state, not final placement feedback
the dark panel material may be visually heavy/noisy at full runtime scale
the contact labels are explanatory, not in-game Russian copy
```

These are not reasons to generate new UI art yet. They are reasons to check runtime usage first.

## GDD Direction

Continue with Pass 12 reuse first.

No new UI source family is approved yet.

Potential future reusable families remain queued only:

```text
selection_overlay_128
log_row_marker_32
panel_header_trim
card_badge_frame
```

## Required Code Task

Produce runtime UI reuse proof using current assets:

```text
games/turkic-jam-2026/raw/ui/ui_panel_felt_dark_96.png
games/turkic-jam-2026/raw/ui/ui_panel_felt_light_96.png
games/turkic-jam-2026/raw/ui/ui_button_dark_64.png
games/turkic-jam-2026/raw/ui/ui_card_playable_96x128.png
games/turkic-jam-2026/raw/ui/ui_card_selected_96x128.png
games/turkic-jam-2026/raw/ui/ui_card_back_96x128.png
games/turkic-jam-2026/raw/ui/ui_slot_equipment_64.png
games/turkic-jam-2026/raw/ui/ui_chip_resource_64.png
games/turkic-jam-2026/raw/ui/ui_tooltip_dark_64.png
```

The screenshot must show real runtime composition:

```text
top HUD chips
left chat/combat log
bottom card hand with selected + playable + back/empty
right hero/equipment/stats panel
tooltip / selected-card hint if currently supported
valid/invalid placement feedback if currently supported
map remains the main object
```

## Review Gate

After Code provides runtime proof, GDD decides:

```text
Pass 12 UI reuse is enough -> integrate/keep and move to next art family
or one specific reusable missing family is authorized
```

Still forbidden:

```text
one-off button generation
one-off card frame generation
one-off panel generation
one-off equipment slot generation
```
