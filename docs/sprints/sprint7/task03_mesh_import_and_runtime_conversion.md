# Task 7.3 — Mesh Import and Runtime Conversion

## Цель

Реализовать mesh importer, который преобразует authoring data в компактный runtime mesh format и валидирует совместимость атрибутов заранее.

## Основание в спецификации

- `§20.3 Mesh format strategy`
- `§23.5 Builder stages`
- `§23.6 Builder validation`
- `§15.4 Vertex input mask`

## Объём работ

- Реализовать import `.glb`/другого выбранного source mesh format-а в builder.
- Валидировать наличие обязательного `POSITION` и корректность опциональных `NORMAL`, `UV0`, `COLOR0`.
- Выполнять offline conversion в compact runtime layout с выбранными типами данных и alignment.
- Подготовить builder-side compatibility validation между mesh data и ожидаемыми shader vertex input masks.
- Подготовить mesh metadata для runtime loader: counts, offsets, index format, attribute layout.
- Согласовать import path с будущим instancing baseline и без лишней runtime unpack-логики.

## Результат

- Mesh authoring data перестаёт попадать в runtime напрямую и превращается в consumable runtime binary.

## Критерии приёмки

- Runtime получает near-GPU-ready mesh format.
- Builder ловит отсутствующий `POSITION` и другие критические несовместимости.
- Конвертация не нарушает locked decision о простом runtime.
- Generated meshes совместимы с loader-ом Sprint 4 и mesh renderer-ом Sprint 5.

## Проверка

- Несколько fixture mesh assets с разным набором атрибутов успешно импортируются или валидно отбрасываются.
- Runtime smoke load использует builder-generated meshes без дополнительных конверсий.

## Зависимости

- Task 4.5.
- Task 7.1.

## Вне scope

- Advanced mesh optimization pipeline.
- Runtime import authoring formats.