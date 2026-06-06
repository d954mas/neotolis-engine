# Pass 12 Generated UI Surfaces Contract

## Goal

Replace the remaining technical-looking UI surfaces with generated bitmap / painted-source bitmap material.

This pass keeps existing runtime filenames, sizes, ids, pack schema, UI layout, and gameplay behavior.

## Source Rule

The visible material must come from generated bitmap / painted-source bitmap:

```text
felt
leather
woven trim
parchment card body
turquoise thread accents
warm gold edge highlights
```

Simple masks, nine-slice-safe shapes, alpha borders, corner cuts and resizing logic are allowed as technical construction. They must not become visible SVG-like flat art.

## Allowed Runtime Files

Exact existing files:

```text
games/turkic-jam-2026/raw/ui/ui_panel_felt_dark_96.png       96x96 RGBA
games/turkic-jam-2026/raw/ui/ui_panel_felt_light_96.png      96x96 RGBA
games/turkic-jam-2026/raw/ui/ui_card_playable_96x128.png     96x128 RGBA
games/turkic-jam-2026/raw/ui/ui_card_selected_96x128.png     96x128 RGBA
games/turkic-jam-2026/raw/ui/ui_card_back_96x128.png         96x128 RGBA
games/turkic-jam-2026/raw/ui/ui_slot_equipment_64.png        64x64 RGBA
games/turkic-jam-2026/raw/ui/ui_chip_resource_64.png         64x64 RGBA
games/turkic-jam-2026/raw/ui/ui_tooltip_dark_64.png          64x64 RGBA
games/turkic-jam-2026/raw/ui/ui_button_dark_64.png           64x64 RGBA
```

Do not touch old Kenney-style `button_*_depth.png` in this pass unless Code confirms they are used in the current gameplay HUD.

## Visual Direction

- quiet work-focused gameplay UI, not a landing page;
- dark felt/leather for panels;
- pale felt/parchment for cards and readable surfaces;
- woven border hints, not dense ornamental frames;
- small turquoise/gold accents only where they help readability;
- 8px radius feel or less at runtime scale;
- no palace/gold fantasy frame, no Aladdin/bazaar look;
- no text, no numerals, no real sacred tamgas.

## Required Outputs

Source and contact sheet:

```text
gamedesign/assets/concept/pass_12_generated_ui_surfaces/
```

Required source:

```text
pass_12_ui_material_source.png
```

Required contact sheet:

```text
pass_12_generated_ui_surfaces_contact_sheet.png
```

Required processing tool:

```text
gamedesign/tools/pass_12_generated_ui_surfaces_runtime.py
```

## Acceptance

Technical:

```text
all files exact expected dimensions
all files RGBA
all files non-empty alpha
pack builder found counts unchanged or improved
desktop L5 screenshot shows the UI surfaces in HUD/card/right-panel context
```

Art:

```text
cards must not read as blank beige rectangles
dark panels must not overpower the map
resource chips must remain readable behind 24px icons and small numbers
selected card frame must be visible but not neon
equipment slots must read as slots, not inventory loot cards
```

Final acceptance requires user/GDD review in a normal gameplay screenshot after the map migration.
