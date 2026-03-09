# Task 7.2 — Shader, Material, and Texture Import Pipeline

## Цель

Собрать offline pipeline для shader/material/texture assets с валидацией ссылок и выпуском runtime-ready binaries.

## Основание в спецификации

- `§15 Shader System`
- `§16 Material System`
- `§20.1 General rule`
- `§23.5 Builder stages`
- `§23.6 Builder validation`

## Объём работ

- Реализовать source import для shader assets и генерацию runtime shader binary с metadata и code blobs.
- Реализовать import/validation material descriptions: shader linkage, `vec4[]` numeric params, texture slot counts и render-state hints.
- Реализовать texture conversion из source format в runtime texture binary, пригодный для прямой runtime upload.
- Проверять ссылки material->shader и material->textures на builder-этапе, а не оставлять эту работу runtime.
- Подготовить placeholder/error reporting strategy builder-а для broken source assets.
- Обеспечить соответствие generated binaries runtime loaders из Sprint 4.

## Результат

- Shader/material/texture цепочка становится полным offline pipeline с жёсткой validation фазой.

## Критерии приёмки

- Runtime не обязан читать source `.png`/shader authoring format.
- Material params упакованы как `vec4[]` в соответствии со спецификацией.
- Builder ловит type/reference mismatches до pack writing.
- Generated binaries проходят runtime validation/load без специальных обходов.

## Проверка

- Integration fixtures собирают маленький набор shader/material/texture assets в runtime formats.
- Ошибочные ссылки и несовместимые counts приводят к builder validation errors.

## Зависимости

- Task 4.2.
- Task 4.3.
- Task 4.4.
- Task 7.1.

## Вне scope

- Node-based material editor.
- Runtime hot reload source assets.