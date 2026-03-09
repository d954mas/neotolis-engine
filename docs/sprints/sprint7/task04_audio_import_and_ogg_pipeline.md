# Task 7.4 — Audio Import and OGG Pipeline

## Цель

Подготовить builder-side audio pipeline, который принимает source audio, валидирует его и выпускает runtime-compatible OGG/Vorbis assets.

## Основание в спецификации

- `§22.2 Platform-agnostic design`
- `§22.9 Audio in resource pipeline`
- `§23.5 Builder stages`
- `§23.6 Builder validation`
- `§28` locked decision 25

## Объём работ

- Реализовать import `.wav` и `.ogg` inputs в builder.
- Для `.wav` выполнять offline conversion в OGG Vorbis; для уже готового `.ogg` — валидировать и упаковывать as-is или после нормализации, если это нужно выбранному runtime format.
- Подготовить runtime audio asset metadata для корректной передачи encoded bytes в `audio_clip_create`.
- Валидировать формат, длительность/размер и базовую корректность encoded content на builder-стороне.
- Согласовать generated audio binaries с async decode flow из Sprint 6.
- Подготовить понятные builder diagnostics для decode/encode/source format ошибок.

## Результат

- Audio content pipeline соответствует требованию спецификации: packs содержат OGG Vorbis и пригодны для одинаковой web/desktop API semantics.

## Критерии приёмки

- В pack попадает runtime-compatible encoded audio, а не raw authoring source по умолчанию.
- Builder validation ловит некорректные или неподдерживаемые audio inputs.
- Runtime decode path из Sprint 6 может напрямую работать с builder output.
- Audio import не вносит platform-specific типы в game/runtime API.

## Проверка

- Fixture audio assets из `.wav` и `.ogg` проходят через builder и декодируются runtime-ом.
- Ошибочный audio input приводит к builder failure с понятным сообщением.

## Зависимости

- Task 6.4.
- Task 7.1.

## Вне scope

- Streaming music pipeline.
- Runtime transcoding.