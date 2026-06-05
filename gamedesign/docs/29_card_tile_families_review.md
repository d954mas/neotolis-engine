# 29. Card-tile families review

Цель: уточнить контент карт после проверки Loop Hero-логики и текущих `tiles.tsv`/GDD.

## GDD decision

Принято для P0:

```text
internal id = wolf_track
player-facing card/tile name = Звериная тропа
trigger/log text = Волчий след открыт у дороги
placement = roadside
role = Body risk, Слава
```

Причина: пользователь ставит карту-тайл на buildable-клетку. `Тропа` звучит как место/маршрут риска, который можно поставить. `След` звучит как уже найденный маркер, поэтому оставляем его для визуального/логового проявления на road cell.

Первый выбор:

```text
Юрта
Камень Тамги
Звериная тропа
```

Волк допустим как мотив степи/полупустыни и как важный тюркский образ, но в P0 не делаем его сакральным существом, тотемом или мифическим боссом.

Ключевое решение проекта:

```text
карта = тайл в руке
игрок ставит карту на свободную buildable-клетку
после постановки карта становится постоянным следом/объектом/опасностью в мире
```

Значит, вопрос не в том, "карта или тайл". Вопрос в том, чтобы каждый тайл после постановки:

- имел понятную причину существовать в пустыне;
- работал не только как одноразовое число;
- мог масштабироваться через соседство, форму, круги, риск или трансформацию;
- поддерживал чувство игры-сказки.

## Current Issue

Текущий набор MVP:

| ID | Name | Current function | Issue |
| --- | --- | --- | --- |
| `saxaul` | Саксаул | Малая помощь у дороги. | Хороший P0. Понятно, почему игрок ставит. |
| `oasis` | Оазис | Сильная помощь. | Хорошо как редкая карта, не первая. |
| `yurt` | Юрта | Field support, запасы. | Нужна ясная разница между "Юрта" и "аул". |
| `tamga_stone` | Камень Тамги | Field memory, wisdom. | Хорошая тема, но нельзя делать generic magic stone. |
| `wolf_track` | Волчий след | Roadside Body check, glory. | Слово "след" звучит как уже найденный маркер, а не как объект, который игрок ставит. |
| `mirage` | Мираж | Field Mind check. | Хороший риск, но легко спутать с Оазисом визуально. |
| `storm` | Песчаная буря | Field Spirit check. | Сильная опасность, лучше не слишком рано. |

Главный слабый элемент для первого выбора: `Волчий след`.

Он атмосферный, но игрок может спросить:

```text
Почему я сам ставлю волчий след?
Это след лапы, тропа зверя, опасность, приманка или будущий бой?
```

Для сказочного loop-builder это надо объяснить через тайл: игрок не "создает волка", а отмечает/открывает опасную звериную тропу в пустыне.

## Recommended Reframe

Use this split:

| Layer | Term | Role |
| --- | --- | --- |
| Card/tile name | `Звериная тропа` or `Волчья тропа` | То, что игрок ставит на buildable-клетку. |
| Visual detail | волчьи следы, царапины, темная пыль | Как тайл читается на карте. |
| Log/event text | `Волчий след открыт у дороги.` | Что происходит при срабатывании. |
| Upgraded danger | `Стая` | Плохая синергия от жадности. |
| Controlled variant | `Охотничья тропа` | Синергия с контролем риска. |

This keeps the strong wolf image but makes the placed card more legible.

## Options For First Risk Tile

### Option A: Keep `Волчий след`

```text
Name: Волчий след
Placement: roadside
Effect: Body check when traveler passes linked road cell
Reward: Слава +2
Failure: Силы -2, partial Слава
```

Pros:

- short and atmospheric;
- asset already reads as paw marks;
- easy to keep current config.

Cons:

- less clear why player places a "след";
- weaker as a scalable card family;
- can feel like a marker, not a buildable tile.

Use only if speed matters more than clarity.

### Option B: Rename To `Волчья тропа`

```text
Name: Волчья тропа
Placement: roadside
Effect: Body check on linked road cell
Reward: Слава +2
Failure: Силы -2, partial Слава
Visual: paw marks and scratches crossing a narrow dust path
```

Pros:

- clearer as a tile the player can place;
- still keeps wolf danger;
- scales naturally: two wolf trails can become a `Стая`.

Cons:

- more directly wolf-coded, so cultural guardrails still apply;
- needs visual distinction from ordinary `decor_tracks`.

Good P0 candidate.

### Option C: Rename To `Звериная тропа`

```text
Name: Звериная тропа
Placement: roadside
Effect: Body check on linked road cell
Reward: Слава +2
Failure: Силы -2, partial Слава
Visual: paw marks, claw cuts, disturbed sand
```

Pros:

- safest culturally;
- scalable to wolves later without committing too early;
- clearer that this is a path of danger, not a single footprint.

Cons:

- less iconic than wolf;
- slightly more generic.

Best narrative-safe P0 candidate.

### Option D: Move `Волчий след` To Event Text Only

```text
Card/tile: Звериная тропа
Trigger log: Волчий след открыт у дороги.
```

Pros:

- keeps the phrase `Волчий след`;
- avoids the weirdness of placing a footprint;
- lets art show paw marks while the system name stays scalable.

Cons:

- requires one extra naming layer in UI/content.

Recommended if we introduce `cards.tsv`.

## Card-Tile Families

Instead of a flat list of 7 one-off cards, build families.

### 1. Help On The Path

| Tier | Tile | Function |
| --- | --- | --- |
| P0 | Саксаул | Small roadside help: Силы +1, Запасы +1. |
| P1 | Колодец | Roadside recovery, less supplies than Oasis. |
| P1/P2 | Оазис | Strong rare help, may support transformations. |

Why it scales:

```text
small help -> reliable recovery -> rare landmark
```

### 2. Clan Shelter

| Tier | Tile | Function |
| --- | --- | --- |
| P0/P1 | Юрта | Field support, Запасы per circle. |
| P1 | Малая стоянка | Transformation from Юрта + Саксаул. |
| P2 | Родовая стоянка | Larger field support, boosts nearby memory/help tiles. |

Risk:

`Юрта` near danger should not always be safe. It can create choices: protect, exploit, or risk.

### 3. Memory And Tamga

| Tier | Tile | Function |
| --- | --- | --- |
| P0 | Камень Тамги | Field memory, Мудрость. |
| P1 | Выветренная Тамга | Remnant/older memory, weaker but safer. |
| P2 | Родовой знак | Transformation with Юрта or multiple Tamga stones. |

Why it scales:

```text
single memory -> old memory -> clan-level sign
```

### 4. Beast Danger

| Tier | Tile | Function |
| --- | --- | --- |
| P0 | Звериная тропа / Волчья тропа | Roadside Body risk, Слава. |
| P1 | Стая | Bad synergy from too many beast trails. Higher risk/reward. |
| P1 | Охотничья тропа | Controlled version with watchtower/hunter support. |

Why it scales:

```text
one dangerous trail -> greed creates pack -> planning turns danger into hunt
```

### 5. Desert Deception

| Tier | Tile | Function |
| --- | --- | --- |
| P1 | Мираж | Field Mind risk, Мудрость/Слава. |
| P1/P2 | Видение | Мираж + Камень Тамги, risk becomes knowledge. |
| P2 | Ложный путь | Bad synergy if too many mirages distort nearby road slots. |

### 6. Wind And Storm

| Tier | Tile | Function |
| --- | --- | --- |
| P1 | Песчаная буря | Field Spirit risk, high Слава. |
| P2 | След Бури | Remnant after storm, lower risk, memory/resource effect. |
| P2 | Засыпанный источник | Буря + Оазис, danger plus big reward. |

## First Choice Review

Current first choice:

```text
Юрта
Камень Тамги
Волчий след
```

Problem:

- good strategic labels for designers;
- for player, two field concepts and one risk marker may feel abstract;
- not enough obvious map consequence.

Better first choice variants:

### Variant 1: Safe/Memory/Risk

```text
Юрта
Камень Тамги
Звериная тропа
```

Best if we want to keep current structure.

### Variant 2: Help/Memory/Risk

```text
Колодец
Камень Тамги
Звериная тропа
```

Cleaner player read:

```text
heal
memory
risk
```

But it delays Юрта/field economy.

### Variant 3: Shelter/Trail/Sign

```text
Юрта
Звериная тропа
Камень Тамги
```

Same cards as current, but UI copy should present them as:

```text
Стоянка
Опасная тропа
Знак памяти
```

This is more сказочно and less systemic.

## Recommended P0 Decision

For first playable:

```text
Keep card = tile.
Keep first guaranteed card = Саксаул.
Rename or alias `wolf_track` from `Волчий след` to `Звериная тропа` in player-facing copy.
Use `Волчий след` as log/visual detail.
First choice: Юрта / Камень Тамги / Звериная тропа.
```

If changing IDs is expensive, keep `wolf_track` internally:

```text
id = wolf_track
display_name = Звериная тропа
trigger_text = Волчий след открыт у дороги.
```

## Text Drafts

Card short text:

```text
Саксаул: тень и хворост у пути. Силы +1. Запасы +1.
Юрта: малая стоянка в пустыне. Запасы аула +2 за круг.
Камень Тамги: знак памяти в песке. Мудрость +2.
Звериная тропа: опасный след у дороги. Проверка Body. Слава +2.
```

Trigger text:

```text
Саксаул: путник находит тень и хворост. Силы +1. Запасы +1.
Юрта: стоянка поддерживает род. Запасы аула +2.
Камень Тамги: знак держит память пути. Мудрость +2.
Звериная тропа: волчий след открыт у дороги. Проверка Body.
```

Success/fail:

```text
Путник проходит звериную тропу. Слава +2.
Звериная тропа ранит путника. Силы -2. Слава +1.
```

Avoid for now:

```text
Дух волка испытывает путника.
Волк рода зовет героя.
Священный волк принимает жертву.
```

These may be useful later only after cultural research.
