# Pass 15 UI Asset Kit Contact Notes

Status: visual proof/contact only. No new UI generation. No raw overwrite.

## Source Policy

- Visible UI surfaces in the contact sheet come from existing runtime PNGs in `games/turkic-jam-2026/raw/ui/`.
- The contact-sheet map rectangle, labels, arrows/legend and grid lines are `technical_contact_sheet_only`.
- This file is not a new runtime UI mockup and does not authorize new UI ids.

## Reuse Shown

- `ui_panel_felt_dark_96.png` -> left log, right hero/equipment panel, dark hand panel context.
- `ui_chip_resource_64.png` -> all top HUD resource chips.
- `ui_card_playable_96x128.png` -> playable hand cards.
- `ui_card_selected_96x128.png` -> selected hand card.
- `ui_card_back_96x128.png` -> empty/back card.
- `ui_slot_equipment_64.png` -> all equipment slots.
- `ui_tooltip_dark_64.png` -> selected-card hint / hover tooltip.

## Runtime-Only States

- Text labels, numbers, selected-card lift, card dimming, hover/selected hint position, valid-cell overlay and log event colors are runtime states.
- The valid-cell overlay in the contact sheet is a placeholder proof of state ownership, not a generated UI asset.

## UI Generation Gate

Do not generate a new button, card, panel, slot, chip or tooltip from this contact sheet. If GDD finds a gap, the next art task must authorize one reusable generated bitmap family source.
