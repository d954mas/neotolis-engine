# Task 1.5 — Logging, Errors, and Debug Baseline

## Цель

Ввести единый debug/error baseline, чтобы последующие подсистемы не создавали собственные несогласованные правила логирования и обработки отказов.

## Основание в спецификации

- `§24.1 Logging levels`
- `§24.2 Error policy`
- `§24.3 Debug overlay`
- `§25 Engine/Game Boundary`

## Объём работ

- Ввести уровни логирования `INFO`, `WARN`, `ERROR`, `ASSERT`, `PANIC`.
- Разделить fatal и recoverable ошибки в соответствии со спецификацией.
- Подготовить общие macros/helpers для assertions, panic paths и recoverable report paths.
- Определить минимальный набор runtime counters, которые будут наполняться по мере появления подсистем: frame time, fixed step count, temp memory usage, draw calls, batch count, loaded resources, active packs, audio voices.
- Подготовить hooks для browser console и для будущего overlay UI.
- Зафиксировать правило: runtime делает safety-net validation и fallback, но не повторяет тяжёлую builder validation.

## Результат

- Все следующие задачи получают единый язык для ошибок, asserts и debug instrumentation.
- Появляется база для later debug overlay без архитектурного долга.

## Критерии приёмки

- Fatal условия приводят к предсказуемой остановке/abort-path.
- Recoverable ошибки логируются и оставляют систему в валидном fallback-state.
- Логирование не превращается в глобальный service locator или event bus.
- Debug counters можно собирать без heap и без ломки hot path.

## Проверка

- Есть минимальный smoke-сценарий для recoverable и fatal путей.
- Формат сообщений позволяет быстро понять модуль, причину и severity.

## Зависимости

- Task 1.2.
- Task 1.4.
- Task 1.6.

## Вне scope

- Полноценный визуальный overlay UI.
- Телеметрия, crash reporting, удалённый лог-агрегатор.