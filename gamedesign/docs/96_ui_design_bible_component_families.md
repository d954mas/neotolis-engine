# 96. UI Design Bible - Component Families

Status: active baseline for UI art and implementation.

Owner: GDD / Art direction.

Audience: Art thread, Code thread.

## Core Target

UI для `Песнь Тамги` должен ощущаться как тюркско-кочевая игра-сказка:

```text
войлок
потертая кожа
тонкая тканая окантовка
деревянные и костяные бирки
тепло костра
охра и песочная пыль
бирюзовый акцент Тамги
```

Это не web dashboard, не generic fantasy UI и не арабская сказка.

Запрещенный визуальный дрейф:

```text
Aladdin / palace / golden dome / bazaar / genie lamp
generic MMO magic UI
blue neon fantasy
flat SVG-looking web buttons
random generated frame per panel
heavy carpet wallpaper
```

## Production Rules

Видимые production/candidate-final пиксели должны быть generated bitmap / painterly raster.

SVG, procedural shapes, ImageDraw, layout rectangles are allowed only as:

```text
technical_only mask
technical_only alpha cleanup
technical_only slicing guide
technical_only nine-slice guide
technical_only debug/layout overlay
```

Они не принимаются как финальный видимый арт.

UI нельзя генерировать отдельными случайными элементами:

```text
one generated button for each command - rejected
one generated card frame for each card - rejected
one generated panel for each screen zone - rejected
```

Правильный подход:

```text
generated material/source sheet
-> reusable component family
-> runtime slices
-> contact sheet
-> reuse map
-> desktop runtime proof
```

## Material Families

| Material | Visual Role | Use | Must Not Become |
| --- | --- | --- | --- |
| `dark_felt` | темная войлочная/кожаная подложка | лог, правая панель, tooltip, modal body | плоская черная web-card |
| `light_felt_parchment` | светлый войлок/плотная бумага | playable card face, notes, selected summaries | белая HTML-карточка |
| `worn_leather` | ремни, держатели, слоты | equipment slots, card holders | тяжелая leather inventory plaque для всех карт |
| `woven_trim` | тонкая кочевая геометрическая окантовка | headers, card edge, selected accents | ковровый фон или орнамент вокруг всего экрана |
| `wood_bone_tag` | маленькие бирки и счетчики | badges, tiny command tags | отдельные декоративные кнопки для каждого действия |
| `teal_tamga_accent` | память, Тамга, valid target | selected badge, wisdom/memory, valid cell edge | generic magic blue glow |
| `warm_fire_edge` | активность, аул, последний лог | selected state, latest event, fire-related focus | gold-heavy royal UI |

## Component Families

### `panel_dark`

Current runtime:

```text
games/turkic-jam-2026/raw/ui/ui_panel_felt_dark_96.png
```

Use:

```text
left chat/combat log
right hero/equipment/stats panel
modal body
large dark tooltip surface
```

States:

```text
normal
active section edge
latest event warm edge
danger edge
```

Rule: one dark felt panel family reused by scale/nine-slice. Do not generate a new dark panel for every zone.

### `panel_light`

Current runtime:

```text
games/turkic-jam-2026/raw/ui/ui_panel_felt_light_96.png
```

Use:

```text
small notes
selected-card summaries
short explanation panels
```

Rule: light support surface, not a white website card.

### `card_face`

Current runtime:

```text
games/turkic-jam-2026/raw/ui/ui_card_playable_96x128.png
```

Use:

```text
all playable tile cards
```

Required reading:

```text
this is a spendable tile-placement card
not equipment
not relic
not inventory item
```

Layout:

```text
title
large tile art
short effect line
small count/cost/badge if needed
```

Rule: all playable cards share this family. Card identity changes through title/art/effect, not a new frame.

### `card_selected`

Current runtime:

```text
games/turkic-jam-2026/raw/ui/ui_card_selected_96x128.png
```

Use:

```text
selected card in hand
lifted card state
placement-ready state
```

Rule: selected state is edge/accent/lift/tint. Do not generate a different card body.

### `card_back`

Current runtime:

```text
games/turkic-jam-2026/raw/ui/ui_card_back_96x128.png
```

Use:

```text
empty hand slot
hidden/back card
future unknown card state
```

Rule: dark felt back with one fictional tamga-like mark. It must not look like a playable card.

### `button`

Current runtime:

```text
games/turkic-jam-2026/raw/ui/ui_button_dark_64.png
```

Use:

```text
compact commands only
```

States:

```text
normal
hover
pressed
disabled
```

Rule: states are runtime brightness/edge/text changes. No one-off generated buttons.

### `resource_chip`

Current runtime:

```text
games/turkic-jam-2026/raw/ui/ui_chip_resource_64.png
```

Use:

```text
resources
day
loop count
short top HUD counters
```

Rule: top HUD uses chips, not a full heavy top bar.

### `equipment_slot`

Current runtime:

```text
games/turkic-jam-2026/raw/ui/ui_slot_equipment_64.png
```

Use:

```text
weapon slot
clothes slot
tamga slot after unlock
relic slot after unlock
```

Rule: same slot family for all equipment-like holders. Locked systems should be hidden or visually locked, not explained by clutter.

### `tooltip`

Current runtime:

```text
games/turkic-jam-2026/raw/ui/ui_tooltip_dark_64.png
```

Use:

```text
card hover
selected card hint
cell preview
short current check detail
```

Rule: compact, text-first, never a permanent bottom instruction slab.

### `log_row`

Current status:

```text
runtime text + marker treatment
future optional reusable marker family
```

Use:

```text
event line
combat result
resource gain/loss
memory/tamga event
```

Rule: log is important, but must not steal map area. Latest event can get warm/fire accent.

### `selection_overlay`

Current status:

```text
runtime-only placement/valid/invalid state
future optional reusable overlay family
```

Use:

```text
selected card
valid buildable cell
invalid/no-build cell
hover preview
```

Rule: overlay is an edge/glow/marker family, not a new full tile image.

## Layout Reuse

The real gameplay layout should reuse the same component families:

| Zone | Component Families |
| --- | --- |
| top HUD | `resource_chip`, small icons, runtime text |
| left chat/combat log | `panel_dark`, `log_row`, runtime text |
| center map | map/tile art, `selection_overlay` |
| bottom card hand | `card_face`, `card_selected`, `card_back`, `tooltip` |
| right hero panel | `panel_dark`, `equipment_slot`, stat icons, runtime text |

Map remains the main object. UI supports play; it does not become the screen.

## Acceptance Checklist

Before GDD accepts a UI/art delivery:

```text
visible art is generated bitmap / painterly raster
SVG/procedural pieces are technical_only, not visible production art
existing source/runtime families were checked first
new generation, if any, is one family source sheet
no one-off regenerated buttons/cards/panels/slots
runtime filenames and dimensions are explicit
reuse map exists
contact sheet exists in gameplay context
map remains largest and readable
cards read as playable tile cards
right panel reads as hero/equipment/stats
log is present but not oversized
no Arabian fantasy drift
no real sacred/clan marks copied
```

If any point fails, the pass returns for revision.
