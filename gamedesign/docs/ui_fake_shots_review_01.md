# UI Fake Shots Review 01

## Source Images

```text
gamedesign/assets/concept/ui_fake_shot_a_conservative_playable_polish.png
gamedesign/assets/concept/ui_fake_shot_b_board_focus.png
gamedesign/assets/concept/ui_fake_shot_c_card_hand_focus.png
gamedesign/assets/concept/ui_fake_shot_d_sts_card_targeting.png
gamedesign/assets/concept/ui_fake_shot_e_turkic_identity_high.png
gamedesign/assets/concept/ui_fake_shot_f_playable_cards_hero_panel.png
```

## Recommendation

Use a hybrid:

```text
normal run screen = A Conservative Playable Polish + F identity layer
placement/card targeting state = F Playable Cards Hero Panel, with D interaction logic
optional placement zoom/attention = light B Board Focus behavior
```

Do not use C as the default run screen. It is useful only as a warning about card-hand overemphasis.

Do not use E directly. It is useful as an identity direction, but has too much title/branding, leather weight and ornamental frame density.

## A. Conservative Playable Polish

Status: best baseline for normal gameplay.

What works:

- Preserves the current prototype layout.
- Map becomes readable without changing UX.
- HUD/log/right panel/card hand all remain balanced.
- Valid placement cells are visible.
- Card hand improves without becoming a separate menu.

Risks:

- Map has too much decor noise.
- Some base decor cells look occupied.
- Cards are still somewhat decorative for first implementation.

Use for:

```text
normal run
autowalk
reading log
watching tile triggers
```

## B. Board Focus

Status: good reference for placement attention, not full default.

What works:

- Board is clearly the game.
- Side panels recede.
- Valid cells become the brightest interaction.
- Current hero cell is easy to find.

Risks:

- Log and hero panel become too secondary.
- Long-term stat readability may suffer.

Use for:

```text
card selected
placement preview
camera/board focus mode
```

## C. Card Hand Focus

Status: useful exploration, not recommended as default.

What works:

- Card hand feels important.
- Selected card has stronger affordance.
- Empty slots recede.

Risks:

- Cards can steal attention from the map.
- The game can start feeling like a menu instead of a board.
- Bottom UI can become too tall.

Use only for:

```text
rare card choice
deck/reward selection screen
not normal run
```

## D. STS Card Targeting

Status: best placement interaction target.

What works:

- Selected card is lifted and readable.
- Other cards remain visible but recede.
- Valid map cells are obvious.
- Hovered cell can show a tile ghost.
- Bottom hint becomes contextual instead of a long instruction line.
- This matches the user's Slay the Spire reference without copying the whole STS screen.

Risks:

- Selected card currently overlaps too far into the left log area.
- It must not become a giant modal card.
- Targeting should be map-cell based, not enemy-arrow based.

Use for:

```text
hover card
select card
choose valid build cell
confirm placement
cancel placement
```

## E. Turkic Identity High

Status: identity reference, not implementation target.

What works:

- Stronger Turkic/nomadic identity.
- Tamga marks, woven trim and warm palette make the game more memorable.
- Aul/map/card relationship is visually richer.

Problems:

- Gameplay HUD should not show the game title.
- Too leather-heavy; cards look like inventory/relic plaques.
- Ornament density is too high for first playable.
- Some frames feel generic premium fantasy UI.

Use for:

```text
motif library
palette push
tamga/icon/border inspiration
```

Do not copy:

```text
title in HUD
heavy black leather card faces
full decorated frames around every panel
```

## F. Playable Cards + Hero Panel

Status: accepted visual direction for the next UI pass.

What works:

- No game title in HUD.
- Selected `saxaul` reads as a spendable card: light face, art, small count badge.
- Large bottom instruction panel is gone.
- Tooltip moved to top-center over the map.
- Right panel finally has Loop Hero-like purpose: hero doll, equipment slots, relic, last tamga, current cell, storage.
- Turkic identity is present through trim, tamga icons, warm palette and felt/leather materials.

Problems:

- Selected card is too large and overlaps into the log area.
- Right hero panel is over-detailed for MVP; reduce slot count and hide locked systems until they appear in play.
- Borders are still too heavy and should be 30-40% lighter.
- Empty card backs still feel more like relic/inventory plaques than cards.
- `Хранилище` block is unclear in gameplay HUD and should move out of the normal run screen.
- `Реликвия` and `Последняя Тамга` need locked/empty states or should appear only after the relevant event.

Use for:

```text
placement state target
hero panel target
card material direction
tooltip placement target
```

Implementation simplification:

```text
selected card lift: yes
selected card max height: keep inside bottom zone
tooltip top-center: yes
right panel: hero doll + 4 equipment slots + relic + last_tamga + stats
storage/keepsakes: post-run or inter-run screen, not gameplay HUD
```

## F Decision Update

Accept F as the UI visual direction for the next pass:

```text
warm Turkic nomadic identity
brighter map and card faces
no gameplay title in HUD
compact top-center tooltip
Loop Hero-like right hero panel
bottom cards read as spendable placement tools
```

But simplify the right panel for MVP.

### Right Panel MVP

Always visible:

```text
hero doll / silhouette
hero archetype + "Путник рода"
Body / Mind / Spirit
Силы
current cell
current check / current danger summary when active
```

Visible as locked placeholders only if there is room:

```text
equipment slots: 3-4 small empty slots
```

Hidden until unlocked/event:

```text
Реликвия
Последняя Тамга
```

Remove from normal gameplay HUD:

```text
Хранилище
рюкзак
keepsakes row
```

These belong to death/result/inter-run screens, not the normal run. In normal gameplay they add confusion and compete with the card hand.

### Naming

Use:

```text
Реликвия -> Реликвия пути
Последняя Тамга -> Тамга прошлого
Хранилище -> Сумка рода / Хранилище аула, but only between runs
```

First playable rule:

```text
do not show a named slot until the player has seen the system
```

Before unlock, a slot may be visually present but unnamed/disabled:

```text
locked icon
small "позже" state only on hover, not permanent text
```

## Final Interaction Spec

### Normal Run

```text
cards sit in bottom hand
hovered card lifts slightly
map keeps normal brightness
log shows latest meaningful events
hero continues walking unless a decision pause is active
```

### Card Hover

```text
hovered card rises 8-14 px
card border brightens
card art/title/effect become readable
no valid-cell glow yet unless UX wants preview-on-hover
```

### Card Selected

```text
selected card locks lifted
selected card max size stays inside bottom UI zone
other cards dim to 70-80% brightness
valid build cells glow on map
road_path, road_buffer, aul_core stay unhighlighted
bottom hint becomes short: "Выберите клетку"
```

### Cell Hover While Card Selected

```text
valid cell glow strengthens
transparent ghost of active tile appears on cell
linked road cell may get a subtle pulse for roadside cards
right panel can show one-line effect preview
```

### Placement Confirmed

```text
tile sprite lands on cell
fx_tile_placed plays
card count decreases or card leaves hand
valid highlights disappear
log adds card_placed line
```

### Cancel

```text
right click, Esc, or click outside valid target cancels selected card
card returns to hand
valid highlights disappear
no log entry
```

## Card Hand Rules

For first playable:

```text
5 slots max
selected card can lift but should not overlap the left log
empty slots stay dark and quiet
counts/charges use small corner badges
card title never uses giant poster text
```

Tooltip rule:

```text
no permanent bottom instruction panel
card hover: tooltip above card
card selected: tooltip top-center over map or near selected target area
cell hover: tooltip becomes effect preview
```

For later many-card hand:

```text
6-10 cards: fan or slight horizontal overlap
hover pulls one card forward
selected card locks forward
10+ cards: scrollable hand or compact deck drawer
```

## Implementation Priority

| Priority | Feature |
| --- | --- |
| P0 | Hover lift for cards. |
| P0 | Selected card lock state. |
| P0 | Valid placement cell highlight. |
| P0 | Cancel selected card with Esc/right-click/outside click. |
| P1 | Ghost tile preview on hovered valid cell. |
| P1 | Linked road-cell pulse for `roadside` cards. |
| P1 | Small count badge for `Дар x3` / charges. |
| P2 | Fan/overlap behavior for larger hands. |
