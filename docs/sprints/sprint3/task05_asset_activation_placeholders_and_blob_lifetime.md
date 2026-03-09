# Task 3.5 — Asset Activation, Placeholders, and Blob Lifetime

## Цель

Завершить ресурсный baseline: rate-limited activation queue, placeholder policy и правила жизни pack blob-ов в памяти.

## Основание в спецификации

- `§17.7 Placeholder policy`
- `§18.7 Asset activation strategy`
- `§18.8 Retry policy`
- `§18.9 Memory note`
- `§5.2 Memory categories`

## Объём работ

- Реализовать eager-with-rate-limit activation: после `PACK_STATE_READY` обрабатывать не более `N` assets за кадр.
- Подготовить asset ready queue и явную политику budget-а на кадр.
- Реализовать typed placeholder handles для mesh, texture, material, shader, audio и будущих sprite/font.
- Зафиксировать правила перехода `REGISTERED -> LOADING -> READY/FAILED` для конкретных asset activators.
- Описать lifetime pack blob-а: metadata остаётся resident, blob хранится как transient buffer минимум до завершения нужных активаций и по выбранной cache policy.
- Подготовить API unload/release, которое не разрушает `AssetMeta` и не нарушает placeholder semantics.

## Результат

- Любой consumer ресурса получает либо готовый handle, либо контролируемый placeholder.
- Активация assets размазана по кадрам и не создаёт крупных spike-ов.

## Критерии приёмки

- `resource_step()` ограничивает число активаций за кадр.
- Asset failure не крашит runtime и оставляет явный `FAILED` state.
- Blob lifetime не приводит к dangling pointers после активации.
- Placeholder policy едина для всех runtime consumers.

## Проверка

- Smoke-сценарий с большим pack-ом показывает постепенную активацию за несколько кадров.
- Неуспешная активация одного asset не ломает регистрацию остальных assets в pack-е.

## Зависимости

- Task 3.1.
- Task 3.2.
- Task 3.3.
- Task 3.4.

## Вне scope

- Полноценная eviction/cache tuning.
- HTTP Range partial streaming.