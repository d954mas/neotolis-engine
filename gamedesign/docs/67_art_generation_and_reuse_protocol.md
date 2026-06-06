# 67. Art Generation And Reuse Protocol

Status: mandatory workflow rule for Art/GDD production.

Purpose: prevent two repeated mistakes:

1. using SVG/script-looking output instead of generated bitmap art;
2. regenerating similar UI elements many times instead of decomposing and reusing a small design system.

## Hard Rules

### Rule 1: No SVG-Looking Production Art

Production-facing visual art must come from generated bitmap / painted-source bitmap.

Forbidden as final/candidate-final:

```text
pure SVG look
flat vector shapes
ImageDraw-only art presented as art
procedural rectangles as finished UI
generic icon-pack style
```

Allowed technical use:

```text
mask
alpha cleanup
chroma removal
slicing guide
9-slice-safe shape
layout placeholder
debug/QA visual
exact-size export helper
```

If a technical tool creates the shape, the visible material still must come from generated bitmap source.

### Rule 2: Generate Sources, Reuse Components

Do not generate the same UI element repeatedly.

Correct workflow:

```text
generated material/source sheet
-> component inventory
-> reusable runtime surfaces
-> exact-size exports
-> contact sheet
-> pack/L5 proof
```

Wrong workflow:

```text
generate button 1
generate button 2
generate button 3
generate another card frame
generate another similar card frame
```

For UI, generate material language, not every final instance.

## Component Inventory First

Every new Art pass must start with this short inventory:

```text
component families:
- panels
- cards
- buttons
- chips
- slots
- icons
- active tile objects
- FX frames

existing reusable sources:
- pass_12_ui_material_source.png
- pass_11 icon sheet
- pass_10 hero/equipment sheet
- pass_9 active tile sheet
- pass_8 card art sheet

new generated source needed:
- yes/no

runtime files touched:
- exact list

reuse map:
- source swatch -> runtime files
```

If this inventory is missing, the pass is not ready.

## UI Decomposition Rules

UI is built from reusable families:

| Family | Reuse Rule |
| --- | --- |
| `panel` | same dark/light felt material, different layout sizes through 9-slice/tiling |
| `card_surface` | same parchment/felt body, selected state is edge/accent, not a new full style |
| `button` | same dark felt/leather base, states are brightness/edge changes |
| `chip` | same resource chip base behind all resource icons |
| `slot` | same leather/felt holder behind all equipment items |
| `tooltip` | same dark panel material, text-first |
| `icon` | generated icon set, not one-off mixed styles |

Do not generate separate unrelated visual styles for each state.

## Runtime Asset Reuse Map

Current Pass 12 reuse map:

```text
dark felt source
-> ui_panel_felt_dark_96.png
-> ui_button_dark_64.png

light felt/parchment source
-> ui_panel_felt_light_96.png
-> ui_card_playable_96x128.png
-> ui_card_selected_96x128.png

woven source
-> ui_card_back_96x128.png
-> selected/card edge accents only

leather source
-> ui_slot_equipment_64.png

gold material source
-> ui_chip_resource_64.png

dark tooltip material source
-> ui_tooltip_dark_64.png
```

This is the expected pattern for later UI, not a one-off exception.

## Review Checklist

Before accepting any art delivery, GDD checks:

```text
Does the visible art come from generated bitmap / painted source?
Did the pass reuse existing source sheets before generating new ones?
Is there a component inventory?
Is there a reuse map?
Are exact runtime filenames and sizes preserved?
Is there a contact sheet?
Is there pack/L5 evidence?
Does it avoid duplicated UI styles?
Does it avoid real sacred marks and Arabian fantasy visual language?
```

If any answer is no, return the pass for correction.

## Designer Instruction

When creating a new asset:

1. Search existing generated sources first.
2. If existing source can solve it, reuse and slice.
3. If new source is needed, generate one source sheet for the family.
4. Do not generate several isolated buttons/cards/panels.
5. Do not deliver SVG or vector-looking art as production.
6. Deliver source, runtime files, contact sheet, reuse map and risks.

## Code Instruction

Code should not need to solve art-style drift.

Code receives:

```text
exact runtime filenames
exact dimensions
pack ids unchanged
contact sheet
L5 target screenshot path
```

Code should reject requests that ask for new UI ids or layout changes only because Art generated a different-looking one-off element.
