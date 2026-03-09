# Sprint 6 — Input, Audio, and Diagnostics

## Цель спринта

Добавить engine-owned subsystems, которыми пользуется игра в каждом кадре: polling input, pointer capture, audio playback и debug overlay/statistics.

## Почему это отдельный спринт

- Эти подсистемы не должны размывать ECS и renderer архитектуру, но интегрируются с frame loop и resource system.
- `Input` и `Audio` в спецификации являются engine modules, а не компонентами или game-side wrappers.
- Debug overlay нужен для верификации производительности и корректности рантайма до появления реального контента.

## Scope спринта

- Input polling state и begin/apply/reset semantics.
- Pointer capture и координатное преобразование через display info.
- Audio handles, clip/voice pool и web backend.
- Async decode integration через resource pipeline.
- Overlay/diagnostics и smoke harness для подсистем.

## Задачи

1. [Task 6.1 — Input Polling Core and Pointer State](./task01_input_polling_core_and_pointer_state.md)
2. [Task 6.2 — Pointer Capture and Coordinate Mapping](./task02_pointer_capture_and_coordinate_mapping.md)
3. [Task 6.3 — Audio Core Module and Voice Pool](./task03_audio_core_module_and_voice_pool.md)
4. [Task 6.4 — Web Audio Bridge and Resource Integration](./task04_web_audio_bridge_and_resource_integration.md)
5. [Task 6.5 — Debug Overlay and Subsystem Smoke Harness](./task05_debug_overlay_and_subsystem_smoke_harness.md)

## Результат спринта

- Game code получает предсказуемый polling input и handle-based audio API.
- Команда разработки получает базовые runtime stats и smoke сценарии для дальнейшей работы.

## Definition of Done

- Input остаётся polling-based, а capture — explicit ownership API.
- Audio public API не содержит platform-specific типов.
- Overlay собирает метрики из спецификации без тяжёлой UI framework зависимости.

## Опорные разделы спецификации

- `§21 Input System`
- `§22 Audio System`
- `§24.3 Debug overlay`
- `§27 Suggested Implementation Order` пункты 16-17