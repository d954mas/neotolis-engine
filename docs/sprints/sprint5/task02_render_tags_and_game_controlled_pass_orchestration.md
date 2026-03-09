# Task 5.2 — Render Tags and Game-Controlled Pass Orchestration

## Цель

Закрепить game-owned pass orchestration и render tags как фильтры/группировки, а не как engine-enforced pipeline semantics.

## Основание в спецификации

- `§10.1 RenderTag philosophy`
- `§10.2 What tags mean`
- `§11.1 Engine/game boundary`
- `§13.1 Sort policy is pass-controlled`
- `§25.2 Game owns`

## Объём работ

- Ввести простой механизм выдачи `RenderTag`, который создаёт game code, а не engine enum registry с предзаданными тегами.
- Подготовить pass builder helpers, которые фильтруют entities/render items по tag.
- Зафиксировать, что game code определяет pass order, camera selection, sort mode и batching choice для каждого pass-а.
- Подготовить примеры game-side render loop-а, которые явно вызывают сборку/отрисовку pass-ов в коде.
- Исключить скрытый render graph, auto-generated dependencies и implicit ordering rules.
- Зафиксировать, что backend ничего не знает о значении tag-ов.

## Результат

- Render pipeline остаётся code-first и управляется игрой, как требует baseline архитектуры.

## Критерии приёмки

- `RenderTag` не кодирует draw type, material type или backend pipeline class.
- Pass order задаётся только кодом игры.
- Никакая engine subsystem не переставляет passes автоматически.
- Tag filtering работает как explicit input для build phase.

## Проверка

- Пример с `TAG_WORLD`, `TAG_UI`, `TAG_DEBUG` собирает и исполняет passes в явном порядке.
- Изменение порядка passes не требует правок backend-а.

## Зависимости

- Task 5.1.
- Sprint 2.

## Вне scope

- Declarative render pipeline description.
- Editor-driven pass configuration.