# 66. Visual Design Bible

Status: current practical design bible for generated visual production.

Purpose: define the target look for the game UI, cards, panels, icons, and runtime art so Art/GDD/Code do not drift into generic fantasy, flat SVG placeholders, or unrelated generated styles.

## One Sentence

`Song of Tamga` should look like a readable 2D loop-builder made from warm sand, felt, leather, woven trim, hand-painted card objects and fictional Tamga-memory marks: practical nomadic game UI, not ornate fantasy UI.

## Source Rule

Production-facing visual art uses generated bitmap / painted-source bitmap.

Allowed:

- generated bitmap material sheets;
- generated character/card/tile/icon sheets;
- local slicing, chroma cleanup, masks, nine-slice-safe framing and exact-size export;
- technical masks where the visible material is generated bitmap.

Not allowed as final-looking production art:

- pure SVG/script-looking shapes;
- flat placeholder rectangles presented as art;
- generic dark fantasy UI;
- palace/bazaar/Aladdin visual language;
- real sacred/tribal tamgas copied without research.

## Generation And Reuse Protocol

Mandatory companion document:

```text
gamedesign/docs/67_art_generation_and_reuse_protocol.md
```

Hard production rule:

```text
generate source systems, not isolated repeated UI elements
```

Correct UI workflow:

```text
one generated material/source sheet
-> reusable component families
-> exact runtime exports
-> contact sheet
-> pack/L5 evidence
```

Wrong UI workflow:

```text
generate button A
generate button B
generate another button
generate a new unrelated card frame
generate another unrelated panel
```

Before any new art pass, Art must provide:

```text
component inventory
existing generated sources to reuse
new generated source needed yes/no
runtime files touched
reuse map
risks
```

If the pass skips this inventory or delivers SVG/script-looking art, GDD rejects it.

## Current Visual References

Use these project-local references before creating new assets:

```text
gamedesign/assets/concept/pass_12_generated_ui_surfaces/pass_12_ui_material_source.png
gamedesign/assets/concept/pass_12_generated_ui_surfaces/pass_12_generated_ui_surfaces_contact_sheet.png
gamedesign/assets/concept/pass_11_generated_hud_icons/pass_11_generated_hud_icons_contact_sheet.png
gamedesign/assets/concept/pass_10_generated_wayfarer_equipment/pass_10_generated_wayfarer_equipment_contact_sheet.png
gamedesign/assets/concept/pass_9_generated_active_tiles/pass_9_generated_active_tiles_runtime_contact_sheet.png
gamedesign/assets/concept/pass_8_generated_bitmap_repaint/pass_8_generated_card_art_runtime_contact_sheet.png
tmp/visual_qa_l5_pass12_ui_surfaces.png
tmp/visual_qa_l5_pass11_hud_icons.png
tmp/visual_qa_l5_pass10_wayfarer_equipment_freshpack.png
tmp/visual_qa_l5_pass9_active_tiles.png
tmp/visual_qa_l5_pass8_cards_layout.png
```

Priority reference for UI/buttons/panels/cards:

```text
pass_12_ui_material_source.png
pass_12_generated_ui_surfaces_contact_sheet.png
tmp/visual_qa_l5_pass12_ui_surfaces.png
```

## Visual Pillars

| Pillar | Rule |
| --- | --- |
| Readability first | Every runtime asset must be readable at actual gameplay scale before it is called accepted. |
| Material, not ornament | UI surfaces use felt, leather, parchment, woven trim. Decoration is edge language, not full-frame ornament. |
| Warm desert base | Sand/ochre/clay dominate the world. Turquoise and gold are accents, not the whole palette. |
| Fictional memory marks | Tamga-like marks must remain fictional until researched. They are memory/legacy, not generic magic runes. |
| Practical nomadic UI | Panels feel like felt/leather/card materials used by a traveling clan. No ornate palace UI. |
| Generated bitmap baseline | If it is a player-facing card, tile, hero, icon, equipment or UI material, its visible art source must be generated/painterly bitmap. |

## Palette

Use this as target range, not strict CSS tokens:

| Token | Hex | Use |
| --- | --- | --- |
| `sand_base` | `#D8B56B` | map base and buildable field |
| `sand_shadow` | `#A9783C` | road, tracks, ground detail |
| `felt_dark` | `#231F18` | panels, buttons, tooltip bodies |
| `felt_light` | `#E8D6A4` | readable cards and light panels |
| `leather_dark` | `#5B321A` | equipment slots, strong UI edges |
| `woven_teal` | `#2B8C84` | selected state, Tamga, wisdom, memory |
| `ochre_gold` | `#C99A37` | resource chip edge, selected detail |
| `danger_warm` | `#8F3A25` | risk events, warning accents |
| `storm_dust` | `#B8B1A0` | storm, mirage, dusty FX |

Rule: the screen must not become a one-note beige/brown UI. Use teal and dark felt to separate gameplay categories, but never turn the game into turquoise/gold fantasy.

## UI Surface Language

### Panels

Runtime files:

```text
ui_panel_felt_dark_96.png
ui_panel_felt_light_96.png
ui_tooltip_dark_64.png
ui_button_dark_64.png
```

Rules:

- dark panels frame information without stealing focus from the map;
- light panels are for readable info surfaces only;
- borders are thin, practical, slightly worn;
- corner radius feel: 6-8 px at 96px assets;
- no huge ornamental frames.

### Cards

Runtime files:

```text
ui_card_playable_96x128.png
ui_card_selected_96x128.png
ui_card_back_96x128.png
card_art_*.png
```

Rules:

- generated card art is the main read;
- card surface is parchment/felt, not blank beige;
- selected card uses turquoise edge plus gold stitch, not neon glow;
- card back can use woven material, but if many empty slots are visible it may need a calmer variant;
- Russian card names must stay readable at bottom.

### Resource Chips

Runtime files:

```text
ui_chip_resource_64.png
icon_*_32.png
```

Rules:

- chip is a quiet material base behind a 24-32px icon and number;
- chip must not compete with icon contrast;
- use gold warmth carefully; if too loud in gameplay, reduce brightness before repainting icons.

### Equipment Slots

Runtime files:

```text
ui_slot_equipment_64.png
equip_slot_*.png
equip_*.png
```

Rules:

- slots are leather/felt holders, not reward cards;
- empty slot must feel available but not bright;
- equipped item must be readable over the slot without a heavy glow.

## Shape Language

| Area | Shape |
| --- | --- |
| Map road | packed curved strips, tracks, practical path language |
| Road buffer | low stones, stakes, broken packed edge, no-build but not a wall |
| Aul | round yurts, fire center, felt/wood, small camp first |
| Cards | vertical parchment/felt object cards |
| Right hero panel | doll + equipment, RPG readable, compact |
| Log/chat | dark practical strip, text-first |
| Tamga/memory | stone, bone, stitched mark, turquoise accent |
| Danger | track, dust, warm red/brown, readable contrast |

## Buttons And Interaction

Buttons should feel like pressed felt/leather pieces, not web buttons.

States:

```text
idle       dark felt/leather, low edge
hover      slightly lighter top edge, no glow flood
active     pressed darker center
selected   thin teal/gold edge
disabled   desaturated dark, no saturated red
invalid    short warm red/dust mark, not permanent neon
```

Rules:

- no large rounded pill buttons unless the existing UI needs text action;
- icon buttons should use generated icon art where possible;
- tooltip panels use `ui_tooltip_dark_64` language;
- action text can be simple, but the surface should stay material.

## Layout Rules

Current gameplay screen:

```text
top    HUD resource chips, circle/day/speed
left   chat/combat log
center map, road, aul, battle bubble
right  hero doll, stats, equipment/current event
bottom card hand
```

Rules:

- map remains the largest readable area;
- log is important, but must not push the map into a tiny viewport;
- card hand must have enough width for 5 cards;
- right panel is a Loop Hero/RPG-style hero panel, not a button list;
- do not put UI cards inside other UI cards.

## Runtime Acceptance

A visual pass is not final until it passes:

```text
L1 raw PNG exists
L2 pack builder finds it
L3 runtime atlas binds it
L4 it is drawn in the relevant screen/QA harness
L5 desktop screenshot proves readability at real scale
```

Current generated candidate passes with L5 QA proof:

```text
Pass 8  generated card art
Pass 9  generated active tiles
Pass 10 generated wayfarer/equipment
Pass 11 generated HUD/utility icons
Pass 12 generated UI surfaces
```

Still pending:

```text
normal gameplay screenshot after map migration
final user/GDD readability acceptance
targeted fixes for noisy cards/UI, if needed
generated world/base ground pass if map technical art remains too script-like
generated FX pass or explicit technical exception after motion QA
```

## Generation Prompt Baseline

Use this prompt family for UI material generation:

```text
hand-painted generated bitmap material sheet for a 2D roguelite loop-builder game, practical Turkic nomadic visual language, dark felt, warm leather, pale parchment felt, woven trim, muted turquoise thread, ochre gold edge wear, readable at small UI scale, no text, no symbols, no real tamgas, no palace fantasy, no vector, no SVG, no flat placeholder shapes
```

Negative prompt:

```text
arabian palace, genie lamp, flying carpet, bazaar, ornate golden frame, generic fantasy RPG chrome, neon magic, real sacred signs, text, letters, numbers, watermark, flat SVG style, pure vector shapes
```

## Do / Do Not

Do:

- use generated material as the visible UI source;
- keep edges practical and quiet;
- use teal/gold as small accents;
- preserve exact runtime sizes and filenames;
- check every asset in desktop L5 before final acceptance.

Do not:

- make broad fake-shots when exact runtime assets are needed;
- repaint everything blindly before normal gameplay screenshot;
- make card backs louder than playable cards;
- treat technical script placeholders as finished art;
- overuse ornament or culturally sensitive marks.

## Next Visual Priorities

1. Normal gameplay screenshot after map migration: review Pass 8-12 together.
2. Generated world/base ground pass if map still reads like technical grid.
3. Generated FX/motion pass or explicit technical exception if FX are readable in motion.
4. Targeted card/active tile fixes only for assets that fail actual gameplay readability.
