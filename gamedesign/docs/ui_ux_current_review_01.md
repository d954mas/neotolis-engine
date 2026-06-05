# UI/UX Current Review 01

Reference screenshot:

```text
C:/Users/ROG/YandexDisk/Скриншоты/2026-06-06_00-43-07.png
```

## Summary

Current UI/UX direction is structurally good.

The screen already matches the GDD layout:

```text
top: HUD
left: log
center: map
right: hero/storyteller panel
bottom: card hand
```

Do not redesign the whole screen. The next pass should improve hierarchy, readability and visual feedback while preserving the working layout.

## What Works

| Area | Keep |
| --- | --- |
| Main layout | Map in the center, log left, hero panel right, cards bottom is correct. |
| Top HUD | Resources and circle progress are visible immediately. |
| Circle marker | `КРУГ 4/10` is strong and centered. Good run-progress anchor. |
| Card hand | Cards at the bottom read as player's main tool. |
| Selected card state | The blue selected `Дар x3` treatment is very visible. |
| Left log | The game feels alive because events persist as text. |
| Right panel | Space for hero/stats already exists. Good for future doll/equipment/tamga. |
| Center map | Current prototype keeps gameplay board as the main visual. |

## Main Problems

| Priority | Problem | Why it matters |
| --- | --- | --- |
| P0 | Map visual hierarchy is too low | The map is the game, but the black void and block placeholders make it feel like debug UI. |
| P0 | Empty cells read as "debug dark squares" | GDD says no empty cells. Free buildable cells should show base decor and placement meaning. |
| P0 | Selected card text overflows/feels poster-like | `Дар x3` is too large and too bright compared to the rest of hand. |
| P0 | Valid placement instruction takes too much permanent space | The bottom hint behaves like a panel, but it is only temporary guidance. |
| P1 | Right hero panel lacks visual role | It has stats but no hero silhouette/doll, so it feels unfinished. |
| P1 | Left log uses repeated low-value lines | Many `Пустая клетка` lines reduce signal and make the log feel noisy. |
| P1 | Aul is text inside a block | It should be the warm visual anchor, not a labeled rectangle. |
| P1 | Top HUD chips are serviceable but flat | They work, but icons/consistent spacing would improve scan speed. |
| P2 | No battle/trigger bubble layer yet | Local events should appear near the current cell, not only in log. |

## UX Interpretation

The current screen communicates:

```text
I have resources.
The hero is auto-walking.
I have cards.
I can place a selected card somewhere near the road.
```

It does not yet strongly communicate:

```text
which cells are road_path vs road_buffer vs buildable;
which empty cells are valid for this card;
why a cell is invalid;
what the current/next road cell is;
where exactly the next tile effect will trigger;
who the hero is visually.
```

## Recommended UI/UX Direction

### Preserve Layout

Keep the existing frame:

```text
top HUD: compact
left log: narrow, persistent
center map: largest gameplay space
right hero/storyteller: status and current check
bottom hand: 5 cards + contextual hint
```

### Improve Hierarchy

Visual hierarchy target:

```text
1. current map state
2. selected card and valid placement cells
3. circle/resources/stamina
4. latest log events
5. right panel details
```

### Card Hand

Current issue: cards look like large UI blocks, and selected `Дар x3` dominates too hard.

Target:

- card size remains close to current;
- selected card gets border/glow and slight lift;
- card art/icon area above title;
- title remains readable but not giant;
- empty slots are quieter and smaller in contrast;
- permanent bottom hint is removed or collapsed;
- hover/selected-card hints appear as compact tooltip above the card or near top-center of the map;
- card interaction borrows from Slay the Spire: hover lift, selected focus, visible targeting state, easy cancel.

Possible card layout:

```text
icon/art 60%
name 25%
count/rarity 15%
```

### Slay The Spire Card Interaction Reference

Use Slay the Spire as the main UX reference for card interaction, not as a full visual skin.

What to borrow:

| Pattern | Adaptation for Song of Tamga |
| --- | --- |
| Hover lift | Card rises from the hand, scales slightly, and becomes readable without opening a modal. |
| Selected card focus | Selected card stays highlighted; non-selected cards remain visible but recede. |
| Targeting state | When a card is selected, valid map cells become the obvious targets. |
| Cancel by clicking away | Player can back out without placing the card. |
| Clear count/charges | `Дар x3` style counts should live as a small corner badge, not as giant body text. |
| Many-card readability | If the hand grows, use overlap/fan/scroll behavior instead of shrinking cards until text breaks. |

What not to copy:

- Enemy-target arrow as-is; our targets are map cells.
- Full combat-card text density; our cards are placement tools.
- Busy fantasy card frames; first playable needs utility and fast scanning.

Recommended placement interaction:

```text
normal hand
-> hover card: lift + brighten
-> compact tooltip appears above hovered card or top-center over the map
-> click card: selected card locks lifted, valid map cells glow
-> hover valid cell: projected tile ghost appears
-> click valid cell: tile placed, card consumes/decrements
-> right click / Esc / click empty UI: cancel selected card
```

For many cards later:

```text
5 cards or fewer: fixed slots
6-10 cards: slight fan/overlap with hovered card pulled forward
10+ cards: horizontal scroll or compact deck drawer, not tiny unreadable cards
```

### Log

Current issue: repeated `Пустая клетка` lines are low-value.

Target visible log:

- last 6-8 meaningful events;
- empty cells either not logged or collapsed;
- tone colors: gain, card, memory, danger, death;
- newest event visually strongest.

Example:

```text
Камень Тамги питает род: +3 Мудрость.
Дар найден: выбери карту.
Саксаул ждет у дороги.
Арын проходит 8-ю клетку.
```

### Right Panel

Current issue: it has space, but no strong "hero" read.

Target:

- top label: `Сказитель` or hero name, not both competing;
- hero silhouette/doll inside panel;
- stats grouped tightly;
- equipment/relic/tamga slots around or below the doll, closer to Loop Hero's character/equipment read;
- `Клетка 8 / 24` shown as route position;
- current check/battle bubble summary appears here only when active.

Right panel should absorb space freed from the removed bottom hint:

```text
hero doll / silhouette
weapon/tool slot
clothing/armor slot
relic slot
last_tamga slot
small inventory/resource keepsakes area
current encounter/check summary
```

For first playable, slots can be locked/empty silhouettes. The important UX is that this area promises hero progression and makes the right panel feel purposeful.

### Map

Current issue: debug blocks.

Target:

- road is continuous darker path;
- road buffer is visible no-build edge;
- buildable cells have quiet base decor;
- selected card highlights only valid cells;
- invalid cells are not red unless there is an error;
- current hero cell has warm ring/dust highlight;
- aul is warm anchor.

## Fake Shot Directions To Compare

### A. Conservative Playable Polish

Same layout, improved assets and hierarchy. Best for immediate implementation.

Changes:

- replace placeholder blocks with art assets;
- keep UI panels mostly as they are;
- add card icon/art areas;
- add valid placement glow;
- reduce log noise.

### B. Strong Board Focus

Make the center map visually larger and dim side panels slightly.

Changes:

- map gets more breathing room;
- panels become darker/less contrast;
- selected placement cells are the brightest interaction element;
- hero panel becomes compact.

Risk: may reduce readability of stats/log.

### C. Rich Card-Hand Pass

Keep current map size but make cards feel like the primary player action.

Changes:

- larger card art;
- selected card lifted;
- bottom hint becomes compact hover/selected tooltip above the card or top-center over map;
- empty slots recede;
- Slay the Spire-like interaction: hover lift, selected lock, board targeting state, easy cancel.

Risk: card hand can steal attention from map if too decorative.

## First UI Acceptance Targets

UI pass is successful if an external tester can say:

```text
I see where the hero is.
I see where the road is.
I see where I can place this card.
I see why road/buffer cells are not valid.
I see what happened recently.
I see how close I am to finishing the run.
```
