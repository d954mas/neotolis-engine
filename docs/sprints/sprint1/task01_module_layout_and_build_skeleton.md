# Task 1.1 — Module Layout and Build Skeleton

## Цель

Создать согласованный каркас репозитория и build targets, чтобы runtime, builder и game-код развивались независимо, но в единой архитектуре.

## Основание в спецификации

- `§3.2 Platform layer`
- `§23 Builder Architecture`
- `§25 Engine/Game Boundary`
- `§26 Module Layout`

## Объём работ

- Создать верхнеуровневые директории `engine/`, `builder/`, `game/` и разложить внутри будущие подсистемы по `§26`.
- Разделить два базовых build target-а: runtime под `emcc` и builder как native C23 binary.
- Ввести общие базовые заголовки для platform-independent типов, handles, asserts и compile-time flags.
- Зафиксировать правила условной компиляции platform-specific реализации: в сборку попадает только один backend file на платформу.
- Подготовить entrypoints для runtime и builder без попытки рано вводить сложный meta-build framework.
- Выделить build-system policy, профили, флаги и output layout в отдельную подробную задачу [Task 1.6](./task06_build_system_toolchains_profiles_and_flags.md), чтобы не смешивать каркас модулей и toolchain governance в одном документе.

## Результат

- Каркас каталогов и заголовков совпадает с архитектурным baseline.
- Runtime, builder и game не смешивают ответственность.
- Появляется понятная точка старта для следующих спринтов.

## Критерии приёмки

- Структура каталогов не конфликтует с `§26`.
- Builder не компилируется вместе с runtime в один бинарник.
- Platform-specific детали не торчат в публичные API game-слоя.
- В каркасе нет признаков будущего plugin system, reflection-heavy registry или hidden scheduler.

## Проверка

- Просмотр дерева репозитория подтверждает отдельные области для `engine`, `builder`, `game`.
- Есть явное место для будущих build instructions и оно отделено от module layout.

## Зависимости

- Нет.

## Вне scope

- Реальная реализация подсистем.
- Настоящая asset conversion логика.
- Детальная build/flag policy; она вынесена в Task 1.6.