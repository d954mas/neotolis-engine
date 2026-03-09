# Task 2.4 — Sparse Component Storage Pattern

## Цель

Зафиксировать и внедрить canonical storage pattern для уникальных компонентов с sparse lookup и dense iteration.

## Основание в спецификации

- `§7.1 Storage model`
- `§7.2 Canonical storage layout`
- `§7.3 Component index type`
- `§7.4 Component API style`
- `§9 Render-Related Components`

## Объём работ

- Реализовать общий storage pattern с `data[CAPACITY]`, `entity_to_index[MAX_ENTITIES]`, `index_to_entity[CAPACITY]`, `count`.
- Ввести typed APIs в стиле `transform_add`, `transform_get`, `transform_has`, `transform_remove`, без generic mega-API.
- Зафиксировать baseline index type как `uint16_t`.
- Подготовить concrete storages для `Transform`, `RenderState`, `Mesh`, `Material`, `Sprite`, `Text`, `Shadow`.
- Задокументировать поведение на capacity exhaustion и на повторное добавление уникального компонента.
- Подготовить cleanup hooks для интеграции с deferred destruction.

## Результат

- Компоненты хранятся data-oriented способом и готовы к плотной итерации.
- Будущие рендер- и gameplay-системы получают единый predictable API.

## Критерии приёмки

- Dense iteration не требует обхода `MAX_ENTITIES`.
- Sparse lookup занимает O(1) и не аллоцирует память.
- Компонентные storages заранее выделены и ограничены compile-time capacities.
- API остаётся typed и читаемым.

## Проверка

- Сценарии add/get/remove покрывают swap-remove dense storage semantics.
- Удаление entity очищает компонентные слоты без разрушения dense packing.

## Зависимости

- Task 2.1.
- Task 2.3.

## Вне scope

- Shared/multi-instance components.
- Reflection/introspection registry поверх storages.