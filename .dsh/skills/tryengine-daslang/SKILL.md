---
name: tryengine-daslang
description: Мост C++ ↔ daslang в tryengine: DECLARE_MODULE/PULL_MODULE, .das_module, project.das_project, [export]-точки входа, DECS, как добавить биндинг и проверить его; какие MCP-инструменты работают.
---

# tryengine: C++ ↔ daslang

Читай перед правкой любого `.das`, перед добавлением типа/функции в `.das`-API и перед
изменением регистрации модулей.

## Как устроен мост

1. C++-модуль daScript (`Module_Renderer`, `Module_Resources`, `Module_Input`,
   `Module_AsyncFileManager`, `Module_TryEditor`) регистрирует handled-типы и функции.
2. Хост (редактор/игра) в `ScriptSystemConfig.register_modules` делает
   `PULL_MODULE(...)` — статически слинкованные модули.
3. `daslang` CLI и подсветка в редакторе берут те же модули из `modules/<имя>/*.shared_module`
   через дескриптор `.das_module`.
4. `.das`-файлы движка оборачивают C++ API: `require tryengine/Engine`, `tryengine/Render` и т.д.

Важно: `DECLARE_MODULE` и функции регистрации должны быть в **глобальном** namespace,
иначе `NEED_ALL_DEFAULT_MODULES` не найдёт символы:

- `engine_source/core/include/engine/core/GameScriptSetup.hpp` — `RegisterGameScriptModules`;
- `editor/include/editor/EditorScriptSetup.hpp` — `RegisterEditorScriptModules`.

`ScriptSystemConfig` собирается designated initializers — **порядок полей менять нельзя**
(`register_modules`, `create_file_access`, `prepare_file_access`, `create_context`, `policies`).

## Где что лежит

| Путь | Что |
| --- | --- |
| `engine_source/daslang/*.das` | API движка: `Engine`, `Render`, `Input`, `Scene`, `Types`, `EcsMacro`, `Serializable`, `QuadTree`, `ConfigFormat` |
| `editor/daslang/*.das` | редактор: `EntryPoint`, `Editor`, `EditorUI`, `Hierarchy`, `Inspector`, `SceneViewPort`, `AssetBrowser`, `BuildManager`, `EditorActions` |
| `game/scripts/Game.das` | игра: `GameStart`, `GameUpdate`, `EngineStart`-хуки |
| `modules/tryengine/.das_module` | дескриптор нативных модулей движка для CLI/редактора |
| `modules/tryeditor/.das_module` | дескриптор `Module_TryEditor` |
| `project.das_project` | резолвер модулей (`module_get`) и абсолютный `DAS_PAK_ROOT` |

## Точки входа и вызовы

- Редактор грузит `./editor/daslang/EntryPoint.das`; тот требует `tryeditor/EditorUI`,
  `trygame/Game` и т.д. То есть при старте редактора компилируется и игра.
- Игра грузит `./game/scripts/Game.das` **из `script_pak`**, а не с диска.
- C++ вызывает функции по имени: `EngineStart`, `GameStart`, `EngineUpdate`, `GameUpdate`
  (игра), `EditorStart`, `EditorUpdate` (редактор) — через
  `ScriptSystem::InvokeFunctionFast` / `InvokeFunctionSafe`.
- Экспортируй через `[export] def Name()`. Переименование `[export]`-функции ломает C++
  молча — это публичный контракт.

## Сборка скриптов

`tryengine::core::ScriptBuildSystem` (в `engine_source/core`):

- `BuildPak(path)` — компилирует `.das` с диска через `project.das_project`, собирает все
  прочитанные файлы в пак и сериализует его (`ScriptPakBinary`).
- `TestCompile(bytes)` — компилирует уже собранный пак и симулирует контекст.

В редакторе они вызываются по клавишам: `Z` — `BuildPak("./game/scripts/Game.das")`,
`X` — `TestCompile("script_pak")`. Гарантии, что пак свежий, нет: это ручное действие.

## Как добавить биндинг (чек-лист)

1. Определи C++-тип/функцию в нужной библиотеке; для handled-типов используй существующие
   паттерны `MAKE_TYPE_FACTORY`/`addExtern` из соседних `Module_*.cpp`.
2. Добавь модуль в `DECLARE_MODULE`/`PULL_MODULE` в глобальном namespace оба setup-файла
   (игровой и редакторский), если он нужен обоим хостам.
3. Обнови `modules/tryengine/.das_module` (или `tryeditor`) — `register_dynamic_module`
   с тем же именем класса, что в `DECLARE_MODULE`.
4. Собери модули **в Release**: `tools/agent/build.sh modules`.
5. Напиши/поправь `.das`-обёртку в `engine_source/daslang` или `editor/daslang`.
6. Проверь: `tools/agent/das-check.sh <файл.das>` (компиляция + симуляция).
7. Если добавил новый префикс модулей или каталог — обнови `project.das_project`.
8. Если тип виден в игре — не забудь, что `script_pak` пересобирается только в редакторе.

## DECS (ECS в скриптах)

`engine_source/daslang/EcsMacro.das` даёт макросы `create_entity`, `update_entity`,
`apply_decs_template`, `EcsRegister`. Типы компонентов приходят из `tryengine/Types`.
Живой пример — `game/scripts/Game.das` (`GameStart`, `GameUpdate`).

## Стиль `.das`

- gen2 (фигурные скобки), первый непустой `options gen2`.
- Заголовок модуля: `module Game shared public` (или `module XXX private` для внутренних).
- Имена: функции/типы `PascalCase`, переменные/поля `snake_case`, константы `UPPER_CASE`.
- Комментарии — по-русски, как в остальном репозитории; без комментариев-заглушек и
  отладочных `print` (кроме осознанных логов).
- Форматирование: `tools/agent/fmt.sh --write <файл.das>` (`das-fmt`).
- `require` — в начале файла, по одному на строку, без неиспользуемых.

## MCP-инструменты (сервер `tryengine-daslang`)

Работают: `lint` (единственный способ линтовать этот проект), `find_symbol`,
`list_module_api`, `describe_type`, `format_file`, `cpp_compile_check`, `cpp_build_info`,
`grep_usage`/`cpp_grep_usage` (при установленном ast-grep).

Не работают на движковых скриптах: `compile_check`, `list_functions`, `list_types`,
`program_log` — они симулируют программу и падают на LLVM-эмиттере
(`just_in_time` → `llvm_macro`). Для компиляции используй только
`tools/agent/das-check.sh`.

Аргументы MCP, которые всегда надо передавать:

```json
{
  "project": "/home/artem/tryengine/project.das_project",
  "load_modules": [
    "/home/artem/tryengine/modules/tryengine",
    "/home/artem/tryengine/modules/tryeditor",
    "/opt/daslang/modules/dasLLVM"
  ]
}
```

## Внешние справочники daslang (читать по необходимости)

SDK лежит в `/opt/daslang`. Полезные файлы:

- `/opt/daslang/skills/cpp_integration.md` — как биндить типы/функции/перечисления;
- `/opt/daslang/skills/dynamic_modules.md` — `.das_module`, `register_dynamic_module`;
- `/opt/daslang/skills/decs.md` — DECS-макросы;
- `/opt/daslang/skills/das_formatting.md` — правила оформления;
- `/opt/daslang/skills/external_module_debugging.md` — почему «модуль не найден»;
- `/opt/daslang/skills/daslang/SKILL.md` + `references/*.md` — язык, generics, macros.

DSH сам эти скиллы не подхватывает: они вне его корней поиска. Читай их через `read`.
