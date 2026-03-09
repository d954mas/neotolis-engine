# Task 4.4 — Texture Runtime Format and Upload Path

## Цель

Зафиксировать baseline runtime format для текстур и подготовить путь загрузки в GPU без runtime source-format import.

## Основание в спецификации

- `§20.1 General rule`
- `§20.2 Runtime format validation`
- `§17.7 Placeholder policy`
- `§11.2 Renderer backend API shape`

## Объём работ

- Определить минимальный runtime texture header, достаточный для width/height/format/mip metadata и безопасной загрузки.
- Подготовить loader, который читает runtime texture binary и создаёт runtime handle.
- Зафиксировать baseline GPU upload path, который later использует renderer/gpu backend, но не смешивает ресурсную и draw-логику.
- Реализовать placeholder texture для missing/failed states.
- Учесть будущую поддержку NPOT textures и alignment без возврата к source `.png` parsing в runtime.
- Подготовить ошибки и fallbacks для несовместимых texture formats.

## Результат

- Texture assets становятся first-class runtime resources и готовы к material binding.

## Критерии приёмки

- Runtime не декодирует `.png` или другие authoring formats.
- Texture format имеет собственный header и validation path.
- Placeholder texture доступна через resource resolve так же, как и обычный runtime handle.
- GPU upload contract не нарушает engine/game boundary.

## Проверка

- Valid и invalid texture fixtures проверяют validation и placeholder fallback.
- NPOT-friendly метаданные проходят без ad-hoc runtime конверсий.

## Зависимости

- Task 3.5.
- Task 4.1.

## Вне scope

- Texture compression matrix для всех будущих платформ.
- Editor preview pipeline.