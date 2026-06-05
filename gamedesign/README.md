# Геймдизайн

Интерактивный GDD и прототип level editor для `games/turkic-jam-2026`.

## Как открыть

Запусти локальный сервер из корня репозитория:

```powershell
py -3.12 -m http.server 5186
```

Потом открой:

```text
http://localhost:5186/gamedesign/
```

Просто открыть `index.html` тоже можно, но браузер может запретить чтение JSON-файлов с диска. В этом случае сайт использует встроенный стартовый набор данных.

## Структура

- `index.html` - оболочка сайта.
- `styles.css` - оформление.
- `app.js` - интерактивный редактор.
- `docs/gdd.md` - входная точка GDD и порядок чтения.
- `docs/00_concept.md` - крупный концепт.
- `docs/01_world_layout.md` - аул, дорога и пустынные ячейки.
- `docs/02_core_loop.md` - основной игровой цикл.
- `docs/03_systems.md` - системы героя, смерти, Тамги, реликта и аула.
- `docs/04_content_balance.md` - контент MVP и связь с `.ini` / `.tsv`.
- `docs/05_art_direction.md` - арт-дирекшен.
- `docs/06_claude_brief.md` - бриф для реализации Claude.
- `docs/07_ftue.md` - первый опыт игрока и интро.
- `docs/08_loop_hero_research_review.md` - исследование Loop Hero, ревью GDD и план следующих шагов.
- `docs/09_player_actions.md` - действия игрока, карты, смерть и loop между забегами.
- `docs/10_loop_hero_cards_reference.md` - как устроены карты и синергии Loop Hero, что переносим в нашу систему.
- `docs/11_ftue_first_loop_plan.md` - FTUE, первый круг, смерть, новый герой и acceptance checklist.
- `docs/12_playable_developer_plan.md` - план для разработчика после ревью Code: build-slots, карты, смерть, аул, новый герой.
- `docs/13_event_log_and_texts.md` - журнал событий, точки решения, первые шаблоны текста.
- `docs/14_gdd_art_work_plan.md` - план GDD/арт-работы на время реализации playable.
- `docs/15_first_cards_content.md` - первый контент карт, роли, тексты, следующие кандидаты.
- `docs/16_ui_ux_layout.md` - 16:9 gameplay UI, HUD, рука карт, лог, состояния экрана.
- `docs/17_first_10_minutes.md` - поминутный сценарий первых 10 минут playable.
- `docs/18_playable_review_checklist.md` - чеклист ревью первого playable-билда.
- `docs/song_of_tamga_concept.md` - Markdown-конверсия `pesn_tamgi_kontsept.docx`.
- `docs/song_of_tamga_final_ideas.md` - Markdown-конверсия `pesn_tamgi_vyzhimka_finalnye_idei.docx`.
- `docs/song_of_tamga_gdd.md` - Markdown-конверсия `pesn_tamgi_gdd.docx`.
- `data/gdd.json` - GDD, палитра тайлов и список объектов.
- `data/levels/level_01.json` - первый уровень.
- `schema/gdd.schema.json` - черновая JSON Schema для будущей валидации.

## Архитектурная граница

Спека движка прямо исключает scene editor / authoring editor из runtime scope. Поэтому эта папка - отдельный design tool. JSON здесь можно использовать как источник для будущего builder/codegen, но не стоит грузить его напрямую в WASM runtime: runtime по спека должен читать prebuilt binary packs.
