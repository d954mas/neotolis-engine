# Task 7.5 — NTPACK Writer, Validation, and End-to-End Smoke

## Цель

Закрыть content pipeline pack writer-ом, embedded manifest generation, builder validation и полным smoke-тестом от source assets до runtime consumption.

## Основание в спецификации

- `§19.2 Binary layout`
- `§19.5 Debugging`
- `§23.5 Builder stages`
- `§23.6 Builder validation`
- `§24 Error policy`

## Объём работ

- Реализовать pack placement с alignment, header generation, `AssetEntry[]`, `header_size`, `total_size`, checksum.
- Встроить manifest прямо в NTPACK header region согласно flat binary design.
- Реализовать `pack_dump(filename)` и человекочитаемый debug output состава pack-а.
- Собрать builder validation pass для ссылок, типов, shader/material/mesh compatibility, корректности generated runtime formats и audio validity.
- Подготовить end-to-end smoke: builder генерирует pack, runtime грузит его через resource system, активирует assets и проходит render/audio smoke scenario.
- Задокументировать минимальный regression checklist для изменения runtime format или builder importer-а.

## Результат

- Спецификация замыкается в полный pipeline: source -> build -> pack -> load -> activate -> use.

## Критерии приёмки

- Pack binary соответствует NTPACK layout и успешно парсится runtime parser-ом из Sprint 3.
- `pack_dump` помогает диагностировать содержимое без внешних утилит.
- Builder validation перехватывает ошибки раньше runtime whenever possible.
- End-to-end smoke покрывает как минимум один shader/material/texture/mesh asset и один audio asset.

## Проверка

- Generated pack успешно проходит `pack_parse`, asset registration и runtime activation.
- Набор негативных fixture-ов подтверждает, что builder ловит ошибки до записи broken pack-а.

## Зависимости

- Task 3.3.
- Task 7.2.
- Task 7.3.
- Task 7.4.

## Вне scope

- HTTP Range partial loading implementation.
- Полноценная build database/incremental pipeline.