# Task 6.3 — Audio Core Module and Voice Pool

## Цель

Реализовать platform-agnostic audio API с handle-based clips/voices, общими lifecycle/state правилами и voice eviction policy.

## Основание в спецификации

- `§22.1 Architecture overview`
- `§22.2 Platform-agnostic design`
- `§22.3 Audio state`
- `§22.4 Audio clips`
- `§22.5 Audio voices`
- `§22.6 Public API`

## Объём работ

- Ввести `AudioState`, `AudioClipHandle`, `AudioVoiceHandle`, `AudioClipState`, `AudioVoiceState` и invalid handle constants.
- Реализовать public API lifecycle, clip management, playback, voice control и master volume строго в platform-agnostic терминах.
- Спроектировать preallocated pools для clips и voices по `MAX_AUDIO_CLIPS` и `MAX_AUDIO_VOICES`.
- Реализовать generation validation для clip/voice internal slots.
- Ввести eviction policy: если пул голосов заполнен, вытеснять самый старый non-looping voice; если все looping, новый звук не играть.
- Подготовить `audio_update()` для voice state maintenance и future fade/safety timeouts.

## Результат

- Игра получает единый audio API, одинаковый для web и будущего desktop backend.

## Критерии приёмки

- Public API не содержит platform-specific types.
- `audio_play` в `AUDIO_SUSPENDED` возвращает invalid voice без краша.
- Pools не аллоцируют память на hot path.
- Voice eviction реализует именно политику из спецификации.

## Проверка

- Smoke-сценарии покрывают clip create/destroy, play/stop, suspended state и full voice pool.
- Handle invalidation работает после destroy/reuse slots.

## Зависимости

- Sprint 1.
- Sprint 3.

## Вне scope

- 3D audio.
- Mix buses, effects, streaming long tracks.