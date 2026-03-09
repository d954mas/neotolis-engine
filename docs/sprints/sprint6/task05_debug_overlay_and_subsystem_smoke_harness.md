# Task 6.5 — Debug Overlay and Subsystem Smoke Harness

## Цель

Собрать минимальный diagnostic surface для рантайма и подготовить smoke harness, на котором можно проверять ключевые подсистемы до появления настоящей игры.

## Основание в спецификации

- `§24.3 Debug overlay`
- `§4 Frame Lifecycle`
- `§22.8 Integration with frame loop`

## Объём работ

- Подготовить overlay/debug reporting для `frame time`, `fixed step count`, `draw call count`, `batch count`, `loaded resource count`, `active pack count`, `temp memory usage`, `audio voice count`.
- Встроить сбор counters в engine modules без hidden allocations и тяжёлой UI framework зависимости.
- Подготовить browser-console и simple on-screen presentation path для этих метрик.
- Собрать smoke harness/game sample, который проходит frame lifecycle, resource loading, render passes, input capture и audio resume/playback.
- Задокументировать минимальные smoke-сценарии, которые должны выполняться после каждого крупного integration change.
- Подготовить список residual gaps: text later-ready path, builder not yet integrated до Sprint 7, open questions из `§29`.

## Результат

- У проекта появляется минимальная интеграционная среда и observability для следующих итераций.

## Критерии приёмки

- Overlay отражает реальные runtime counters, а не дублирует лог-сообщения.
- Smoke harness не превращается в скрытый sample game framework.
- Метрики собираются из engine-owned subsystems централизованно.
- Ранние performance/regression проблемы видны до полноценного контента.

## Проверка

- Smoke harness демонстрирует хотя бы один pass каждого ключевого subsystem flow: input, resources, render, audio.
- Overlay/counters обновляются кадр за кадром и не требуют heap allocation.

## Зависимости

- Sprint 5.
- Task 6.1.
- Task 6.4.

## Вне scope

- Production-grade profiler.
- Remote telemetry service.