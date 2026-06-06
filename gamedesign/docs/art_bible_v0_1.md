# Art Bible v0.1 - Song of Tamga

Роль документа: быстрый арт-источник для первого playable. Не энциклопедия мира.

## Короткое решение

Базовое направление для первой итерации: `flat_readable_jam_style`.

Причина: за джемовое время он быстрее всего дает читаемость карты, дороги, `road_buffer`, buildable-клеток и первого обучающего тайла `saxaul`. Текстура и декоративность вторичны; силуэт, зона placement и мгновенное различение тайлов важнее.

GDD owner status: accepted as production baseline for the first playable. Painterly and parchment/miniature directions stay as later polish/style exploration, not as the production baseline.

## Art Bible v0.1

### Reusable Asset Composition Rule

Production art should separate reusable surfaces from semantic content.

Core rule:

```text
base surface / 9-slice frame / ground layer
-> icon, marker, active object or item art
-> text/value/state overlay
```

Do not bake many semantic variants into separate full surfaces if the visual system can use one reusable surface plus a small icon/overlay. This keeps the art consistent and prevents generated assets from drifting into four different versions of the same UI cell.

Examples:

- Equipment inventory cell: one `ui_slot_9s` / base slot surface, with weapon/clothes/tool/tamga marker as an overlay.
- Card surface: one playable card face, with card art/title/count/placement icon layered on top.
- Map tile: ground/base decor separate from active tile object sprite.
- HUD chip: one chip frame, with resource/stat icon and number layered on top.

Temporary compatibility rule: if runtime currently requires several filenames for the same surface type, those files should be identical aliases of the same base surface until the runtime exposes a reusable base id plus separate overlay ids.

### Камера и форма

- Вид: 2D top-down с легкой декоративной перспективой объектов.
- Сетка: квадратные тайлы, визуально близко к Loop Hero, но с центральным аулом.
- Базовый тайл: `64x64` logical px для 1x, `128x128` для 2x.
- Объекты внутри тайла должны читаться в `48x48` safe area, с тенью/пылью в оставшихся краях.
- Силуэты крупные, без мелкой ковровой детализации.
- Контраст должен отвечать на вопрос игрока: `road_path`, `road_buffer`, buildable desert, active tile.

### Палитра

| Role | Hex | Use |
| --- | --- | --- |
| `sand_base` | `#D8B56B` | основной песок buildable-клеток |
| `sand_light` | `#E8CC86` | верхние гребни барханов, подсветка |
| `sand_shadow` | `#A9783C` | нижние края, следы, трещины |
| `road_packed` | `#9A6D3A` | утоптанная дорога |
| `road_dust` | `#C79A57` | дорожная пыль, колеи |
| `felt_yurt` | `#E9DFC8` | юрта, войлок |
| `wood_dark` | `#5F3A24` | колья, хворост, ветви |
| `clan_red` | `#A6422A` | малые акценты аула и тамги рода |
| `tamga_teal` | `#2B8C84` | мудрость, тамга, memory FX |
| `danger_red` | `#8F2E24` | риск, wolf/storm warning |
| `storm_bluegray` | `#8EA0A7` | буря, духи, пыльный воздух |
| `deep_violet` | `#3B263F` | редкая опасность, миражная тень |

Правило насыщенности: пустыня теплая и спокойная; интерактивные/опасные знаки получают локальный контраст, а не общий кислотный цвет.

### Shape Language

| Group | Shape |
| --- | --- |
| `aul_core` | круглые/конические юрты, мягкие овалы, теплый огонь |
| `road_path` | ленты, колеи, плотные коричневые пятна, направленные следы |
| `road_buffer` | низкие камни, колышки, утоптанная кайма, broken edge |
| `roadside_build` | читаемый песочный тайл с декором и легкой подсветкой при placement |
| `field_build` | больше воздуха, декор не должен спорить с активными тайлами |
| `memory` | резаные знаки, камень/кость, бирюзовый малый свет |
| `danger` | острые следы, темные пятна, спирали ветра, красно-фиолетовый акцент |

### Уровень детализации

- `base_decor` содержит 1 главный признак и 1-2 малых штриха.
- Активный тайл содержит 1 главный силуэт, тень и один акцент.
- UI icons читаются в `24x24`; карта-иконки в `40x40`.
- Анимации первого playable: 2-4 кадра, без сложных partial effects.
- Не рисовать реалистичную микротекстуру песка на каждом тайле: она убьет читаемость placement.

### Запрещено

- Арабские дворцы, купола-дворцы, лампы джиннов, ковры-самолеты.
- Базарная перегруженность, золотые арки, сказочный "Aladdin" silhouette.
- Большие пальмы как стандартный символ помощи; пальма допустима только для редкого `oasis`, и лучше заменить на низкую зелень/воду.
- Большой пальмовый оазис как язык первой помощи.
- Пышная тропическая зелень.
- Орнаментальный магический UI.
- Фотореалистичный texture noise.
- Пустые песочные квадраты без `base_decor_id`.
- Слишком яркая магия вокруг обычного `saxaul`.
- Декоративные рамки UI, которые съедают карту.

## Saxaul vs Oasis

| Tile | Visual | Feeling | Gameplay Read |
| --- | --- | --- | --- |
| `saxaul` | низкий темный куст, сухие ветви, маленькая тень, 2-3 палки хвороста | обычная дорожная помощь | `roadside`, малая помощь: Силы +1, Запасы +1 |
| `oasis` | маленькая вода, зеленое пятно, светлый влажный край, редкая прохлада | ценная находка | сильная помощь, не первый обучающий тайл |

`saxaul` должен быть low/wide shrub: примерно `45-60%` ширины тайла и `25-40%` высоты, сидит низко к земле, имеет `2-4` сухие ветки/хворост и маленькую тень. Он должен выглядеть как скромная первая помощь, сильно слабее `oasis` по ценности и контрасту.

`oasis` должен иметь заметное синее пятно воды и более высокий value contrast, но без дворцовой сказочности, пальмового "тропического" языка и крупных magic FX.

## Road Buffer

`road_buffer` - это не пустота. Это визуальная кромка пути и no-build зона.

Asset ids:

- `buffer_edge_stones_n`
- `buffer_edge_stones_e`
- `buffer_edge_stones_s`
- `buffer_edge_stones_w`
- `buffer_packed_sand`
- `buffer_stakes_01`
- `buffer_cart_marks_01`
- `buffer_low_berm_01`

Визуально: каменная кайма, редкие колышки, утоптанный песок, следы повозки. При hover/placement недоступность показывать глухим теплым затемнением, не красной тревогой.

P0 visual rule: `road_path` and `road_buffer` must be distinguishable at a glance.

- `road_path`: continuous packed-earth route, smoother and darker, with travel direction/track language.
- `road_buffer`: broken edge language, sparse stones/stakes/cart marks, visually no-build, lower contrast than active tiles.
- Do not let the buffer become a wall or decorative fence.

## Free Buildable Decor Cells

Свободная buildable-клетка рисует `base_decor_id`, даже если `tile_id = none`.

| Asset id | Silhouette | Rule |
| --- | --- | --- |
| `decor_dune_01` | низкий бархан с одним гребнем | самый спокойный фон |
| `decor_stones_01` | 3-5 камней, сухая земля | не похож на `tamga_stone` |
| `decor_dry_grass_01` | пучки сухой травы | не путать с `saxaul`: ниже и тоньше |
| `decor_tracks_01` | старые следы в песке | не волчья лапа, скорее проход/колея |
| `decor_bones_01` | мелкие кости/обломки | тревожно, но без активной опасности |
| `decor_cracks_01` | трещины сухой глины | плоский dark line pattern |

## First Playable Asset List

### Hero

| Asset id | Size | Frames | Notes |
| --- | --- | --- | --- |
| `hero_wayfarer_idle_s` | `64x64` | 2 | стоит на road cell, плащ/шапка, маленькая тень |
| `hero_wayfarer_walk_s` | `64x64` | 4 | направление south |
| `hero_wayfarer_walk_e` | `64x64` | 4 | направление east |
| `hero_wayfarer_walk_n` | `64x64` | 4 | направление north |
| `hero_wayfarer_walk_w` | `64x64` | 4 | направление west |
| `hero_wayfarer_panel` | `128x192` | 1 | силуэт для правой панели |

### Aul

| Asset id | Size | Notes |
| --- | --- | --- |
| `aul_ground_2x2` | `128x128` | утоптанная земля на 2x2 |
| `aul_yurt_small_01` | `64x64` | войлочная юрта |
| `aul_yurt_small_02` | `64x64` | вариант юрты |
| `aul_fire_01` | `64x64` | 3 кадра пламени |
| `aul_supplies_rack_01` | `64x64` | маленькое улучшение |
| `aul_tamga_post_01` | `64x64` | знак рода |

### Road

| Asset id | Size | Notes |
| --- | --- | --- |
| `road_straight_ns` | `64x64` | дорога вертикальная |
| `road_straight_ew` | `64x64` | дорога горизонтальная |
| `road_corner_ne` | `64x64` | поворот |
| `road_corner_es` | `64x64` | поворот |
| `road_corner_sw` | `64x64` | поворот |
| `road_corner_wn` | `64x64` | поворот |
| `road_entry_aul` | `64x64` | выход из аула |
| `road_current_highlight` | `64x64` | мягкое кольцо/пыль под героем |

### Road Buffer

| Asset id | Size | Notes |
| --- | --- | --- |
| `buffer_edge_stones_01` | `64x64` | универсальный край |
| `buffer_packed_sand_01` | `64x64` | no-build утоптанная зона |
| `buffer_stakes_01` | `64x64` | колышки, редкий вариант |
| `buffer_cart_marks_01` | `64x64` | следы повозки |

### Base Decor

| Asset id | Size | Notes |
| --- | --- | --- |
| `decor_dune_01` | `64x64` | спокойный песок |
| `decor_stones_01` | `64x64` | камни |
| `decor_dry_grass_01` | `64x64` | сухая трава |
| `decor_tracks_01` | `64x64` | старые следы |
| `decor_bones_01` | `64x64` | кости |
| `decor_cracks_01` | `64x64` | трещины |

### Tiles

| Asset id | Size | Placement | Notes |
| --- | --- | --- | --- |
| `tile_saxaul_01` | `64x64` | `roadside` | первый обучающий тайл |
| `tile_oasis_01` | `64x64` | `roadside`/rare `field` | сильная помощь |
| `tile_yurt_01` | `64x64` | `field` | стоянка/экономика |
| `tile_tamga_stone_01` | `64x64` | `field` | память |
| `tile_wolf_track_01` | `64x64` | `roadside` | риск |
| `tile_mirage_01` | `64x64` | `field` | Mind-риск |
| `tile_storm_01` | `64x64` | `field` | Spirit-риск |
| `tile_last_tamga_01` | `64x64` | road event marker | смерть/память |

### UI Icons

| Asset id | Size | Notes |
| --- | --- | --- |
| `icon_stamina` | `24x24` | сердце/силы без modern medical cross |
| `icon_supplies` | `24x24` | мешок/связка припасов |
| `icon_wisdom` | `24x24` | бирюзовый знак/резьба |
| `icon_glory` | `24x24` | красный родовой знак/лента |
| `icon_circle` | `24x24` | loop ring |
| `icon_last_tamga` | `24x24` | камень/кость со знаком |

### FX

| Asset id | Size | Frames | Notes |
| --- | --- | --- | --- |
| `fx_dust_step` | `64x64` | 4 | шаг героя |
| `fx_pickup_popup` | `96x32` | 3 | small gain marker |
| `fx_tile_placed` | `64x64` | 4 | песок оседает |
| `fx_tile_trigger` | `64x64` | 4 | мягкий radial pulse |
| `fx_last_tamga_spawn` | `64x64` | 6 | бирюзовый знак проступает |

## Atlas Plan

### Layer Model

Do not bake every gameplay object into a full sand tile.

Rendering/composition target:

```text
ground_base      -> opaque sand/road/aul ground tile
ground_decor     -> optional opaque/alpha quiet decor for empty buildable cells
object_sprite    -> alpha sprite for active tile object: saxaul, oasis, yurt, tamga_stone
unit_sprite      -> alpha sprite for hero/enemies
fx_sprite        -> alpha sprite for dust, popup, trigger, tamga spawn
placement_overlay -> alpha highlight/grid/debug overlay
```

Practical rule:

- `road_*`, `aul_ground_*`, and the simplest `decor_*` can be full `64x64` tiles.
- `tile_saxaul_01`, `tile_oasis_01`, `tile_yurt_01`, `tile_tamga_stone_01`, `tile_wolf_track_01`, `tile_mirage_01`, `tile_storm_01`, hero, UI and FX should be transparent-background sprites.
- If a base decor element needs to be reused over multiple sand variants, make it alpha too: `decor_stones_01_sprite`, `decor_dry_grass_01_sprite`, etc.
- Empty buildable cells are still visually non-empty, but that can be composed as `sand_base + decor_sprite`, not necessarily one baked bitmap per decor.

GDD owner status: confirmed. Active gameplay objects must not be baked into full sand tiles. This is required for placement overlays, valid slot highlighting, placement animations and future synergies.

### Resolution

- `ground_tiles_1x.png`: `512x512`, opaque cells `64x64`, padding `2px`, extrude `1px`.
- `ground_tiles_2x.png`: `1024x1024`, opaque cells `128x128`, padding `4px`, extrude `2px`.
- `object_sprites_1x.png`: `512x512`, transparent cells `64x64`, padding `2px`, extrude `1px`.
- `object_sprites_2x.png`: `1024x1024`, transparent cells `128x128`, padding `4px`, extrude `2px`.
- `characters_1x.png`: `512x256`, transparent frame `64x64`, padding `2px`.
- `characters_2x.png`: `1024x512`, transparent frame `128x128`, padding `4px`.
- `ui_icons_1x.png`: `256x256`, icons `24x24` in `32x32` slots, padding `2px`.
- `fx_1x.png`: `512x256`, transparent frame `64x64` or `96x32`, padding `2px`.

### `ground_tiles_1x` layout

```text
row 0: road_straight_ns | road_straight_ew | road_corner_ne | road_corner_es | road_corner_sw | road_corner_wn | road_entry_aul | road_current_highlight
row 1: buffer_edge_stones_01 | buffer_packed_sand_01 | buffer_stakes_01 | buffer_cart_marks_01 | decor_dune_01 | decor_stones_01 | decor_dry_grass_01 | decor_tracks_01
row 2: decor_bones_01 | decor_cracks_01 | aul_ground_2x2_q0 | aul_ground_2x2_q1 | aul_ground_2x2_q2 | aul_ground_2x2_q3 | reserved | reserved
```

### `object_sprites_1x` layout

```text
row 0: tile_saxaul_01 | tile_oasis_01 | tile_yurt_01 | tile_tamga_stone_01 | tile_wolf_track_01 | tile_mirage_01 | tile_storm_01 | tile_last_tamga_01
row 1: aul_yurt_small_01 | aul_yurt_small_02 | aul_fire_01_f0 | aul_fire_01_f1 | aul_fire_01_f2 | aul_tamga_post_01 | aul_supplies_rack_01 | reserved
row 2: decor_stones_01_sprite | decor_dry_grass_01_sprite | decor_tracks_01_sprite | decor_bones_01_sprite | decor_cracks_01_sprite | reserved | reserved | reserved
```

2x2 assets can be either four `64x64` quadrants or a separate `128x128` sprite. For the first playable, quadrants are easier for a fixed tile grid.

### Naming Rules

- World tile assets: `road_*`, `buffer_*`, `decor_*`, `tile_*`, `aul_*`.
- UI assets: `icon_*`, `card_*`.
- FX assets: `fx_*`.
- Animation frames suffix: `_f0`, `_f1`, `_f2`.
- Direction suffix: `_n`, `_e`, `_s`, `_w`, `_ns`, `_ew`, `_ne`, `_es`, `_sw`, `_wn`.

### Cutting Rules

- Trim disabled for ground tiles; every tile keeps full `64x64`.
- Trim disabled for object atlases too unless the runtime already supports per-sprite offsets. Transparent sprites still live in fixed `64x64` cells.
- Sprite origin: center bottom for hero; center for map tiles; center for FX.
- Padding is mandatory because WebGL filtering can bleed neighboring cells.
- Use nearest or pixel-perfect upscale for low-res style; use linear only if painterly variant is chosen.

## Style Directions

### A. Flat Readable Jam Style

Описание: production baseline for first playable. Плоские крупные формы, 4-6 цветов на asset, легкие тени, минимум текстуры.

Плюсы:

- Быстрее всего сгенерировать и дорисовать руками.
- Максимальная читаемость `road_buffer` и placement.
- Хорошо работает на маленьких `64x64` тайлах.
- Легко поддерживать единый atlas.

Минусы:

- Может выглядеть слишком прототипно без аккуратного lighting pass.
- Меньше атмосферной глубины пустыни.

Prompt:

```text
top down 2D game tile sprite, Turkic nomadic desert steppe theme, flat readable jam art style, simple bold silhouette, warm ochre sand palette, small felt yurt and road loop assets, clean shapes, subtle shadow, 64x64 sprite, transparent background, game asset atlas friendly
```

Negative prompt:

```text
arabian palace, genie lamp, flying carpet, ornate bazaar, golden dome, aladdin, realistic photo, excessive texture, tiny unreadable details, isometric city, UI text, watermark
```

### B. Painterly Pixel / Low-Res Texture Style

Описание: later polish candidate, not first playable baseline. Низкое разрешение, hand-painted мазки внутри пиксельной формы, сухой песок и войлок более живые.

Плюсы:

- Сильнее атмосфера ветра, песка и памяти рода.
- Красивее fake shots и маркетинговые кадры.
- Base decor может выглядеть живее.

Минусы:

- Риск потерять читаемость placement.
- Дольше выравнивать style между ассетами.
- AI-генерация часто дает слишком много шумной фактуры.

Prompt:

```text
low resolution painterly pixel art game tile, top down desert steppe, Turkic nomadic camp, felt yurt, tamga stone, dry saxaul bush, warm ochre and clay palette, chunky readable silhouette, limited texture, 64x64 tile sprite, atlas ready, transparent background
```

Negative prompt:

```text
photorealistic, smooth 3d render, arabian palace, genie lamp, flying carpet, ornate carpet pattern, over detailed sand grains, unreadable tiny marks, text, watermark
```

### C. Parchment / Miniature Map Style

Описание: later style exploration candidate, not first playable baseline. Карта как нарисованная на пергаменте миниатюра: линии, знаки, плоская заливка, будто родовой путь отмечают на старой карте.

Плюсы:

- Очень хорошо поддерживает тему памяти, тамги и имен.
- UI и карта могут стать единым визуальным языком.
- Быстро делать иконки и card art.

Минусы:

- Может ослабить ощущение живого автопути героя.
- Road/path и active tile могут выглядеть как условные символы, а не мир.
- FX пыли, смерти и pickup сложнее сделать убедительными.

Prompt:

```text
top down parchment miniature map game tile, Turkic nomadic desert route, simple ink and flat color, tamga symbols, small yurt camp, road loop, ochre parchment background, readable board game icon style, 64x64 sprite, clean atlas asset
```

Negative prompt:

```text
arabian palace, genie lamp, flying carpet, ornate bazaar, heavy medieval european heraldry, realistic terrain, high detail noise, calligraphy text, watermark
```

## What To Generate First

1. `active_tiles_transparent_sheet_01`: transparent sprite sheet for `saxaul`, `yurt`, `tamga_stone`, `wolf_track` in one scale.
2. `tile_saxaul_01`: final-ish low/wide transparent sprite for first tutorial tile.
3. `road_buffer_set_01`: buffer stones, stakes, packed sand, cart marks, with clear no-build language.
4. `decor_base_set_01`: `dune`, `stones`, `dry_grass`, `tracks`, `bones`, `cracks`, quieter than active tiles.
5. `fake_shot_gameplay_02`: 16:9 frame with stronger `road_path` vs `road_buffer`, warmer aul anchor, less buildable-cell noise, more space around hero/FX.
6. `hero_wayfarer_walk_s/e/n/w`: simple readable 4-frame walk.

First fake shot prompt:

```text
16:9 top down 2D game screenshot mockup, roguelite loop builder, Turkic nomadic clan camp in the center with small felt yurts and campfire, square tile grid, road loop around the camp, visible no-build road buffer made of stones stakes and packed sand, warm desert steppe field around the road, every free buildable cell has small decor dunes stones dry grass tracks bones cracks, one small saxaul bush tile placed roadside, tiny wayfarer hero walking on road, flat readable jam art style, clean silhouettes, warm ochre palette, teal tamga accent, no UI text
```

First fake shot negative prompt:

```text
arabian palace, genie lamp, flying carpet, ornate bazaar, aladdin, empty blank grid cells, huge palm oasis as first tile, photorealism, 3d render, excessive details, unreadable UI, text labels, watermark
```
