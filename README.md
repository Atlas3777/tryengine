# tryengine

`tryengine` — небольшой 3D ECS-движок на C++20.

## Технологический стек
* **Core:** [EnTT](https://github.com/skypjack/entt) (ECS).
* **Graphics:** [SDL GPU](https://wiki.libsdl.org/SDL3/CategoryGPU) (низкоуровневая абстракция над Vulkan/D3D12/Metal).
* **UI:** [ImGUI](https://github.com/ocornut/imgui) (Docking branch).
* **Format Loading:** [tinygltf](https://github.com/syoyo/tinygltf) (GLB/GLTF) и [stb](https://github.com/nothings/stb) (Image loading/writing).
## Детали реализации
* **Decoupled Logic:** Архитектура разделена на game_client и game_server. Ядро движка не имеет жесткой привязки к графическому контексту, что позволяет билдить headless-сервер.
* **Artifact-based Asset Pipeline:** Ресурсы не грузятся «как есть». Вместо этого реализован пре-процессинг: из GLB вытягиваются меши и текстуры, которые конвертируются в кастомные бинарные форматы (артефакты). Это минимизирует время загрузки в рантайме.
* **Tooling:** Редактор интегрирован в общую систему через ImGui Dockspaces, включая Inspector, Hierarchy и Viewports.

## Структура проекта
```text
├── editor/         # Редактор и инструменты импорта (tinygltf, stb)
├── engine_source/  # Ядро (Core, Graphics, Resources)
├── engine_content/  # Базовые ресурсы движка
├── game/           # Ресурсы и игровая логика
├── game_client/    # Клиентская часть
└── game_server/    # Серверная часть (headless)
```

## Build & Run
### Системные требования
> [!IMPORTANT]
> Убедитесь, что в системе установлен **glslangValidator** (входит в Vulkan SDK). Без него компиляция шейдеров невозможна.

### Сборка
Каталоги сборки — `cmake-build-debug` (Debug) и `cmake-build-release` (Release); их же
использует CLion. Оба делят кеш исходников `third_party_src/` и `ccache`.

```bash
# Сборка приложений (Debug по умолчанию)
cmake --build cmake-build-debug --target editor game_client -j "$(nproc)"

# Модули-биндинги daslang (подсветка кастомных типов в редакторе и работа CLI) — только Release
cmake --build cmake-build-release --target tryengine_shared tryeditor_shared -j "$(nproc)"

# Запуск — строго из корня репозитория
./cmake-build-debug/bin/editor
./cmake-build-debug/bin/game_client
```

`script_pak` собирается в редакторе клавишей `Z` (тест-компиляция — `X`): `game_client`
читает готовый пак и `.das` не компилирует.

*Разработка ведется на Fedora Linux. Кроссплатформенность: заложена через SDL3, требует тестов на других OS.*

## Для агентов
Правила, команды и границы — в [AGENTS.md](AGENTS.md). Скрипты — `tools/agent/`
(`verify.sh`, `das-check.sh`, `build.sh`, `doctor.sh`), скиллы — `.dsh/skills/`.

## Roadmap
* [ ] Расширение пайплайна SDL GPU (PBR, тени).
* [ ] Многопоточность
* [ ] Физическая система на XPBD(Extended Position Based Dynamics)
* [ ] Сетевой код (Client-side prediction).


<img alt="Image" height="1011" src="https://github.com/user-attachments/assets/5471e19d-585d-407a-908f-0a3ff5a34fc9" width="1920"/>