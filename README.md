# tryengine

`tryengine` — небольшой 3D-движок на C++23: ядро, рендер и платформа написаны на C++,
игровая и редакторская логика — на [daslang](https://github.com/GaijinEntertainment/daScript).

## Технологический стек

* **Core:** C++23, EASTL (контейнеры и строки), `Result<T>`/`Error` вместо исключений.
* **ECS:** [DECS](https://github.com/borisbat/daslang) (`daslib/decs`) — ECS внутри
  daslang: сущности и компоненты живут в скриптах.
* **Graphics:** [Vulkan](https://www.vulkan.org/) 1.4 напрямую: [volk](https://github.com/zeux/volk)
  (загрузчик), [vk-bootstrap](https://github.com/charles-lunarg/vk-bootstrap) (инициализация),
  [VMA](https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator) (аллокации),
  dynamic rendering.
* **Platform:** [SDL3](https://github.com/libsdl-org/SDL) — только окно, ввод и
  Vulkan-поверхность.
* **UI:** [ImGui](https://github.com/ocornut/imgui) (docking branch, Vulkan backend) для интерфейса редактора.
* **Scripts:** [daslang](https://github.com/borisbat/daslang) 0.6.5 (JIT), мост C++ ↔ `.das`
  через нативные модули.
* **Assets:** [tinygltf](https://github.com/syoyo/tinygltf) (glTF/GLB),
  [stb](https://github.com/nothings/stb) (изображения), [bc7enc](https://github.com/richgel999/bc7enc_rdo)
  (BC7), [Slang](https://github.com/shader-slang/slang) (шейдеры → SPIR-V),
  [glaze](https://github.com/stephenberry/glaze) (JSON).

## Детали реализации

* **Decoupled Logic:** архитектура разделена на `game_client` и `game_server`. Ядро
  (`engine_core`) не зависит от Vulkan, SDL и ImGui, поэтому headless-сервер линкует его
  без графического контекста.
* **Artifact-based Asset Pipeline:** ресурсы не грузятся «как есть». Импортер вытаскивает
  из glTF меши и текстуры, компилирует шейдеры Slang в SPIR-V и складывает всё в
  кастомные бинарные артефакты (`game/artifacts`, `engine_content/artifacts`). Рантайм
  читает готовые артефакты по id.
* **Рендер:** Vulkan-граф `RenderGraph` (узел = pass + scope), swapchain и кадровая
  синхронизация — плюс ImGui на Vulkan backend.
* **Scripting/ECS:** логика игры и редактора — на daslang. Компоненты (`Transform`,
  `Camera`, `PointLight`, ...) описаны в `engine_source/daslang/Types.das`, доступ к ECS —
  через макросы `create_entity`, `update_entity`, `apply_decs_template` из
  `engine_source/daslang/EcsMacro.das`.
* **Tooling:** редактор интегрирован в общую систему через ImGui Dockspace, включая
  Inspector, Hierarchy и Viewports.

## Структура проекта

```text
├── engine_source/          # ядро: core, async, resources, graphics (Vulkan), platform (SDL3)
├── engine_source/daslang/  # API движка для .das: Engine, Render, Input, Scene, Types, EcsMacro
├── editor/                 # редактор: C++ (ImGui, импортеры) и editor/daslang
├── game_client/            # клиент: читает script_pak, .das не компилирует
├── game_server/            # headless-заглушка, в сборку пока не входит
├── game/                   # игровые скрипты, ассеты и артефакты
├── engine_content/         # базовые ресурсы движка и их артефакты
├── modules/                # по папке на биндинг: .das_module (в git) + shared_module
├── tools/agent/            # скрипты сборки и проверки для агентов
├── build-debug/            # CMake Debug (генерируется)
└── build-release/          # CMake Release (генерируется)
```

## Build & Run

### Системные требования

> [!IMPORTANT]
> Нужен драйвер с поддержкой **Vulkan 1.4**.

* Fedora Linux (основная платформа разработки), GCC 15 с C++23.
* CMake ≥ 3.31, Ninja, `ccache`, `mold`.
* daslang 0.6.5 в `/opt/daslang` (`find_package(DAS)`) и его модуль `dasLLVM`.

### Сборка

Каталоги сборки — `build-debug` (Debug) и `build-release` (Release). Оба делят кеш исходников `3rdpatry/` (CPM) и `ccache`.

```bash
# Сборка приложений (Debug по умолчанию)
cmake --build build-debug --target editor game_client -j "$(nproc)"

# Модули-биндинги daslang (нужны редактору и CLI для нативных типов) — только Release
cmake --build build-release --target das_modules -j "$(nproc)"

# Запуск — строго из корня репозитория
./build-debug/bin/editor
./build-debug/bin/game_client
```

Те же команды через обёртку: `tools/agent/build.sh editor`, `tools/agent/build.sh modules`.
Биндинги собираются только в Release: каждый C++-модуль (`tryRenderer`, `tryResources`,
`tryInput`, `tryAsyncFile`, `tryeditor`) — это отдельная библиотека
`modules/<имя>/<имя>.shared_module`, и обе конфигурации пишут её в один и тот же файл,
поэтому Debug-сборка затирает Release-модуль. Цель `das_modules` собирает их все.

`script_pak` собирается в редакторе в окне `BuildSettings` (кнопка `Build Scripts` или
`Z`) и кладётся в корень выбранной папки билда — рядом с `content/`, а не в корень
репозитория. Тест-компиляция — `Test Compile Scripts` или `X`. `game_client` читает
готовый пак и `.das` не компилирует.

## Для агентов

Правила, команды и границы — в [AGENTS.md](AGENTS.md). Скрипты — `tools/agent/`
(`verify.sh`, `das-check.sh`, `build.sh`, `doctor.sh`), скиллы — `.dsh/skills/`.

<img alt="Image" height="1011" src="https://github.com/user-attachments/assets/5471e19d-585d-407a-908f-0a3ff5a34fc9" width="1920"/>
