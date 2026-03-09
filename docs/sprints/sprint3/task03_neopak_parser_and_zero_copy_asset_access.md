# Task 3.3 — NEOPAK Parser and Zero-Copy Asset Access

## Цель

Реализовать runtime parser формата NEOPAK и zero-copy доступ к asset data внутри WASM heap blob-а.

## Основание в спецификации

- `§19.1 Design rationale`
- `§19.2 Binary layout`
- `§19.3 Runtime parsing`
- `§19.4 Asset data access`
- `§20.2 Runtime format validation`

## Объём работ

- Описать `PackHeader` и `AssetEntry` согласно flat binary layout из спецификации.
- Реализовать проверки `magic`, `version`, `header_size`, `total_size`, offset/size границ и alignment sanity.
- Реализовать регистрацию `AssetMeta` entries напрямую из загруженного blob-а.
- Добавить `pack_get_asset_data(pack, offset, size)` без промежуточного копирования payload.
- Подготовить optional checksum hook под `CRC32`, не превращая runtime в тяжёлый validator.
- Подготовить диагностический вывод для malformed pack-ов и несовместимых версий.

## Результат

- Runtime умеет быстро разбирать flat pack без ZIP/архивной логики и без разворачивания manifest в отдельные файлы.

## Критерии приёмки

- Parsing работает на прямом чтении структур, а не на тяжёлой сериализации/десериализации.
- Доступ к asset data остаётся zero-copy внутри blob-а.
- Некорректные header/entry значения переводят pack в `FAILED`, а не оставляют частично зарегистрированное состояние.
- Runtime не делает builder-level deep validation содержимого assets на этой стадии.

## Проверка

- Есть синтетические valid/invalid pack fixtures для parser smoke.
- Asset entries после parsing указывают на корректные диапазоны внутри blob-а.

## Зависимости

- Task 3.1.
- Task 3.2.

## Вне scope

- Partial loading через HTTP Range.
- Builder-side pack writing.