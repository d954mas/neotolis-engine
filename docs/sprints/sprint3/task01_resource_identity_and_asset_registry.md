# Task 3.1 — Resource Identity and Asset Registry

## Цель

Построить стабильный resource registry, который разделяет `ResourceId`, typed `AssetRef`, resident `AssetMeta` и runtime `Handle`.

## Основание в спецификации

- `§17.1 Core concepts`
- `§17.2 ResourceId`
- `§17.3 Typed refs`
- `§17.4 Stable AssetMeta entries`
- `§17.5 AssetMeta`
- `§17.6 Asset types`

## Объём работ

- Ввести `ResourceId` как стабильный идентификатор ресурса внутри runtime.
- Описать и реализовать typed refs: `MeshAssetRef`, `TextureAssetRef`, `MaterialAssetRef`, `ShaderAssetRef`, `AudioAssetRef` и будущие `SpriteAssetRef`, `FontAssetRef`.
- Ввести typed runtime handles и разделить их семантику от refs.
- Реализовать resident `AssetMeta` table с полями `id`, `type`, `pack_index`, `entry_offset`, `entry_size`, `format_version`, `state`.
- Зафиксировать правило: unload очищает runtime handle/state, но не удаляет metadata slot.
- Подготовить helper-API для resolve, state query и placeholder fallback без универсального reflection layer.

## Результат

- Все подсистемы получают единый способ ссылаться на asset до и после его runtime activation.
- `AssetRef` не требует generation, потому что slot metadata стабилен.

## Критерии приёмки

- Registry умеет различать existence, type и runtime readiness ресурса.
- `AssetMeta` не перемещается и не переиспользуется как временный слот.
- Typed refs и handles не подменяют друг друга на уровне API.
- Asset type mismatch детектируется явно.

## Проверка

- Smoke-реестр покрывает lookup по `ResourceId`, resolve placeholder и переходы state.
- Стабильность `AssetMeta` сохраняется после unload/reload одного и того же asset.

## Зависимости

- Sprint 1.
- Sprint 2.

## Вне scope

- Реальная загрузка паков.
- Формат-специфичный runtime parsing.