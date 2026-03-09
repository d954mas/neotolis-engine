# Task 2.5 — Transform System and Dirty Propagation

## Цель

Реализовать transform subsystem, который читает hierarchy из entity system, обновляет world matrices top-down и корректно propagates dirty state по subtree.

## Основание в спецификации

- `§8.1 Transform component data`
- `§8.2 Hierarchy source`
- `§8.3 Update model`
- `§8.4 Dirty propagation`
- `§8.5 Non-transform nodes in hierarchy`
- `§4.4 transform_update()`

## Объём работ

- Ввести `TransformComponent` с `local_position`, `local_rotation`, `local_scale`, `world_matrix`, `dirty`.
- Реализовать top-down traversal от roots, используя entity hierarchy как source of truth.
- При локальном изменении помечать текущий узел и всех потомков dirty.
- При reparent помечать subtree dirty через entity/hierarchy integration.
- Поддержать non-transform nodes: traversal идёт по дереву сущностей, а при отсутствии transform используется последний унаследованный basis.
- Подготовить расширяемую структуру для будущих `previous_world_matrix` и interpolation, не вводя их раньше времени.

## Результат

- World transforms вычисляются в одном месте и в правильной фазе кадра.
- Система сохраняет explicit traversal semantics и не требует скрытых observer-механизмов.

## Критерии приёмки

- Transform не хранит parent/child links.
- `transform_update` вызывается только из engine frame loop.
- Dirty propagation корректно охватывает descendants.
- Узлы без Transform не рвут traversal цепочку.

## Проверка

- Smoke-сцена с несколькими уровнями иерархии даёт ожидаемые world matrices.
- Reparent и локальные изменения не требуют полного перестроения всех transforms, кроме нужной subtree.

## Зависимости

- Task 2.2.
- Task 2.4.

## Вне scope

- Render interpolation implementation.
- Bounds propagation и spatial culling.