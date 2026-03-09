# Task 5.3 — Render Items, Sort Keys, and Batch Keys

## Цель

Реализовать CPU-side render item pipeline: build, sort и batching-group preparation без утечки dangling pointers и без универсального «одного правильного» sort layout.

## Основание в спецификации

- `§12.1 RenderItem concept`
- `§12.2 Universal RenderItem model`
- `§12.3 Sort key meaning`
- `§12.4 Batch key meaning`
- `§12.5 Why both keys exist`
- `§13.2 Depth sorting`

## Объём работ

- Ввести `RenderItem` с `sort_key`, `batch_key`, `draw_type`, `material`, `sort_depth`, `world_matrix`, `color`, `params0` и union для draw payload.
- Хранить `world_matrix` по значению в frame scratch, а не по указателю на transform storage.
- Реализовать build phase, который собирает render items из ECS/component storages в preallocated frame arrays.
- Реализовать pass-dependent формирование `sort_key` и `batch_key` вместо одного универсального bit layout на все случаи.
- Вычислять depth только для тех pass-ов, где этого требует sort policy.
- Подготовить sort + batch-run iteration API для последующих renderers.

## Результат

- Render pipeline становится явной CPU preparation стадией между ECS и backend draw calls.

## Критерии приёмки

- `sort_key` и `batch_key` используются независимо и не подменяют друг друга.
- Build/sort pipeline работает в frame scratch memory без heap growth.
- Depth не считается «на всякий случай» для всех items.
- Render item не ломается при структурных изменениях component storages после build phase.

## Проверка

- Тестовые passes с material-sort и depth-sort дают ожидаемый порядок item-ов.
- Batch-run grouping реагирует на изменение `batch_key`, а не на побочные поля.

## Зависимости

- Task 1.4.
- Task 4.3.
- Task 4.5.
- Task 5.2.

## Вне scope

- Финальная bit-packing optimization для всех сортировочных сценариев.
- GPU-driven rendering.