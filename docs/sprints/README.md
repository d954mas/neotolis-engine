# План спринтов Neotolis Engine

Этот каталог переводит `docs/neotolis_engine_spec_1.md` в пошаговый delivery-план. Репозиторий пока пустой, поэтому план построен как greenfield-реализация без предположений о существующем коде.

## Принципы декомпозиции

- Сперва идут архитектурные зависимости, потом feature-level задачи.
- Каждый спринт сохраняет baseline из спецификации: `code-first`, `explicit over implicit`, простой runtime, отсутствие heap-аллокаций в hot path.
- Game-side orchestration не переносится в движок: нет скрытого scheduler, system registry и render graph.
- Builder остаётся отдельным C-бинарником и появляется в конце плана только после стабилизации runtime-контрактов.
- До появления полноценного builder-а runtime можно поднимать на синтетических тестовых pack/blob fixtures, чтобы не блокировать ранние спринты.
- Build system, профили сборки и флаги считаются отдельным архитектурным слоем проекта, а не «технической мелочью после кода», поэтому вынесены в отдельную задачу Sprint 1.

## Спринты

| Спринт | Фокус | Основной результат |
| --- | --- | --- |
| [Sprint 1](./sprint1/README.md) | Foundation runtime | Каркас проекта, frame lifecycle, platform/web, memory policy, logging, build contract |
| [Sprint 2](./sprint2/README.md) | ECS foundation | Entity handles, hierarchy, destruction policy, component storage, transforms |
| [Sprint 3](./sprint3/README.md) | Resources and packs | Resource registry, PackState/AssetState, NEOPAK parsing, async loading |
| [Sprint 4](./sprint4/README.md) | Runtime asset layer | Validation framework, shader/material/texture/mesh runtime formats |
| [Sprint 5](./sprint5/README.md) | Rendering | WebGL 2 backend, render passes, render items, mesh/sprite/text/shadow path |
| [Sprint 6](./sprint6/README.md) | Interaction and diagnostics | Input polling, pointer capture, audio, debug overlay, smoke harness |
| [Sprint 7](./sprint7/README.md) | Builder and integration | Builder API, importers, NEOPAK writer, validation, end-to-end flow |

## Что сознательно не планируется

- Editor, scene authoring UI, plugin system, Lua, full physics, WebGPU runtime backend.
- Универсальный reflection-heavy слой.
- Скрытая автоматизация game/render orchestration.

## Выявленные точки, требующие отдельной фиксации

- В `§23.1` пример использует `add_audio("pattern")`, а в `§23.4` API разводит `add_audio(path)` и `add_audios(pattern)`. Это несогласованность спецификации; в Sprint 7 она вынесена в отдельное решение, а не нормализуется молча.
- `Text rendering` в спецификации отмечен как `later-ready`, поэтому план включает интерфейс и интеграционный каркас, но не требует сложного layout/text shaping в ранних спринтах.
- Часть runtime format headers в `§29` помечена как open question. Sprint 4 фиксирует их минимально достаточный baseline без выхода за рамки locked decisions.
- Спецификация фиксирует `C17`, `runtime = emcc/WASM` и `builder = native C binary`, но не фиксирует конкретную систему сборки, профили, warning policy, sanitizer policy и набор compile/link flags. Это пробел baseline; он закрывается отдельной задачей [Task 1.6](./sprint1/task06_build_system_toolchains_profiles_and_flags.md), а не догадками по ходу реализации.