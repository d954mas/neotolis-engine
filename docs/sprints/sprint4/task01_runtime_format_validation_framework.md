# Task 4.1 — Runtime Format Validation Framework

## Цель

Ввести общий каркас safety-net validation для runtime formats, чтобы все asset loaders проверяли совместимые минимальные invariants единообразно.

## Основание в спецификации

- `§20.1 General rule`
- `§20.2 Runtime format validation`
- `§24.2 Error policy`

## Объём работ

- Зафиксировать общий набор runtime header полей: magic, type, version, size/offset bounds.
- Реализовать shared helpers для проверки header integrity и безопасного чтения variable-size payload sections.
- Подготовить mapping format type -> loader и единый путь recoverable/fatal reporting.
- Разделить builder-primary validation и runtime safety-net validation в коде и документации.
- Подготовить тестовые malformed fixtures для каждого поддерживаемого asset family.
- Зафиксировать, какие несовместимости приводят к placeholder fallback, а какие являются fatal только на startup/critical backend init.

## Результат

- Все loaders используют единые правила проверки форматов и не дублируют несогласованную validation-логику.

## Критерии приёмки

- Runtime не пытается повторить тяжёлую builder validation ссылок и authoring semantics.
- Invalid sizes/offsets не приводят к out-of-bounds чтению.
- Логирование validation failures проходит через общий error baseline.
- Loader contract одинаков для shader, material, texture, mesh и будущих форматов.

## Проверка

- Invalid fixtures воспроизводимо приводят к `FAILED` state или placeholder fallback.
- Valid fixtures проходят в loaders без формат-специфичных обходных путей.

## Зависимости

- Sprint 3.
- Task 1.5.

## Вне scope

- Builder-side schema generation.
- Авторские source форматы.