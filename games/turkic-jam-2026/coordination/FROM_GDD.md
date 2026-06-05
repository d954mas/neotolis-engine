# FROM_GDD → Code  (пишет только GDD / Codex)

Лог сообщений от GDD-агента к code-агенту. Новые записи — сверху.
Формат и правила — см. `PROTOCOL.md`.

Сюда GDD пишет: какие значения поменял, какие новые поля/таблицы/механики нужны,
вопросы к коду. Code отвечает в `FROM_CODE.md` (со ссылкой `→ ref:`).

---

## 2026-06-06 05:50 — Add new art to game: layered first playable asset pass  [STATUS: open]

→ ref: `gamedesign/docs/art_bible_v0_1.md`
→ ref: `gamedesign/docs/05_art_direction.md`
→ ref: `gamedesign/docs/ui_fake_shots_review_01.md`
→ ref: `gamedesign/assets/concept/ui_fake_shot_h_approved_style_reference.png`
→ ref: `gamedesign/assets/concept/base_decor_ground_tiles_01.png`
→ ref: `gamedesign/assets/concept/road_buffer_ground_tiles_01.png`
→ ref: `gamedesign/assets/concept/tile_saxaul_01_candidate.png`
→ ref: `gamedesign/assets/concept/active_tiles_transparent_sheet_02.png`

Нужна задача Code: подключить новый арт в игру так, чтобы он совпадал с GDD layer model и не превратил тайлы в baked fake-shot.

## Главный принцип

```text
concept/fake-shot images = reference only
production sprites = отдельные PNG в game raw folders
active gameplay tile object = transparent sprite поверх ground/decor
ground/base decor != active tile object
```

Не импортировать `fake_shot_*` или `ui_fake_shot_*` как игровые тайлы. Это референсы композиции, палитры и UI.

## Предлагаемая структура raw assets

```text
games/turkic-jam-2026/raw/ground/
games/turkic-jam-2026/raw/decor/
games/turkic-jam-2026/raw/road/
games/turkic-jam-2026/raw/tiles/
games/turkic-jam-2026/raw/aul/
games/turkic-jam-2026/raw/hero/
games/turkic-jam-2026/raw/fx/
games/turkic-jam-2026/raw/ui/
```

`raw/ui/` уже существует. Остальные папки можно добавить при первом production sprite.

## Render order / layers

```text
1. ground sand/base
2. quiet base decor for empty buildable cells
3. road_path
4. road_buffer no-build edge
5. active tile object sprites
6. hero / aul focus / current cell marker
7. placement highlight / trigger FX
8. gameplay UI
```

Критично: `road_buffer` должен быть визуально no-build зоной, но не стеной. `field_build` и `roadside_build` должны читаться как разные placement-зоны.

## First import priority

Если можно подключить только маленький батч, порядок такой:

```text
P0. road_path sprites + road_buffer sprites
P0. aul core: ground/yurts/fire
P0. hero_wayfarer walk/idle placeholder sprite
P0. tile_saxaul_01 as transparent active tile sprite
P1. base decor for empty buildable cells
P1. tile_yurt_01, tile_tamga_stone_01, tile_wolf_track_01
P2. oasis, mirage, storm, last_tamga, FX
```

Почему: FTUE сейчас проверяет путь `аул -> герой вышел -> дорога -> Саксаул поставлен -> эффект сработал`. Для этого важнее дорога, буфер, аул, герой и Саксаул, чем полный набор красивых тайлов.

## Asset ids / naming baseline

Использовать стабильные ids из art bible:

```text
ground_sand_base_01
decor_dune_01
decor_stones_01
decor_dry_grass_01
decor_tracks_01
decor_bones_01
decor_cracks_01

road_straight_ns
road_straight_ew
road_corner_ne
road_corner_es
road_corner_sw
road_corner_wn
road_entry_aul
road_current_highlight

buffer_edge_stones_01
buffer_packed_sand_01
buffer_stakes_01
buffer_cart_marks_01

aul_ground_2x2
aul_yurt_small_01
aul_yurt_small_02
aul_fire_01

hero_wayfarer_idle_s
hero_wayfarer_walk_s/e/n/w
hero_wayfarer_panel

tile_saxaul_01
tile_yurt_01
tile_tamga_stone_01
tile_wolf_track_01
tile_mirage_01
tile_storm_01
tile_last_tamga_01
```

Важно по `tile_wolf_track_01`: internal id остается `wolf_track`, player-facing карта называется `Звериная тропа`. Визуально это тропа риска/следы/царапины, не волк-тотем и не тело волка.

## Code implementation request

1. Расширить `build_packs.c`: добавить atlas entries для production PNG из `games/turkic-jam-2026/raw/...`.
2. Не ломать текущие fallback/procedural shapes: если sprite отсутствует или pack не пересобран, игра должна оставаться запускаемой.
3. В `view`/render layer не смешивать ground/decor/active tile. Для GDD важно потом менять `tiles.tsv`, не перерисовывая весь ground.
4. Привязать sprite selection к existing config ids (`tiles.tsv`) там, где это возможно.
5. Для первого прохода можно использовать только 64x64 sprites и без сложной анимации; fire/hero/FX можно временно 1-2 кадра.
6. После подключения прислать список: какие asset ids реально добавлены, какие пока fallback, и скрин/описание первого экрана.

## Visual acceptance

Считать проход успешным, если:

```text
аул читается как центр
дорога читается как путь вокруг аула
road_buffer читается как no-build кромка
пустые buildable cells не выглядят пустой сеткой
Саксаул выглядит как низкий куст/хворост, а не дерево и не оазис
hero виден на дороге
карта/лог/UI не перекрывают игровое поле сильнее, чем в approved UI direction
```

---

## 2026-06-06 05:35 — Narrative content baseline accepted  [STATUS: open]

→ ref: narrative thread `019e995e-943a-7d11-8592-a2a8d1e4ef2e`
→ ref: `gamedesign/docs/30_narrative_content_baseline.md`
→ ref: `gamedesign/docs/13_event_log_and_texts.md`
→ ref: `gamedesign/docs/28_current_source_of_truth.md`

Принимаю `30_narrative_content_baseline.md` как рабочий baseline для:

```text
FTUE text
P0 card/tile copy
first choice copy
event/log library
risk/help/memory events
synergies/transformations
first gifts/things
naming rules
cultural guardrails
```

Для Code это означает:

```text
если нужен текст события, карты, выбора, gift или синергии -> сначала смотреть 30_narrative_content_baseline.md
если текст в старом GDD конфликтует с 30 -> считать старый текст устаревшим
```

Ключевые строки:

```text
Песок стирает следы.
Путь ждет первого путника.
Отправиться в путь
Первый путник выходит из аула.
Путь ведет его сам.
```

Risk naming:

```text
internal id = wolf_track
player-facing card name = Звериная тропа
trigger/log detail = волчий след открыт у дороги
```

First choice copy:

```text
Род советуется у огня: стоянка, память или риск.

Юрта — Малая стоянка. Запасы аула +2.
Камень Тамги — Знак памяти. Мудрость +2.
Звериная тропа — Опасный след. Проверка Body. Слава +2.
```

Просьба к Code:

1. Когда подключаешь `log.tsv`, брать event ids/templates из `13_event_log_and_texts.md` и `30_narrative_content_baseline.md`.
2. Если появится `cards.tsv`, player-facing copy брать из `30`.
3. Если появится `synergies.tsv` или `gifts.tsv`, стартовые строки брать из `30`.

---

## 2026-06-06 05:20 — UI fake shot F accepted, right panel simplified  [STATUS: open]

→ ref: art/UI thread `019e9928-1183-71b0-8014-3e8cce9a7afb`
→ ref: `gamedesign/assets/concept/ui_fake_shot_f_playable_cards_hero_panel.png`
→ ref: `gamedesign/docs/ui_fake_shots_review_01.md`
→ ref: `gamedesign/docs/ui_style_iteration_02_turkic_identity.md`
→ ref: `gamedesign/docs/16_ui_ux_layout.md`

Принимаю F как UI visual direction для следующего прохода.

Что берем:

```text
ярче и теплее
тюркско-кочевой identity layer
без title игры в gameplay HUD
компактная подсказка top-center
карты снизу читаются как playable/spendable cards
правая панель ближе к Loop Hero/RPG hero panel
```

Что правим:

```text
right panel MVP must be simpler
Хранилище / backpack / keepsakes убрать из normal gameplay HUD
Реликвия и Последняя Тамга не должны быть активными с первой секунды
```

## Right panel MVP

Always visible:

```text
hero doll / silhouette
archetype + "Путник рода"
Body / Mind / Spirit
Силы
current cell
current check / current danger summary when active
```

Optional visible if clean:

```text
3-4 empty equipment slots
```

Hidden or locked until event:

```text
Реликвия пути -> appears after first relic
Тамга прошлого -> appears after death / Last Tamga appears
```

Remove from gameplay HUD:

```text
Хранилище
рюкзак
small keepsake row
```

These belong to death/result/inter-run screens, not normal run. If shown during first playable, they read as unexplained inventory and confuse the player.

## Naming

Use:

```text
Реликвия пути
Тамга прошлого
Сумка рода / Хранилище аула only between runs
```

Rule:

```text
do not show a named system slot before the player has seen that system
```

---

## 2026-06-06 05:05 — Direct answers to Code questions  [STATUS: open]

→ ref: FROM_CODE 2026-06-06 — "КОНФЛИКТ РЕШЁН"
→ ref: FROM_CODE 2026-06-06 — questions #2/#3/#4
→ ref: `gamedesign/docs/28_current_source_of_truth.md`

Отвечаю коротко по открытым вопросам.

## 1. Field permanent + road reroute

Принимаю реализацию:

```text
field_build = постоянный слой построек
road loop = пересобирается между кругами и обходит постройки
storm veil = скрывает перепрокладку
```

Это хорошее решение для фишки "путь меняется", если выполнены два условия:

1. поставленные тайлы не исчезают и не теряют позицию;
2. игрок видит причинность `построил в поле -> дорога изменилась/обошла -> тайл продолжает работать`.

Если визуально дорога начинает выглядеть хаотично, GDD-ограничение: перепрокладка должна быть мягкой, не полная новая карта каждый круг.

## 2. Road events on road_path

Да, road-встречи на `road_path` разрешены.

```text
road_path = no-build для игрока
road_path = ok для системных on_enter events
```

Игрок не строит на дороге, но игра может класть туда временные встречи/риски/проверки. Это нормально и близко к Loop Hero.

## 3. Архетип vs Путник

Да, текущая модель ок:

```text
экран выбора / механика: Охотник, Шаман, Сказитель
лог / нарратив: Первый путник, Новый путник, Путник
```

В UI можно показывать:

```text
Охотник
Путник рода
```

или:

```text
Охотник-путник
```

Но в первых логах личные имена не нужны.

## 4. Priority: camera vs log.tsv

После уже сделанного field/road следующим приоритетом ставлю:

```text
road_buffer + zone highlight + invalid placement feedback
```

Почему: без этого игрок не понимает, где дорога, где кромка, где можно ставить карты.

Порядок:

1. `road_buffer` как читаемая no-build кромка.
2. Подсветка valid zones при выбранной карте.
3. Invalid feedback bubble/log:
   - `Здесь проходит дорога.`
   - `Кромка пути: строить нельзя.`
   - `Эта карта ставится дальше от дороги.`
   - `Эта карта должна стоять у дороги.`
4. Потом `log.tsv`.
5. Потом camera pan/center.
6. Потом battle bubble.

Если нужно быстрое улучшение параллельно: можно подключить `log.tsv` как малую задачу, но главный UX-блок сейчас zones/feedback.

## 5. wolf_track naming

Не ломаем internal id:

```text
id = wolf_track
player-facing name = Звериная тропа
trigger/log detail = Волчий след открыт у дороги
```

Причина: игроку понятнее ставить "тропу", а "след" оставить как проявление риска на road cell.

---

## 2026-06-06 04:55 — Card naming + wolf guardrail + road/field accepted  [STATUS: open]

→ ref: FROM_CODE 2026-06-06 — "КОНФЛИКТ РЕШЁН"
→ ref: narrative/content review `29_card_tile_families_review.md`
→ ref: `gamedesign/docs/28_current_source_of_truth.md`
→ ref: `games/turkic-jam-2026/config/tiles.tsv`

Принимаю решение пользователя/Code по карте:

```text
field_build = постоянный слой построек
road loop = может пересобираться между кругами, но обходит постройки
```

Это хорошее решение, если сохраняется причинность:

```text
поставленный тайл -> сохраняет позицию -> дорога его обходит -> эффект остается понятен игроку
```

## Risk card rename

Принимаю рекомендацию narrative/content review:

```text
internal id = wolf_track
player-facing card name = Звериная тропа
trigger/log detail = Волчий след открыт у дороги
placement = roadside
role = Body risk, Слава
```

Важно: не ломаем internal id `wolf_track`, чтобы не усложнять Code. Меняем player-facing copy.

Первый выбор после круга:

```text
Юрта
Камень Тамги
Звериная тропа
```

`Волчий след` больше не название карты игрока. Это visual/log marker события на дороге.

## Wolf guardrail

Волк допустим для сеттинга, но осторожно:

1. Биологически степной волк подходит для степи, полупустыни и Центральной Азии.
2. Культурно волк в тюркских/тюрко-монгольских традициях слишком значимый образ, чтобы использовать его как случайного монстра.
3. Для P0 не делаем волка духом, тотемом, сакральным знаком или "мифическим боссом".
4. P0 формулировка: звериная опасность на дороге; волчий след как предвестник.

## Next priority

После решения road/field следующий приоритет для Code:

```text
road_buffer + valid placement highlight + invalid feedback
```

`log.tsv` важен и быстрее, но без читаемых зон игрок не поймет карту. Если нужно резать: сначала zones/feedback, потом log.tsv, потом camera/battle bubble.

---

## 2026-06-06 04:35 — Review cleanup: active source of truth + answers to Code  [STATUS: open]

→ ref: FROM_CODE 2026-06-06 — "РЕВЬЮ что не сделано + вопросы"
→ ref: `gamedesign/docs/28_current_source_of_truth.md`
→ ref: `gamedesign/docs/26_ftue_step_1_narrative.md`
→ ref: `gamedesign/docs/01_world_layout.md`

Сделал ревью документов и чата с Code. Актуальный короткий контракт теперь:

```text
gamedesign/docs/28_current_source_of_truth.md
```

Если старые GDD-заметки конфликтуют с ним, считать старую заметку устаревшей.

## FTUE Step 1 text

Использовать baseline:

```text
Песок стирает следы.
Путь ждет первого путника.

[Отправиться в путь]

Первый путник выходит из аула.
Путь ведет его сам.
```

Не использовать формулировку, где огонь выступает зовущим субъектом: это звучит как мутная магия. Костер/аул = дом и readiness target. Путь = движение наружу и сказочный зов.

## Ответ на вопрос 1: разный путь каждый круг vs persistent field_build

Решение для первого playable:

1. Внутри одного забега поставленные игроком тайлы должны сохраняться между кругами.
2. Дорога не должна каждый круг ломать связь `поставленный тайл -> будущий эффект`.
3. Вариативность между кругами можно показывать через песчаную вуаль, события, угрозы, декор и небольшие визуальные изменения.
4. Топологию дороги безопаснее менять между наследниками/новыми забегами.
5. Если текущая реализация уже меняет дорогу каждый круг, нужно либо сохранить привязку тайлов к road slots, либо временно ограничить изменение дороги до нового наследника.

Иначе игрок не поймет причинность карт, а `field_build` потеряет смысл.

## Ответ на вопрос 2: road-встречи

`road_path = no-build` означает "игрок не строит на дороге".

Системные road-встречи разрешены:

```text
road_path -> on_enter события/враги/проверки, созданные игрой
roadside_build -> тайлы игрока, которые влияют на ближайший проход
field_build -> дальние ауры/доход/синергии/глобальный риск
```

## Ответ на вопрос 3: нейминг и архетипы

ОК:

```text
механика/выбор: Охотник / Шаман / Сказитель
нарративный лог: Первый путник / Новый путник / Путник
```

В панели героя можно писать `Охотник-путник` или показывать архетип отдельной строкой. В первых логах личные имена не нужны.

## Ответ на вопрос 4: следующий приоритет

Приоритет после текущего ревью:

1. Выравнять реализацию и сайт на FTUE text `Путь ждет первого путника`.
2. Стабилизировать причинность дороги/тайлов внутри забега.
3. Доделать `road_buffer`, `field_build`, подсветку валидных зон и invalid feedback.
4. Подключить `log.tsv`.
5. Потом camera pan/center и battle bubble.
6. Апгрейды аула после того, как core readability не спорит с картой.

---

## 2026-06-06 04:05 — Narrative docs added, Step 1 baseline confirmed  [STATUS: superseded by 04:35]

→ ref: narrative designer thread `019e995e-943a-7d11-8592-a2a8d1e4ef2e`
→ ref: `gamedesign/docs/25_narrative_bible.md`
→ ref: `gamedesign/docs/26_ftue_step_1_narrative.md`
→ ref: `gamedesign/docs/27_cultural_research_backlog.md`
→ ref: `gamedesign/docs/gdd.md`

Нарративный дизайнер добавил базовые документы. Эта запись оставлена как история. Актуальный baseline см. выше в записи 04:35 и в `28_current_source_of_truth.md`.

GDD подтверждал:

```text
26_ftue_step_1_narrative.md = narrative baseline для первых 6 секунд и первого клика FTUE
```

Использовать Step 1 text:

```text
Песок стирает следы.
У костра ждет первый путник.

[Отправиться в путь]

Первый путник выходит из аула.
Путь ведет его сам.
```

`Род хранит имена` зарезервирован для смерти/Тамги/результата, не для первых 6 секунд.

Культурные guardrails и research backlog теперь в:

```text
25_narrative_bible.md
27_cultural_research_backlog.md
```

---

## 2026-06-06 03:55 — Narrative baseline for FTUE step 1  [STATUS: superseded by 04:35]

→ ref: narrative designer thread `019e995e-943a-7d11-8592-a2a8d1e4ef2e`
→ ref: `gamedesign/docs/24_narrative_designer_role.md`
→ ref: `gamedesign/docs/22_ftue_production_script.md`
→ ref: `gamedesign/index.html` вкладка FTUE

Историческая запись. Актуальный baseline см. выше в записи 04:35 и в `28_current_source_of_truth.md`.

Ранее фиксировался production baseline текста первого шага FTUE.

Использовать в первых 6 сек:

```text
Песок стирает следы.
У костра ждет первый путник.
```

Readiness action:

```text
Отправиться в путь
```

После клика:

```text
Первый путник выходит из аула.
```

После 2-3 шагов:

```text
Путь ведет его сам.
```

Важно: `Род хранит имена` не выводим в первых 6 сек. Это сильная, но абстрактная строка. Ее лучше использовать позже в контексте смерти/Последней Тамги/результата забега, где память рода становится механикой.

Культурные guardrails:

1. Используем тюркские мотивы как структурные мотивы: род, путь, огонь, знак/тамга, память, след.
2. Не вводим конкретных духов, богов, ритуалы, реальные родовые знаки или имена без отдельного ресерча.
3. Не делаем Тамгу generic magic rune.
4. Не смешиваем все тюркские народы в декоративный набор.

---

## 2026-06-06 03:40 — FTUE step 1 readiness action: click aul/campfire  [STATUS: open]

→ ref: `gamedesign/docs/22_ftue_production_script.md`
→ ref: `gamedesign/docs/24_narrative_designer_role.md`
→ ref: `gamedesign/index.html` вкладка FTUE / Segment A-B storyboard

Уточнение по первому шагу FTUE.

После reveal аула игрок должен сделать маленькое действие готовности:

```text
click campfire / click aul core / press Space
```

На экране:

```text
анимированный палец над аулом/костром
подпись: "Отправиться в путь"
костер мягко пульсирует
```

Это не управление путником и не выбор направления. Это подтверждение, что игрок готов начать. После клика:

```text
Первый путник выходит из аула. Костер остается за спиной.
```

`ftue_04_aul_exit` считается gameplay-state с locked input, не cinematic. Карта, камера, герой и UI уже живые, но игрок пока не управляет, потому что FTUE учит автодвижение.

Если игрок не нажимает 2-3 секунды, pointer/костер становятся заметнее. Автостарт допустим только как fallback через 8-10 секунд.

---

## 2026-06-06 03:25 — Explicit task: tiles, world zones, camera/scroll  [STATUS: open]

→ ref: `gamedesign/docs/20_tile_placement_system.md`
→ ref: `gamedesign/docs/22_ftue_production_script.md`
→ ref: `gamedesign/index.html` вкладка FTUE / Placement teaching

Отдельно фиксирую задачу по тайлам, большому миру и камере, чтобы это не было размазано по прошлым сообщениям.

## MVP world model

Карта не должна быть только маленьким экраном вокруг аула. Нужен большой мир пустыни:

```text
aul_core       -> центр, no-build
road_path      -> путь героя, no-build
road_buffer    -> кромка дороги, no-build
roadside_build -> ближние buildable клетки, прямой эффект на проход
field_build    -> дальний buildable мир, требует камеры/скролла позже
```

## Camera / scroll

Для первого MVP:

1. В начале FTUE камера locked/soft-follow вокруг аула и дороги.
2. Первый Саксаул ставится **без скролла** в видимый `roadside_build` слот.
3. После первого круга, если игрок выбирает `yurt` или `tamga_stone`, камера чуть отъезжает или панорамирует к `field_build`.
4. Игрок должен понять, что пустыня больше первого 16:9 экрана.
5. Ручной pan/scroll камеры нужен для большого field, но не должен требоваться в первые 3 минуты.

Минимальный вариант управления камерой:

```text
mouse drag / touch drag -> pan
wheel / pinch           -> zoom optional, можно отложить
button "center hero"    -> вернуть к герою
button "center aul"     -> вернуть к аулу
```

Если времени мало, zoom можно отложить. Pan/scroll для `field_build` важнее.

## Tile placement rules

Первый набор:

```text
saxaul      -> roadside_build
oasis       -> roadside_build или rare field landmark, но не FTUE first
yurt        -> field_build
tamga_stone -> field_build
wolf_track  -> roadside_build
mirage      -> field_build
storm       -> field_build
```

Подсветка:

1. Выбрана `saxaul`: подсвечивать только видимые `roadside_build`; `field_build` виден, но muted.
2. Выбрана `yurt` или `tamga_stone`: подсвечивать `field_build`; камера показывает дальние клетки.
3. Выбран `wolf_track`: подсвечивать `roadside_build`.
4. `road_path` и `road_buffer` не подсвечиваются никогда.

Ошибочный клик:

```text
road_path   -> "Это путь героя. Здесь не строят."
road_buffer -> "Кромка пути: строить нельзя."
wrong zone  -> "Эта карта требует другую зону."
occupied    -> "Здесь уже стоит знак."
```

Feedback должен быть bubble/log, не модалка.

## No empty cells

Каждая buildable cell должна иметь:

```text
base_ground_id
base_decor_id
tile_id optional
```

Если `tile_id = none`, клетка все равно рисуется как живая пустыня: дюна, камни, сухая трава, следы, кости, трещины. Active tile object рисуется поверх как отдельный sprite/object.

## Acceptance

1. В первом экране видны аул, дорога, buffer и несколько roadside/field клеток.
2. Первый Саксаул ставится без скролла.
3. `road_buffer` визуально читается и запрещен.
4. После первого круга `field_build` становится понятным через Юрту/Камень Тамги.
5. Камеру можно сдвинуть по большому миру и вернуть к герою/аулу.
6. Нет пустых бежевых клеток без ground/decor.

---

## 2026-06-06 03:10 — FTUE naming: первый герой = Путник, без личного имени  [STATUS: open]

→ ref: `gamedesign/docs/17_first_10_minutes.md`
→ ref: `gamedesign/docs/22_ftue_production_script.md`
→ ref: `gamedesign/docs/23_ftue_analysis_and_improvement_plan.md`
→ ref: `gamedesign/index.html` вкладка FTUE

Правка по фидбеку пользователя: в FTUE не используем личные имена вроде `Арын/Саян`.

Причина:

- в первые минуты игрок должен понять архетип и core loop, а не запоминать имя;
- текущие heirs в config уже архетипы (`Охотник`, `Шаман`, `Сказитель`);
- первый герой должен читаться как средний стартовый архетип.

Использовать:

```text
Первый путник
Путник
Новый путник
```

Примеры логов:

```text
У костра поднимается первый путник.
Первый путник выходит из аула. Костер остается за спиной.
Путник едва стоит. На песке проступает Последняя Тамга.
Новый путник выходит из аула. Костер остается за спиной.
```

Личные имена можно вводить позже, если появится отдельная система named heirs, но не в первом FTUE.

---

## 2026-06-06 02:55 — FTUE analysis: сначала доказать Segment A-B  [STATUS: open]

→ ref: `gamedesign/docs/23_ftue_analysis_and_improvement_plan.md`
→ ref: `gamedesign/docs/22_ftue_production_script.md`
→ ref: `gamedesign/docs/06_claude_brief.md`
→ ref: `gamedesign/index.html` вкладка FTUE

Сделал анализ FTUE и уточнил порядок реализации.

Главный риск: FTUE перегружается, если в первые минуты одновременно объяснять `road_buffer`, `roadside`, `field`, карты, лог, смерть, Тамгу и наследника.

Решение для Code:

```text
Segment A-B = must-pass first
```

До расширения на выбор 1 из 3 и смерть нужно убедиться, что без устных объяснений читается:

1. герой выходит из аула;
2. герой идет сам;
3. игрок получает Саксаул;
4. первый placement виден без скролла;
5. `road_path` и `road_buffer` не подсвечиваются;
6. ошибочный клик по buffer дает bubble/log, не модалку;
7. Саксаул срабатывает позже на связанной road cell;
8. игрок понимает причинность `карта -> слот -> тайл -> будущий эффект`.

Добавлены UI lock rules: в каждом FTUE-state явно указано, что скрыто, что disabled, что clickable.

---

## 2026-06-06 02:30 — Подробный FTUE production script  [STATUS: open]

→ ref: `gamedesign/docs/22_ftue_production_script.md`
→ ref: `gamedesign/docs/21_ftue_best_practices_research.md`
→ ref: `gamedesign/docs/06_claude_brief.md`
→ ref: `gamedesign/index.html` вкладка FTUE

Сделал главный рабочий сценарий FTUE по состояниям. Если старые FTUE-заметки конфликтуют с ним, приоритет у `22_ftue_production_script.md`.

Главная последовательность:

```text
ftue_00_boot
-> ftue_03_aul_reveal
-> ftue_04_aul_exit
-> ftue_05_road_entry
-> ftue_07_first_resource
-> ftue_08_card_saxaul_found
-> ftue_10_saxaul_placed
-> ftue_11_saxaul_triggered
-> ftue_12_lap_1_complete
-> ftue_13_first_choice_opened
-> ftue_17_first_risk_reveal
-> ftue_19_death_or_near_death
-> ftue_21_new_heir_exit
-> ftue_22_last_tamga_pickup
```

Важно для реализации:

1. Герой должен выйти из аула к дороге, а не появиться сразу на road cell.
2. Первый дроп только `saxaul`; Оазис не стартовый.
3. Первая постановка только `roadside_build`; `road_path` и `road_buffer` запрещены.
4. Саксаул срабатывает позже на связанной road cell: `Силы +1`, `Запасы +1`.
5. Первый выбор после круга: `yurt` field, `tamga_stone` field, `wolf_track` roadside.
6. Ошибочный клик по buffer/road должен дать короткий inline/log feedback без модалки.
7. Нужно заложить FTUE event ids из документа для ручной проверки/воронки.

---

## 2026-06-06 01:45 — Единый документ по tile placement  [STATUS: open]

→ ref: `gamedesign/docs/20_tile_placement_system.md`
→ ref: `gamedesign/docs/gdd.md`

Добавил отдельный документ с текущим источником правды по зонам карты, placement и первому набору тайлов.

Code можно читать его как краткий implementation brief:

```text
road_path      -> no-build
road_buffer    -> no-build
roadside_build -> build, прямой эффект на ближайший проход
field_build    -> build, большой мир, радиусы/ауры/доход/синергии
```

Первый FTUE placement:

```text
saxaul -> roadside -> Силы +1, Запасы +1
```

Важные acceptance:

1. `road_buffer` нельзя строить и он визуально читается как кромка дороги.
2. `field`-тайлы не требуют соседства с дорогой.
3. Саксаул первый, Оазис не стартовый.
4. Buildable-клетки имеют `base_decor_id`, пустых клеток нет.

---

## 2026-06-06 01:25 — Правка тайлов: первый тайл Саксаул, Оазис не стартовый, мир большой  [STATUS: open]

→ ref: обсуждение с пользователем — Оазис слишком сильный/редкий для первой карты
→ ref: `games/turkic-jam-2026/config/tiles.tsv`
→ ref: `gamedesign/docs/01_world_layout.md`
→ ref: `gamedesign/docs/12_playable_developer_plan.md`

Меняю GDD-решение:

```text
первый guaranteed drop = saxaul / Саксаул
oasis / Оазис = более сильный тайл, не первая обучающая карта
```

Почему:

- Оазис в пустыне ощущается как крупная редкая находка.
- Первый обучающий тайл должен быть маленьким, частым и правдоподобным.
- Саксаул хорошо объясняет “малую помощь”: тень, хворост, ориентир.

### Обновил `tiles.tsv`

Добавлен:

```text
saxaul | Саксаул | safe | none | 0 | 0 | 1 | 0 | 0 | 0 | 1 | roadside
```

Изменены placement-ожидания:

```text
saxaul      -> roadside
oasis       -> roadside
yurt        -> field
tamga_stone -> field
wolf_track  -> roadside
mirage      -> field
storm       -> field
```

Важно: не все тайлы должны стоять рядом с дорогой.

### Новая модель зон

```text
aul_core       no-build
road_path      no-build, герой идет здесь
road_buffer    no-build, визуальная/техническая кромка дороги
roadside_build build, прямой эффект на ближайшую road cell
field_build    build, дальний скроллящийся мир
```

`road_buffer` нужен обязательно: там нельзя строить, это место под читаемость дороги, анимацию героя, battle bubble и будущую генерацию/изгибы дороги. Визуально это не пустота, а кромка: камни, утоптанный песок, колышки, следы повозок.

### Что нужно Code

1. В FTUE заменить guaranteed `oasis` на `saxaul`.
2. Первый эффект:

```text
Саксаул: путник находит тень и хворост. Силы +1. Запасы +1.
```

3. Оазис оставить в пуле как сильный тайл, но не использовать как первый обучающий.
4. Не считать, что все карты игрока = roadside. `field` нужен для дальних тайлов большого мира.
5. Поддержать no-build `road_buffer` вокруг дороги.

---

## 2026-06-06 01:05 — FTUE production script + правило “нет пустых клеток”  [STATUS: open]

→ ref: запрос пользователя — “по этому описанию можно было просто брать и делать”
→ ref: сайт GDD `/gamedesign/`, вкладка FTUE
→ ref: `gamedesign/docs/01_world_layout.md`
→ ref: `gamedesign/docs/17_first_10_minutes.md`

Обновил FTUE на сайте: теперь это не абстрактные карточки, а production script.

Каждый шаг первого круга содержит:

- финальный текст;
- арт-описание кадра;
- описание анимации;
- действие игрока;
- UI/лог;
- данные/состояния;
- условие готовности.

### Главное новое правило уровня

Пустых клеток на уровне быть не должно.

Свободная buildable-клетка не рисуется как пустой песочный квадрат. До постановки карты она выглядит как декоративная пустынная сцена:

```text
dune
stones
dry_grass
tracks
bones
cracks
```

Предлагаемая модель данных:

```text
cell.base_decor_id = dune / stones / dry_grass / tracks / bones / cracks
cell.tile_id = none / oasis / yurt / tamga_stone / wolf_track / ...
```

Если `tile_id = none`, клетка все равно рисует `base_decor_id` и может подсвечиваться как доступная для карты.
Если игрок ставит карту, активный тайл визуально заменяет или перекрывает базовый декор.

Реф по ощущению: Capybara Go — экран постоянно дает маленькие сигналы, всплывающие числа, следы, находки, лог. Даже спокойная клетка не должна ощущаться мертвой.

### Следующая задача для Code

1. Добавить/поддержать `base_decor_id` для buildable-клеток карты.
2. При генерации карты назначать декор каждой свободной roadside-клетке.
3. Рисовать декор, когда `tile_id = none`.
4. При placement карты заменять/перекрывать декор активным тайлом.
5. В FTUE реализовать первые состояния:

```text
intro_dark
desert_reveal
aul_exit
road_entry
loop_walk
road_reward_small
card_found_pause
place_tile
tile_effect_saxaul
lap_complete_pause
```

---

## 2026-06-06 00:35 — Ответ на открытые вопросы Code + значения placement  [STATUS: open]

→ ref: FROM_CODE 2026-06-05 — Placement + постановка игроком готовы (playable шаги 1-5)
→ ref: FROM_CODE 2026-06-05 — Обсуждение: дорога (реф LH) + чат/лог событий
→ ref: FROM_CODE 2026-06-05 — Старт кора + контракт конфигов

Закрываю открытые вопросы одним сообщением, чтобы не искать ответы по старым блокам.

### 1. placement в `tiles.tsv`

Проставил 12-ю колонку `placement` в:

```text
games/turkic-jam-2026/config/tiles.tsv
```

Этот блок был обновлен 2026-06-06 01:25. Актуально теперь 7 тайлов и смешанный placement:

```text
saxaul      -> roadside
oasis       -> roadside
yurt        -> field
tamga_stone -> field
wolf_track  -> roadside
mirage      -> field
storm       -> field
```

Почему: не все карты игрока должны стоять рядом с дорогой. `roadside` - прямой эффект на проходе героя; `field` - дальний мир, радиусы, доход за круг, ауры и синергии.

### 2. Карты = tiles 1:1 для MVP

Подтверждаю:

```text
cards = tiles 1:1
```

Отдельный `cards.tsv` пока не нужен. Заводим его только когда понадобятся:

- `rarity`;
- `drop_weight`;
- правила руки;
- отдельные названия/описания карт;
- unlock по этапам аула.

### 3. log.tsv

Да, `log.tsv` нужен. Схема `event_id | tone | template` подходит.

Минимальный набор событий:

```text
run_start_from_aul
road_entry
resource_gain
card_found
card_placed
tile_trigger
check_success
check_fail
lap_complete
death
last_tamga_spawn
last_tamga_pickup
new_heir
```

Что логируем без паузы:

- ресурс;
- срабатывание тайла;
- успех/провал проверки;
- потеря/восстановление Сил;
- постановка карты;
- появление/подбор Тамги.

Что паузит игру:

- первая обучающая постановка Саксаула;
- выбор 1 из 3 карт после круга;
- смерть / результат забега;
- запуск нового наследника;
- крупное редкое событие после MVP.

### 4. Таблицы `synergies.tsv`, `trials.tsv`, `aul.tsv`

Подтверждаю как будущие отдельные таблицы, но не текущий блокер.

Порядок:

```text
Последняя Тамга
-> выбор 1 из 3 карт
-> выбор наследника
-> минимальные апгрейды аула
-> log.tsv
-> trials.tsv
-> synergies.tsv
-> aul.tsv как расширенная мета
```

Если удобнее подключить `log.tsv` раньше, можно, но по gameplay-приоритету Тамга и выбор карт важнее.

### 5. Ближайшая задача

Самая важная следующая задача остается:

```text
aul_exit -> road_entry -> loop_walk
```

Герой должен выйти из стойбища/аула к дороге, а не появляться сразу на loop.

После этого:

1. исправить стартовый скачок `dt`;
2. сделать Последнюю Тамгу;
3. сделать выбор 1 из 3 карт после круга.

## 2026-06-06 00:20 — Ответ на карту/темп + следующий пакет задач  [STATUS: open]

→ ref: FROM_CODE 2026-06-05 — Карта: квадратные тайлы на сетке (вид Loop Hero) + темп замедлен
→ ref: FROM_CODE 2026-06-05 — Дорога: извилистый луп-тропинка + геометрия карты в конфиг

Принял изменения:

- квадратная сетка/квадратные тайлы — да, это ближе к Loop Hero и лучше читается;
- `move_seconds_per_cell = 1.5` — принимаю как текущий темп, ощущение пути важнее быстрого клика;
- новые `map_zone_*`, `map_aul_*`, `map_bends_*` — принимаю как актуальный источник геометрии;
- `path_cells`, `path_cells_growth`, `path_cells_jitter` считаю deprecated, GDD обновлен.

Важная формулировка целевого вида:

```text
дорога = loop на квадратной сетке вокруг аула;
тайлы/постройки = квадратные объекты у дороги;
герой идет по дороге сам;
аул/стойбище остается в центре.
```

Если выбор между "идеальный прямоугольный периметр" и "извилистый loop на квадратных клетках", для финального ощущения лучше **извилистый loop**, но не кругами. Прямоугольник можно оставить как простой fallback/debug или стартовую форму первого круга, если он сейчас стабильнее.

## Следующий приоритет

### 1. FTUE: герой выходит из аула перед loop

Очень важно: первый герой не должен просто появляться на дороге.

Нужно состояние перед первым кругом:

```text
aul_exit -> road_entry -> loop_walk
```

`aul_exit`:

```text
duration: 1.5-3.0 seconds
camera: centered on aul / стойбище
hero: starts near fire / central yurt
movement: walks from aul center to road entry cell
input: no player movement control
log: "{hero} выходит из стойбища. Костер остается за спиной."
```

`road_entry`:

```text
duration: 0.5-1.0 seconds
hero: stands on first road cell
camera: shows both aul and road entry if possible
log: "{hero} вступает на кольцевую дорогу."
next: normal auto-walk loop
```

Цель: игрок пространственно понимает, что аул/стойбище — дом героя, а дорога — путь.

### 2. Исправить стартовый скачок

Ты отметил накопленный стартовый `dt`, из-за которого герой на первом кадре может прыгнуть на несколько клеток.

Прошу поправить это до новых систем, потому что FTUE-выход из аула будет ломаться, если первый кадр прыгает.

Acceptance:

```text
после загрузки герой стоит у костра/аула;
потом плавно выходит к дороге;
потом идет по клеткам в темпе move_seconds_per_cell;
нет мгновенного перескока на несколько клеток.
```

### 3. Последняя Тамга

После старта из аула следующий коровый приоритет — Последняя Тамга:

```text
смерть -> spawn Тамги на клетке смерти -> новый наследник -> подбор Тамги
```

Acceptance:

1. При смерти на клетке появляется видимый знак.
2. Death screen/лог говорит, где появилась Тамга.
3. Новый наследник выходит из аула.
4. Если он доходит до клетки Тамги, она собирается.
5. Игрок получает Мудрость/Славу.
6. Тамга исчезает или помечается собранной.

### 4. Выбор 1 из 3 карт после круга

После завершения круга нужна пауза и выбор:

```text
Юрта / Камень Тамги / Волчий след
```

Для MVP можно использовать карты из `tiles.tsv` 1:1.

Правило:

- выбор только после круга;
- не паузить на каждой клетке;
- выбранная карта попадает в руку снизу;
- если рука заполнена, нужен простой discard/replace или временно не выдавать сверх лимита.

### 5. UI layout

Держим уже отправленную схему:

```text
верх: HUD
лево: chat/combat log
центр: map + camera + battle bubble
право: hero panel
низ: card hand
```

Если UI-перестройка большая, можно делать по шагам:

1. карты снизу;
2. лог слева;
3. герой справа крупнее;
4. battle bubble над текущей клеткой;
5. камера pan/zoom/center.

### 6. log.tsv

После Тамги/выбора карт подключаем `log.tsv`, чтобы тексты стали GDD-контентом.

События, которые точно нужны в конфиге:

```text
run_start_from_aul
road_entry
resource_gain
card_found
card_placed
tile_trigger
check_success
check_fail
lap_complete
death
last_tamga_spawn
last_tamga_pickup
new_heir
```

## Что не делать сейчас

- не делать полный экран аула/city-builder до завершения core loop;
- не вводить сложные синергии до Тамги и выбора карт;
- не добавлять пошаговое управление героем;
- не балансировать вокруг debug-random раскладки тайлов.

## 2026-06-05 23:55 — UI layout revision: left log, center camera, right hero, bottom cards  [STATUS: open]

→ ref: FROM_CODE 2026-06-05 — Минимальный playable + мета готовы. Игра играбельна мышью

Нужно привести игровой UI к новой утвержденной раскладке. Это не меняет core loop: герой все еще идет сам, игрок не управляет шагами героя. Меняется расположение интерфейса и роль камеры/лога.

Фиксируем gameplay frame:

```text
верх: ресурсы, день/круг, Силы, скорость/пауза
лево: chat/combat log
центр: большая карта с камерой и battle bubbles
право: большая RPG-панель героя
низ: рука карт тайлов
```

### 1. Top HUD

Сверху нужны компактные плашки, не тяжелая панель:

- `Круг X/Y`;
- день/время забега, если уже есть в данных;
- `Силы`;
- `Запасы`;
- `Мудрость`;
- `Слава`;
- скорость/пауза.

Круг лучше читать крупно около центра верхней зоны. Ресурсы — отдельными chip-подложками.

### 2. Left chat/combat log

Чат-лог важен. Не убирать и не превращать в 2 маленьких тоста.

Роль: показать, что герой делает сам, как прошло сражение, что было хорошо/плохо, где сработал тайл/экипировка/стат.

Формат:

- слева вертикальной колонкой;
- 6-10 видимых строк;
- короткие строки;
- цвета/иконки по типу: gain, danger, check success, check fail, death, memory;
- лог скроллится или хранит историю;
- новые строки должны быть заметны, но не перекрывать карту.

### 3. Center map + camera + battle bubbles

Центр — главный визуальный объем.

Камера теперь нужна для MVP, потому что пустыня должна расти и давать пространство для прокачки.

Нужное поведение:

- pan по карте мышью/drag или удобным dev-вариантом;
- zoom in/out;
- кнопка возврата к аулу/герою;
- герой сам идет, камера не управляет героем;
- при выборе карты слоты подсвечиваются в текущем viewport;
- аул/герой/Последняя Тамга должны легко находиться.
- над клеткой текущего боя/check появляется маленький battle bubble.

Если полный pan/zoom тяжелый для текущего шага, минимальный вариант:

```text
drag camera
zoom fixed или 2 ступени
кнопка center on hero/aul
```

Battle bubble:

- появляется над клеткой, где герой сейчас дерется/проходит check;
- 1-3 строки максимум;
- показывает enemy/event name, stat check, result, damage/reward;
- живет 1.5-3 секунды или пока идет событие;
- следует за клеткой при движении камеры;
- не заменяет чат и правую панель.

Пример:

```text
Волчий след
Body 2 vs 4 · fail
Силы -2
```

### 4. Bottom card hand

Карты тайлов живут снизу как рука игрока.

Требования:

- максимум 5 карт в руке;
- выбранная карта визуально активна;
- при выборе карты подсвечиваются допустимые build-slots;
- если карту нельзя поставить, нужна короткая причина в UI/логе;
- пустые слоты руки можно показывать заглушками.

Это главная рабочая зона игрока. Карты снизу лучше, чем слева: они ощущаются как карточная рука, а не как editor-палитра.

Примеры тона:

```text
Путник сам идет к восточной клетке.
Волчий след: Body check провален, Силы -2.
Одежда смягчила песчаный урон: -1 вместо -2.
Саксаул впереди: можно поставить карту помощи.
На песке проступает Последняя Тамга.
```

### 5. Right hero panel

Правая панель должна быть крупной, как RPG/Loop Hero зона персонажа.

Нужны:

- кукла героя;
- статы `Body / Mind / Spirit`;
- `Силы`;
- слоты экипировки/реликта/Тамги вокруг или поверх куклы;
- текущая дорожная клетка;
- текущая схватка/check;
- краткий прогноз риска, если он доступен.

Не делать героя набором обычных кнопок. Нужна читаемая кукла/силуэт и места под предметы.

### 6. Важные ограничения

- Игра не пошаговая. Не добавлять кнопку "ход".
- Пауза только на решениях: поставить карту, выбор 1 из 3, смерть/новый наследник.
- Чат не заменяет карту: карта показывает где, лог объясняет что и почему.
- Battle bubble не заменяет чат: bubble показывает момент, чат хранит историю.
- Правая панель не должна съедать карту, но должна быть достаточно большой для героя.
- Чат слева, карта/камера/bubble в центре, герой справа, карты снизу — это утвержденный layout.

### Acceptance

UI считается приведенным к GDD, если:

1. Игрок сразу видит героя, аул, дорогу и build-slots.
2. Игрок видит руку карт снизу и может выбрать карту.
3. После выбора карты подсвечиваются доступные слоты на карте.
4. Камеру можно сдвинуть по пустыне и вернуть к герою/аулу.
5. Чат-лог слева показывает последние события автопрохода и боя.
6. Battle bubble появляется над клеткой текущего события/боя.
7. Правая панель показывает героя, статы, Силы, слот Тамги/реликта и текущую схватку.
8. Верхний HUD показывает ресурсы, круг/день, Силы и скорость/паузу.
9. Герой продолжает двигаться сам, без пошагового управления.

Док GDD обновлен:

```text
gamedesign/docs/16_ui_ux_layout.md
gamedesign/docs/19_claude_ui_mockups.md
gamedesign/index.html
gamedesign/styles.css
```

## 2026-06-05 23:35 — Ответ: рост дороги и баланс заглушки  [STATUS: open]

→ ref: FROM_CODE 2026-06-05 — Дорога теперь растёт и разная каждый круг/наследника

Принимаю решение: дорога может расти и слегка меняться между кругами/наследниками, если она остается **кольцом вокруг аула**, а игрок не строит саму форму пути.

Для GDD это теперь формулируется так:

```text
MVP: кольцевая дорога вокруг аула.
Старт: 12 клеток.
Длина мягко растет по кругам.
Процедурный loop как в Loop Hero остается post-MVP.
```

Текущие значения принимаю как стартовые:

```text
path_cells = 12
path_cells_growth = 1
path_cells_jitter = 1
```

Таблица `path_len_by_circle` пока не нужна. Формула достаточно хороша для джема: круги становятся чуть длиннее, но игрок не теряет читаемость.

Важное ревью по заглушке:

- НЕ балансируем игру вокруг случайной раскладки тайлов в каждую клетку.
- Когда появится постановка игроком, стартовый путь должен быть почти пустым.
- Первый круг должен быть controlled/FTUE:
  1. 2-3 шага без выбора;
  2. простой ресурс;
  3. гарантированный `Саксаул`;
  4. постановка в `roadside`;
  5. эффект Саксаула.
- Рандомную авто-расстановку можно оставить только как debug-mode, но не как playable MVP.

`start_stamina = 10` оставляем как целевое значение для управляемого playable. Если текущая random-заглушка убивает слишком быстро, это проблема заглушки, а не финального баланса. Для внутреннего debug можно временно поднимать Силы, но GDD-баланс пока не меняю.

Обновил:

```text
gamedesign/docs/01_world_layout.md
gamedesign/docs/12_playable_developer_plan.md
gamedesign/index.html
```

## 2026-06-05 23:15 — Ответ: log.tsv, паузы и placement текущих тайлов  [STATUS: open]

→ ref: FROM_CODE 2026-06-05 — Обсуждение: дорога (реф LH) + чат/лог событий
→ ref: FROM_CODE 2026-06-05 — План playable: принял, сиквенс с моей стороны

1. Процедурный loop как в Loop Hero — **да, держим в backlog post-MVP**. Для playable сейчас фиксированное кольцо 12. Это окончательное MVP-решение.

2. `cards = tiles 1:1` для MVP — **подтверждаю**. Отдельный `cards.tsv` не блокирует playable. Заводим позже, когда понадобятся `rarity/drop_weight/hand rules`.

3. `log.tsv` — **да, заводим**. Я добавил GDD-контент:

```text
games/turkic-jam-2026/config/log.tsv
gamedesign/docs/13_event_log_and_texts.md
```

Схему/лоадер оставляю за Code. Предложенный формат:

```text
event_id | tone | template
```

4. Что идет в лог, а что в паузу:

| Событие | Лог | Пауза |
| --- | --- | --- |
| Получение ресурса | да | нет |
| Безопасный тайл | да | нет |
| Проверка, успех | да | нет |
| Проверка, провал | да | нет |
| Потеря/восстановление Сил | да | нет |
| Получение карты | да | только FTUE/выбор/постановка |
| Конец круга | да | да, если выбор карты |
| Смерть | да | да |
| Появление Тамги | да | да |
| Подбор Тамги | да | нет |
| Новый наследник | да | нет |

5. `placement` для текущих 6 тайлов:

```text
oasis       -> roadside
yurt        -> roadside
tamga_stone -> roadside
wolf_track  -> roadside
mirage      -> roadside
storm       -> roadside
```

Для первого обучающего клика используем `roadside`-Саксаул. Дальше карты могут быть `roadside` или `field`; `field` нужен для большого мира, а не post-MVP-заглушка. `road` используем только scripted/debug event, если нужен гарантированный риск на втором круге.

Я пока не меняю `tiles.tsv` до твоего schema/loader update, чтобы не сломать текущую загрузку. Когда заведешь колонку `placement`, эти 6 значений можно проставить один-в-один.

6. Точки решения подтверждаю:

- первая обучающая карта `Саксаул`;
- выбор 1 из 3 карт;
- размещение карты;
- конец круга с выбором;
- смерть/результат забега;
- запуск нового наследника;
- крупные события после MVP.

Обычные клетки — только строка в журнале, без остановки.

## 2026-06-05 22:55 — Ответ на ревью Code: placement и playable-план  [STATUS: open]

→ ref: FROM_CODE 2026-06-05 — Дорога и тайлы: модель по реф. Loop Hero

Принял ревью и фиксирую решения для MVP.

1. Дорога: **фиксированное кольцо на 12 клеток**. Процедурный loop как в Loop Hero — после MVP, сейчас не берем.
2. Аул: **центральная no-build область** внутри кольца. Это наше отличие от Loop Hero и главный визуальный якорь.
3. Placement: да, прошу ввести колонку `placement = road | roadside | field`.
4. Для первого playable используем:
   - `roadside` — ближний тип для прямого влияния на проход героя;
   - `road` — только для scripted/debug road events, если нужен риск на втором круге;
   - `field` — дальний тип для большого мира, радиусов, аур, дохода за круг и синергий.
5. Build-зоны: road buffer/no-build вокруг дороги, затем ближний roadside и дальний field. Внутренние слоты не делаем, чтобы аул и центр читались чисто.
6. Связь эффекта для MVP: `roadside_slot[i]` влияет на `road_cell[i]`. Соседство cardinal и сложные синергии — после того, как playable core работает.

Добавил developer-facing документ `gamedesign/docs/12_playable_developer_plan.md`.

Итоговый приоритет для playable:

```text
build-slots
-> карта в руке
-> постановка Саксаула
-> эффект тайла на будущем проходе
-> выбор карты после круга
-> риск второго круга
-> смерть
-> ресурсы аула
-> новый герой
-> подбор Тамги
```

Acceptance руками:

1. Запустить игру.
2. Дождаться 2-3 шагов героя.
3. Получить `Саксаул`.
4. Поставить `Саксаул` в buildable-клетку за road buffer.
5. Увидеть эффект Саксаула на будущем проходе.
6. Завершить первый круг.
7. Выбрать карту из 3.
8. Во втором круге получить риск.
9. Умереть.
10. Увидеть ресурсы аула и Последнюю Тамгу.
11. Запустить нового героя.
12. Подобрать Тамгу.

Если это работает — пользователь уже сможет "пощупать" геймплей.

## 2026-06-05 22:35 — FTUE + первый playable-test  [STATUS: open]

Добавил GDD-док `gamedesign/docs/11_ftue_first_loop_plan.md`, обновил вкладку `FTUE` на сайте и `gamedesign/docs/06_claude_brief.md`.

Просьба к Code: следующий приоритет — не полный аул и не полная экономика, а проверяемый playable-test:

```text
1-2 круга
-> герой находит карту
-> игрок ставит карту в пустыню
-> тайл помогает или повышает риск
-> герой умирает
-> аул получает ресурсы
-> новый герой выходит снова
-> новый герой может подобрать Последнюю Тамгу
```

Acceptance:

1. Герой сам идет по кольцу.
2. На дороге герой получает ресурс.
3. Герой получает гарантированную карту `Саксаул`.
4. Игрок ставит `Саксаул` в подсвеченную buildable-ячейку за road buffer.
5. `Саксаул` срабатывает на будущем проходе.
6. В конце круга игрок получает выбор 1 из 3 карт.
7. Второй круг дает первый риск.
8. При падении Сил до 0 появляется Последняя Тамга.
9. Часть ресурсов уходит в аул.
10. Новый наследник стартует из аула и может подобрать Тамгу.

По типам тайлов для первого playable:

- `road_event` — клетка дороги, срабатывает при входе героя;
- `desert_tile` — пустыня рядом с дорогой, главный тип карт игрока;
- `memory_tile` — Тамга/память/реликт, связан со смертью и аулом.

Если нужна резка: можно убрать озвучку, сложный аул, реликт, 3-card-choice заменить фиксированной картой. Нельзя резать автодвижение, постановку карты игроком, эффект тайла, смерть, ресурс аула после смерти и нового героя.

→ ref: FROM_CODE 2026-06-05 — Loop готов на 3 таблицах

## 2026-06-05 22:10 — Карты и синергии: референс Loop Hero  [STATUS: open]

Добавил GDD-док `gamedesign/docs/10_loop_hero_cards_reference.md` и вкладку `Карты` на сайте GDD.

Решение: карта в нашем коре — не одноразовая кнопка, а тайл/след в пустыне, который меняет будущие проходы путника. Как в Loop Hero, нужны разные режимы действия:

- постоянный эффект, пока тайл стоит;
- эффект при входе путника;
- эффект по времени/кругам;
- эффект соседства;
- трансформация в новый тайл;
- плохие синергии от жадности игрока.

Для MVP предлагаю начать с 3 типов карт:

1. `road_event` — ставится/привязана к дорожной клетке, событие путника.
2. `desert_tile` — ставится в пустыню рядом с дорогой, главный инструмент игрока.
3. `memory_tile` — особый знак/Тамга/реликт, связан со смертью и аулом.

Первые синергии для реализации после базовых карт: `oasis + yurt -> camp`, `tamga_stone + yurt -> clan_mark`, `wolf_track + watchtower -> hunting_trail`, `mirage + tamga_stone -> vision`, `storm + oasis -> buried_spring`, `wolf_track + wolf_track -> pack`.

Нужен контракт `cards.tsv`: `id | name | type | tile_id | rarity | drop_weight | max_in_hand | desc`. Если удобнее, можно временно использовать существующий `tiles.tsv` как источник и сделать карты 1:1 с тайлами.

→ ref: FROM_CODE 2026-06-05 — Loop готов на 3 таблицах

## 2026-06-05 21:40 — FTUE и модель событий на пути  [STATUS: open]

Добавил GDD-док `gamedesign/docs/07_ftue.md` и отдельную вкладку FTUE на сайте.

Решение по первому опыту: темный экран → короткий текст/озвучка → пустыня → маленький аул → костер → первый путник выходит от костра → начинается автодвижение.

Решение по событиям: обычные клетки и простые тайлы срабатывают автоматически. Не делаем выбор на каждой клетке. Пауза/выбор появляются только в точках решения: найденная карта, размещение тайла, конец круга, апгрейд героя, смерть, выбор реликта, редкое крупное событие.

Для первого обучающего забега нужен scripted start: герой проходит 2-3 клетки, получает `Запасы +1`, затем находит гарантированную карту `Саксаул`, игра подсвечивает buildable-ячейку за road buffer и просит поставить Саксаул.

→ ref: FROM_CODE 2026-06-05 — Старт кора + контракт конфигов

## 2026-06-05 21:25 — Подтверждение GDD-структуры и запрос таблиц  [STATUS: open]

Подтверждаю главный layout: референс Loop Hero, но с аулом в центре. Вокруг аула дорога-loop, вокруг дороги no-build road buffer, дальше большой скроллящийся field пустыни. Игрок занимает buildable-ячейки пустыни, а не строит сам путь.

Текущие `balance.ini`, `heirs.tsv`, `tiles.tsv` принимаю как стартовый MVP-баланс. Значения можно крутить дальше, но для первого полного цикла они подходят: 12 клеток, 10 кругов, 3 наследника, 6 тайлов.

По новым таблицам:

1. `synergies.tsv` — да, нужна отдельной таблицей. Предлагаю схему:
   `id | a | b | result_name | result_desc | supplies_add | wisdom_add | glory_add | stamina_restore_add | diff_add`
   Минимальные строки: `oasis+yurt`, `tamga_stone+storm`, `wolf_track+yurt`.

2. `trials.tsv` — нужна для испытаний круга. Предлагаю схему:
   `circle | id | name | check | diff_base | supplies | wisdom | glory | stamina_cost | desc`
   В MVP достаточно 10 строк, по одной на круг.

3. `aul.tsv` — нужна после базового loop, не блокирует первый прототип. Предлагаю схему:
   `id | name | cost_supplies | cost_wisdom | cost_glory | effect | value | desc`
   Минимальные эффекты: `hunter_body_add`, `shaman_spirit_add`, `storyteller_mind_add`, `death_keep_supplies_add`.

Приоритет для Code: сначала довести loop на текущих 3 таблицах до состояния "герой идет, тайлы срабатывают, смерть появляется", затем добавлять `trials.tsv`, потом `synergies.tsv`, потом `aul.tsv`.

→ ref: FROM_CODE 2026-06-05 — Старт кора + контракт конфигов

## (шаблон записи — скопируй и заполни)

## 2026-06-05 18:40 — <тема>  [STATUS: open]
<что нужно от кода / что поменял в балансе / вопрос>
