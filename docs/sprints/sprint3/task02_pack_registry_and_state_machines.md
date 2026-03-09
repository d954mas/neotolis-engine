# Task 3.2 — Pack Registry and State Machines

## Цель

Сформировать pack-level lifecycle и state-машины для pack и assets, чтобы runtime управлял loading flow предсказуемо и без blocking semantics.

## Основание в спецификации

- `§18.2 Pack state machine`
- `§18.3 Asset state machine`
- `§18.5 Loading progress`

## Объём работ

- Ввести `PackMeta` registry с `id`, `url`, `state`, `blob_size`, `blob_data`, `bytes_received`, `bytes_total`, `last_used_time`.
- Реализовать состояния `PACK_STATE_NONE`, `REQUESTED`, `DOWNLOADING`, `LOADED`, `READY`, `FAILED`.
- Реализовать состояния `ASSET_STATE_UNKNOWN`, `REGISTERED`, `LOADING`, `READY`, `FAILED`.
- Определить API для `pack_request_load`, query progress, query state и retry scheduling.
- Подготовить таблицы pending requests и mapping `request_id -> PackMeta`.
- Зафиксировать правило, что `READY` pack означает «manifest parsed и assets registered», а не «все assets уже активированы».

## Результат

- Pack и asset lifecycle описаны формально и одинаково интерпретируются всеми подсистемами.

## Критерии приёмки

- Pack и asset states не смешиваются в одну общую enum/state модель.
- Есть явное различие между `LOADED` pack blob и `READY` pack manifest.
- Progress допускает `bytes_total == 0` как unknown total size.
- Retry/failure policy оформлена централизованно.

## Проверка

- Тестовые переходы покрывают happy path, fail path и повторный запрос pack-а.
- UI/game-side code может безопасно опрашивать pack progress без знания внутренних fetch деталей.

## Зависимости

- Task 3.1.

## Вне scope

- Фактический fetch bridge.
- Asset format activation logic.