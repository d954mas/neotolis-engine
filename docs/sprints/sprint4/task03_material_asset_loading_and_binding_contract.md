# Task 4.3 — Material Asset Loading and Binding Contract

## Цель

Реализовать runtime-side material contract согласно спецификации: shader + render state + values, без отдельной полной `MaterialRuntime` копии.

## Основание в спецификации

- `§16.1 MaterialAsset purpose`
- `§16.2 Numeric params policy`
- `§16.3 MaterialAsset binary layout`
- `§16.4 No separate MaterialRuntime copy`
- `§16.5 Render state and material`

## Объём работ

- Описать и реализовать `MaterialAssetHeader` с `shader`, render states, `param_count`, `texture_count`.
- Реализовать computed-offset access к `vec4 params[]` и `TextureAssetRef textures[]` без flexible-array hack с двумя FAM.
- Зафиксировать numeric params policy: все числа хранятся как `vec4[]`.
- Подготовить runtime handle, который указывает на runtime-loaded material asset storage, а не на отдельную полную копию.
- Подготовить binding contract для renderer: material задаёт shader ref, texture refs и render-state hints.
- Реализовать placeholder material для mismatched/failed assets.

## Результат

- Material pipeline полностью согласован с locked decisions и готов к использованию в sort/batch logic.

## Критерии приёмки

- Нет дублирующего full-copy `MaterialRuntime` по умолчанию.
- `params` и `textures` читаются через вычисляемые offsets безопасно и предсказуемо.
- Render-state hints доступны renderer-у, но не превращают material в скрытый pass graph.
- Invalid shader/material linkage приводит к placeholder material path.

## Проверка

- Материалы с разным количеством params/textures корректно читаются из бинарного блока.
- Placeholder material используется при format/link errors без краша runtime.

## Зависимости

- Task 3.1.
- Task 4.1.
- Task 4.2.

## Вне scope

- Material graph.
- Редактор материалов.