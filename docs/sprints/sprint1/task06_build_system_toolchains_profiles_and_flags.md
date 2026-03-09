# Task 1.6 — Build System, Toolchains, Profiles, and Flags

## Цель

Зафиксировать и внедрить единую систему сборки проекта: раздельные target-ы для runtime и builder, профили сборки, compiler/linker flags, warning policy, output layout и команды проверки.

## Почему это одна из первых задач спринта

Это не «обвязка в конце». После Task 1.1 это второй gating-шаг всего roadmap. Пока не зафиксированы target-ы, toolchains и config policy, любая реализация core loop, platform layer или memory subsystem будет делаться на плавающей инфраструктуре и быстро разъедется по ad-hoc командам и несовместимым флагам.

## Почему это отдельная задача

Спецификация и `AGENTS.md` фиксируют только high-level требования: `C23`, `runtime = WASM через emcc`, `builder = native C binary`. Конкретная система сборки, профили, флаги предупреждений, linker options, sanitizer policy и структура build outputs в baseline-спеке не заданы. Это пробел в архитектурном плане, и его нужно закрыть явно, а не заполнять локальными догадками каждого следующего спринта.

## Основание в спецификации и проектных инструкциях

- `§3.1 Initial platform target`
- `§23.1 Builder model`
- `§25.1 Engine owns`
- `§26 Module Layout`
- `AGENTS.md` раздел `Сборка`

## Объём работ

- Выбрать и зафиксировать одну project-wide систему оркестрации сборки.
  Это может быть `CMake`, `Meson`, `premake`, `make + scripts` или другая схема, но решение должно одинаково покрывать runtime, builder, game sample и smoke targets.
- Ввести отдельные target-ы минимум для:
  - `runtime_wasm`
  - `builder_native`
  - `game_sample_wasm` или эквивалентный smoke target
  - `tests_native`/`smoke_native`, если тестовая инфраструктура появляется на этом этапе
- Определить стандартные профили сборки:
  - `debug`
  - `release`
  - опционально `relwithdebinfo` или `size`, если проект сочтёт это обязательным
- Зафиксировать policy по compiler standard и warning levels:
  - `C23` как обязательный стандарт
  - warning baseline для `clang/gcc`
  - warning baseline для `msvc`, если builder должен собираться под Windows native toolchain
  - policy по `warnings as errors`: глобально или только в CI/selected targets
- Зафиксировать runtime-specific `emcc`/link flags:
  - WebGL 2 only
  - отсутствие WebGL 1 fallback
  - экспорт необходимых entrypoints/callbacks для platform/audio/fetch bridge
  - режимы debug symbols/source maps/assertions
  - режимы release optimization/LTO/size optimization
  - memory/stack policy, совместимую с compile-time capacities и runtime simplicity
- Зафиксировать builder-specific native flags:
  - debug/release optimization levels
  - asserts policy
  - sanitizer policy для debug-проверок, если toolchain это поддерживает
  - platform defines, отличающие builder от runtime
- Ввести единый набор compile definitions/macros для конфигураций и target split.
  Примеры класса решений: `NEO_PLATFORM_WEB`, `NEO_TARGET_RUNTIME`, `NEO_TARGET_BUILDER`, `NEO_CONFIG_DEBUG`, `NEO_ENABLE_ASSERTS`.
- Определить output layout, чтобы артефакты не смешивались:
  - `build/wasm-debug/`
  - `build/wasm-release/`
  - `build/builder-debug/`
  - `build/builder-release/`
  - отдельная зона для generated packs/smoke outputs
- Ввести canonical команды сборки, запуска smoke и проверки.
  Пока точные команды отсутствуют, задача должна закончиться их появлением в репозитории и в `AGENTS.md`.
- Подготовить policy для third-party зависимостей и JS glue:
  - что живёт в репозитории
  - что генерируется build-ом
  - как подключается platform/web JS bridge
- Убедиться, что build system не протаскивает в проект то, чего нет в архитектуре: plugin registration, reflection generation, hidden codegen pipeline или монолитную editor-centric сборку.

## Результат

- У проекта появляется единый и воспроизводимый build contract для всей команды.
- Runtime и builder собираются раздельно, но по одной понятной схеме.
- Следующие спринты могут добавлять код без расползания флагов, defines и ad-hoc команд.

## Критерии приёмки

- Есть один документированный способ собрать runtime и builder с нуля.
- Runtime target использует `emcc` и не допускает WebGL 1 fallback.
- Builder target является нативным C23-бинарником и не зависит от wasm toolchain.
- Для каждого профиля заданы ожидаемые optimization/debug/assert behaviors.
- Warning policy и config macros задокументированы и воспроизводимы.
- Output directories и имена target-ов стандартизированы.
- `AGENTS.md` обновлён конкретными build/run/test командами после появления реальной инфраструктуры.

## Проверка

- Пустой или минимальный runtime target собирается в debug и release профилях.
- Builder target собирается отдельно от runtime.
- Smoke command может прогнать хотя бы минимальный lifecycle entrypoint без ручной возни с флагами.
- Новому разработчику достаточно прочитать документацию репозитория, чтобы понять, чем и как собирать runtime, builder и smoke targets.

## Зависимости

- Task 1.1.

## Вне scope

- Полная CI/CD инфраструктура.
- Distributed build cache.
- Asset build database и incremental rebuild graph.
- Автоматическое генерирование архитектурных решений из build system.

## Примечание о спецификации

Эта задача не «исправляет» спецификацию и не подменяет её. Она закрывает участок, который baseline-спека оставляет открытым: конкретную build orchestration policy. Если позже в `docs/neotolis_engine_spec_1.md` появятся жёсткие требования к build system или флагам, roadmap должен быть синхронизирован с ними явно.