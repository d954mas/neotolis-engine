# Task 1.3 — Web Platform Layer and Display Management

## Цель

Спроектировать и реализовать web-specific platform layer, через который проходят frame stepping, display info, resize и browser bridge, не протекая в остальной runtime.

## Основание в спецификации

- `§3.1 Initial platform target`
- `§3.3 Platform responsibilities`
- `§3.4 Canvas, DPR, and Viewport`
- `§25.1 Engine owns`

## Объём работ

- Ввести `platform.h`, `platform_web.c`, `platform_web.h` как engine subsystem, а не ECS-компонент.
- Описать `PlatformDisplayInfo` и поддержку `canvas_width`, `canvas_height`, `framebuffer_width`, `framebuffer_height`, `dpr`, `render_scale`.
- Реализовать hooks для startup/shutdown, frame scheduling и resize/orientation updates.
- Подготовить JS bridge layer для получения browser events, display metrics и frame callback.
- Заложить преобразование координат из CSS pixels в framebuffer space для будущего input слоя.
- Не допускать прямых вызовов browser API из renderer, resources, input и audio; они должны идти через platform abstraction.

## Результат

- Все browser-specific детали локализованы в platform module.
- Остальной движок работает через platform-owned контракт, пригодный для будущего `platform_win32`.

## Критерии приёмки

- Platform layer не содержит gameplay logic, render pass policy или resource manifests.
- Display info отражает различие между CSS size и actual framebuffer size.
- Поддержан `render_scale`, чтобы не захардкодить рендер в native DPR.
- Frame scheduling hook не привязывает engine core к конкретному JS API напрямую.

## Проверка

- Resize/orientation smoke меняет display info и инициирует корректное обновление viewport-dependent данных.
- Путь к browser interop централизован и не размазан по репозиторию.

## Зависимости

- Task 1.1.
- Task 1.2.
- Task 1.6.

## Вне scope

- Async fetch bridge для ресурсов.
- Audio/Web Audio bridge.