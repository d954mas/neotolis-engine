# 92. Pass 15 UI inventory review

Status: accepted; visual contact proof requested.

Reviewed:

```text
gamedesign/assets/concept/pass_15_ui_asset_kit/pass_15_ui_asset_kit_inventory.md
gamedesign/assets/concept/pass_15_ui_asset_kit/pass_15_ui_asset_kit_reuse_map.md
```

## Decision

```text
PASS 15 UI INVENTORY ACCEPTED
NO NEW UI GENERATION YET
NO RAW OVERWRITE
NEXT: CONTACT SHEET / GAMEPLAY CONTEXT PROOF FROM EXISTING PASS 12 FAMILIES
```

## Why Accepted

The inventory follows the GDD pipeline rules:

```text
starts from existing runtime UI
answers Can Pass 12 solve this?
maps every gameplay UI zone to a reusable family
keeps labels, numbers, hover, selected lift, dimming and tooltips runtime-only
does not request one-off buttons/cards/panels/slots
does not request broad new UI generation
```

Accepted reusable families:

```text
panel_dark -> log, right hero panel, modal bodies
panel_light -> notes / selected summaries
button -> compact commands only
card_face -> playable cards
card_selected -> selected card state
card_back -> empty/back/hidden hand slots
equipment_slot -> weapon/clothes/tool/tamga slots
resource_chip -> top HUD chips
tooltip -> hover / selected-card hint
```

## Gaps To Keep As Queue, Not Immediate Generation

These may become future reusable families, but only after screenshot proof:

```text
log_row_marker_32
selection_overlay_128
panel_header_trim
card_badge_frame
```

Do not generate them yet.

## Required Next Art Step

Create a contact/proof sheet using current existing UI assets.

Required files:

```text
gamedesign/assets/concept/pass_15_ui_asset_kit/pass_15_ui_asset_kit_contact.png
gamedesign/assets/concept/pass_15_ui_asset_kit/pass_15_ui_asset_kit_contact_notes.md
```

Rules:

```text
use existing Pass 12 runtime UI files
do not generate new UI images
do not overwrite raw
do not create one-off buttons/cards/panels/slots
show reuse visibly in gameplay context
```

The contact sheet must show:

```text
top HUD resource chips
left chat/combat log
bottom card hand: playable, selected, empty/back
right hero/equipment/stats panel
equipment slots
tooltip / selected-card hint
valid-cell overlay placeholder as runtime-only state, not generated art
```

## Review Gate

GDD reviews the contact sheet before any UI source generation request.

If the contact sheet proves Pass 12 is enough, Code can get a UI reuse/integration task.

If it proves a missing family, GDD will authorize exactly one reusable generated bitmap family source, not one-off UI elements.
