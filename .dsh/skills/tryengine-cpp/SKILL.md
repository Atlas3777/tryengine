---
name: tryengine-cpp
description: 'Правила C++ в tryengine: слои и раскладка файлов, Engine/системы, Result/Error/Log, EASTL без исключений и RTTI, async-задачи, ресурсы и pak, рендер-граф Vulkan, стиль и проверка.'
---

# tryengine: C++

Читай перед правкой любого `.cpp/.hpp` и перед добавлением системы/зависимости.

## Слои и зависимости

```
engine_source/core        Engine, Result/Error, Log, ScriptSystem, ScriptPak   (без Vulkan/SDL/ImGui)
engine_source/async       Task<T>, ThreadPool, MainThread, pollable-хендлы
engine_source/resources   PakManager, ResourceManager, AsyncFileManager (io_uring), артефакты
engine_source/graphics    VulkanDevice, VulkanSwapchain, RenderGraph, проходы, загрузчики (Vulkan)
engine_source/platform    окно SDL3, события, InputService
engine_source/physics     PhysicsWorld на Jolt: тела, коллайдеры, контакты, debug-линии
editor                    редактор, ImGui (Vulkan backend), импорт ассетов, пайплайн
game_client               игровое приложение
```

Правила:

- `engine_core` не должен тянуть Vulkan, SDL или ImGui — headless-сервер линкует его.
- SDL3 используется только для окна, ввода и Vulkan-поверхности; SDL GPU и SDL Renderer
  в проекте выключены (`SDL_GPU OFF`, `SDL_RENDER OFF`).
- Новая зависимость — только через `CPMAddPackage` в `CMakeLists.txt` соответствующей
  библиотеки; это изменение уровня сборки, согласуй его.
- Кросс-слойные знания — через интерфейсы в `core`/`resources`, а не включением заголовков
  «сверху вниз».
- ECS в C++ нет: сущности и компоненты живут в daslang/DECS. `SlotMap` и `TypeRegistry` в
  `core` — утилиты, а не реестр сущностей; не заводи второй ECS на C++.

## Раскладка и имена

- `<lib>/include/engine/<lib>/Name.hpp` — заголовки, `<lib>/src/Name.cpp` — реализация.
- Include пишутся от корня include-каталога: `#include "engine/core/Engine.hpp"`.
- Типы и методы — `PascalCase`, поля — `snake_case_` (с подчёркиванием), свободные
  функции — `PascalCase`, константы — `UPPER_CASE` или `static constexpr`.
- `#pragma once`; forward declaration предпочтительнее include, если хватает.

## Engine и системы

```cpp
class MySystem { public: explicit MySystem(Args...); ... };

auto& system = engine.RegisterSystem<MySystem>(args...);
auto& again = engine.Get<MySystem>();      // TRY_ASSERT, если не зарегистрирована
MySystem* maybe = engine.TryGet<MySystem>();
```

- Лимит систем — `Engine::MAX_SYSTEMS` (32), регистрация дважды — assert.
- Порядок регистрации задаёт зависимости; системы получают друг друга через `engine.Get<>()`.
- Регистрация живёт в `EditorApp::Init` / `GameApp::Init` — не выдумывай второй контейнер.

## Ошибки и логи

- `tryengine::Result<T>` = `std::expected<T, core::Error>`; `Error` — `[[nodiscard]]`, хранит
  `ErrorCode` + сообщение и в Debug ругается, если его не проверили.
- Возврат ошибки: `return LogAndMakeError("... {}", value);` — пишет лог и отдаёт `Error`.
- Проверяй результат: `if (!r.has_value()) { ... }`, `TRY_ASSERT(r.has_value(), "...")` во
  внутренних инвариантах.
- Логи: `LogInfo/LogWarning/LogError(LogCategory::X, "...")`; `LogCategory` расширяется
  осознанно (компилятор ловит опечатки).
- Исключений нет: код не бросает и не ловит. Всё через `Result`, `TRY_ASSERT`, ранний возврат.

## EASTL и std

- Контейнеры/строки — EASTL (`eastl::vector`, `eastl::string`, `eastl::unique_ptr`,
  `eastl::function`). Не смешивай с `std`-контейнерами в одном типе.
- Из `std` разрешено то, что уже используется: `std::expected`, `std::move`,
  `std::forward`, `std::unique_ptr` (вместе с EASTL), `std::source_location`, `std::format`,
  `std::chrono`, `std::atomic`.
- RTTI и исключения выключены (`-fno-rtti -fno-exceptions`): никаких `dynamic_cast`,
  `typeid`, `throw`.

## Async

- `Task<T>` + корутины; запуск `RunAndForget(ThreadPool(), Task)` или
  `RunPollable(ThreadPool(), Task)` (даёт `IsReady()`/`GetResult()` для опроса).
- Переключение на главный поток — `co_await ExecutorSwitch{MainThread()};`.
- Файловые операции — через `AsyncFileManager` (io_uring), не блокируй главный поток
  синхронным чтением.
- Кадровый цикл вручную «крутит» `fm.Pull()`, `MainThread().Pull()`, `fm.Submit()` — не
  блокируй главный поток ожиданием задач.

## Ресурсы и pak

- Ассеты не читаются «как есть»: импортер превращает их в артефакты (`game/artifacts`,
  `engine_content/artifacts`), `PakManager`/`ResourceManager` грузят по id (`uint64_t`).
- `script_pak` — бинарный пак `.das`-файлов (`ScriptPakBinary`), читается `game_client`.

## Рендер

- Vulkan 1.4 напрямую: `VulkanDevice` (volk + vk-bootstrap + VMA), `VulkanSwapchain`,
  dynamic rendering. SDL GPU не используется.
- `RenderGraph` — узел = pass + scope. Новый проход регистрируется в графе и получает
  ресурсы через него, а не напрямую.
- ImGui рендерится через `imgui_impl_vulkan` (docking branch).
- Шейдеры пишутся на Slang и компилируются в SPIR-V при импорте ассета; бинарники шейдеров
  пакуются в артефакты, `*.spv` в git не коммитятся.

## Редактор

- `EditorApp` создаёт `VulkanDevice`, `VulkanSwapchain`, `FrameSync`, системы,
  `ScriptSystem`, `ScriptBuildSystem`, `EditorRender`.
- ImGui-панели — в `editor/src/render`, ui-логика в `editor/daslang`; состояние окна в
  `editor/imgui.ini` (путь относительный, поэтому запуск только из корня репо).
- `Z` собирает `script_pak`, `X` — тест-компиляция (`EditorApp::Run`).

## Скриптовый контракт

- Имена `[export]`-функций (`EngineStart`, `GameStart`, `EngineUpdate`, `GameUpdate`,
  `EditorStart`, `EditorUpdate`) — публичный API для C++; переименование ломает вызовы
  `InvokeFunctionFast/Safe`.
- `DECLARE_MODULE`/`PULL_MODULE` и функции регистрации — только в глобальном namespace.
- Меняешь биндинги — пересобери модули в Release (`tools/agent/build.sh modules`).

## Конвенция осей

В скриптах и математике движка: `X` — вправо, `Y` — вперёд, `Z` — вверх (см. `Game.das`,
`float3 right_raw/front_raw/up_raw`). Не путай с Y-up конвенцией других движков.

## Стиль и проверка

- `.clang-format`: Google, 120 колонок, 4 пробела, указатель влево; `.clang-tidy`:
  `bugprone-*` + `performance-*`, readability выключен.
- Комментарии — по-русски, по делу; без закомментированного кода и отладочных `print`.
- Перед ответом: `tools/agent/verify.sh` (формат + синтакс + сборка затронутых таргетов),
  при необходимости `--tidy`.

## Грабли

- Не добавляй `std::mutex`/`std::thread` там, где уже есть `ThreadPool`/`Task`.
- Не храни указатели на системы между кадрами, если система может быть уничтожена.
- Не меняй порядок полей в `ScriptSystemConfig` (designated initializers).
- Не трогай `third_party_src/` и `build-*`; не пересобирай весь проект без нужды.
