# Task 1.4 — Memory Policy and Compile-Time Limits

## Цель

Оформить memory model движка заранее, чтобы все следующие подсистемы строились вокруг preallocated storages и frame scratch, а не вокруг поздних оптимизаций.

## Основание в спецификации

- `§5.1 High-level memory rules`
- `§5.2 Memory categories`
- `§5.3 Compile-time limits`
- `§28` locked decisions 20 и 28-30

## Объём работ

- Разделить память на permanent, pack/blob transient и frame scratch категории.
- Вынести compile-time лимиты в общий header и зафиксировать baseline значения из `§5.3`.
- Подготовить allocator/pool abstractions для каждой категории без использования heap в hot path.
- Определить, какие будущие структуры обязаны жить в permanent memory: entity tables, component storages, asset metadata, runtime pools.
- Определить, какие структуры являются frame scratch: render items, temporary sort arrays, CPU sprite batch buffers, render-pass temporary lists.
- Ввести `frame_temp_reset` как формальную границу жизни временных данных.
- Заложить early diagnostics для temp memory usage и capacity exhaustion.

## Результат

- Память описана как часть архитектуры, а не как внутренняя деталь отдельных модулей.
- Следующие спринты могут проектировать ECS, resources и render path сразу в нужных memory budget-ах.

## Критерии приёмки

- Лимиты заданы compile-time константами, а не растущими динамическими контейнерами.
- Горячие проходы не требуют `malloc/realloc/free`.
- Есть явное различие между resident metadata и transient pack blobs.
- Переполнение лимита обрабатывается предсказуемо: log/assert/fail path, а не silent corruption.

## Проверка

- Документирован список hot-path структур и их memory category.
- Есть механизм сброса frame scratch и счетчики использования.

## Зависимости

- Task 1.2.
- Task 1.6.

## Вне scope

- Финальная tuning-настройка всех capacity.
- Streaming/partial loading memory policy beyond baseline.