# Turkic Jam 2026 — заметки по движку

Журнал работы над игрой на Neotolis Engine во время джема.
Сюда записываю **правки в движке**, **сложности** и **что добавлено в базу**.

- Ветка: `turkic-jam-2026`
- Движок: Neotolis Engine (C17, Web/WASM, WebGL 2)
- Игра/шаблон: `games/turkic-jam-2026/` (таргет `turkic_jam`)
- Старт: 2026-06-05

---

## Что добавлено в базу (ядро шаблона)

Минимальный, но крепкий каркас под любую 2D/UI-игру. Всё в `games/turkic-jam-2026/`.

| Компонент | Файл(ы) | Что даёт |
|-----------|---------|----------|
| Рабочий шаблон + bootstrap | `main.c` | окно 1280×720, gfx/UI init, рендер-цикл, восстановление GL-контекста (web), общий «card»-шелл |
| Менеджер сцен | `src/scene.h` + `main.c` (`game_goto`/`apply_transition`) | таблица функций `on_enter/on_update/on_exit`, переход между сценами на границе кадра |
| Сцена Menu | `src/scenes/scene_menu.c` | заголовок + крупная кнопка **START** + кнопка языка |
| Сцена Game (заглушка) | `src/scenes/scene_game.c` | счёт по тапу, persisted **best**, кнопка BACK |
| Локализация i18n | `src/i18n.{h,c}` | EN / RU / TR, `i18n(key)`, переключение `i18n_cycle()`, реальный рендер кириллицы и турецкого |
| Сохранения (KV) | `src/save.{h,c}` | web: `localStorage` (EM_JS), native: файл; `save_set/get_int/str` + `save_flush` |
| UI-kit (Kenney) | `src/ui_kit.{h,c}` | `tj_button()` (slice9 Kenney, hover/press-анимация), стили заголовка/текста/подсказки |
| Сборка пака | `build_packs.c` | `.ntpack`: шейдеры + атлас (white + 3 Kenney-кнопки) + шрифт Roboto (ASCII+кириллица+турецкий) |
| Kenney UI (арт) | `raw/ui/button_{blue,green,red}_depth.png` | переиспользованы из `ui_buttons_demo` (CC0) |
| CMake-таргет | `CMakeLists.txt` + корневой `CMakeLists.txt` | `turkic_jam` (рантайм) + `build_turkic_jam_packs` (билдер), копирование пака по пресетам |
| VS Code | `.vscode/tasks.json`, `.vscode/launch.json` | задачи: собрать паки/игру, запустить native, собрать+поднять web; launch: Debug Desktop / Debug WASM (`:8100`) |

**Проверено:** native-debug → `turkic_jam.exe` ✅, wasm-debug → `index.html` ✅, clang-format ✅, clang-tidy ✅.

### Слой 2 (добавлено далее, после ресёрча гайдов)

| Компонент | Файл(ы) | Что даёт |
|-----------|---------|----------|
| RNG (seeded) | `src/rng.{h,c}` | xorshift32: `rng_u32/float/range/range_int/chance` + explicit-state |
| Juice | `src/juice.{h,c}` | easing (`out_quad/in_out_quad/out_back`) + trauma screen-shake; шейк применяется к «card» через XFORM |
| Сцена Settings | `src/scenes/scene_settings.c` | выбор языка (подсветка текущего) + сброс прогресса |
| Сцена Pause | `src/scenes/scene_pause.c` | `P`/Resume → игра, Menu → меню (счёт сохраняется в `game_ctx`) |
| Сцена GameOver | `src/scenes/scene_gameover.c` | финальный счёт + Retry/Menu |
| README + GDD | `README.md`, `GDD.md` | архитектура/как добавить сцену+строку; шаблон ГДД на одну страницу |
| Web-пакет | `scripts/package_game_web.sh` + задача `Package turkic_jam web (zip)` | zip релизной web-сборки для itch.io |
| Launch паков | `.vscode/launch.json` → `Build Turkic Jam Packs` | запуск/отладка билдера паков |

Геймплей-демо: Game-сцена — тап (+1, screen-shake), best сохраняется, `P` = пауза, Lose → game-over. Меню: START / Settings / язык.

### Слой 3 — devapi (нужен API для общения с движком)

Нужен **движковый модуль `nt_devapi`** — канал общения с игрой для **тестов и работы ботов** (live introspection + эмуляция инпута). Архитектура: **реестр эндпоинтов** (подсистемы регистрируют `ui.tree`, `entity.list`, `input.*`), единый `dispatch(line)->json`. Протокол — console-style `key=value` (`input.click x=640 y=360`), ответ — строка JSON (в движке нет JSON-парсера, только пишем).

- **ПК (сейчас):** TCP-сокет на `127.0.0.1` (Winsock/posix, non-blocking, опрос в кадре). Низкая задержка, бот = цикл recv/send, драйв из Bash.
- **Браузер (потом):** экспорт C-функции + Playwright (`page.evaluate(Module.ccall)`) — тот же реестр.
- Гейт компиляции `NT_DEVAPI_ENABLED` (как `NT_UI_DEBUG_TOOLS`); ON в пресете `native-debug`.
- Эндпоинты v1: `ping`, `endpoints`, `view`, `ui.tree`, `ui.element id=`, `entity.list`, `input.key key=P mode=tap`, `input.move`, `input.click x= y=`, `input.click_ui id=`, `input.button`.
- Файлы: `engine/devapi/nt_devapi.{h,c}` + `nt_devapi_net.c`; клиент `tools/devapi/devapi_cli.py` + бот `devapi_bot_demo.py`; скилл `.claude/skills/drive-game/SKILL.md`.
- **Статус: ✅ сделано и проверено на ПК.** Запуск: `turkic_jam.exe --devapi 9123`. Прогон бота: прочитал `ui.tree` (меню), кликнул START через `input.click`, сцена сменилась на игровую (`Playing/Score/TAP +1/Lose`). Бот реально может играть.
- **Автотест** `tools/devapi/smoke_test.py` сам поднимает игру и прогоняет весь флоу (меню→настройки→назад→старт→тап→пауза→resume→lose→gameover→retry) с проверками — **9/9 PASS, exit 0**. VS Code: задача `Smoke test (devapi)`. Заодно подтвердил рендер EN/RU/TR (`English | Русский | Türkçe`).
- Находки: `-Wformat-nonliteral` на vsnprintf-обёртке → атрибут `format(printf,…)`; `bugprone-narrowing-conversions` int→char → ci-compare без сужения; `readability-non-const-parameter` на хендлерах фикс-ABI → NOLINT; PowerShell-пайп строки добавляет BOM в первую строку (драйвить Python-клиентом, не пайпом).
- Браузер (потом): экспорт C-функции + Playwright (тот же реестр/протокол).

### Слой 4 — кор «Песнь Тамги» (data-driven)

Игра — roguelite loop-builder (GDD-доки в Downloads). Кор **полностью на конфигах**:
баланс в `config/` (ini + `|`-таблицы), кор читает в рантайме, ничего не хардкодит.
- `config/`: `balance.ini`, `heirs.tsv`, `tiles.tsv` (+ `CONFIG.md` — схема). Старт сразу в игру — `start_in_game=1`.
- `src/config.{h,c}` — загрузчик; `src/sim.{h,c}` — забег (авто-движение по кольцу из N клеток, stat-check тайлов `diff=base+per*circle`, Силы→смерть).
- `scene_game` = реальный забег (меню опционально). `g->prev` чтобы пауза не рестартила забег.
- **Координация с GDD-агентом (Кодекс):** `coordination/` (PROTOCOL + FROM_CODE/FROM_GDD) + `WORKING_AGREEMENT.md`. Интерфейс = конфиги (схема — Code, значения — GDD). Кодекс уже ответил в логе, прислал схемы `trials/synergies/aul`.
- Проверено devapi-смоуком 5/5 PASS: config загружен (6 тайлов, 3 наследника), забег идёт сам, смерть→game-over→retry. Игра регистрирует свой эндпоинт `game.config`.
- Отклонение от спеки: runtime-парсер конфигов (осознанно, ради итераций; релиз — запаковать).
- Дальше (приоритет GDD): `trials.tsv` → `synergies.tsv` → `aul.tsv`; затем расстановка тайлов картами, выбор наследника, экран аула, Последняя Тамга/реликт.

---

## Сложности / трудности в движке и тулчейне

| Дата | Область | Что произошло | Статус |
|------|---------|---------------|--------|
| 2026-06-05 | build | Скопированный репозиторий — все 21 CMake-кеша в `build/_cmake` ссылались на старый путь `c:/projects/neotolis-engine`. Лечится удалением `build/_cmake` + переконфиг. | ✅ решено |
| 2026-06-05 | tooling | `clang-format` ломает JS внутри `EM_JS`: `===` → `== =`. Нужен `/* clang-format off */` (чистый токен, без текста после `off`!) вокруг EM_JS. | ✅ решено |
| 2026-06-05 | tooling | Строгие флаги WASM (`-Wextra-semi -Werror`) ругаются на `;` после `EM_JS(...)`. Обёрнуто `#pragma clang diagnostic ignored "-Wextra-semi"`. | ✅ решено |
| 2026-06-05 | lint | `scripts/tidy.sh` не сканировал `games/` — добавил папку в `find`. | ✅ решено |
| 2026-06-05 | lint | Проектный `.clang-tidy` строгий: cognitive-complexity ≤ 25 (разбил `try_bind_resources`), запрещены избыточные касты (enum-константа в C уже `int`). | ✅ учтено |
| 2026-06-05 | font/i18n | Шрифт по умолчанию — Roboto, **есть** кириллица и турецкий. Charset расширен в `build_packs.c`. Движок UTF-8-aware (`engine/utf8`). | ✅ работает |
| 2026-06-05 | warnings | WASM-пресет строже native: `-Wsign-conversion` ловит enum-**переменную** → `int` (enum-**константа** по C — уже `int`, потому в `main.c` не всплыло). Каст `(int)lang`. | ✅ решено |
| 2026-06-05 | lint | Проектный `.clang-tidy`: `readability-math-missing-parentheses` требует скобки вокруг `*` среди `+`/`-` (поправлено в `rng/juice`). | ✅ учтено |
| 2026-06-05 | vscode | В `launch.json` не было конфига билдера паков → добавлен `Build Turkic Jam Packs`. | ✅ решено |
| 2026-06-05 | engine | **Аудио-модуля в движке нет** (`engine/` без audio/sound). Топ-эссеншл джема — требует platform-слоя (web: WebAudio/OpenAL). | ⏳ отдельная сессия |

---

## Правки вне игры (кандидаты в master)

| Файл | Изменение | Зачем |
|------|-----------|-------|
| `CMakeLists.txt` (корень) | `add_subdirectory(games/turkic-jam-2026)` | подключить игру как таргет верхнего уровня |
| `scripts/tidy.sh` | в `find` добавлена папка `games` | чтобы pre-commit tidy покрывал игры |

> В `engine/` исходники не менялись — вся игра изолирована в `games/`.

---

## Отложено (важное, но не сейчас)

- [ ] **Аудио** (музыка + SFX): в движке нет модуля — нужен platform-слой (web: WebAudio/OpenAL). Самый весомый пробел.
- [ ] Реальный арт: полный набор Kenney UI + абстрактные фоны (нужен интернет/ассеты). Сейчас фон — тёмный «card», кнопки — 3 Kenney-кнопки.
- [ ] Анимации переходов между сценами (fade), частицы/«поп» на событиях.
- [ ] Игровой «actor»-слой поверх sprite_comp/transform_comp для 2D-объектов.
- [x] Сцены Settings / Pause / Game-over — сделано (слой 2).
- [x] RNG, juice (easing + shake) — сделано (слой 2).
- [x] Web-пакет для сабмита, GDD-шаблон — сделано (слой 2).

---

## Как запускать

**Native (desktop):**
- VS Code → Run and Debug → **Debug Desktop turkic_jam** (соберёт паки+игру, запустит).
- CLI: `cmake --build build/_cmake/native-debug --target turkic_jam` → `build/games/turkic-jam-2026/native-debug/turkic_jam.exe`.

**Web (WASM):**
- VS Code → Run and Debug → **Debug WASM turkic_jam** (соберёт паки, wasm, поднимет сервер на `:8100`, откроет Chrome).
- Сначала один раз: задача **Build & run turkic_jam packs (debug)** (генерирует `.ntpack` + заголовки в `generated/`).

**Управление:** мышь — кнопки; `L` — сменить язык (EN→RU→TR); `Esc` — выход (native).

---

## Заметки по ходу

<!-- Свободный текст: гипотезы, обходные пути, ссылки на спеку (docs/neotolis_engine_spec_1.md) -->
