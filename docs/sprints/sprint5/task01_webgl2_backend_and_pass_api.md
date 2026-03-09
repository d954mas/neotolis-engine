# Task 5.1 — WebGL 2 Backend and Pass API

## Цель

Реализовать минимальный, но устойчивый renderer backend поверх WebGL 2 и зафиксировать engine-level pass API, не зеркалящий WebGL напрямую и не притворяющийся полным WebGPU abstraction layer.

## Основание в спецификации

- `§3.1 Initial platform target`
- `§11.1 Engine/game boundary`
- `§11.2 Renderer backend API shape`
- `§28` locked decisions 1-3

## Объём работ

- Реализовать `renderer_begin_frame`, `renderer_end_frame`, `renderer_begin_pass`, `renderer_end_pass`, `renderer_set_camera`, `renderer_draw_mesh`, `renderer_draw_sprite`.
- Подготовить WebGL 2 context initialization и базовую state setup без WebGL 1 fallback.
- Описать минимальный `PassDesc`/camera contract, достаточный для clear/load/store, depth и viewport state.
- Подготовить тонкий GPU state cache, чтобы избегать лишних state changes без введения скрытой render graph-системы.
- Сконцентрировать WebGL-specific вызовы внутри backend/gpu слоя.
- Обеспечить диагностические сообщения для critical backend init failure как fatal path.

## Результат

- Engine получает низкоуровневый renderer API, пригодный для consumption game-side pass logic.

## Критерии приёмки

- Backend не знает про render tags, gameplay systems и high-level pass policy.
- Нет WebGL 1 fallback или универсального multi-backend каркаса раньше времени.
- Renderer API остаётся engine-oriented, а не копией raw GL functions.
- Critical backend init failure обрабатывается как fatal startup error.

## Проверка

- Пустой кадр с одним clear pass проходит через полный frame lifecycle.
- WebGL-specific код локализован в backend/gpu модулях.

## Зависимости

- Sprint 4.
- Task 1.3.

## Вне scope

- WebGPU backend.
- Сложный state graph или render graph planner.