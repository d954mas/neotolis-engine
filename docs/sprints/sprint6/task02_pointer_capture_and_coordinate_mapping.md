# Task 6.2 — Pointer Capture and Coordinate Mapping

## Цель

Добавить explicit capture ownership и корректное преобразование входных координат из CSS space в framebuffer/render space.

## Основание в спецификации

- `§3.4 Canvas, DPR, and Viewport`
- `§21.3 Input capture`
- `§25.2 Game owns`

## Объём работ

- Реализовать API `input_try_capture`, `input_release_capture`, `input_is_owner`, `input_pointer_captured`.
- Зафиксировать `owner` как generic `uint32_t`, не привязанный жёстко к entity id.
- Подключить преобразование координат через `PlatformDisplayInfo`, учитывая `dpr` и `render_scale`.
- Подготовить правила auto-release capture на pointer release.
- Зафиксировать, что raw input state всегда доступен, а capture влияет только на ownership/processing semantics.
- Подготовить примеры game-side правил владения для UI, camera drag и world interaction.

## Результат

- Input capture становится централизованным контрактом, а не ad-hoc флагами отдельных систем.

## Критерии приёмки

- Capture ownership не ломает доступ к raw pointer state.
- Координаты корректно отражают отличие CSS pixels и framebuffer pixels.
- Auto-release работает без утечек ownership.
- Engine не навязывает конкретную game semantics owner id.

## Проверка

- Resize/DPR change не ломает world/UI hit testing.
- Конфликт двух владельцев одного pointer-а обрабатывается предсказуемо через explicit API.

## Зависимости

- Task 1.3.
- Task 6.1.

## Вне scope

- Высокоуровневая UI система.
- Gesture arbitration между несколькими системами поверх capture API.