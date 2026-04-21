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
├── editor/       # Редактор и инструменты импорта (tinygltf, stb)
├── engine/       # Ядро (Core, Graphics, Resources)
├── game/         # Ресурсы и игровая логика
├── game_client/  # Клиентская часть
└── game_server/  # Серверная часть (headless)
```

## Build & Run
Движок использует CMake. Зависимости подтягиваются автоматически.

```bash
# Сборка и запуск из корня проетка (рекомендуется Ninja)
cmake -B build -G Ninja
cmake --build build

# Запуск редактора
./build/editor/editor
```
*Разработка ведется на Fedora Linux. Кроссплатформенность: заложена через SDL3, требует тестов на других OS.*

## Roadmap
* [ ] Расширение пайплайна SDL GPU (PBR, тени).
* [ ] Многопоточность
* [ ] Физическая система на XPBD(Extended Position Based Dynamics)
* [ ] Сетевой код (Client-side prediction).


<img alt="Image" height="1011" src="https://github.com/user-attachments/assets/5471e19d-585d-407a-908f-0a3ff5a34fc9" width="1920"/>

<img width="795" height="592" alt="Image" src="https://github.com/user-attachments/assets/366aea5f-1902-4121-9c7f-337d59a66c54" />  
