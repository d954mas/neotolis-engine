# Task 2.1 — Entity Handles and Slot Table

## Цель

Реализовать entity identity model с generation validation и central slot table, на который смогут опираться все остальные подсистемы.

## Основание в спецификации

- `§6.1 Entity identity`
- `§6.2 Why generation exists`
- `§6.3 Entity table`

## Объём работ

- Ввести `EntityIndex`, `EntityGeneration`, `EntityHandle`.
- Реализовать central slot table с `generation`, `alive`, `enabled`.
- Реализовать операции `create`, `is_valid`, `is_alive`, `enable/disable`, `destroy_mark`.
- Зафиксировать `INVALID_ENTITY_INDEX` и правила проверки stale handles.
- Подготовить свободный список/механизм повторного использования слотов без нарушения generation semantics.
- Документировать ограничение `uint16_t` generation и критерий будущего перехода на `uint32_t`, но не менять baseline заранее.

## Результат

- Любой stale handle корректно определяется как невалидный.
- Entity lifetime управляется единообразно, а не через ad-hoc флаги в компонентах.

## Критерии приёмки

- Повторное использование slot-а увеличивает generation.
- `alive` и `enabled` не смешиваются в одну семантику.
- Entity table существует отдельно от Transform storage.
- Нет скрытого выделения памяти при создании entity.

## Проверка

- Набор тестовых сценариев покрывает create/destroy/reuse и invalid handle access.
- Удалённый entity не может внезапно стать валидным через старый handle.

## Зависимости

- Sprint 1.

## Вне scope

- Удаление компонентов при destroy.
- Иерархические операции.