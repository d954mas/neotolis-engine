# Sprint 5 — Rendering and Pass Orchestration

## Цель спринта

Поднять render foundation движка: WebGL 2 backend, game-controlled render passes, `RenderItem` pipeline, mesh draw path и baseline для sprite/text/shadow rendering.

## Почему render вынесен в отдельный спринт

- Спецификация жёстко отделяет runtime asset layer от render orchestration.
- Rendering должен опираться на уже готовые transform, resources, shader/material/mesh contracts.
- Самая критичная архитектурная граница здесь: engine предоставляет primitives, game определяет pass order, tags, sort policy и batching choice.

## Scope спринта

- WebGL 2 backend как единственный baseline backend.
- Render passes без скрытого render graph.
- `RenderItem`, `sort_key`, `batch_key`, CPU-side build/sort pipeline.
- Mesh renderer и ранний instancing path.
- Sprite batching, text later-ready shell и shadow overrides.

## Задачи

1. [Task 5.1 — WebGL 2 Backend and Pass API](./task01_webgl2_backend_and_pass_api.md)
2. [Task 5.2 — Render Tags and Game-Controlled Pass Orchestration](./task02_render_tags_and_game_controlled_pass_orchestration.md)
3. [Task 5.3 — Render Items, Sort Keys, and Batch Keys](./task03_render_items_sort_keys_and_batch_keys.md)
4. [Task 5.4 — Mesh Renderer and Instancing Baseline](./task04_mesh_renderer_and_instancing_baseline.md)
5. [Task 5.5 — Sprite, Text, and Shadow Rendering Paths](./task05_sprite_text_and_shadow_rendering_paths.md)

## Результат спринта

- Игра получает explicit render pipeline в коде без навязанной engine-side графовой системы.
- Renderer может потреблять готовые `RenderItem` и выдавать draw calls по предсказуемым правилам.

## Definition of Done

- WebGL 2 является единственным baseline backend.
- `sort_key` и `batch_key` реализованы как отдельные понятия.
- Sprite batching живёт в `SpriteRenderer`, а не размазывается по общему render graph.

## Опорные разделы спецификации

- `§10 Render Tags`
- `§11 Rendering Architecture`
- `§12 Render Items, Sort Keys, Batch Keys`
- `§13 Sorting Policy`
- `§14 Batching and Instancing`
- `§27 Suggested Implementation Order` пункты 12-15