# Task 6.1 — Input Polling Core and Pointer State

## Цель

Реализовать input subsystem как polling-based engine module с unified pointer model для mouse и touch.

## Основание в спецификации

- `§21.1 Model`
- `§21.2 Pointer state`
- `§4.2 Engine frame order`

## Объём работ

- Ввести `InputPointer` с полями `active`, `down`, `pressed`, `released`, `x`, `y`, `prev_x`, `prev_y`, `dx`, `dy`, `capture_owner`.
- Реализовать `input_begin_frame()` с корректным сбросом transient полей `pressed`, `released`, `dx`, `dy`.
- Подготовить event ingestion API, через которое platform layer передаёт pointer events в input system.
- Поддержать `MAX_POINTERS` и unified обработку mouse/touch как индексов pointer pool.
- Обеспечить polling API для game-side query без callback subscriptions.
- Связать input update с frame lifecycle перед `game_update` и `game_render`.

## Результат

- Игра может в любой момент кадра опрашивать консистентное состояние указателей.

## Критерии приёмки

- Input subsystem не требует event-driven game callbacks.
- Pointer deltas и pressed/released корректно живут один кадр.
- Mouse и touch используют одну модель данных.
- Input state хранится в preallocated массивах.

## Проверка

- Smoke-сценарии на press/move/release подтверждают правильное обновление transient и persistent полей.
- Несколько simultaneous pointers не ломают state machine.

## Зависимости

- Sprint 1.

## Вне scope

- Полноценная клавиатурная карта и action-mapping framework.
- Gesture recognition layer.