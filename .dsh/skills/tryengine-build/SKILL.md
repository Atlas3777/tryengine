---
name: tryengine-build
description: 'Сборка и проверка tryengine: каталоги build-debug/build-release, правило Release для модулей daslang, гейт .das, script_pak, типовые ошибки сборки и компиляции скриптов.'
---

# tryengine: сборка и проверка

Читай перед любым `build`, перед правкой `.das` и когда сборка/компиляция падает.

## Каталоги

| Каталог | Конфигурация | Кто использует |
| --- | --- | --- |
| `build-debug` | Debug | (профиль Debug), агенты по умолчанию |
| `build-release` | Release | (профиль Release), модули-биндинги |

Оба делят кеш исходников `3rdparty/` (CPM). Третьего каталога нет, создавать
нельзя. Бинарники лежат в `<каталог>/bin/editor` и `<каталог>/bin/game_client`.

Пути заданы в `tools/agent/env.sh` (`DEBUG_DIR`, `RELEASE_DIR`); при необходимости их
можно переопределить через `TRY_DEBUG_DIR` и `TRY_RELEASE_DIR`.

Агентские сборки идут через `tools/agent/build.sh` (обёртка над `cmake --build`). Она
использует **свой** каталог ccache — `.cache/ccache`. Первая сборка с пустым кешем компилирует всё; дальше
инкрементально. Переопределение: `TRY_CCACHE_DIR`, `TRY_JOBS`.

## Команды

```bash
tools/agent/build.sh editor                  # Debug, основной таргет
tools/agent/build.sh game_client             # Debug
tools/agent/build.sh game_client --release   # Release
tools/agent/build.sh modules                 # tryengine_shared + tryeditor_shared, ВСЕГДА Release
tools/agent/build.sh all                     # editor + game_client
```

Вручную — только так: `cmake --build build-debug --target editor -j 8`.

## Модули-биндинги: только Release

`tryengine_shared` и `tryeditor_shared` — это C++-модули daslang (`Module_Renderer`,
`Module_Resources`, `Module_Input`, `Module_AsyncFileManager`, `Module_TryEditor`). Они
нужны редактору и `daslang` CLI для подсветки и проверки кастомных типов.

Оба профиля пишут **один и тот же файл** `modules/<имя>/<имя>.shared_module`, поэтому
Debug-сборка затирает Release-модуль. Отсюда правило: модули собираются только из
`build-release`.

Пересобирай модули после правок в:
- `engine_source/graphics/src/Module_Renderer.cpp` (регистрирует `tryRenderer`,
  `tryResources`, `tryInput`, `tryAsyncFile`), `editor/src/Module_TryEditor.cpp`;
- `engine_source/core/include/engine/core/GameScriptSetup.hpp`,
  `editor/include/editor/EditorScriptSetup.hpp` (`DECLARE_MODULE`/`PULL_MODULE`);
- любом коде, который регистрирует типы/функции для `.das` (`addExtern`, `MAKE_TYPE_FACTORY`).

Модули и их `.das_module.manifest` не хранятся в git (генерируются). После свежего клона —
`tools/agent/build.sh modules` один раз.

## Гейт `.das`

```bash
tools/agent/das-check.sh                      # изменённые .das
tools/agent/das-check.sh --all                # все 26
tools/agent/das-check.sh editor/daslang/Editor.das
```

Внутри — одна команда на файл:

```bash
daslang -dry-run -no-lint \
  -project project.das_project \
  -load_module "$PWD/modules/tryengine" \
  -load_module "$PWD/modules/tryeditor" \
  -load_module /opt/daslang/modules/dasLLVM \
  <файл.das>
```

Почему именно так:

- `-project` резолвит `tryengine/`, `tryeditor/`, `trygame/`, `imgui/`, `llvm/`, `daslib/`.
  Без него проект не компилируется вообще.
- `-load_module` всё равно нужен: нативные модули (`tryInput`, `tryRenderer`, ...)
  регистрируются дескрипторами `modules/*/.das_module`. Без него —
  `error[20605]: missing prerequisite 'tryInput'`.
- `/opt/daslang/modules/dasLLVM` нужен, потому что `engine_source/daslang/Engine.das`
  требует `daslib/just_in_time`; без модуля LLVM симуляция падает с
  `LLVM JIT: the emitter module llvm/daslib/llvm_macro did not load`.
- `-dry-run` компилирует и **симулирует** (строже, чем `-compile-only`), но не выполняет.

`daslang` умеет печатать `error[` и при этом возвращать 0 — проверяй и код, и вывод.
Скрипт `das-check.sh` делает это за тебя (и складывает полные логи в
`tools/agent/.tmp/`).

`daslang -tool lint` для этого проекта бесполезен: он не видит нативные модули и печатает
`SKIP (missing prerequisite)`, а `-project` ломает сам инструмент lint. Линт — через MCP
`mcp__tryengine-daslang__lint`.

## `script_pak` и игра

`game_client` не компилирует `.das`: он читает `./script_pak`. Пак собирается в редакторе:
`Z` — собрать пак, `X` — тест-компиляция (`editor/src/EditorApp.cpp`). Правка `.das` не
попадёт в игру, пока пак не пересобран в GUI. Агент из CLI этого сделать не может — так и
пиши в отчёте.

Проверка «как в редакторе» без CLI-гейта: `tools/agent/editor-check.sh` (запускает
редактор из корня с таймаутом, ловит `error[`/`[Error]`, окно открывается и закрывается).
Требует дисплей.

## Симптом → причина → действие

| Симптом | Причина | Что делать |
| --- | --- | --- |
| `error[20605] missing prerequisite 'tryInput'` | нет `-load_module` для `modules/tryengine` | запускать через `das-check.sh`, не собирать флаги руками |
| `LLVM JIT: the emitter module ... did not load` | нет `-load_module /opt/daslang/modules/dasLLVM` | добавить модуль (уже в скрипте) |
| `ninja: warning: build log version is too old` | запускали системный `ninja` вместо `cmake --build` | не повторять; сборка теперь может быть полной |
| `ccache: error: Read-only file system` | сборка без `CCACHE_DIR` внутри репо | запускать через `tools/agent/build.sh` |
| `ccache`/сборка не видит изменений | правки в модулях, а не в приложении | `tools/agent/build.sh modules` (Release) |
| `SKIP ... missing prerequisite` в lint | CLI-линтер не видит нативные модули | линтовать через MCP |
| `compile DB не знает файл` / `SKIP` в синтакс-чеке | новый файл, каталог ещё не переконфигурирован | собрать таргет, затем повторить |
| редактор не стартует, `Failed to create window` | нет дисплея/драйвера Vulkan | CLI-гейт остаётся единственной проверкой |

## Границы

Не трогай `3rdparty/`, `build-debug/`, `build-release/`, `.cache/ccache`, `.idea/`,
`script_pak`, `content/`, `game/artifacts`. Не пересобирай весь проект «на всякий случай»:
точечный таргет дешевле. Не меняй `CMakeLists.txt` без явной задачи.
