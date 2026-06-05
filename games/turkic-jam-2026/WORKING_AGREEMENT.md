# Песнь Тамги — правила работы над проектом

Игра на движке Neotolis (C17, Web/WASM). Два агента работают параллельно —
координация описана в `coordination/PROTOCOL.md`.

## Кто за что

| | Code (Claude) | GDD (Codex) |
|--|--------------|-------------|
| Владеет | `engine/`, `games/turkic-jam-2026/src/`, `config/CONFIG.md` (схема), загрузчик конфигов, движок/тулинг | дизайн-доки, **значения** в `config/*.ini/*.tsv` |
| Делает | системы/механики/UI, читает конфиги, тесты | баланс, контент, новые механики (через запрос) |
| Не трогает | значения баланса в `config/*` | `src/`, схему конфигов, движок |

## Главный принцип: всё через конфиги

Баланс не хардкодится. Все числа — в `config/` (см. `config/CONFIG.md`). Code
читает их в рантайме; правка значений не требует пересборки C. Запуск с
`--config <dir>` указывает на каталог конфигов (по умолчанию `config/` рядом с exe).

## Раскладка

```
games/turkic-jam-2026/
  config/          баланс (GDD owns values, Code owns schema) + CONFIG.md
  coordination/    PROTOCOL.md, FROM_CODE.md, FROM_GDD.md
  src/             игровой код (Code): config.*, sim.*, scenes/, ui_kit, i18n, save, rng, juice
  main.c           bootstrap + scene loop
  build_packs.c    оффлайн-сборка ассетов
  GDD.md/README.md документация шаблона
```

## Сборка / запуск / тест (Code)

- Сборка (native, с devapi + asserts): `cmake --build build/_cmake/native-debug --target turkic_jam`
- Запуск: `build/games/turkic-jam-2026/native-debug/turkic_jam.exe --devapi 9123 [--config <dir>]`
- **Тест/итерация через devapi**: `python tools/devapi/smoke_test.py 9123` (см. скилл `drive-game`).
  Это канал «бот ↔ игра»: читать UI/состояние, эмулировать ввод. Им же гоняю регрессии.
- Перед коммитом: build + ctest + clang-format + `bash scripts/tidy.sh` (см. AGENTS.md).

## Итерация баланса (GDD)

1. Правишь `config/*.ini` / `config/*.tsv` (формат — `CONFIG.md`).
2. Перезапуск игры (или `--config` на свой каталог) — числа применяются без пересборки.
3. Нужна новая механика/поле/таблица — запись в `coordination/FROM_GDD.md`.

## Тон/контент

Тюркско-кочевой тон (аул, тамга, юрты, степь, духи ветра, волк, сказители, память
рода). Не уходить в «Алладин»-вайб. Меланхолично, но не мрачно (из GDD §25).
