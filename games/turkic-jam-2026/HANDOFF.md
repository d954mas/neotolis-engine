# Handoff — Turkic Jam «Песнь Тамги»: первые 5 минут / интро-полиш

Запущено в **ветке игры `turkic-jam-2026`** (главный каталог `C:/projects/neotolis-engine-turkic-jam-2026`).
Цель сессии: довести первые 5 минут игры — FTUE/интро понятно и красиво. Работаем **по одному
решению за раз** (объясняешь варианты → пользователь выбирает; не батчить вопросы).

## Где работать / сборка
- Ветка `turkic-jam-2026` (НЕ master — master чистый движок без игры; не трогать).
- Сборка: `cmake --build build/_cmake/native-debug --target turkic_jam`
- Пак (после правок ассетов/`build_packs.c`): собрать таргет `build_turkic_jam_packs`, затем
  `build/games/turkic-jam-2026/native-debug/build_turkic_jam_packs.exe build/games/turkic-jam-2026`
  (перегенерит `generated/*.h`).
- **Config-готча:** игра грузит `config/` рядом с exe (`build/games/turkic-jam-2026/native-debug/config/`),
  а не репо-конфиг. После правок `config/*.ini|tsv` — копировать туда (или `--config <репо/config>`).
- Pre-commit: build + `ctest` (тест-сборка может быть не настроена — отметить честно) +
  `clang-format --dry-run --Werror <файлы>` + `bash scripts/tidy.sh build/_cmake/native-debug`
  (game-модуль уже несёт пред-существующий tidy-долг по cognitive-complexity — не пугаться).

## Проверка/драйв игры (важно)
- OS-скриншот GLFW-окна отдаёт белое/чёрное — НЕ использовать. Вместо этого движковый дамп:
  - devapi (живой кадр): запустить с `--devapi 9123`, затем `python tools/devapi/devapi_cli.py 9123 "visual_qa.dump path=<file.png>"` → читать PNG.
  - либо флаг `--dump-frame <file> --dump-frame-after <N>` (одноразовый дамп кадра N).
- Драйв: `tools/devapi/devapi_cli.py 9123 ...` (`game.run`, `ui.tree`, `input.click x= y=`, `game.kill`, `visual_qa.dump`). Окно интермиттентно само закрывается в песочнице — это артефакт headless, НЕ баг кода (проверено lldb: `nt_app_quit` не вызывается); перезапускать.
- Чтобы увидеть первый забег (интро): сбросить сейв — `ftue_done=0` в `build/games/turkic-jam-2026/native-debug/turkic_jam_save.txt`.

## Что уже сделано (закоммичено на turkic-jam-2026, tip 14bd02c9)
- **Кинематографичный интро v2** (по разделению движка: **Clay = только UI; сцена/частицы/эффекты = спрайты**):
  - Чёрный экран (спрайт-заливка `fx_solid_01`) + дрейфующий песок (спрайт, Kenney Particle Pack soft-circle `fx_sand_grain_01`, тёплый тон) + строки «Песок стирает следы.» / «Путь ждёт первого путника.» + «нажми, чтобы продолжить» + **Kenney-палец «tap»** (CC0, затонирован в сепию).
  - Первый тап **разблокирует веб-аудио** (main.c уже резюмит звук на первом клике) + запускает dawn-reveal (спрайтовая вуаль уходит) → мир с артом → правая панель **«Отправить путника»**.
  - Новая фаза `TJ_PHASE_AUL_READY` (герой ждёт у костра, пока не отправят); прогрессивное раскрытие (HUD/лог/рука скрыты на интро). Код: `src/view.c` (`draw_intro_fx`, `tj_view_intro_black`, `tj_view_launch_panel`), `src/scenes/scene_game.c` (`s_intro_*`, `ftue_step_tick`), `src/sim.c` (фаза/`tj_run_send_wayfarer`), `g->intro_*` в `src/game.h`.
- **Review round-1:** темп медленнее (`move_seconds_per_cell` 1.3, combat интервалы ×~1.33), заголовок окна «Песнь Тамги», бой/событие не поверх экрана смерти, убран дубль-хинт мешочка.
- **Унификация:** кнопка панели аула «Новый забег» → «Отправить путника». Boot захардкожен в SCENE_GAME (SCENE_MENU deprecated).
- Влит арт-пасс Codex + аудио-движок (miniaudio). devapi `visual_qa.dump` добавлен.
- Доки: полный UX/FTUE-ревью — `games/turkic-jam-2026/FTUE_UX_REVIEW.md`; источники аудио (CC0/CC-BY) — `games/turkic-jam-2026/AUDIO_SOURCES.md`.

## Открытые задачи (приоритет)
1. **Частицы песка** — сейчас читаются как тонкие «кольца» (виден ободок круга Kenney circle_01). Сделать мягче/плотнее или взять другой круг/дым (в `draw_intro_fx`, `view.c`). Kenney Particle Pack распакован в `%TEMP%\kpp`, Input Prompts в `%TEMP%\kip`.
2. **Кнопка «Отправить путника»** — текст+позиция должны СОВПАДАТЬ с кнопкой на экране аула после смерти (интро учит этой кнопке). Якорить обе к низу панели, чтобы совпадало пиксель-в-пиксель.
3. **Аудио** — вписать выбранные пользователем треки из `AUDIO_SOURCES.md` (music + SFX) через `nt_audio` + `src/audio_assets.c`; интро-музыка стартует на первом тапе. Конвертировать OGG→WAV/MP3.
4. **Credits** — секция в README + окно «Credits» из модалки настроек (движок-депы, Kenney CC0 [без атрибуции], CC-BY аудио). Блок атрибуции — в `AUDIO_SOURCES.md`.
5. **Отложено:** сломанный рендер дороги (тайлы арт-пасса не стыкуются на детурах — это генерация/арт, чинить позже).

## Контекст дизайна (кратко)
- Тон: **сказочные вайбы/мотивы** (костёр, аул, путник, степь, Тамга) — НО не медленная поэтичная книжка. Игра **играет сама**, темп спокойный. Подача: атмосферно + по делу.
- Доки FTUE: `gamedesign/docs/26_ftue_step_1_narrative.md` (интро), `22/23` (сценарий/анализ). **Готча:** их МЕХАНИКА (Саксаул/roadside/wisdom/glory/3-выбор) — устарела (до Capybara-редизайна); актуально = мешочек/мердж/одна валюта припасы/авто-бой.
- Память проекта (подгрузится сама): intro-direction, ask-one-question-at-a-time, worktree-base, balance/audio decisions.
