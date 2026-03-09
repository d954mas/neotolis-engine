# Task 4.5 — Mesh Runtime Format and Asset Loading

## Цель

Описать и реализовать runtime mesh format и путь загрузки mesh assets в near-GPU-ready виде, согласованный с shader vertex input mask и render path.

## Основание в спецификации

- `§20.3 Mesh format strategy`
- `§15.4 Vertex input mask`
- `§17.6 Asset types`
- `§20.2 Runtime format validation`

## Объём работ

- Определить runtime mesh header и секции для vertex/index data, attribute layout и counts.
- Поддержать обязательный `POSITION` и опциональные `NORMAL`, `UV0`, `COLOR0`.
- Зафиксировать baseline типы данных из спецификации: `float16/float32` для positions, `snorm8` для normals, `unorm16` для UV, `uint8 normalized` для colors.
- Реализовать loader и runtime handle для mesh asset, включая validation layout-а и буферных диапазонов.
- Подготовить совместимость с будущим instancing path и shader input mask checks.
- Подготовить placeholder mesh для ошибочных и отсутствующих assets.

## Результат

- Mesh assets становятся готовыми к прямому использованию в renderer без authoring-oriented разбора.

## Критерии приёмки

- Runtime mesh format ориентирован на consumption, а не на удобство редактирования.
- Loader не делает тяжёлых runtime unpack/conversion этапов, которых можно избежать builder-ом.
- Missing required `POSITION` приводит к fail path.
- Shader/mesh compatibility может быть sanity-checked до draw call.

## Проверка

- Fixtures с разными комбинациями атрибутов корректно валидируются и загружаются.
- Placeholder mesh возвращается вместо undefined runtime state.

## Зависимости

- Task 3.5.
- Task 4.1.
- Task 4.2.

## Вне scope

- GLTF import.
- Mesh optimization/quantization pipeline builder-уровня.