# UI Hero Panel Reference Review

## References

- Loop Hero screenshots / MobyGames: https://www.mobygames.com/game/160063/loop-hero/screenshots/
- Loop Hero screenshots / SteamDB: https://steamdb.info/app/1282730/screenshots/
- Kingdom Loop screenshots / SteamDB: https://steamdb.info/app/3541000/screenshots/
- Kingdom Loop Steam page: https://store.steampowered.com/app/3541000/

## Current Decision

`Last Tamga` is not a permanent inventory slot.

Rule:

```text
Last Tamga appears on the map
-> next heir reaches it
-> pickup resolves it
-> reward is applied
-> no permanent Last Tamga slot in hero panel
```

If later design wants a physical Tamga item, it should enter the backpack/inventory as a normal item. Do not reserve a named `Last Tamga` slot in the first playable HUD.

## Loop Hero Hero Panel

Loop Hero's expedition UI is built around a constant equipment/inventory flow:

```text
hero auto-walks and auto-fights
loot constantly drops
player compares equipment
player replaces gear while the run continues
inventory pressure is part of the loop
```

What its side panel does well:

| Pattern | Why it works |
| --- | --- |
| Always-visible hero/equipment area | The player constantly receives gear and needs a place to compare it. |
| Equipment slots around/near hero | The player understands "this belongs on the hero." |
| Inventory list/grid nearby | New loot has a temporary home before replacement/discard. |
| Stats tied to gear | Changing equipment immediately affects hero power. |
| Compact but dense | It supports repeated decisions without opening a separate screen. |

What we should borrow:

```text
right panel has purpose, not just portrait + stats
hero doll/silhouette anchors equipment/status
small slots can show future progression
current stats are near the hero
current event/check can live in this area
```

What we should not copy blindly:

```text
constant loot inventory pressure
large backpack grid in first playable
many unexplained item slots
full equipment management before items exist
```

Reason: first playable is about cards, placement, road effects, death/inheritance. It is not yet about gear churn.

## Kingdom Loop Hero Panel / UI

Kingdom Loop presents itself as a strategic roguelike with deckbuilding, kingdom management, heroes, equipment/artifacts, army and turn-based battles. Steam describes heroes, equipment and artifacts as part of a broader army/combat system.

Useful patterns:

| Pattern | Usefulness |
| --- | --- |
| Modern readable fantasy panels | Good reminder to make panels clearer/brighter than old debug UI. |
| Hero as strategic commander | Relevant if our hero panel later gains traits/relics. |
| Deckbuilding + kingdom management | Good reference for separating card/kingdom systems. |
| Items/artifacts | Useful later, once relics are real. |

What not to borrow:

```text
army/unit management emphasis
turn-based battle UI
generic fantasy hero framing
too many systems visible early
```

Reason: our MVP has one heir walking the road. The panel should not imply army management or tactical combat.

## Our Hero Panel Target

For first playable, the right panel should communicate:

```text
who is walking
how close they are to death
what their core stats are
where they are on the loop
what is happening/checking now
```

It should hint at future progression, but not expose unexplained systems.

## MVP Right Panel Layout

Recommended MVP content:

```text
Header: hero role/name, e.g. "Арын" or "Наследник"
Hero doll/silhouette
Stats: Тело / Ум / Дух / Силы
Route: Клетка 8 / 24
Current event/check area
Small gear row: 3-4 empty/locked silhouettes max
```

Do not show:

```text
Хранилище
Рюкзак
Реликвия as an active named system
Последняя Тамга slot
large inventory grid
```

If we want a future hint:

```text
one small locked row below hero, no labels
tooltip: "Предметы появятся позже" only if needed
```

But for first playable, even that can wait.

## Post-MVP / Later Right Panel

Add only when systems exist:

| System | Where it should live |
| --- | --- |
| Gear/equipment | right panel, Loop Hero-like slots |
| Relics | right panel or post-run result screen |
| Backpack/storage | separate inventory screen or inter-run screen |
| Aul storage | death/result/inter-run aul screen |
| Last Tamga item | normal backpack item only if design changes it into an item |

## Revised F Direction

Keep from fake-shot F:

```text
stronger Turkic visual identity
right panel has hero doll
equipment-like slots exist as visual language
top-center tooltip replaces bottom instruction panel
cards are light playable cards, not leather plaques
```

Remove or reduce:

```text
Хранилище block
Реликвия block
Последняя Тамга block
too many right-panel slots
heavy borders
selected card overlap into log
```

## Prompt For Next Fake-Shot G

```text
16:9 gameplay UI mockup for a 2D roguelite loop-builder, revised hero panel. No game title in HUD. Keep layout: top compact HUD, left chronicle log, center desert loop map, right hero panel, bottom card hand. Turkic nomadic identity: restrained felt/leather panels, thin woven geometric trim, tamga icons, warm fire gold, teal memory accents. Center map: warm aul, road loop, visible road buffer, quiet buildable decor cells, valid placement glow. Bottom hand: selected Saxaul is a light spendable playable card with art and small count badge; other cards dim; no large bottom instruction panel; compact tooltip top-center over map. Right panel: simplify like Loop Hero but MVP-focused: hero doll/silhouette, 3-4 empty equipment/tool slot silhouettes, stats Тело Ум Дух Силы, current cell, current event/check summary. Do not show backpack, storage, relic block, or Last Tamga slot. Make panel purposeful but not inventory-heavy. Brighter memorable Turkic UI, readable and utilitarian. Avoid game title, backpack grid, storage, relic label, Last Tamga slot, arabian palace, genie lamp, flying carpet, ornate bazaar, golden dome, Aladdin, gold-heavy UI, black leather card faces, photorealism, website layout, watermark.
```

## Implementation Note

If code already has a right panel, first pass can use placeholder slots:

```text
slot_weapon_tool
slot_clothing
slot_charm
slot_empty_locked
```

They are visual affordances only until item systems exist. No gameplay inventory should be implied yet.
