# Task 1.2 — Engine Core Loop and Lifecycle

## Цель

Зафиксировать и реализовать top-level frame order движка и жёсткий контракт между engine и game callbacks.

## Основание в спецификации

- `§4.1 Engine callbacks exposed to game`
- `§4.2 Engine frame order`
- `§4.3 Fixed update loop`
- `§4.6 Systems registry not used`
- `§25 Engine/Game Boundary`

## Объём работ

- Ввести game callbacks: `game_init`, `game_fixed_update`, `game_update`, `game_render`, `game_shutdown`.
- Реализовать engine-owned frame flow в порядке из `§4.2`: `platform_step`, `input_begin_frame`, `input_event_apply`, `resource_step`, `audio_update`, fixed loop, `game_update`, `transform_update`, `game_render`, `frame_temp_reset`.
- Ввести `EngineSettings` с `fixed_dt` и `max_fixed_steps`.
- Реализовать accumulator-based fixed update loop без скрытой регистрации systems.
- Явно оставить orchestration gameplay systems на стороне `game_*` функций.
- Подготовить места интеграции для будущих `resource_step`, `audio_update`, `transform_update`, даже если на этом этапе они временно пустые.

## Результат

- У движка появляется единственная canonical frame sequence.
- Игра получает только explicit callback-точки и сама управляет порядком своей логики.

## Критерии приёмки

- Нет глобального scheduler-а, phase system или dependency graph.
- Fixed loop ограничен `max_fixed_steps`, чтобы избежать spiral of death.
- `transform_update` вызывается после gameplay update и до render.
- `frame_temp_reset` выполняется в конце каждого кадра.

## Проверка

- Smoke-run с пустым game module проходит последовательность callbacks в правильном порядке.
- Параметры `fixed_dt` и `max_fixed_steps` меняются через runtime settings, а не через внешнюю config DSL.

## Зависимости

- Task 1.1.
- Task 1.6.

## Вне scope

- Реализация конкретных gameplay systems.
- Async loading, input и audio детализация.