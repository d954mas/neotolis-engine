# Sprint 1 — Foundation Runtime

## Цель спринта

Заложить минимальный, но архитектурно корректный runtime-каркас движка: модульную структуру, основной frame lifecycle, web platform layer, memory policy, build contract и базовую диагностику.

## Почему это первый спринт

- `§3`, `§4` и `§5` задают каркас, на который опираются все остальные подсистемы.
- Без platform abstraction и frame loop нельзя корректно встроить async loading, input, audio и renderer.
- Без compile-time limits и явной memory policy легко нарушить один из главных baseline-принципов: отсутствие heap-аллоцирования в hot path.
- Без фиксированного build system layer проект очень быстро расползётся на ad-hoc команды, несовместимые флаги и неявные target-ы для runtime и builder.

## Scope спринта

- Разделить будущий код на `engine/`, `builder/`, `game/`.
- Зафиксировать top-level runtime lifecycle и game callbacks.
- Описать platform/web boundary и JS bridge surface для дисплея, resize и frame stepping.
- Ввести memory categories, лимиты и frame scratch reset.
- Ввести logging/error policy и минимальные debug counters/hooks.
- Зафиксировать build system baseline: target-ы, профили, compiler/linker flags, warning policy, output layout и smoke targets.
- Зафиксировать repo topology, пригодную как для standalone-разработки движка, так и для подключения engine как submodule в внешний game-репозиторий.

## Порядок выполнения внутри спринта

1. [Task 1.1 — Module Layout and Build Skeleton](./task01_module_layout_and_build_skeleton.md)
2. [Task 1.6 — Build System, Toolchains, Profiles, and Flags](./task06_build_system_toolchains_profiles_and_flags.md)
3. [Task 1.2 — Engine Core Loop and Lifecycle](./task02_engine_core_loop_and_lifecycle.md)
4. [Task 1.3 — Web Platform Layer and Display Management](./task03_web_platform_layer_and_display_management.md)
5. [Task 1.4 — Memory Policy and Compile-Time Limits](./task04_memory_policy_and_compile_time_limits.md)
6. [Task 1.5 — Logging, Errors, and Debug Baseline](./task05_logging_errors_and_debug_baseline.md)

## Задачи

1. [Task 1.1 — Module Layout and Build Skeleton](./task01_module_layout_and_build_skeleton.md)
2. [Task 1.6 — Build System, Toolchains, Profiles, and Flags](./task06_build_system_toolchains_profiles_and_flags.md)
3. [Task 1.2 — Engine Core Loop and Lifecycle](./task02_engine_core_loop_and_lifecycle.md)
4. [Task 1.3 — Web Platform Layer and Display Management](./task03_web_platform_layer_and_display_management.md)
5. [Task 1.4 — Memory Policy and Compile-Time Limits](./task04_memory_policy_and_compile_time_limits.md)
6. [Task 1.5 — Logging, Errors, and Debug Baseline](./task05_logging_errors_and_debug_baseline.md)

## Результат спринта

- Появляется skeleton, на который можно безопасно навешивать ECS, resources и rendering.
- Game-side callbacks уже существуют как жёсткий контракт и не подменяются engine scheduler-ом.
- Память и диагностика оформлены до появления feature-кода, а не задним числом.
- Фиксируется единый build contract для runtime и builder, чтобы дальнейшие спринты не плодили несогласованные команды и флаги.
- Репозиторий с первого спринта проектируется как reusable engine repo, а не как одноразовый монолит под единственную in-repo игру.

## Definition of Done

- Структура модулей и runtime entrypoints соответствуют `§3`, `§4`, `§24`, `§25`, `§26`.
- Нет скрытых runtime-сервисов, которые принимают решения вместо игры.
- Все базовые hot-path массивы и scratch-области проектируются как preallocated.
- Для runtime и builder заданы отдельные target-ы, профили сборки и документированная flag policy.
- Engine target не зависит жёстко от in-repo `game`/`examples`/`testbed` кода и пригоден для внешнего потребителя.

## Опорные разделы спецификации

- `§3 Platform Architecture`
- `§4 Frame Lifecycle`
- `§5 Memory Policy`
- `§24 Logging, Errors, Debugging`
- `§25 Engine/Game Boundary`
- `§26 Module Layout`
- `§27 Suggested Implementation Order` пункты 1-2
- `AGENTS.md` раздел `Сборка` как дополнительный источник требований по toolchain split