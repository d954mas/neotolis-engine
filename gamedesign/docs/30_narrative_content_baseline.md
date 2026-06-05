# 30. Narrative content baseline

Цель: дать GDD и Code единый нарративный baseline для FTUE, карт-тайлов, событий, логов, синергий и первых "вещей" без длинного лора.

Core rule:

```text
карта = тайл в руке
тайл = след, помощь, стоянка, знак или опасность в мире
игрок не колдует и не командует путником
игрок оставляет в пустыне то, что изменит будущий путь
```

## Tone Target

Это игра-сказка. Сказочность должна быть в структуре:

```text
дом -> путь -> помощь -> испытание -> след -> смерть -> знак -> наследник
```

Но строки должны оставаться игровыми:

```text
факт + смысл + число
```

## FTUE Text Baseline

| Moment | Text |
| --- | --- |
| Intro 1 | `Песок стирает следы.` |
| Intro 2 | `Путь ждет первого путника.` |
| Readiness action | `Отправиться в путь` |
| After click | `Первый путник выходит из аула.` |
| Autowalk hint | `Путь ведет его сам.` |
| First circle | `Круг 1 начат.` |

Fallback if `Путь ждет первого путника` tests unclear:

```text
У костра ждет первый путник.
```

Do not use:

```text
Огонь зовет первого путника.
```

Reason: the fire becomes an unclear magical subject. Костер is home/readiness target; путь is the call outward.

## Card-Tile Families

### P0 Core Set

| Internal ID | Player name | Role | Placement | Promise |
| --- | --- | --- | --- | --- |
| `saxaul` | Саксаул | малая помощь | `roadside` | тень и хворост на будущем проходе |
| `yurt` | Юрта | стоянка рода | `field` | безопасные Запасы за круг |
| `tamga_stone` | Камень Тамги | память | `field` | Мудрость и будущие синергии |
| `wolf_track` | Звериная тропа | риск | `roadside` | Body-проверка ради Славы |

`wolf_track` keeps its internal id. Player-facing name should be `Звериная тропа`. `Волчий след` is a visual/log detail when the tile triggers.

### P1 Expansion

| Family | Cards | Why |
| --- | --- | --- |
| Help | Колодец, Оазис | scale from small help to rare landmark |
| Shelter | Малая стоянка, Родовая стоянка | make field economy visible |
| Memory | Выветренная Тамга, Родовой знак | connect death, signs and wisdom |
| Beast danger | Стая, Охотничья тропа | turn risk into bad/good transformation |
| Desert danger | Мираж, Песчаная буря, След Бури | add Mind/Spirit pressure |

### P2 / Later

| Family | Cards | Note |
| --- | --- | --- |
| Songs | Песнь дороги, Песнь памяти, Песнь очищения | special tools, not early spam |
| Caravan traces | Караванный след, Потерянный груз | supplies with risk |
| Old places | Старый колодец, Размытый курган | use after cultural research |

## P0 Card Copy

Use these as first card texts:

| Card | Short UI text | Trigger/log |
| --- | --- | --- |
| Саксаул | `Тень и хворост у пути. Силы +1. Запасы +1.` | `Саксаул: путник находит тень и хворост. Силы +1. Запасы +1.` |
| Юрта | `Малая стоянка в пустыне. Запасы аула +2 за круг.` | `Юрта: стоянка поддерживает род. Запасы аула +2.` |
| Камень Тамги | `Знак памяти в песке. Мудрость +2.` | `Камень Тамги: знак держит память пути. Мудрость +2.` |
| Звериная тропа | `Опасный след у дороги. Проверка Body. Слава +2.` | `Звериная тропа: волчий след открыт у дороги. Проверка Body.` |

## First Choice Copy

When the first 1-of-3 choice opens:

```text
Род советуется у огня: стоянка, память или риск.
```

Cards:

```text
Юрта
Малая стоянка. Запасы аула +2.

Камень Тамги
Знак памяти. Мудрость +2.

Звериная тропа
Опасный след. Проверка Body. Слава +2.
```

This is better than:

```text
безопасность, память или риск
```

Reason: "безопасность" is a category, "стоянка" is a thing the player places.

## Event Library

### Neutral Path Events

| Event | Trigger | Text | Effect |
| --- | --- | --- | --- |
| `path_supplies` | road cell | `Путь дает роду: Запасы +1.` | supplies +1 |
| `old_tracks` | decor/road | `Старые следы ведут дальше. Мудрость +1.` | wisdom +1 |
| `wind_clears_sand` | road | `Ветер открывает кромку пути.` | reveal nearby slot |
| `quiet_night` | lap | `Ночь проходит спокойно. Силы +1.` | stamina +1 |

### Help Events

| Event | Trigger | Text | Effect |
| --- | --- | --- | --- |
| `saxaul_trigger` | saxaul linked cell | `Саксаул: путник находит тень и хворост. Силы +1. Запасы +1.` | stamina +1, supplies +1 |
| `well_trigger` | well linked cell | `Колодец: путник набирает воды. Силы +2.` | stamina +2 |
| `oasis_trigger` | oasis linked cell | `Оазис: вода возвращает силы. Силы +3. Запасы +2.` | stamina +3, supplies +2 |

### Memory Events

| Event | Trigger | Text | Effect |
| --- | --- | --- | --- |
| `tamga_stone_income` | lap/field | `Камень Тамги: знак держит память пути. Мудрость +2.` | wisdom +2 |
| `last_tamga_spawn` | death/near death | `На песке проступает Последняя Тамга.` | create last_tamga |
| `last_tamga_pickup` | heir reaches tamga | `Путник поднимает Тамгу прошлого. Мудрость +1. Слава +1.` | wisdom +1, glory +1 |
| `clan_remembers` | result | `Род хранит имя у огня. Память рода +1.` | clan_memory +1 |

### Risk Events

| Event | Trigger | Text | Effect |
| --- | --- | --- | --- |
| `beast_trail_reveal` | before check | `Песок темнеет впереди: волчий след открыт у дороги.` | show risk |
| `beast_trail_success` | body success | `Звериная тропа: проверка Body пройдена. Слава +2.` | glory +2 |
| `beast_trail_fail` | body fail | `Звериная тропа: проверка Body провалена. Силы -2. Слава +1.` | stamina -2, glory +1 |
| `mirage_success` | mind success | `Мираж рассыпается. Мудрость +1. Слава +1.` | wisdom +1, glory +1 |
| `mirage_fail` | mind fail | `Мираж сбивает с пути. Силы -1.` | stamina -1 |
| `storm_success` | spirit success | `Путник проходит бурю. Слава +3.` | glory +3 |
| `storm_fail` | spirit fail | `Буря забирает силы. Силы -3. Слава +1.` | stamina -3, glory +1 |

## Synergies And Transformations

### P0/P1 Safe Discoveries

| Input | Result | Text | Effect |
| --- | --- | --- | --- |
| Саксаул adjacent to Юрта | Малая стоянка | `Саксаул и юрта держат малую стоянку.` | supplies +3 per lap or stamina +1 on nearby pass |
| Камень Тамги adjacent to Юрта | Родовой знак | `У стоянки поднят знак рода.` | wisdom +2, clan_memory +1 on lap |
| Колодец adjacent to Юрта | Водная стоянка | `У воды встает стоянка.` | stamina +2, supplies +2 |

### Risk Transformations

| Input | Result | Text | Effect |
| --- | --- | --- | --- |
| 2 Звериные тропы adjacent | Стая | `Две звериные тропы собирают стаю.` | harder Body check, higher glory |
| Звериная тропа + Сторожевая вышка | Охотничья тропа | `Род учится читать звериный след.` | lower damage, still grants glory |
| Мираж + Камень Тамги | Видение | `Знак удерживает мираж. Риск становится знанием.` | Mind check, better wisdom reward |
| Буря + Оазис | Засыпанный источник | `Буря засыпает источник. Под песком остается награда.` | Spirit check, large reward |

### Bad Greed

| Input | Result | Text | Effect |
| --- | --- | --- | --- |
| too many risk tiles near same road arc | Тяжелый путь | `Путь тяжелеет от следов.` | +diff on checks |
| too many shelters without risk | Тихий круг | `Круг проходит тихо, но слава не растет.` | low glory pressure |

## First "Things" / Gifts

These are not full equipment yet. They can be rewards, relics, or UI placeholders for future item systems.

| ID | Name | Role | Text | Guardrail |
| --- | --- | --- | --- | --- |
| `waterskin` | Бурдюк | survival gift | `Бурдюк держит воду. Первый провал риска теряет на 1 Силу меньше.` | бытовой, safe |
| `flint` | Кремень | path gift | `Кремень бережет огонь в дороге. Саксаул дает Запасы +1.` | no ritual framing |
| `red_thread` | Красная нить | memory gift | `Красная нить помнит путь к аулу. Память рода +1 при возвращении.` | avoid sacred claim |
| `bone_whistle` | Костяной свисток | warning gift | `Свисток пугает зверя. Звериная тропа наносит на 1 меньше урона.` | simple tool, not shamanic |
| `marked_belt` | Пояс со знаком | clan gift | `Знак на поясе держит имя. Первая Тамга дает Мудрость +1.` | use fictional mark |
| `dry_cheese` | Курт | food gift | `Сухой сыр в сумке. Силы +1 при начале круга.` | research spelling/term before final UI |

Use "gift" or "thing" in docs until the item system is defined. Avoid "sacred relic" for P0.

## Naming Rules

Good card names:

```text
Саксаул
Юрта
Камень Тамги
Звериная тропа
Колодец
Оазис
Мираж
Песчаная буря
```

Good transformed names:

```text
Малая стоянка
Родовой знак
Стая
Охотничья тропа
Видение
След Бури
Засыпанный источник
```

Avoid until research:

```text
Дух волка
Священный волк
Шаманская юрта
Курган как обычный early tile
реальные родовые тамги
имена богов/духов
```

## Cultural Notes

- Тамга is safe as a fictional clan/memory sign if not copied from real clan marks.
- Wolf imagery needs restraint. In Turkic traditions the wolf can be ancestral/protective/sacred, so P0 should keep wolf as a track/danger sign, not a deity, spirit or totem.
- Saxaul is a strong first tile because it is practical: desert shrub, fuel, shade, sand-holding plant. It feels local and non-magical.

## Implementation Notes

Suggested content split:

```text
tiles.tsv          -> mechanical tile rows, internal ids
cards.tsv          -> player-facing name, short text, rarity, first_seen_circle
log.tsv            -> event_id, tone, template
synergies.tsv      -> tile_a, tile_b/shape, result_tile, effect
gifts.tsv          -> optional later item/gift system
```

MVP can keep `tiles.tsv` only, but as soon as text matters, `cards.tsv` and `log.tsv` should exist so GDD can own wording without touching mechanics.
