# Task 5.5 — Sprite, Text, and Shadow Rendering Paths

## Цель

Закрыть secondary render paths спецификации: CPU-batched sprites, later-ready text shell и shadow participation через `ShadowComponent`.

## Основание в спецификации

- `§9.4 Sprite component`
- `§9.5 Text component`
- `§9.6 Shadow component`
- `§14.1 SpriteRenderer`
- `§14.3 Sprite batching strategy`

## Объём работ

- Реализовать `SpriteRenderer`, который собирает совместимые sprite items в CPU batch до 256 элементов и flush-ит batch при несовместимом state или заполнении.
- Зафиксировать, что sprite rendering является отдельным render kind, а не special mode mesh renderer-а.
- Подготовить text rendering shell: `TextComponent`, `FontAssetRef`, `StringId`, placeholder render path и интерфейсы для later implementation без обещания полного text layout в этом baseline.
- Реализовать shadow participation policy: если `ShadowComponent` присутствует и `enabled`, pass использует override mesh/material либо primary mesh/material/default shadow path.
- Подготовить общие stats/hook-и для sprite batch count, text placeholder usage и shadow draw participation.
- Согласовать все три пути с render tags и game-defined pass orchestration.

## Результат

- Renderer покрывает не только mesh path, но и основные отдельные render kinds из спецификации.

## Критерии приёмки

- Sprite batching логика локализована внутри `SpriteRenderer`.
- Text path честно маркирован как baseline shell/later-ready, а не как скрыто неполная финальная система.
- Shadow overrides работают через explicit component data.
- Ни один из путей не требует hidden render graph или implicit pass generation.

## Проверка

- Sprite-heavy сцена демонстрирует flush по capacity и state incompatibility.
- Объект с `ShadowComponent` попадает в shadow pass по явно выбранной игре логике.
- Text entities не ломают render build даже при placeholder реализации.

## Зависимости

- Task 5.2.
- Task 5.3.
- Sprint 4.

## Вне scope

- Сложный text shaping/layout.
- Cascaded shadows или advanced shadow filtering.