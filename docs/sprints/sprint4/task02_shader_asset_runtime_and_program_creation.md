# Task 4.2 — Shader Asset Runtime and Program Creation

## Цель

Реализовать runtime parsing `ShaderAsset` и подготовить контракт между asset layer и будущим GPU backend для создания программ и layout metadata.

## Основание в спецификации

- `§15.1 ShaderAsset purpose`
- `§15.2 ShaderAsset fields`
- `§15.3 Four levels of shader data`
- `§15.4 Vertex input mask`
- `§15.5 Object params`
- `§15.6 Global params`

## Объём работ

- Описать binary/runtime representation `ShaderAsset` с полями `vs`, `fs`, `vertex_input_mask`, `material_vec4_count`, `texture_slot_count`, `object_usage_mask`, `global_usage_mask`, default render states.
- Реализовать loader, который читает shader metadata и кодовые blobs из runtime format.
- Подготовить GPU-facing contract для compilation/link stage без прямой WebGL-specific логики в resource registry.
- Зафиксировать object/global param conventions: `world_matrix`, `object_color`, `object_params0`, минимальный набор globals.
- Подготовить compatibility checks между shader vertex inputs и mesh runtime formats как safety net.
- Зафиксировать placeholder shader/program path для fail-safe rendering.

## Результат

- Shader assets становятся самостоятельными runtime resources с чётким интерфейсом, а не строками, разбросанными по renderer.

## Критерии приёмки

- Shader loader не хранит material values, только interface и default render hints.
- Vertex input mask доступен для validation и render binding.
- Default blend/depth/cull/sort hints извлекаются из asset, а не захардкожены в draw code.
- Ошибка компиляции shader-а ведёт к placeholder/failure path, а не к неявному undefined behavior.

## Проверка

- Valid/invalid shader fixtures покрывают loading и compile-failure scenarios.
- Совместимость shader/mesh sanity-check логируется предсказуемо.

## Зависимости

- Task 4.1.

## Вне scope

- Полноценный material binding.
- UBO optimization tuning.