# UI Design System v0.1

Purpose: production-oriented UI kit for the first playable.

This is not a marketing mockup. The game uses nine-slice UI, so the design system must define reusable sliced surfaces, states and sizing rules.

## Goals

1. Preserve the current working gameplay layout.
2. Add a distinct Turkic nomadic identity without decorative overload.
3. Make card play/placement readable like a card game interaction.
4. Build UI from reusable 9-slice pieces.
5. Keep runtime implementation simple.

## Visual Baseline

Approved direction:

```text
flat_readable_jam_style
Turkic nomadic identity
restrained felt/leather/wood/bone materials
tamga and woven geometric accents
warm desert + teal memory + clan red danger/glory
```

Do not use:

```text
game title in gameplay HUD
heavy leather card faces
large permanent bottom instruction panel
full carpet wallpaper
gold-heavy ornate fantasy UI
arabian palace / genie / flying carpet / bazaar language
```

## Core UI Surfaces

### Nine-Slice Assets

| Asset id | Use | Material | Border target |
| --- | --- | --- | --- |
| `ui_panel_dark_9s` | left log, right hero panel, bottom containers | dark felt/leather | 12 px corner, 8 px edge |
| `ui_panel_warm_9s` | highlighted/active panel | dark felt + warm stitched edge | 12 px corner, 8 px edge |
| `ui_chip_9s` | top HUD chips | carved dark wood/felt | 8 px corner, 6 px edge |
| `ui_card_face_9s` | playable card face | light felt/parchment | 10 px corner, 8 px edge |
| `ui_card_selected_9s` | selected card border | light card + green/teal edge | 10 px corner, 8 px edge |
| `ui_card_back_9s` | card back / empty card slot | dark felt with tamga | 10 px corner, 8 px edge |
| `ui_slot_9s` | equipment/tamga/relic slots | inset dark felt/wood | 8 px corner, 6 px edge |
| `ui_tooltip_9s` | compact temporary hints | dark felt, thin warm edge | 8 px corner, 6 px edge |
| `ui_button_9s` | pause/speed/settings buttons | compact dark chip | 8 px corner, 6 px edge |

### Slice Rules

Target source sizes:

| Type | Source size | Slice margin |
| --- | --- | --- |
| panels | `96x96` | `24 px` |
| chips/buttons | `64x64` | `16 px` |
| cards | `96x128` | `18 px` |
| slots | `64x64` | `14 px` |
| tooltips | `64x64` | `16 px` |

Runtime stretch rules:

- Corners never scale.
- Edges scale in one axis only.
- Center can tile or stretch; prefer stretch for simple felt grain.
- Decoration should live mostly in corners/edges, not center.
- Keep border density low so panels can resize without becoming noisy.

## Color Tokens

| Token | Hex | Use |
| --- | --- | --- |
| `ui_bg` | `#0D111A` | gameplay frame background |
| `ui_panel` | `#151C2A` | panel center |
| `ui_panel_2` | `#1E2636` | raised panel center |
| `ui_edge_warm` | `#7B5A32` | stitched/woven warm edge |
| `ui_edge_dim` | `#343B4A` | quiet inactive edge |
| `text_primary` | `#E8D7B5` | main text |
| `text_secondary` | `#A9A394` | secondary text |
| `sand_card` | `#D8B56B` | playable card face |
| `card_light` | `#E7D7B8` | light felt/parchment card |
| `fire_gold` | `#F4C95D` | circle/current/active warmth |
| `tamga_teal` | `#35B8A6` | memory/tamga accent |
| `valid_green` | `#9AD66F` | valid placement |
| `danger_red` | `#C95A3A` | danger/risk |
| `disabled` | `#596071` | empty/disabled UI |

## Typography Rules

The implementation may use the current font first. Art direction:

- resource numbers must be more prominent than labels;
- avoid decorative font for body text;
- section labels can use small caps / letter spacing only if readable;
- cards need short text only;
- no long tooltip paragraphs.

Suggested hierarchy:

| Role | Size target |
| --- | --- |
| circle marker | 26-32 px |
| HUD numbers | 20-24 px |
| panel section title | 18-22 px |
| card title | 18-22 px |
| card effect | 13-15 px |
| log line | 14-16 px |
| tooltip | 15-17 px |

## HUD Chips

Use `ui_chip_9s`.

Layout:

```text
icon 24
number 20-24
optional short label 12-14
```

First playable chips:

```text
supplies
wisdom
glory
circle
day
stamina
speed
settings
```

Avoid long Russian labels in every chip if icons + numbers are enough. Labels can appear on hover or in tooltip.

## Card System

Cards must read as spendable playable cards, not inventory items.

### Card Surface

Use:

```text
ui_card_face_9s for playable face
ui_card_selected_9s for selected/active card
ui_card_back_9s for backs/unknown cards
```

Card face structure:

```text
top corner: count/charge badge
top/mid: tile art
bottom: title
bottom-small: effect line
optional corner: placement icon
```

### Card States

| State | Visual |
| --- | --- |
| `idle` | normal card face |
| `hover` | lift 8-14 px, brighten edge |
| `selected` | lift/lock, green/teal edge, valid cells glow |
| `disabled` | desaturate, dim text, no lift |
| `empty_slot` | dark card back or quiet slot |
| `spent` | quick fade/shrink or count decrement |

### Card Interaction

```text
normal hand
-> hover card: lift + brighten, compact tooltip above card if needed
-> click card: selected lock, valid cells glow
-> hover valid cell: ghost tile preview
-> click valid cell: place tile, consume/decrement card
-> Esc/right click/outside click: cancel
```

### Many Cards

First playable:

```text
5 fixed slots
```

Future:

```text
6-10 cards: fan/overlap
10+ cards: horizontal scroll or deck drawer
```

## Tooltips

No permanent bottom-right instruction panel.

Use `ui_tooltip_9s`.

Tooltip placement:

| Situation | Placement |
| --- | --- |
| card hover | above hovered card |
| card selected | top-center over map, near selected target area |
| valid cell hover | near cell or top-center, with effect preview |
| invalid cell hover | tiny reason tooltip near cursor/cell |

Tooltip copy should be short:

```text
Выберите клетку.
Ставится у дороги.
Здесь кромка пути.
Эффект при проходе героя.
```

## Hero Panel

Right panel should be Loop Hero-like in function, but not visually copied.

MVP content:

```text
hero doll/silhouette
Body / Mind / Spirit / Stamina
current cell
current check/event summary
4 equipment/tool slots as locked or empty silhouettes
```

Not MVP:

```text
small storage/backpack grid
large inventory
full equipment management
```

Relic/Tamga rule:

- `Last Tamga` is collected and resolved. It is not a permanent inventory slot.
- If design later wants it as a physical item, it can go into backpack/inventory as a normal item.
- `Relic` is future-facing. Hide or lock it until the system exists.
- Avoid showing multiple unexplained named systems in the first 5 minutes.

## Log Panel

Use `ui_panel_dark_9s`.

Rules:

- show 6-8 meaningful recent events;
- collapse repeated empty events;
- newest event brightest;
- event marker uses diamond/tamga shape;
- tone colors: gain green, memory teal, danger red, death ash.

## Map Overlays

Not part of 9-slice, but required for UI consistency:

| Overlay | Visual |
| --- | --- |
| `valid_cell` | soft green/gold glow, low opacity |
| `hover_cell` | brighter edge + ghost tile |
| `invalid_cell` | dim/no glow, optional tiny reason tooltip |
| `current_hero_cell` | warm dust ring |
| `linked_road_cell` | subtle pulse for roadside tile effect |

## Required Generated UI Kit

First design-system asset batch:

```text
ui_panel_dark_9s
ui_chip_9s
ui_card_face_9s
ui_card_selected_9s
ui_card_back_9s
ui_slot_9s
ui_tooltip_9s
ui_button_9s
```

Second batch:

```text
icon_supplies
icon_wisdom
icon_glory
icon_circle
icon_day
icon_stamina
icon_speed
icon_settings
icon_body
icon_mind
icon_spirit
icon_last_tamga
```

## Prompt: Nine-Slice Sheet 01

```text
Create a production UI kit sheet for a 2D roguelite loop-builder game with Turkic nomadic visual identity. Show reusable nine-slice UI pieces, not a full screen. Include: dark felt panel frame, compact HUD chip frame, light playable card face frame, selected playable card frame with green/teal edge, dark card back frame with single teal tamga, equipment slot frame, compact tooltip frame, small button frame. Style: flat readable game UI, restrained woven geometric trim, felt/leather/wood/bone materials, warm ochre edges, teal tamga accents, muted clan red details. Keep centers mostly clean for stretching, decoration mostly in corners and borders, no text labels, no game title, no full carpet wallpaper, no ornate gold fantasy, no arabian palace, no genie lamp, no flying carpet, no bazaar, no photorealism, no watermark.
```

## Acceptance

Design system works if:

```text
same pieces can build HUD, log, right panel, cards, slots and tooltips
cards read as playable/spendable
right panel reads as hero/equipment area
tooltip no longer consumes permanent bottom space
Turkic identity is visible at first glance
map remains the main gameplay object
```
