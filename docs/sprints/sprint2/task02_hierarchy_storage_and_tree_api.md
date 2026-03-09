# Task 2.2 — Hierarchy Storage and Tree API

## Цель

Реализовать tree storage внутри entity system и закрепить правило, что hierarchy принадлежит не Transform, а базовой entity модели.

## Основание в спецификации

- `§6.3 Entity table`
- `§6.4 Hierarchy policy`
- `§6.5 Root detection`

## Объём работ

- Ввести массивы `parent`, `first_child`, `next_sibling`, `prev_sibling`.
- Реализовать операции attach/detach/reparent с проверкой валидности parent-child связей.
- Зафиксировать правило root detection: root определяется только через `parent == INVALID_ENTITY_INDEX`.
- Учесть поддержку subtree enable/disable на основе entity hierarchy, не дублируя отдельную tree system.
- Подготовить traversal API для будущих transform и gameplay-проходов.
- Определить политику защиты от циклов и повторного включения одного node в несколько деревьев.

## Результат

- Entity tree становится единым источником истины для иерархии, inheritance и subtree semantics.

## Критерии приёмки

- Иерархия не хранится в Transform component.
- Reparent корректно обновляет sibling links и root state.
- Tree traversal может идти без дополнительных heap-структур.
- Невалидные операции на дереве детектируются предсказуемо.

## Проверка

- Smoke-последовательности attach/detach/reparent сохраняют связность дерева.
- Root detection работает без отдельного root component.

## Зависимости

- Task 2.1.

## Вне scope

- Dirty propagation transform-подсистемы.
- Автоматический gameplay callback на изменение hierarchy.