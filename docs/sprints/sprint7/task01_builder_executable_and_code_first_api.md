# Task 7.1 — Builder Executable and Code-First API

## Цель

Поднять builder как отдельную программу на C23 с code-defined rules и зафиксировать его публичный API для packs и asset additions.

## Основание в спецификации

- `§23.1 Builder model`
- `§23.2 Why code-based builder`
- `§23.3 Builder module layers`
- `§23.4 Core builder API`
- `§28` locked decisions 7-8

## Объём работ

- Создать `builder/` программу с модулями `main_builder.c`, `builder_pack.c`, `builder_manifest.c`, importer modules и `builder_project.c`.
- Реализовать `start_pack`, `finish_pack`, typed add-функции для одиночных assets и wildcard паттернов.
- Явно разрешить несогласованность спецификации между `add_audio("pattern")` в `§23.1` и `add_audio(path)`/`add_audios(pattern)` в `§23.4`: выбрать и задокументировать canonical naming, не скрывая расхождение.
- Подготовить project-level entrypoint, где game/project код описывает builder rules обычным C-кодом.
- Исключить DSL, JSON config и plugin-style importer registration на этом baseline.
- Подготовить базовый CLI contract для запуска builder и выбора output location/pack set.

## Результат

- Builder становится отдельной точкой входа в content pipeline и следует той же code-first философии, что и runtime.

## Критерии приёмки

- Builder не зависит от runtime frame loop или platform/web code.
- API typed и явный, без универсального `add_files()`.
- Несогласованность `add_audio`/`add_audios` зафиксирована документированным решением.
- Build rules описываются кодом и допускают project-specific grouping logic.

## Проверка

- Пустой builder project стартует, создаёт pack context и корректно завершает execution.
- Пример rules file читается и исполняется без отдельного DSL parser-а.

## Зависимости

- Task 1.1.
- Sprint 4.

## Вне scope

- GUI/editor для контентного пайплайна.
- Remote asset database.