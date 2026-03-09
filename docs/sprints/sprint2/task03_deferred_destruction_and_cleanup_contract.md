# Task 2.3 — Deferred Destruction and Cleanup Contract

## Цель

Ввести безопасный baseline для разрушения entity и очистки компонентных хранилищ без mid-frame structural corruption.

## Основание в спецификации

- `§6.6 Entity destruction and cleanup`
- `§4.2 Engine frame order`

## Объём работ

- Спроектировать deferred destruction queue как рекомендованный v0.1 путь.
- Зафиксировать точку обработки очереди в frame lifecycle: после game-side логики и до `transform_update` либо в другой одной явной точке, но без двусмысленности.
- Описать контракт между entity system и component storages: кто и как удаляет компонент при destroy.
- Предусмотреть идемпотентность повторного `destroy_mark`.
- Определить, что происходит с children при удалении parent: каскадное удаление или обязательный detach-этап должен быть выбран и задокументирован, не ломая explicit semantics.
- Согласовать destroy flow с future resources/render item build, чтобы не оставались висячие ссылки.

## Результат

- У движка появляется предсказуемый lifecycle удаления без риска ломать dense storages посреди gameplay iteration.

## Критерии приёмки

- Destroy не меняет структуру storages в произвольный момент кадра.
- Очистка компонентов централизована и повторяема.
- Stale handles после destroy отвергаются generation-проверкой.
- Политика удаления subtree явно зафиксирована в документации/коде.

## Проверка

- Тестовый сценарий с массовым удалением сущностей не ломает иерархию и dense arrays.
- После flush destruction queue не остаются dangling component mappings.

## Зависимости

- Task 2.1.
- Task 2.2.

## Вне scope

- Сложный event system для destroy notifications.
- Cross-thread lifetime management.