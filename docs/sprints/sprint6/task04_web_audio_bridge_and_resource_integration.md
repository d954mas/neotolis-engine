# Task 6.4 — Web Audio Bridge and Resource Integration

## Цель

Подключить web-specific audio backend, async decode flow и интеграцию audio assets в resource pipeline.

## Основание в спецификации

- `§22.7 JS bridge contract (web implementation)`
- `§22.8 Integration with frame loop`
- `§22.9 Audio in resource pipeline`
- `§18 Async Loading System`

## Объём работ

- Реализовать JS bridge функции `js_audio_init`, `js_audio_shutdown`, `js_audio_resume`, `js_audio_decode`, `js_audio_play`, `js_audio_stop`, `js_audio_set_volume`, `js_audio_set_pitch`, `js_audio_set_master_volume`.
- Реализовать callbacks `audio_on_clip_decoded`, `audio_on_voice_ended`, `audio_on_state_changed`.
- Подключить `audio_try_resume()` к user gesture через frame flow (`input_begin_frame` или `platform_step`).
- Связать `AudioAssetRef` activation с `audio_clip_create(data, size)` и переходами `AUDIO_CLIP_DECODING -> READY/FAILED`.
- Зафиксировать правила работы в `AUDIO_SUSPENDED`, `AUDIO_RUNNING`, `AUDIO_FAILED`.
- Подготовить diagnostic logging и placeholder/failure behavior для decode failures.

## Результат

- Audio становится полноценной частью runtime resource pipeline и web lifecycle.

## Критерии приёмки

- Decode flow остаётся асинхронным и не блокирует frame loop.
- User gesture resume встроен централизованно, а не размазан по game code.
- Audio clip readiness синхронизирована с `AssetState`.
- Decode/playback failures не ломают общую работу runtime.

## Проверка

- Первый pointer press способен перевести audio из `SUSPENDED` в `RUNNING`.
- Декодирование valid/invalid OGG assets корректно обновляет states и logs.

## Зависимости

- Task 3.5.
- Task 6.3.

## Вне scope

- Desktop audio backend.
- Продвинутые DSP/effects.