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

---

## Правки вне игры (кандидаты в master)

| Файл | Изменение | Зачем |
|------|-----------|-------|
| `CMakeLists.txt` (корень) | `add_subdirectory(games/turkic-jam-2026)` | подключить игру как таргет верхнего уровня |
| `scripts/tidy.sh` | в `find` добавлена папка `games` | чтобы pre-commit tidy покрывал игры |

> В `engine/` исходники не менялись — вся игра изолирована в `games/`.

---

## Отложено (важное, но не сейчас)

- [ ] Реальный арт: набор Kenney UI целиком + абстрактные фоны (нужен интернет/ассеты). Сейчас фон — тёмный «card», кнопки — 3 Kenney-кнопки.
- [ ] Звук/музыка (если движок поддержит — проверить).
- [ ] Больше сцен (settings, pause, game-over), анимации переходов.
- [ ] Кнопка «НАЧАТЬ» на кириллице в самом меню (i18n уже умеет — просто язык RU).

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
