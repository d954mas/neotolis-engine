# Task 3.4 — Fetch Bridge and Loading Progress

## Цель

Подключить web-specific async fetch bridge к resource system и встроить его в frame loop без блокирующих ожиданий.

## Основание в спецификации

- `§18.1 Overview`
- `§18.4 Pack loading flow`
- `§18.5 Loading progress`
- `§18.6 JS bridge — fetch contract`

## Объём работ

- Реализовать C API `platform_request_fetch(request_id, url)` и callbacks `platform_on_fetch_progress`, `platform_on_fetch_complete`.
- Связать `request_id` с `PackMeta` и обработкой state transitions.
- Обновлять `bytes_received` и `bytes_total` через progress callbacks из `ReadableStream`-совместимого JS bridge.
- На completion переводить pack в `LOADED` и передавать blob pointer/size в runtime.
- Встроить обработку fetch callbacks в `resource_step()` и гарантировать, что активация начинается только на следующей безопасной фазе кадра.
- Реализовать retry policy 1-2 попытки с exponential backoff и логированием ошибки после исчерпания ретраев.

## Результат

- Pack loading становится полноценной частью frame-based runtime, а не внешним ad-hoc процессом.

## Критерии приёмки

- Main thread не блокируется ожиданием сети.
- Progress и completion callbacks не обходят `resource_step()` и не мутируют registry из произвольной точки кода.
- `bytes_total == 0` корректно поддерживается как сценарий без процента.
- Ошибки сети переводят pack в `FAILED` после исчерпания retries.

## Проверка

- Smoke-тест с успешной и неуспешной загрузкой подтверждает корректные state transitions.
- Повторный запрос после failure не ломает request bookkeeping.

## Зависимости

- Task 1.3.
- Task 3.2.
- Task 3.3.

## Вне scope

- Browser caching strategy.
- Non-web platform I/O backends.