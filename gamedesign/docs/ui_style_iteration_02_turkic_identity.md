# UI Style Iteration 02 - Turkic Identity

## Problem

Fake-shots A-D improved structure and readability, but the visual identity is still too generic.

Current issue:

```text
clean dark fantasy UI
some desert map art
not enough Turkic nomadic identity
not enough color/brightness contrast
too much neural-polish softness
```

The UI should feel like `Song of Tamga`, not a generic roguelite prototype with desert art.

Iteration E found the opposite edge:

```text
identity improved
but title/branding is unnecessary in gameplay
leather-heavy card hand makes cards feel like inventory/relics
card spend/play affordance is weaker than needed
```

## Direction

Keep the UX structure:

```text
top HUD
left log
center map
right hero/storyteller
bottom card hand
```

Change the visual language:

```text
dark panels -> felt/leather/wood surfaces
generic icons -> tamga-like pictograms
plain borders -> restrained Turkic geometric trim
blue fantasy glow -> teal tamga / warm fire / ochre dust accents
generic card frames -> small felt-card / carved-tag language
```

Important correction:

```text
gameplay screen does not need the game title
cards must read as playable/spendable cards, not leather inventory plaques
```

## Turkic Visual Motifs To Use

| Motif | Use |
| --- | --- |
| `tamga marks` | resource icons, selected-card badge, memory events, last tamga |
| `felt yurt trim` | card borders, right-panel top trim, HUD chip edges |
| `woven geometric bands` | very thin separators, not full wallpaper |
| `wood / bone tags` | card count badges, compact buttons |
| `leather straps` | card hand slots, hero equipment/tamga slots |
| `campfire warmth` | aul anchor, selected/active state, latest log event |
| `turquoise enamel` | wisdom/memory accent, selected valid target |
| `clan red` | danger/risk/glory accent, not global theme |

## What To Avoid

- full carpet background;
- ornate frame around every panel;
- gold-heavy UI;
- arabian fantasy;
- palace/bazaar/lamp language;
- magical MMO spellbook UI;
- generic blue neon sci-fi;
- unreadable decorative text.

## Brightness And Color Fix

The game needs more identity and brightness without losing readability.

Palette adjustments:

| Role | Current issue | Direction |
| --- | --- | --- |
| Panels | too dark/generic | deep indigo/charcoal base with warm felt edge |
| Selected card | too blue fantasy | warm ochre card + teal tamga badge + green placement edge |
| HUD chips | generic pills | small carved/felt chips with icon + number |
| Log | too plain | event dots as tamga/diamond marks, latest event warm |
| Right panel | empty silhouette card | yurt-felt portrait panel with subtle woven trim |
| Map | good warmth | reduce noise, keep aul/fire as bright anchor |

Suggested colors:

| Token | Hex |
| --- | --- |
| `ui_deep_indigo` | `#111827` |
| `ui_panel_felt` | `#1D2333` |
| `ui_panel_warm_edge` | `#6F5532` |
| `felt_light` | `#E6D8B8` |
| `ochre_card` | `#C79A4C` |
| `fire_gold` | `#F4C95D` |
| `tamga_teal_bright` | `#35B8A6` |
| `clan_red_bright` | `#C95A3A` |
| `valid_green` | `#9AD66F` |

## UI Shape Language

### Panels

Panels should not be flat web cards.

Use:

```text
slightly squared panels
1-2 px warm edge
subtle felt/leather grain
small woven band only at section header
```

Do not:

```text
large ornate frame
rounded mobile-card look
blue-glass fantasy panel
```

### Cards

Cards should feel like playable, spendable cards first; Turkic identity is secondary trim.

Use:

```text
light parchment/felt face for playable cards
dark felt/leather back only for card backs and empty/unknown slots
small tamga corner badge
clear count/charge/cost badge
short title
large tile art
tiny effect line
selected card lifts like STS
```

Card back:

```text
deep indigo leather/felt
single teal tamga mark
thin ochre trim
```

Avoid for card faces:

```text
heavy black leather
inventory plaque look
relic/equipment slot look
large ornamental frame
```

Spendable-card cues:

```text
small number badge in top-left or top-right
card stack/deck count nearby
selected card lifted from hand
other cards remain visible but dim
valid map cells glow after selection
card leaves/decrements after placement
```

### Log

Chronicle should feel like a storyteller's record, but remain utilitarian.

Use:

```text
section title: Летопись
event markers: diamond/tamga dots
newest event brighter
tone colors: gain green, memory teal, danger red, death ash
```

No long parchment panel for the whole log yet. It would fight the map.

### Hero Panel

Right panel should feel like a storyteller/hero status surface:

```text
portrait silhouette inside felt panel
small vertical stat icons
one tamga/relic slot
current cell line
current check/battle bubble summary when active
```

Add a restrained woven header strip, not a full decorated frame.

## Fake-Shot E Prompt

```text
16:9 gameplay UI mockup for a 2D roguelite loop-builder game called Song of Tamga, Turkic nomadic visual identity pass. Keep existing layout: top compact HUD, left chronicle log, center desert loop map, right storyteller hero panel, bottom card hand. Add distinctive Turkic nomadic UI motifs without clutter: felt and leather panel surfaces, restrained woven geometric trim, tamga-shaped icons, carved wood/bone badges, bright teal tamga accents, warm campfire gold, muted clan red. Center map: warm aul with yurts and fire, packed dirt road loop, clear no-build road buffer, base decor desert cells, valid placement cells glowing. Bottom hand: selected Saxaul card lifted like Slay the Spire interaction, card face like felt/parchment with saxaul art, small corner count badge, other cards dark felt backs with teal tamga marks. Top HUD chips use icons and brighter numbers. Left log uses tamga/diamond event marks. Right panel has hero silhouette framed by subtle felt/yurt trim. Brighter and more memorable than generic dark fantasy UI, but still readable and utilitarian. Avoid arabian palace, genie lamp, flying carpet, ornate bazaar, golden dome, Aladdin, lush tropical oasis, full carpet wallpaper, gold-heavy UI, ornate magic UI, photorealism, neural over-polish, website layout, watermark.
```

## Fake-Shot F Prompt

```text
16:9 gameplay UI mockup for a 2D roguelite loop-builder, Turkic nomadic identity but cleaner and more playable-card focused. No game title in the gameplay HUD. Keep layout: top compact HUD, left chronicle log, center desert loop map, right expanded hero panel, bottom card hand. UI panels use restrained felt/leather surfaces with thin woven geometric trim and tamga-shaped icons, but avoid heavy frames. Center map: warm aul with yurts and fire, packed dirt road loop, visible no-build road buffer, quiet base decor cells, valid placement cells glowing. Bottom hand: selected Saxaul card is clearly a spendable playable card, not a leather plaque: light parchment/felt card face, large saxaul art, short title, small count/charge badge in corner, selected card lifted like Slay the Spire, other cards dim and recede, card backs use dark felt with teal tamga. Remove the large bottom-right instruction panel; instead show a small compact tooltip above the selected card or top-center over the map saying choose a cell. Use the freed bottom/right space to extend the hero panel like Loop Hero: hero doll silhouette, equipment slots, relic slot, last tamga slot, small inventory/keepsake slots, stats, current cell. Top HUD chips use icon + number only, brighter colors, no branding title. Left log uses small tamga/diamond event markers. Bright, memorable, Turkic nomadic, but readable and utilitarian. Avoid game title text, large bottom instruction panel, arabian palace, genie lamp, flying carpet, ornate bazaar, golden dome, Aladdin, lush tropical oasis, full carpet wallpaper, gold-heavy UI, ornate magic UI, black leather card faces, inventory plaque cards, photorealism, neural over-polish, website layout, watermark.
```

## Hero Panel Expansion

The permanent bottom-right hint area is not worth the screen space.

Replace it with:

```text
right panel extends lower
hero doll / silhouette is larger
equipment slots around doll
current cell / current check summary
```

For MVP, do not show all future meta systems at once.

Always visible:

```text
hero doll
archetype / Путник рода
Body / Mind / Spirit / Силы
current cell
current check summary
```

Locked or hidden until event:

```text
Реликвия пути
Тамга прошлого
```

Remove from normal gameplay HUD:

```text
Хранилище
рюкзак
small keepsake row
```

Those belong to death/result/inter-run screens. If they appear in the run HUD before being taught, the player reads them as active inventory and gets confused.

Tooltip behavior:

```text
card hover -> small tooltip above card
card selected -> small tooltip top-center over map or above selected card
cell hover -> tooltip changes to effect preview
no permanent bottom instruction panel
```

## Acceptance For Iteration 02

This direction works if the viewer can say:

```text
this is a Turkic nomadic clan game
the UI still reads quickly
the map remains the main object
the cards feel like tile-placement tools
the selected card/valid cells are obvious
the right panel shows current run information, not unexplained meta inventory
```
