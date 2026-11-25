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
| `engine_source/graphics` | `VulkanDevice`, `VulkanSwapchain`, `RenderGraph`, проходы, загрузчики, игровой UI (`UiPass`/`UiSystem`); биндинги `tryRenderer`, `tryResources`, `tryUi` (`src/Module_Renderer.cpp`, `Module_Resources.cpp`, `Module_Ui.cpp`) |
| `engine_source/platform` | окно SDL3, события, `InputService`, SDL-фильтры; биндинг `tryInput` (`src/Module_Input.cpp`) |
| `engine_source/resources` | загрузка pak/артефактов, `ResourceManager`, `AsyncFileManager` (io_uring), загрузчики; биндинг `tryAsyncFile` (`src/Module_AsyncFileManager.cpp`) |
| `engine_source/async` | корутины `Task<T>`, пул потоков и главный исполнитель, pollable-хендлы |
| `engine_source/physics` | `PhysicsWorld` на Jolt Physics: тела, примитивные коллайдеры, контакты, debug-линии; биндинг `tryPhysics` (`src/Module_Physics.cpp`); ECS-компоненты `RigidBody`/`*Collider`, runtime-компонент `PhysicsBody` и синхронизация с ними — `engine_source/daslang/Physics.das` |
| `engine_source/daslang` | исходники API движка для `.das` (`require tryengine/...`) |
| `editor/src`, `editor/include` | приложение редактора, панели ImGui, импортеры (glTF, Slang, текстуры), asset pipeline, `Module_TryEditor` |
| `editor/daslang` | редакторские скрипты (`require tryeditor/...`), точка входа `EntryPoint.das` |
| `game_client` | рантайм-приложение (читает `./script_pak`, `.das` не компилирует) |
| `game_server` | headless-заглушка; в сборку пока не добавлена |
| `game/scripts`, `game/assets`, `game/shaders`, `game/shader_modules`, `game/artifacts` | игровая логика, контент, шейдеры, их модули и артефакты |
| `engine_content` | поставляемый с движком контент: `assets`, `shaders`, `shader_modules` и их артефакты |
| `modules/` | по папке на daslang-модуль: дескриптор `.das_module` (в git) и собранный `.shared_module` |
| `build-debug`, `build-release` | каталоги сборки CMake (генерируются) |
| `tools/agent/`, `tools/tests/`, `.dsh/skills/` | скрипты для агентов, headless-сюита `.das`, скиллы |
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

- Один C++-модуль = один файл = одна shared-библиотека
  `modules/<имя модуля>/<имя модуля>.shared_module`. Файл лежит в том слое, чьи типы он
  отдаёт в скрипт: `engine_source/platform/src/Module_Input.cpp`,
  `engine_source/resources/src/Module_AsyncFileManager.cpp`,
  `engine_source/graphics/src/Module_Renderer.cpp` и `Module_Resources.cpp`,
  `engine_source/physics/src/Module_Physics.cpp`, `editor/src/Module_TryEditor.cpp`,
  `engine_source/core/src/Module_Log.cpp` (журнал — тип ядра).
- Рядом с библиотекой лежит дескриптор `modules/<имя>/.das_module`: daslang читает его для
  `-load_module` и при скане `modules/`, поэтому дескриптор **хранится в git**.
- Цели CMake заводит `tryengine_add_das_module()` (`cmake/DasModule.cmake`):
  `tryRenderer_shared`, `tryResources_shared`, `tryInput_shared`, `tryAsyncFile_shared`,
  `tryeditor_shared`, `tryPhysics_shared`, `tryUi_shared`, `tryLog_shared`; агрегат для
  всех — `das_modules` (`tools/agent/build.sh modules`).
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
tools/agent/verify.sh            # формат + синтаксис + гейт .das + сюита + сборка затронутых таргетов
tools/agent/verify.sh --fast     # без сборки
tools/agent/verify.sh --tidy     # ещё и clang-tidy по изменённым translation unit
tools/agent/test.sh              # только headless-сюита поведения .das
```

### Гейт `.das`

```bash
daslang -dry-run \
  -project project.das_project \
  -load_module "$PWD/modules/tryRenderer" \
  -load_module "$PWD/modules/tryResources" \
  -load_module "$PWD/modules/tryInput" \
  -load_module "$PWD/modules/tryAsyncFile" \
  -load_module "$PWD/modules/tryeditor" \
  -load_module "$PWD/modules/tryPhysics" \
  -load_module "$PWD/modules/tryUi" \
  -load_module "$PWD/modules/tryLog" \
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
- Линт **не выключай**: редактор компилирует с ним, и `-no-lint` однажды скрыл
  `error[30264] type expression result is used` на `type<T>`; `type<T>` — no-storage тег,
  его можно передать, но нельзя читать.
- Проверяй и код возврата, **и** наличие `error[` в выводе: daslang умеет печатать ошибки
  и завершаться с кодом 0.
- Файлы с `//! expect: compile-error` — негативные тесты сюиты: гейт их пропускает, а
  `test.sh` проверяет, что компилятор действительно печатает `error[`.
- `daslang -tool lint <файл>` не видит нативные модули этого проекта (печатает
  `SKIP ... missing prerequisite`), а `-project` ломает сам инструмент lint. Линтуй через
  MCP-инструмент или пропускай линт.

### Сюита поведения `.das`

`tools/agent/test.sh` прогоняет `tools/tests/*.das` — headless-проверки слоя daslang (ECS,
transform pass, префабы, сцены, undo). Гейт компиляции проверяет, что исходники собираются;
сюита — что поведение верно, поэтому она запускает программу, а не `-dry-run`.

- Каждый тест — самостоятельный процесс: глобальное состояние DECS/Time/AssetCache не
  протекает между тестами, и файл можно запустить руками тем же гейтом.
- Тест печатает `FAIL: <что>` и возвращает из `main` 0 при успехе и ненулевой код при
  провале; окончательное решение принимает раннер по коду возврата и выводу.
- Нештатное ожидаемое поведение объявляется первой строкой
  `//! expect: panic|exception|compile-error` (по умолчанию `pass`).
- Запуск: `tools/agent/test.sh [имя ...]`, `--list`, `-j N`, `-v`; логи в
  `tools/agent/.tmp/tests/`. Нужны собранные `modules/`, как и гейту.
- Новый тест кладётся в `tools/tests/<тема>.das`; раннер находит его сам.

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

- Синтаксис gen2 (фигурные скобки). Новые файлы начинаются с `options gen2` и `options relaxed_assign = false`;
- Язык `.das` — английский, включая комментарии и строки интерфейса (см. §7).
- Резолвинг модулей — `project.das_project` → `module_get`: `tryengine/...` указывает на
  `engine_source/daslang`, `tryeditor/...` — на `editor/daslang`, `trygame/...` — на
  `game/scripts`, плюс `imgui/`, `llvm/`, `daslib/`, `live/`. `DAS_PAK_ROOT` абсолютный.
- Точки входа — `[export] def Name()`; C++ вызывает их через
  `ScriptSystem::InvokeFunctionFast<"Name">()`/`InvokeFunctionSafe<"Name">()`, поэтому
  переименование молча ломает C++ сторону. Имя — template-параметр, поиск функции в
  таблице кешируется до смены поколения контекста.
- Горячая перезагрузка компилирует новое поколение в отдельной `ModuleGroup` со свежим
  `FileAccess` и AST-кешем: иначе daScript переиспользует старые модули и правка не видна.
- Первая загрузка и горячая перезагрузка компилируются на `ThreadPool` через
  `ScriptSystemConfig::post_background`. Пока идёт компиляция, `ScriptSystem::IsReady()` ложно,
  и вызовы скриптов отклоняются: компилятор daScript не потокобезопасен. Экран
  загрузки/ошибки рисует C++ (ImGui в редакторе, fallback `UiPass` с точками в игре), а не
  скрипт. Первая неудачная загрузка запирает скрипты (`ScriptStatus::Failed`), но процесс
  не перезапускается: оверлей редактора и F5 зовут `RetryMainScriptAsync`, который читает
  файлы заново. Неудачная перезагрузка оставляет старое поколение рабочим, но
  `ScriptsHealthy()` запрещает Play до успешного F5. Длительности компиляции/симуляции и
  готовность приложения пишутся в лог категории `Script`.
- `daScriptEnvironment` — thread-local. Пул-воркер обязан привязать окружение, созданное
  на главном потоке (`getBound`/`exchangeBound`), и завести `ReuseCacheGuard` на время
  компиляции; без этого `compileDaScript` падает в `AstSerializer` ещё до разбора первого
  модуля. Новое окружение на воркере не создавай: модули проекта и генерация должны
  остаться в том же реестре, что и у главного потока.
- ECS — только DECS (`daslib/decs`) и макросы движка (`create_entity`, `update_entity`,
  `apply_decs_template`, `EcsRegister`), см. `engine_source/daslang/EcsMacro.das`,
  компоненты в `engine_source/daslang/Types.das`, живой пример — `game/scripts/Game.das`.
  Компонент объявляется **только** reader-макросами, у них две формы:
  `%ecs~("transform", type<Transform>) %%` — одно значение с явным именем;
  `%ecs_template~(type<Camera>) %%` — `[decs_template]`-компонент, у которого имя и есть
  тип (поля уже называются `Camera_fov`). Флаги общие: `hidden = true` (Inspector не
  рисует и «Add component» не предлагает), `transient = true` (сцена/префаб не пишут и не
  читают). По умолчанию компонент сохраняется и показывается.
  Возможность *типа* быть записанным — отдельная аннотация `[DecsSerializable]` на
  структуре одного значения: она регистрирует ConfigFormat adder и writer. Без неё
  компонент можно объявить только `transient = true`; `EcsValidateRegistry` в
  `EngineStart` печатает `[ECS ERROR]`, если нет writer'а. Модулю, который помечает
  структуру, нужны свои `require tryengine/Serializable` и
  `require tryengine/ConfigFormatBoost` (require не транзитивен), а объявляющему
  компонент — `require tryengine/EcsMacro`.
  Объявленный тип попадает в `decsState.componentTypeCheck` при регистрации: компонент с
  тем же именем, но другим типом паникует при создании сущности. Первый обновлённый
  архетип проверяется `EcsValidateArchetypes` в начале `EngineUpdate` и печатает
  `[ECS ERROR]`, если имя не объявлено; указатели `mesh`/`material` — исключение.
  У не-шаблонного компонента имя аргумента `query` и есть имя компонента:
  `query() $(transform : Transform)` читает `transform`, а `$(t : Transform)` ищет `t` и
  молча не находит ничего (проверяются архетипы, не запросы). У `decs_template` имя
  аргумента ничего не значит — поля берутся по префиксу типа.
- Игровой UI: `tryengine/Ui` (`engine_source/daslang/Ui.das` + `UiTypes.das`) и нативный
  `tryUi` (`engine_source/graphics/src/Module_Ui.cpp`). Дерево retained: живёт между кадрами в
  daslang, но объявляется каждый кадр, узлы находятся по 64-битному id. Layout, состояния и
  анимации — в скриптах; геометрию строит C++ (`UiPass`), скрипт отдаёт только команды через
  `[export] get_ui_draw_cmds`. Звук наведения/нажатия — событие (`Ui::OnSound`), не
  воспроизведение: аудио-подсистемы в движке нет. Полный контракт — `engine_source/daslang/UI.md`.
- Слои UI: виджеты-примитивы (`tryengine/Ui`, повторяются), составные виджеты игры
  (`game/scripts/UiWidgets.das`: функция с id-параметром, без своего состояния и цветов),
  тема (`game/scripts/UiTheme.das`, ставится один раз `Ui::SetTheme` из `[init]`: по стилю на
  вид + именованные стили и шаблоны кнопок) и экраны: **по файлу на экран**
  (`game/scripts/ScreenMainMenu.das`, `ScreenHud.das`, `ScreenPause.das`, `ScreenShop.das`),
  каждый сам регистрируется в `[init]` (`Ui::RegisterScreen("hud", @@BuildHud)`) и рисуется
  `Ui::Screen`/`FrameScreen`; экран уникален и собран из виджетов. Общие состояние и подписи —
  `game/scripts/GameState.das`, навигация — `game/scripts/GameScreens.das` (политика поверх
  движкового `Ui::ScreenStack`), `game/scripts/GameUi.das` — только манифест (require экранов)
  и `Frame()`. У одинакового локального id в двух экранах разные узлы: имя экрана входит в id.
  Новые стили и цвета добавляются в тему, а не в разметку.
- Поток игры: `GameStart` регистрирует время и зовёт `GameFlow::Start()` — главное меню,
  физика на `game_world_clock`, ни одной сцены; экран меню грузит сцену
  `GameFlow::LoadScene` (loader → `Scene::Load` → HUD). `GameUpdate` идёт только при
  `GameFlow::IsReady()`. Выход из меню — `RequestQuit()` (`tryInput`), тот же флаг, что у
  закрытия окна.
- UI не часть `GameUpdate`: `[export] GameUiFrame()` (`game/scripts/Game.das`) вызывает и
  `game_client`, и редактор. UI живёт на своём клоке (`game_ui_clock`/внутренний UiClock),
  поэтому пауза `game_world_clock` его не останавливает — это нужно меню паузы игры.
  Пауза **редактора** (`Pause`) замораживает всю игру, UI включённо: редактор не объявляет
  UI-кадр, проход перерисовывает последний список команд, и указатель ничего не задевает.
  В Edit режиме игра не запущена: редактор рисует выбранный в панели **Screens** экран
  (`editor/daslang/UiScreens.das`) внутри `Ui::SetPreviewMode(true)` — hover/press работают, но
  действия, звуки и записи в `var` подавлены. Точки `GameUiFrame`, `Ui::Frame`,
  `Ui::Begin/End`, `get_ui_draw_cmds` — контракт с C++: переименование ломает молча.
- Стек экранов — механизм в движке (`Ui::ScreenStack`: `Show`/`Push`/`Pop`/`Remove`/`Draw`,
  имена проверяются по реестру), политика — в игре (`game/scripts/GameScreens.das`: стек
  никогда не пустой, `Close` возвращает HUD). Экземпляр стека один и живёт в игре; порядок
  массива = порядок отрисовки, поэтому второй экран ложится поверх HUD. Модальные окна
  рисуются через `Ui::Backdrop` — полноэкранный слой, который забирает указатель, иначе
  нижний слой кликается сквозь окно.
- Шрифт игрового UI — импортируемый ассет: `.ttf` в `game/assets/fonts`, `FontImporter` печёт
  SDF-атлас в артефакт `FontBinary`, `UiConfig.font_guid`/`kDefaultUiFontGuid` его адресует;
  см. `game/assets/fonts/README.md`.
- Текстуры игрового UI: `Ui::ImageTexture(id, guid, size, style)` адресует текстуру по **guid**
  (не путём: в паке нет таблицы путей для ассетов) и сам собирает материал из UI-шейдера.
  `TextureImporter` (`.png`/`.jpg`/`.jpeg`) печёт `TextureBinary` по настройкам из `.meta`
  (`format`, `generate_mips`, `max_size`, `filter`, `wrap`); guid пишется и в тему игры, и в
  `UiConfig.textures`, потому что билд кладёт UI-текстуры в common-пак только по этому списку.
  Настройки импорта редактируются в панели **Import Settings**, там же кнопка
  **Reimport** (перезапекание без перезапуска редактора); проверка запекания —
  `tools/agent/texture-probe.sh`; см. `game/assets/ui/README.md`.
- Текст контента читается только через `tryengine/Assets.das`, документ разбирается через
  `tryengine/Content.das`: скрипт называет авторский путь,
  движок превращает его в `PathId(путь)` и берёт loose-файл (редактор) или чанк пака
  (игра). Пути в игровом коде пишутся относительно `game/`: `content/...` и `data/...`;
  префикс `game/` подразумевается. Расширение файла указывается всегда явно, ничего не
  дописывается. Прямой `daslib/fio`/`FileReader` в игровой логике запрещён.
- Общий слой документов: `tryengine/Content.das`. Здесь живут путь по виду документа
  (`DocumentKind.Scene/Prefab/Bundle`), stem, `Find` через `Assets::Exists`, разбор
  ConfigFormat и общий сбор `model` в `Dependencies`. `Content` документы
  не кеширует: готовые значения живут в `tryengine/AssetCache.das`.
- `tryengine/AssetCache.das` — один кеш на все виды ассетов: общая ledger-таблица
  (`state`, `deps`, запрос текста) плюс типизированная `table<string; T?>` в модуле вида.
  Вид публикует `AssetKindOps` из `[init]`, поэтому Loader, прогресс и `ClearAll` не знают
  типов. Новый ассет = таблица значений + compile-функция + `RegisterKind`; центрального
  реестра типов нет. `Invalidate`/`Clear`/`ClearAll` идут через типизированную таблицу вида
  и `AssetKindOps`.
- Префабы: `tryengine/Prefab.das` — формат, компиляция и спавн.
  `Prefab::LoadPrefabFile(path)` читает и компилирует один `.prefab` по требованию;
  `Prefab::Spawn("props/sword.prefab", origin)` — синхронный cache-hit и `EntityId`.
  Префаб адресуется путём относительно `content/` (`prefabl.prefab`,
  `props/sword.prefab`) или полным путём; расширение обязательно. Компилированный
  `PrefabTemplate` лежит в `AssetCache`, а не в C++ `ResourceManager`: данные нужны DECS а
  не C++-типу, а сырой текст читается разово (`TextRequests`) и не кешируется. Шаблон
  хранит готовые `ComponentValue` на узел: спавн не разбирает ConfigFormat, а
  `PrefabTemplate.strings` держит строки, запечённые в байты компонентов. Спавн создаёт
  сущности группами по набору компонентов (`create_entities_from_cmp`), а не по одной.
  `CompilePrefab` компонует мировые `_world` и кладёт их в шаблон, поэтому спавн не
  вызывает `UpdateWorldTransforms`: он только сдвигает мир на `origin`. Игра не
  прогревает все префабы: `LoaderRequest::AddPrefab`/`AddBundle` читают только запрошенное
  (пути `content/...` и `data/...`). Полного `RefreshContent` больше нет: префаб грузится
  точечно через `Prefab::LoadPrefabFile`, а редакторская панель `Prefab Inspector`
  выключена и будет переделана под готовые компоненты.
- Ассет-бандлы: `tryengine/Bundle.das`, файлы `game/data/**/*.bundle` (ConfigFormat,
  `version = 1`, `name`, `prefabs: string[]`). В `prefabs` лежат пути `content/...`;
  расширение обязательно. Бандл — авторская группа префабов; билд
  кладёт все `game/data` в common-пак, а `LoaderRequest::AddBundle` разворачивает список в тот же
  шаг предзагрузки префабов.
- `Game.das` — игра и точка входа, а поток сцен — `trygame/GameFlow.das`. `GameStart`
  регистрирует время и зовёт `GameFlow::Start()`: главное меню, физика на
  `game_world_clock`, ни одной сцены. Экран `main_menu` грузит сцену через
  `GameFlow::LoadScene` (loader читает сцену и опциональный префаб, затем `Scene::Load`
  применяет документ из кеша, физика сбрасывается, стек переключается на HUD);
  `GameUpdate` идёт только при `GameFlow::IsReady()`. Ни загрузчик, ни
  `Scene::Load`/`Prefab::Spawn` не читают диск.
- `tryengine/Loader.das` — `Loader::Load(request, callback)` это `[async]`-задача,
  которую запускают через `StartTask`. Пока она идёт, `GetProgress(LoadPhase.Prefabs)` /
  `GetProgress(LoadPhase.Scenes)` отдают `float2(done, total)` по каждой фазе отдельно
  (`10/26` и т.п.), а `PhaseName(phase)` — имя. Callback вызывается один раз на `Ready`;
  после его возврата загрузчик сам делает `Finish` и освобождает ссылки. При ошибке
  callback не вызывается, фаза остаётся `Failed`, доступны `IsFailed()`/`Error()`.
  `GameApp::Init` ничего не прогревает: только паки и `script_pak`. В редакторе Play тот
  же путь; `PlayMode.EndPlay` делает `Loader::Abort`, а задача `Load` завершается по
  своему поколению. `LoaderRequest::AddAsset(kind, path)` принимает любой вид из
  `AssetCache`, поэтому новый ассет не требует правок загрузчика.
- Камера — часть сцены: `test.scene` содержит свою `Camera`, игровой код её двигает, а не
  создаёт и не чинит отсутствующую. `Scene::Load(path)` не принимает callback: успех —
  это `bool`, а scene-specific setup идёт сразу после него. `tryengine/startup`
  автоматически не загружается. Подробности — `engine_source/daslang/LOADER.md`.
- Журнал — один на C++ и скрипты: нативные `LogInfo`/`LogWarn`/`LogError`/`LogCritical`/
  `LogTrace` модуля `tryLog` (доступны через `tryengine/Log`) повторяют C++
  `LogInfo` и компанию; категория — второй, необязательный аргумент (`LogCategory.Physics`),
  по умолчанию `Script`. Запись идёт в консоль, в кольцо `LogHistory` (окно **Log** в
  редакторе) и в `logs/engine-<дата>.log`; файл открыт до выхода, поэтому `tail -f` видит
  строки сразу. Окно фильтрует по важности и категории масками в C++, кнопка Clear чистит
  только кольцо (файл — история запуска). Источник скриптовой строки — место вызова в
  скрипте, так что окно и файл для обоих языков адресуют одно и то же место.
- Форматирование: `das-fmt --verify` (или `das-fmt` для перезаписи). Линт: MCP `lint`.

## 7. Стиль кода

- В C++ комментарии пишутся по-русски: подстраивайся под файл, который правишь, а не
  переключай его язык.
- В Daslang — только английский: комментарии, строки интерфейса, имена. Правило жёсткое:
  ни одного русского слова в `.das`.
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

Остановись и спроси, если: эхо архитектурного решения для выходит за рамки задачи, 
задача требует новой зависимости, правки `CMakeLists.txt`, переименования 
публичного API или `[export]`-функции, либо результат нельзя проверить командами выше.

## 9. Определение готовности

1. `tools/agent/verify.sh` завершился с кодом 0, либо отчёт объясняет, какой шаг не
   применим. Сюита поведения входит в `verify.sh`; отдельно её гоняют
   `tools/agent/test.sh`.
2. `git status --short` показывает только те файлы, которые ты собирался изменить.
3. В отчёте указано, что изменено, какие команды запускались и что **не** проверено (GUI,
   вывод GPU, `script_pak`, всё, что требует дисплея).
4. Если правки `.das` должны попасть в game client, скажи, что `script_pak` нужно
   пересобрать в редакторе (`BuildSettings` → `Build Scripts` / `Z`) в папку билда.
5. Профилирование — **отдельный шаг, а не часть фичи**. Закончив заметную по стоимости
   работу (новый проход, панель, импортёр, система), скажи, какие зоны появились и что
   записывать; снимает и запускает `tools/prof-report.sh` человек, агент разбирает вывод.
   GUI-прогон агент сам не запускает. Зону ставь там же, где делаешь работу: без неё цена
   новой фичи повиснет на корне кадра и её никто не увидит. Подробности —
   `.dsh/skills/tryengine-profile/SKILL.md`.

## 10. Инструменты

- `tools/agent/` — `doctor.sh` (проверка окружения), `build.sh`, `das-check.sh`,
  `editor-check.sh`, `fmt.sh`, `tidy.sh`, `verify.sh`, `test.sh` (headless-сюита поведения
  `.das`), `undo-probe.sh` (история undo/redo на мире DECS без GUI), `log-probe.sh`
  (файловый sink, кольцо журнала, фильтр и Clear без GUI), `shader-probe.sh` (компиляция
  шейдеров и их модулей через настоящий `ShaderCompilerSystem`, без GUI), `gltf-probe.sh`
  (поворот корней модели в Z-up и `.meta` модели через кодек импортёра, без GUI).
- `tools/tests/` — сама сюита: по файлу на тему, каждый тест — самостоятельная программа.
  Соглашения о `Check`, коде возврата и `//! expect:` — в §4.
- `tools/prof-report.sh [file.tryprof]` — отчёт по кадр-логу (по умолчанию самый свежий из
  `logs/`): длительность и перцентили кадра, таблица зон по суммарному и по **собственному**
  времени (без детей), самые медленные кадры с их содержимым. Собирает `tools/prof_report.cpp`
  по требованию в `tools/agent/.tmp` и читает файл **тем же** `Profiler::ImportLog`, что и
  редактор, поэтому отчёт — ещё и проверка файла. В сборку не входит.
- `.dsh/skills/` — `tryengine-build`, `tryengine-daslang`, `tryengine-cpp`,
  `tryengine-profile` (модель кадра, как поставить замер, как записать и прочитать кадр-лог),
  `daslang` (язык: копия апстримного `skills/daslang` из `GaijinEntertainment/daScript`).
- MCP-сервер `tryengine-daslang` — `lint`, `cpp_compile_check`, `cpp_build_info`,
  `cpp_grep_usage`, `find_symbol`, `format_file`. `compile_check` и `list_*` падают на
  точках входа, которые тянут `daslib/just_in_time` (паника симуляции, см. раздел 4), а
  `eval_expression`, `run_script`, `list_module_api`, `describe_type` не работают, пока
  `/opt/daslang` недоступен на запись; для компиляции используй CLI-гейт.

## 11. Ревью, проверки и документация

### Коммиты и дизайн-ноут

- Юнит ревью — ветка целиком (`git diff main...agent/<задача>`), не отдельные коммиты.
  Мелкие коммиты — чекпоинты для отката, не повод останавливаться и спрашивать:
  остановки только по §8. Историю ветки не переписывай (rebase, squash, amend уже
  сделанного) — в `main` человек мержит squash'ем сам.
- Финальный коммит задачи несёт дизайн-ноут в теле, на английском (§8): подход в 1–3
  предложениях; отброшенные альтернативы и почему; инварианты, которые держит решение;
  известные ограничения и что не проверено. Отчёт в чате эфемерен, тело коммита
  долговечно — человек читает его вместо перечитывания всего диффа. Объяснение сдаётся
  вместе с кодом, отдельный запрос «объясни» не должен требоваться.

### Постоянные проверки

- Временные скрипты и тесты — только в `tools/agent/.tmp`, после задачи удаляются.
  Проверка, оставшаяся в `.tmp`, ничего не сторожит: она больше никогда не запустится.
- Поведение слоя daslang, которое понадобится снова, оформляется тестом в `tools/tests/`
  (§4): его гоняет `tools/agent/test.sh` и, через него, `verify.sh`.
- Проверка, которой нужен C++, GPU или собственный процесс, — постоянный
  `tools/agent/<имя>-probe.sh` по образцу `texture-probe.sh`: headless, проверяется код
  возврата **и** наличие `error[` в выводе. Новый probe регистрируется в §10.
- Перед сдачей прогони `tools/agent/test.sh` и постоянные probe затронутых подсистем:
  история/DECS → `undo-probe.sh`, импорт текстур → `texture-probe.sh`. В отчёте (§9) укажи,
  какие тесты и probe запускались.

### Документация

- AGENTS.md описывает долговечное: границы модулей, инварианты, контракты, конвенции,
  «где искать». Имена допустимы только для контрактов, где переименование ломает молча
  (`[export]`, точки входа, макросы, модули). Внутренние имена функций и пересказ кода
  не нужны: источник правды — код, агент читает его сам.
- Не дублируй между корневым и каталожными AGENTS.md: общее — в корне, в каталоге —
  только локальное, со ссылкой на корень.
- Устаревший AGENTS.md — подсказка, не спецификация. Рассинхрон правится пачкой по
  запросу («сверь AGENTS.md каталога X с кодом»), а не после каждой правки.
- Синк после переименования механический: `rg -l 'old_name' -g '*.md'` и замена
  вхождений. Не перечитывай файлы целиком ради замены имени.