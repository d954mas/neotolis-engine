# Sprint 3 — Resources, Packs, and Async Loading

## Цель спринта

Собрать ресурсный слой движка: стабильные `AssetMeta`, pack registry, NTPACK parsing и неблокирующий async loading, встроенный в frame loop.

## Почему этот спринт идёт до renderer

- Рендер и audio по спецификации работают через typed resource refs и runtime handles.
- `§17`, `§18` и `§19` определяют один из главных architectural split: builder валидирует и конвертирует, runtime только регистрирует, активирует и fallback-ит.
- Без resource lifecycle нельзя корректно подключить placeholder policy, material loading и pack-based content delivery.

## Scope спринта

- Resource identity model и typed refs/handles.
- `PackState` и `AssetState` state machines.
- NTPACK header/entry parsing и zero-copy asset access.
- JS fetch bridge, progress callbacks и `resource_step()`.
- Rate-limited asset activation, placeholder policy и pack blob lifetime.

## Задачи

1. [Task 3.1 — Resource Identity and Asset Registry](./task01_resource_identity_and_asset_registry.md)
2. [Task 3.2 — Pack Registry and State Machines](./task02_pack_registry_and_state_machines.md)
3. [Task 3.3 — NTPACK Parser and Zero-Copy Asset Access](./task03_ntpack_parser_and_zero_copy_asset_access.md)
4. [Task 3.4 — Fetch Bridge and Loading Progress](./task04_fetch_bridge_and_loading_progress.md)
5. [Task 3.5 — Asset Activation, Placeholders, and Blob Lifetime](./task05_asset_activation_placeholders_and_blob_lifetime.md)

## Результат спринта

- Runtime умеет принимать pack blobs асинхронно, регистрировать assets и постепенно активировать их без блокировки main thread.
- У движка появляется единый ресурсный контракт для renderer, audio и game-side code.

## Definition of Done

- Asset metadata остаётся resident и stable весь runtime lifetime.
- Loading не блокирует кадр и не полагается на synchronous I/O.
- Missing/not-ready assets возвращают placeholder handles, а не ломают подсистемы.

## Опорные разделы спецификации

- `§17 Resource System`
- `§18 Async Loading System`
- `§19 Pack Format (NTPACK)`
- `§20.1 General rule`
- `§27 Suggested Implementation Order` пункты 6-8