# Sprint 2 — Entity, Hierarchy, and Transform Foundation

## Цель спринта

Построить data-oriented ECS baseline движка: entity handles с generation, hierarchy в entity system, sparse unique component storages и transform update без скрытой магии.

## Почему этот спринт идёт после foundation

- Frame loop и memory policy уже зафиксированы, поэтому можно проектировать ECS под реальные runtime ограничения.
- `§6`, `§7` и `§8` задают основу почти для всех renderable и gameplay-oriented данных.
- Иерархия в спецификации принадлежит entity system, а не transform, и это лучше зафиксировать до появления render path.

## Scope спринта

- Entity identity, alive/enabled state и generation validation.
- Hierarchy arrays и root policy.
- Deferred destruction как baseline.
- Sparse lookup + dense storage pattern для компонентов.
- Transform traversal, dirty propagation и поддержка non-transform nodes.

## Задачи

1. [Task 2.1 — Entity Handles and Slot Table](./task01_entity_handles_and_slot_table.md)
2. [Task 2.2 — Hierarchy Storage and Tree API](./task02_hierarchy_storage_and_tree_api.md)
3. [Task 2.3 — Deferred Destruction and Cleanup Contract](./task03_deferred_destruction_and_cleanup_contract.md)
4. [Task 2.4 — Sparse Component Storage Pattern](./task04_sparse_component_storage_pattern.md)
5. [Task 2.5 — Transform System and Dirty Propagation](./task05_transform_system_and_dirty_propagation.md)

## Результат спринта

- В движке появляется базовый entity/component слой, совместимый со spec baseline.
- Все будущие render/input/gameplay features могут опираться на единый entity lifetime contract.

## Definition of Done

- Entity system не зависит от renderer, resource system или builder.
- Иерархия живёт в entity storage, а не дублируется в transform.
- Компонентные storages preallocated и не требуют runtime heap growth.

## Опорные разделы спецификации

- `§6 Entity System`
- `§7 Component Storage Design`
- `§8 Transform System`
- `§9 Render-Related Components`
- `§27 Suggested Implementation Order` пункты 3-5