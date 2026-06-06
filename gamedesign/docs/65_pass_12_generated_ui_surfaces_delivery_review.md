# Pass 12 Generated UI Surfaces Delivery Review

Status: delivered as candidate generated bitmap runtime UI art.

## Source

Generated bitmap material source:

```text
gamedesign/assets/concept/pass_12_generated_ui_surfaces/pass_12_ui_material_source.png
```

Runtime processing tool:

```text
gamedesign/tools/pass_12_generated_ui_surfaces_runtime.py
```

Review contact sheet:

```text
gamedesign/assets/concept/pass_12_generated_ui_surfaces/pass_12_generated_ui_surfaces_contact_sheet.png
```

## Runtime Files Replaced

Exact existing runtime filenames and dimensions:

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

Old Kenney-style `button_*_depth.png` files were not touched in this pass.

## Technical Validation

```text
all 9 files exact expected dimensions
all 9 files RGBA
all 9 files non-empty alpha
```

Pack builder:

```text
Optional Batch A sprites: found 44 / missing 0
Optional Batch B sprites: found 47 / missing 0
Optional Batch C aul progression sprites: found 6 / missing 0
Optional Batch C hero archetype panels sprites: found 3 / missing 0
Optional Pass 6 future library sprites: found 21 / missing 0
Optional production sprites: found 121 / missing 0
Atlas packed: 125 sprites
Generated merged header: 132 assets
CRC32: 0x689C6987
```

Desktop L5:

```text
Command: .\turkic_jam.exe --visual-qa --dump-frame C:\projects\neotolis-engine-turkic-jam-2026\tmp\visual_qa_l5_pass12_ui_surfaces.png --exit-after-frame
Output PNG: tmp/visual_qa_l5_pass12_ui_surfaces.png
Resolution: 1280x720
Checksum: 0x5560282F
Blank/white/black: no
Runtime status: Batch A 44/44, Batch B 47/47, Batch C aul 6/6, Batch C hero panels 3/3, Pass 6 future 21/21
```

## GDD Review

Decision:

```text
Pass 12 is accepted as candidate generated bitmap runtime UI art.
Pass 12 is not final accepted production UI until user/GDD reviews normal gameplay readability.
```

What works:

- Card surfaces now read as generated material, not blank beige technical rectangles.
- Selected-card frame is visible without becoming neon.
- Resource chips, equipment slots, dark panels and tooltip/button surfaces stay within the quiet gameplay UI direction.
- The generated material source can be reused for later UI variants without changing runtime ids.
- No gameplay layout, ids, filenames or balance data changed.

Risks:

- Card surfaces are more textured than before; if card art or Russian names lose readability in normal gameplay, reduce texture contrast.
- `ui_card_back_96x128` uses the active woven material; it may be too loud if many empty slots are shown at once.
- `ui_chip_resource_64` is warm/gold and may compete with icon art if used behind many resource counters.
- Masked construction is technical, but visible material comes from generated bitmap source; keep this distinction clear.

## Next Gate

No new Code task is opened by this pass because the Code thread is paused during map migration.

After the map migration finishes, request one normal gameplay screenshot with:

```text
top HUD resource chips
bottom card hand with selected card and empty slots
right hero panel/equipment slots
left chat log
map visible behind the UI
```

Final acceptance depends on user/GDD readability review in that normal gameplay context.
