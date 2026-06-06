# 89. Pass 15 UI asset kit work order

Status: queued after Pass 14 Beast Trail approval.

Owner: GDD / Art direction.

Audience: Art thread.

## Purpose

Prepare a reusable UI kit for the current gameplay HUD.

This is not a broad redesign and not a set of one-off generated UI pictures.

## Start With Existing Runtime UI

Current raw UI family:

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

Before generating anything, answer:

```text
Can Pass 12 solve this?
Which existing UI family is reused?
Which state/overlay is runtime-only?
Which source sheet produces the slice?
Why is a new generated source sheet needed, if any?
```

## Required Deliverables

```text
gamedesign/assets/concept/pass_15_ui_asset_kit/pass_15_ui_asset_kit_inventory.md
gamedesign/assets/concept/pass_15_ui_asset_kit/pass_15_ui_asset_kit_contact.png
gamedesign/assets/concept/pass_15_ui_asset_kit/pass_15_ui_asset_kit_reuse_map.md
```

If new generated bitmap source is truly needed:

```text
gamedesign/assets/concept/pass_15_ui_asset_kit/pass_15_ui_asset_kit_source.png
gamedesign/assets/concept/pass_15_ui_asset_kit/proposed_runtime/ui/...
```

No raw overwrite before GDD approval.

## Contact Sheet Requirement

Show the UI kit in gameplay context:

```text
top HUD chips
left chat/combat log
center map edge with selected valid cell
bottom card hand with selected and empty/back card
right hero/equipment/stats panel
tooltip/log row state
```

The contact sheet must make reuse visible:

```text
same panel source across log/right/tooltip
same card face across playable cards
same card back across empty/hidden hand slots
same slot surface across equipment slots
same chip surface across resource counters
```

## Rejection Conditions

Reject if:

```text
visible UI looks SVG/vector/procedural
new one-off button/card/panel/slot is generated
map becomes visually secondary
cards read as inventory plaques instead of playable tile cards
right panel shows unexplained storage/relic systems before gameplay unlock
log panel steals too much screen space
```

## Approval Gate

GDD reviews Pass 15 before Code gets an integration request.

Code receives only:

```text
approved exact runtime filenames
approved dimensions
approved reuse map
desktop screenshot target
```
