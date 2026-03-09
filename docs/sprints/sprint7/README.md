# Sprint 7 — Builder, Importers, and End-to-End Integration

## Цель спринта

Собрать builder как отдельный C-бинарник, который импортирует source assets, валидирует их, конвертирует в runtime formats и пишет NEOPAK packs для полного end-to-end цикла.

## Почему builder закрывает roadmap

- Спецификация прямо говорит, что builder делает тяжёлую работу офлайн, а runtime остаётся простым.
- К этому моменту runtime contracts уже стабилизированы, значит builder может конвертировать assets под конкретные formats, не ломая базовую архитектуру.
- End-to-end smoke к концу спринта должен проверить весь pipeline: source assets -> builder -> pack -> runtime load -> render/audio.

## Scope спринта

- Builder executable и code-defined build API.
- Importers для shader/material/texture.
- Mesh import и runtime conversion.
- Audio import в OGG/Vorbis pipeline.
- Pack writer, embedded manifest, `pack_dump`, cross-asset validation и end-to-end smoke.

## Задачи

1. [Task 7.1 — Builder Executable and Code-First API](./task01_builder_executable_and_code_first_api.md)
2. [Task 7.2 — Shader, Material, and Texture Import Pipeline](./task02_shader_material_and_texture_import_pipeline.md)
3. [Task 7.3 — Mesh Import and Runtime Conversion](./task03_mesh_import_and_runtime_conversion.md)
4. [Task 7.4 — Audio Import and OGG Pipeline](./task04_audio_import_and_ogg_pipeline.md)
5. [Task 7.5 — NEOPAK Writer, Validation, and End-to-End Smoke](./task05_neopak_writer_validation_and_end_to_end_smoke.md)

## Результат спринта

- Проект получает полный offline->runtime pipeline без нарушения принципа runtime simplicity.
- Спецификация материализуется в набор builder rules и pack outputs, пригодных для реальной игры.

## Definition of Done

- Builder остаётся standalone C binary и не переезжает в runtime.
- Правила сборки контента задаются кодом, а не DSL/editor pipeline.
- Валидация ссылок и совместимости живёт в builder, а runtime использует safety-net validation.

## Опорные разделы спецификации

- `§19 Pack Format (NEOPAK)`
- `§20 Runtime Formats`
- `§23 Builder Architecture`
- `§27 Suggested Implementation Order` пункты 18-19