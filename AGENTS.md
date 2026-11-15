# AGENTS.md

Правила для AI-агентов, работающих в этом репозитории. Файл загружается автоматически в
начале каждой сессии, если рабочий каталог внутри чекаута. Держи его коротким: подробности
по задачам лежат в `.dsh/skills/` и подгружаются по запросу через инструмент `skill`.

## 1. Что это

`tryengine` — небольшой 3D-движок на C++23. Рендер — Vulkan 1.4 напрямую (volk,
vk-bootstrap, VMA, dynamic rendering), окно и ввод — SDL3, интерфейс
редактора — ImGui с Vulkan backend. ECS — DECS внутри daslang: сущности и компоненты живут
в скриптах. Игровая и редакторская логика написана на daslang 0.6.5.
Ассеты препроцессируются в собственные бинарные артефакты. Платформа разработки — Fedora
Linux; сборка — CMake + Ninja с GCC 15, `ccache` и `mold`.
Разделение и между C++ и Daslang такое же как в Unity с С++ и C#.
На Daslang вся игровая логика и редактор. C++ низкоуровневая прослойка.

## 2. Раскладка

| Путь | Содержимое |
| --- | --- |
| `engine_source/core` | `Engine` (реестр систем), `Result`/`Error`, логи, `ScriptSystem`, script pak, `SlotMap`/`TypeRegistry` |
| `engine_source/graphics` | `VulkanDevice`, `VulkanSwapchain`, `RenderGraph`, проходы, загрузчики; здесь же лежат биндинги нативных модулей `tryRenderer`, `tryResources`, `tryInput`, `tryAsyncFile` (`src/Module_*.cpp`, по файлу на модуль) |
| `engine_source/platform` | окно SDL3, события, `InputService`, SDL-фильтры |
| `engine_source/resources` | загрузка pak/артефактов, `ResourceManager`, `AsyncFileManager` (io_uring), загрузчики |
| `engine_source/async` | корутины `Task<T>`, пул потоков и главный исполнитель, pollable-хендлы |
| `engine_source/daslang` | исходники API движка для `.das` (`require tryengine/...`) |
| `editor/src`, `editor/include` | приложение редактора, панели ImGui, импортеры (glTF, Slang, текстуры), asset pipeline, `Module_TryEditor` |
| `editor/daslang` | редакторские скрипты (`require tryeditor/...`), точка входа `EntryPoint.das` |
| `game_client` | рантайм-приложение (читает `./script_pak`, `.das` не компилирует) |
| `game_server` | headless-заглушка; в сборку пока не добавлена |
| `game/scripts`, `game/assets`, `game/artifacts` | игровая логика и контент |
| `engine_content` | поставляемый с движком контент и его артефакты |
| `modules/` | по папке на daslang-модуль: дескриптор `.das_module` (в git) и собранный `.shared_module` |
| `build-debug`, `build-release` | каталоги сборки CMake (генерируются) |
| `tools/agent/`, `.dsh/skills/` | скрипты и скиллы для агентов |
| `cmake/`, `3rdparty/` | кеш пакетов CPM и CMake-обвязка (кеш не редактировать) |

## 3. Сборка

Только две конфигурации: `build-debug` (Debug) и `build-release` (Release). Они делят кеш
исходников CPM в `3rdparty/` и кеш `ccache`.

```bash
tools/agent/build.sh editor                 # Debug-редактор (по умолчанию)
tools/agent/build.sh game_client --release  # Release-клиент
tools/agent/build.sh modules                # модули-биндинги daslang, всегда Release
tools/agent/build.sh all                    # editor + game_client в одной конфигурации
```

Вручную один таргет:

```bash
cmake --build build-debug --target editor -j "$(nproc)"
```

Жёсткие правила:

- Используй `cmake --build <каталог> --target <таргет>`. .
- Не создавай третью конфигурацию и не удаляй, не переноси и не перенастраивай две
  существующие. Не передавай `-D` в существующий каталог сборки, если задача не про саму
  сборку.
- Никогда не редактируй `3rdparty/` (общий кеш исходников CPM) и ничего внутри
  `build-*`.
- Бинарники лежат в `<каталог>/bin/editor` и `<каталог>/bin/game_client`. Запускать их
  нужно из корня репозитория: `project.das_project`, `script_pak`, `content/`,
  `game/artifacts` открываются по относительным путям. Оба приложения GUI и требуют
  дисплей; headless-режима нет.
- Агентские сборки используют локальный для workspace кеш `ccache` (`.cache/ccache`),
  потому что песочница не может писать в `~/.cache/ccache`. Первая сборка с пустым
  кешем компилирует всё; расположение меняется через `TRY_CCACHE_DIR`, параллелизм — через
  `TRY_JOBS` (по умолчанию `nproc`, но не больше 8).

### Модули-биндинги daslang (`das_modules`)

- Один C++-модуль = один файл (`engine_source/graphics/src/Module_Input.cpp`,
  `Module_Resources.cpp`, `Module_Renderer.cpp`, `Module_AsyncFileManager.cpp`,
  `editor/src/Module_TryEditor.cpp`) = одна shared-библиотека
  `modules/<имя модуля>/<имя модуля>.shared_module`.
- Рядом с библиотекой лежит дескриптор `modules/<имя>/.das_module`: daslang читает его для
  `-load_module` и при скане `modules/`, поэтому дескриптор **хранится в git**.
- Цели CMake заводит `tryengine_add_das_module()` (`cmake/DasModule.cmake`):
  `tryRenderer_shared`, `tryResources_shared`, `tryInput_shared`, `tryAsyncFile_shared`,
  `tryeditor_shared`; агрегат для всех — `das_modules` (`tools/agent/build.sh modules`).
- Собирай их **только в Release**. Обе конфигурации пишут один и тот же файл
  `modules/<имя>/<имя>.shared_module`; Debug-сборка затирает Release-модуль, и редактор с
  CLI начинают грузить debug-бинарник.
- В приложениях те же `.cpp` линкуются статически (`engine_graphics`/`editor_core`), модули
  регистрируются через `DECLARE_MODULE`/`PULL_MODULE` в
  `engine_source/core/include/engine/core/GameScriptSetup.hpp` и
  `editor/include/editor/EditorScriptSetup.hpp`. Shared-копии нужны внешнему daslang: CLI-гейту
  и редактору скриптов.
- Пересобирай модули после правок файла модуля, setup-файлов и любого `addExtern` /
  регистрации типов, видимых из `.das`.
- Новый модуль: файл `Module_X.cpp`, вызов `tryengine_add_das_module()` в CMake,
  `modules/<имя>/.das_module` с `register_dynamic_module`, `DECLARE_MODULE`/`PULL_MODULE` в
  setup-файлах и имя каталога в `DAS_MODULES` (`tools/agent/env.sh`).
- `.shared_module` и `.das_module.manifest` генерируются и не хранятся в git. После
  свежего клона один раз запусти `tools/agent/build.sh modules`.

## 4. Проверка изменений

Единая точка входа:

```bash
tools/agent/verify.sh            # формат + синтаксис + гейт .das + сборка затронутых таргетов
tools/agent/verify.sh --fast     # без сборки
tools/agent/verify.sh --tidy     # ещё и clang-tidy по изменённым translation unit
```

### Гейт `.das`

```bash
daslang -dry-run -no-lint \
  -project project.das_project \
  -load_module "$PWD/modules/tryRenderer" \
  -load_module "$PWD/modules/tryResources" \
  -load_module "$PWD/modules/tryInput" \
  -load_module "$PWD/modules/tryAsyncFile" \
  -load_module "$PWD/modules/tryeditor" \
  -load_module /opt/daslang/modules/dasLLVM \
  <файл.das>
```

Почему важен каждый флаг:

- `-project project.das_project` резолвит пути модулей `tryengine/`, `tryeditor/`,
  `trygame/`, `imgui/`, `llvm/`, `daslib/`; без него проект не компилируется вообще.
- `-load_module` всё равно нужен: нативные модули (`tryInput`, `tryRenderer`, ...)
  регистрируются дескрипторами `modules/<имя>/.das_module`, и без них `-project` даёт
  `error[20605] missing prerequisite 'tryInput'`. Список каталогов живёт в `DAS_MODULES`
  (`tools/agent/env.sh`) — добавляешь модуль, добавь его и туда.
- `/opt/daslang/modules/dasLLVM` нужен потому, что `engine_source/daslang/Engine.das`
  требует `daslib/just_in_time`; без модуля LLVM симуляция падает с
  `LLVM JIT: the emitter module llvm/daslib/llvm_macro did not load`.
- `-dry-run` компилирует и симулирует (строже, чем `-compile-only`), но не выполняет код.
- Проверяй и код возврата, **и** наличие `error[` в выводе: daslang умеет печатать ошибки
  и завершаться с кодом 0.
- `daslang -tool lint <файл>` не видит нативные модули этого проекта (печатает
  `SKIP ... missing prerequisite`), а `-project` ломает сам инструмент lint. Линтуй через
  MCP-инструмент или пропускай линт.

### `script_pak`

`game_client` не компилирует скрипты: он грузит `./script_pak`. Пак собирается в окне
`BuildSettings` редактора (`Build Scripts` / `Z`) в корень выбранной папки билда, рядом с
`content/`; тест-компиляция — `Test Compile Scripts` / `X`. Правка `.das` не попадёт в игру,
пока пак не пересобран в GUI. `tools/agent/editor-check.sh` запускает редактор с таймаутом и
ищет в консоли `error[`; он компилирует те же исходники, что и CLI-гейт, но требует дисплей.

## 5. Правила C++

- Точка входа: EditorApp/GameApp. Читай их, прежде чем что-то добавить/изменить.
- Движок пишется как надоб моделей. Editor и Game - потребители. Они конфигурируют модули.
- Модули не должны знать об Editor! Они должны просто давать место для расширения, куда
  editor можешь вписаться.
- Исключения и RTTI отключены (`-fno-rtti -fno-exceptions`). Не добавляй `throw`, `try`
  и `dynamic_cast`.
- Контейнеры и строки — из EASTL (`eastl::vector`, `eastl::string`, ...). `std`
  используется только там, где уже есть (`std::expected`, `std::unique_ptr`, `std::move`).
- Ошибки — значения: `tryengine::Result<T>` (`std::expected<T, core::Error>`), `LogError`,
  `LogInfo`, `TRY_ASSERT`, `TRY_INLINE`, `LogAndMakeError`. Не добавляй выходные параметры
  под коды ошибок.
- Системы: объяви класс, зарегистрируй через `engine.RegisterSystem<T>(args...)`, доставай
  через `engine.Get<T>()`. Поля — `snake_case_`, методы и типы — `PascalCase`, свободные
  функции — `PascalCase`.
- ECS в C++ нет: сущности и компоненты живут в daslang/DECS. `SlotMap` и `TypeRegistry` в
  `core` — утилиты, а не реестр сущностей.
- Заголовки лежат в `<lib>/include/engine/<lib>/*.hpp`, исходники — в `<lib>/src/*.cpp`,
  include пишется как `"engine/core/Engine.hpp"`.
- `engine_core` не должен зависеть от Vulkan, SDL и ImGui: headless-сервер линкует его.
- Стиль принудительный: `.clang-format` (Google, 120 колонок, 4 пробела, указатель слева) и
  `.clang-tidy` (bugprone и performance, readability отключён).

## 6. Правила daslang

- Синтаксис gen2 (фигурные скобки). Новые файлы начинаются с `options gen2`; gen1 не
  пишем.
- Резолвинг модулей — `project.das_project` → `module_get`: `tryengine/...` указывает на
  `engine_source/daslang`, `tryeditor/...` — на `editor/daslang`, `trygame/...` — на
  `game/scripts`, плюс `imgui/`, `llvm/`, `daslib/`, `live/`. `DAS_PAK_ROOT` абсолютный.
- Точки входа — `[export] def Name()`; C++ вызывает их по имени через
  `ScriptSystem::InvokeFunctionFast/Safe`, поэтому переименование молча ломает C++ сторону.
- ECS — только DECS (`daslib/decs`) и макросы движка (`create_entity`, `update_entity`,
  `apply_decs_template`, `EcsRegister`), см. `engine_source/daslang/EcsMacro.das`,
  компоненты в `engine_source/daslang/Types.das`, живой пример — `game/scripts/Game.das`.
- Форматирование: `das-fmt --verify` (или `das-fmt` для перезаписи). Линт: MCP `lint`.

## 7. Стиль кода

- Комментарии в этом репозитории пишутся по-русски; подстраивайся под файл, который
  правишь, а не переключай его язык.
- Не оставляй закомментированный код, отладочные принты и TODO-заглушки.
- Следуй уже существующему слою и паттерну; перед изобретением параллельного механизма
  поищи по репозиторию.
- Подробные соглашения: `.dsh/skills/tryengine-cpp/SKILL.md` и
  `.dsh/skills/tryengine-daslang/SKILL.md`.
- Неочевидные детали конкретного каталога — в его `AGENTS.md`
  (`engine_source/graphics/AGENTS.md`, `editor/AGENTS.md`). После задачи с подтверждением
  дописывай туда только долговечные выводы, а не пересказ диффа.

## 8. Границы

Никогда не изменяй и не коммить: `3rdparty/`, `build-*/`, `.idea/`,
`script_pak`, `content/`, `engine_content/artifacts`, `game/artifacts`,
`*.shared_module`, `*.das_module.manifest`, `.cache/`.

Никогда не запускай: `git push --force`, `git reset --hard` в общей ветке, `rm -rf` вне
генерируемых каталогов, `cmake -B` по существующему каталогу сборки, форматтер по всему
репозиторию как попутное изменение.

Git: работай в `agent/<задача>`, коммиты делай мелкими и включай только изменённые файлы.
Без попутных рефакторингов, массового переформатирования, обновлений зависимостей и
несвязанных чисток. Не коммить в `main`, если пользователь не попросил. Комиты пиши в
Conventional стиле на английском языке.

Остановись и спроси, если: задача требует новой зависимости, правки `CMakeLists.txt`,
переименования публичного API или `[export]`-функции, либо результат нельзя проверить
командами выше.

## 9. Определение готовности

1. `tools/agent/verify.sh` завершился с кодом 0, либо отчёт объясняет, какой шаг не
   применим.
2. `git status --short` показывает только те файлы, которые ты собирался изменить.
3. В отчёте указано, что изменено, какие команды запускались и что **не** проверено (GUI,
   вывод GPU, `script_pak`, всё, что требует дисплея).
4. Если правки `.das` должны попасть в game client, скажи, что `script_pak` нужно
   пересобрать в редакторе (`BuildSettings` → `Build Scripts` / `Z`) в папку билда.

## 10. Инструменты

- `tools/agent/` — `doctor.sh` (проверка окружения), `build.sh`, `das-check.sh`,
  `editor-check.sh`, `fmt.sh`, `tidy.sh`, `verify.sh`.
- `.dsh/skills/` — `tryengine-build`, `tryengine-daslang`, `tryengine-cpp`, `daslang`
  (язык: копия апстримного `skills/daslang` из `GaijinEntertainment/daScript`).
- MCP-сервер `tryengine-daslang` — `lint`, `cpp_compile_check`, `cpp_build_info`,
  `cpp_grep_usage`, `find_symbol`, `format_file`. `compile_check` и `list_*` падают на
  точках входа, которые тянут `daslib/just_in_time` (паника симуляции, см. раздел 4), а
  `eval_expression`, `run_script`, `list_module_api`, `describe_type` не работают, пока
  `/opt/daslang` недоступен на запись; для компиляции используй CLI-гейт.
