# Task 5.4 — Mesh Renderer and Instancing Baseline

## Цель

Поднять рабочий mesh draw path и ранний instancing baseline для run-ов с одинаковыми mesh/material/shader layout параметрами.

## Основание в спецификации

- `§14.1 MeshRenderer`
- `§14.2 Mesh instancing`
- `§11.1 Engine provides`
- `§13 Sorting Policy`

## Объём работ

- Реализовать mesh renderer, который потребляет отсортированные `RenderItem` и вызывает backend draw API.
- Реализовать binding pipeline: shader/material/texture/state/mesh buffers.
- Зафиксировать, что одинаковый `batch_key` означает минимум отсутствие лишней смены state, но не гарантирует automatic geometry merge.
- Ввести initial instancing path для run-ов с одинаковыми mesh и material, различающихся только object data.
- Подготовить fallback path: если instancing неприменим, renderer просто делает последовательные draw calls в одном state-compatible run.
- Зафиксировать draw stats для draw calls, state switches и instance batch count.

## Результат

- Opaque/material-sorted mesh rendering становится вертикально завершённым путём от ECS до GPU.

## Критерии приёмки

- Arbitrary meshes не объединяются в один буфер «магически».
- Instancing включается только для реально совместимых run-ов.
- Sort policy pass-а влияет на порядок draw items до входа в renderer.
- Renderer не принимает решений о pass order или tag semantics.

## Проверка

- Сцена с несколькими одинаковыми mesh/material объектами может пройти через instancing path.
- Несовместимые объекты сохраняют корректный draw order и state transitions.

## Зависимости

- Task 5.1.
- Task 5.3.
- Sprint 4.

## Вне scope

- Occlusion culling.
- Complex renderer-specific caching beyond baseline.