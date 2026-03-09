# Sprint 4 — Runtime Asset Layer

## Цель спринта

Зафиксировать runtime-side форматы и загрузку ключевых asset types: shader, material, texture и mesh, сохранив приоритет builder/runtime split и safety-net validation.

## Почему этот спринт отделён от renderer

- Спецификация жёстко разделяет asset representation и render orchestration.
- До начала draw path нужно стабилизировать runtime format contracts, иначе renderer будет зависеть от неустойчивых бинарных layout-ов.
- `§20` и `§15-16` содержат несколько locked decisions, которые критично оформить до feature rendering.

## Scope спринта

- Общий validation framework для runtime formats.
- ShaderAsset runtime parsing и program creation contract.
- MaterialAsset runtime parsing без полной runtime-копии.
- Texture runtime format baseline и upload path.
- Mesh runtime format baseline и GPU-ready asset layout.

## Задачи

1. [Task 4.1 — Runtime Format Validation Framework](./task01_runtime_format_validation_framework.md)
2. [Task 4.2 — Shader Asset Runtime and Program Creation](./task02_shader_asset_runtime_and_program_creation.md)
3. [Task 4.3 — Material Asset Loading and Binding Contract](./task03_material_asset_loading_and_binding_contract.md)
4. [Task 4.4 — Texture Runtime Format and Upload Path](./task04_texture_runtime_format_and_upload_path.md)
5. [Task 4.5 — Mesh Runtime Format and Asset Loading](./task05_mesh_runtime_format_and_asset_loading.md)

## Результат спринта

- Runtime получает стабильные binary contracts для ключевых render assets.
- Builder может позже конвертировать source assets в заранее определённые runtime representations.

## Definition of Done

- Runtime валидирует только magic/version/type/sizes/compatibility safety net.
- Material и shader загрузка не создаёт лишних полных копий runtime-объектов.
- Texture и mesh данные ориентированы на near-GPU-ready consumption.

## Опорные разделы спецификации

- `§15 Shader System`
- `§16 Material System`
- `§20 Runtime Formats`
- `§28` locked decisions 9-14