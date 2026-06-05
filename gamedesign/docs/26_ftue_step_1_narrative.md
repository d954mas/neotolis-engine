# 26. FTUE step 1 narrative spec

Цель: сделать первые 6 секунд и первый клик сильными, понятными, сказочными и культурно аккуратными.

Step 1 не объясняет мифологию, но должен сразу дать чувство сказки. Это сказка не про дворец и чудо из воздуха, а про родовой огонь, путь, песок, след и первого путника.

Он должен доказать игроку:

```text
есть дом
есть род
есть путь
путник выходит сам
игрок начинает путь через аул, а не управляет ногами героя
```

## Decision

Production baseline:

```text
Песок стирает следы.
Путь ждет первого путника.

[Отправиться в путь]
```

После клика:

```text
Первый путник выходит из аула.
Путь ведет его сам.
```

Reason:

- первая строка дает тему следа и памяти;
- вторая строка дает сказочный импульс через путь, который уже ждет за аулом;
- readiness action ясный и не перегружен лором;
- строка "Путь ведет его сам" снимает ожидание WASD/click movement.
- сказочность держится в образах, а не в длинном объяснении мира.
- костер остается знаком дома; движение наружу задают тропа и путь.

## Fairy-Tale Feeling

Первый шаг должен ощущаться как начало сказки:

```text
у рода есть огонь
у огня есть путник
у путника есть путь
у пути останется знак
```

Но в первые 6 секунд нельзя рассказывать всю метафизику Тамги. Сказочность должна быть в постановке:

- темнота и песок открывают уже существующий аул;
- огонь становится первым теплым знаком;
- путник не спавнится, а поднимается у костра;
- игрок нажимает у родового центра;
- после клика путь будто принимает путника, но это все еще readable gameplay.

## Timing

| Time | Visual | Text | Interaction |
| --- | --- | --- | --- |
| 0.0 | Темнота, слышен ветер. Песок движется почти сразу. | - | input ускоряет reveal |
| 0.5 | В темноте появляется песок/пыль. | `Песок стирает следы.` | input ускоряет reveal |
| 1.5 | Виден слабый теплый огонь. | - | input ускоряет reveal |
| 2.5 | Свет костра открывает 2-3 юрты и утоптанную землю. | `Путь ждет первого путника.` | input ускоряет reveal |
| 3.5 | Камера чуть шире, видна кромка дороги. | - | input ускоряет reveal |
| 4.5 | У костра виден путник. Костер мягко пульсирует. | `Отправиться в путь` у pointer. | клик по аулу/костру или Space |
| <= 6.0 | Путник делает первый шаг от костра. | `Первый путник выходит из аула.` | gameplay-state, locked input |
| after 2-3 steps | Путник идет к road_entry сам. | `Путь ведет его сам.` | locked input |

## Readiness Action

Игрок нажимает:

```text
костер
аул
Space
```

Это не команда путнику и не управление движением. Это подтверждение готовности начать путь от имени рода.

UI hint:

```text
animated pointer над аулом/костром
мягкий bounce/pulse раз в 0.8-1.0 сек
подпись: "Отправиться в путь"
```

Pointer должен быть заметным, но не должен превращать сцену в мобильную рекламную подсказку. Если визуально палец слишком выбивается из тона, допустим вариант "малый указатель + пульс костра", но текст остается обязательным.

## Visual Brief

Кадр должен ощущаться так: аул не возникает магически, он уже был в пустыне. Тьма, песок, ветер и свет костра постепенно открывают его игроку.

Required elements:

- темный экран не статичен;
- песок/ветер видны почти сразу;
- костер как первый теплый фокус;
- 2-3 юрты, не город и не дворец;
- утоптанная земля, мешки/связки, простые вещи рода;
- кромка кольцевой дороги;
- первый путник у костра;
- после клика путник выходит сам.

Forbidden in step 1:

- magical spawn of the aul;
- glowing ritual circle;
- palace, dome, bazaar, lamp, genie language;
- huge oasis as first hope signal;
- long modal tutorial;
- personal name for the first traveler before the player understands the loop.

## Text Variants

### Clear

```text
Песок открывает аул.
У костра ждет первый путник.
Отправиться в путь
```

Use if playtests show confusion.

### Production Baseline

```text
Песок стирает следы.
Путь ждет первого путника.
Отправиться в путь
```

Use as production baseline if we want a stronger сказочный tone. It stays clear because the action points toward the road. Avoid wording where fire "calls" the traveler; it reads as unclear magic.

### More Game-Direct

```text
Аул - начало пути.
Путник выйдет сам.
Начать путь
```

Use if players try to control movement too early.

### More Poetic

```text
Песок стирает следы.
Род хранит имена у огня.
Отправиться в путь
```

Use if the game needs a stronger сказочный tone from frame one. It is stronger in mood but less tied to visible action.

## Why "Род хранит имена" Is Reserved

`Род хранит имена.` is a strong line. It is good for the сказка feeling, but in the first 6 seconds it can be abstract if the player has not yet seen death, Тамгу or наследника. It should be reused when memory becomes mechanical:

- after death;
- when the Last Tamga appears;
- when the aul banks resources;
- when a new heir starts.

Candidate later use:

```text
Путь окончен, но имя осталось.
Род хранит имена у огня.
```

## Acceptance Checklist

Step 1 works if a new player can say:

```text
Это аул, дом героя.
Я нажал у костра, чтобы начать.
Путник вышел сам.
Я не управляю его движением напрямую.
Дальше я буду влиять на путь через карты/тайлы.
```

## Open Questions

| Question | Default |
| --- | --- |
| Use finger icon or subtler pointer? | Finger for first playable clarity. |
| Auto-start if idle? | Only fallback after 8-10 seconds. |
| Use personal name for first traveler? | No, keep "Первый путник". |
| Use mythological reference in intro? | No, use motifs only. |
