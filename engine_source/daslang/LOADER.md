# Loader

Загрузчик контента — это задача: caller собирает `LoaderRequest`, запускает
`Loader::Load(request, callback)` через `StartTask` и смотрит прогресс по фазам. Загрузчик
читает документы, разворачивает бандлы, компилирует префабы, собирает зависимости и ждёт
меши/материалы. Сцены он **не применяет** и не решает, что видит игрок: это делает callback
через синхронный `Scene::Load`.

## Слои

| Слой | Отвечает за | Не делает |
| --- | --- | --- |
| `game/scripts/Game.das` | `LoaderRequest` (что грузить), callback, `Scene::Load`/`Prefab::Spawn`, gameplay | не читает текст и не парсит контент |
| `tryengine/Loader.das` | задача `Load`, очередь документов, бандлы, прогресс, callback, освобождение | не применяет сцены, не спавнит префабы |
| `tryengine/Content.das` | путь по виду документа, stem, `Find`, разбор ConfigFormat, общие `Dependencies`, кеш документов | не грузит ресурсы и не спавнит |
| `tryengine/Assets.das` | loose-файл или чанк пака, `RequestText`/`TextIsReady`/`TakeText` и корутина `ReadText` | не знает форматов документов |
| `tryengine/Bundle.das` | формат `.bundle` и список префабов | не загружает префабы |
| `tryengine/Scene.das` | синхронное применение сцены, сохранение | не читает текст в `Load` |
| `tryengine/Prefab.das` | проверка формата, компилированный кеш, `Spawn` | не делает I/O в `Spawn` |

`GameApp::Init` только монтирует паки и `script_pak`; контент не прогревается.

## Request

```das
var request <- LoaderRequest()
request.AddBundle("data/bundles/startup.bundle")
request.AddScene("content/main.scene")
request.AddPrefab("content/props/sword.prefab")

StartTask(Loader::Load(request, @@OnLoaded))
```

- `AddBundle` — путь к `.bundle` относительно `game/`: `data/bundles/startup.bundle`.
- `AddScene` — путь к сцене относительно `game/`: `content/main.scene`.
- `AddPrefab` — путь к префабу: `content/props/sword.prefab`.
- Пути всегда начинаются с `content/` или `data/`; `game/` подразумевается. Расширение
  указывается явно, загрузчик его не дописывает.
- `request` потребляется задачей: после `Load` его не читают.
- `Loader::Load` — единственная точка старта; отдельного `Update` у загрузчика нет.

## Callback и вход в сцену

```das
def private OnLoaded() {
    if (!Scene::Load("content/main.scene")) {
        return
    }
    Prefab::Spawn("props/sword.prefab", float3(0.0f, 0.0f, 0.0f))
}
```

`on_loaded` вызывается один раз, когда все документы и ресурсы запроса готовы. Внутри
callback работает только кеш:

- `Scene::Load(path) : bool` — синхронное применение уже разобранного `Content::Document`.
  Диск не читается, ожидания нет, callback-аргумента у функции нет. Если сцену не
  предзагрузили, `Load` печатает `[SCENE ERROR] ... was not preloaded` и возвращает `false`.
- `Prefab::Spawn(path, origin)` — синхронный hit по `Prefab`-кешу. Если префаба нет,
  печатает `[PREFAB ERROR] ... was not preloaded` и возвращает `INVALID_ENTITY_ID`.

После возврата callback загрузчик сам делает `Finish`: освобождает ссылки на тексты и
ресурсы. `Content`-кеш и скомпилированные префабы при этом остаются, поэтому сцена и
префабы продолжают работать.

## Прогресс

```das
Loader::GetProgress(LoadPhase.Prefabs)   // float2(done, total), например 10 из 26
Loader::GetProgress(LoadPhase.Scenes)
Loader::GetProgress(LoadPhase.Resources)
Loader::PhaseName(Loader::Phase())       // "Bundles", "Scenes", ...
```

`GetProgress(phase) : float2` — состояние конкретной фазы: `x` готово, `y` всего. Фазы идут
в порядке бандлы → сцены → префабы → меши/материалы; пустая фаза пропускается. Во время
фазы `Prefabs` `total` растёт, когда бандлы добавляют префабы, поэтому `10/26` — нормальная
картина. `GetProgress` можно читать из `GameUpdate`/HUD, пока задача `Load` жива; после
`Finish` фаза `Idle` и прогресс нулевой.

## Ошибки

Callback при ошибке не вызывается. Загрузчик печатает `[LOADER ERROR]` и оставляет фазу
`Failed`, чтобы caller мог прочитать `Error()`:

```das
if (Loader::IsFailed()) {
    print("{Loader::Error()}\n")
}
```

`IsActive()`, `IsReady()`, `Phase()`, `Error()` — диагностика. `Abort()` освобождает
ссылки и сбрасывает запрос в `Idle`; редактор вызывает его на Stop. `Load` носит своё
поколение внутри загрузчика, поэтому задача, пережившая `Abort`, не начнёт двигать
следующий запрос.

## Бандлы

`game/data/**/*.bundle`:

```
version: u32 = 1
name: string = "startup"
prefabs: string[] = [
    "content/prefabl.prefab"
    "content/props/sword.prefab"
]
```

`AddBundle("data/bundles/startup.bundle")` находит файл, читает его текст в фазе
`Bundles`, добавляет префабы в очередь фазы `Prefabs` и тем самым увеличивает её `total`.
`prefabs` — пути `content/...` с расширением. Бандл — только группировка префабов, не
отдельный кеш ресурсов.

## Где грузятся префабы

1. `AddPrefab`/`AddBundle` ставят `.prefab` в очередь.
2. Задача `Load` читает текст, `Content::Cache` разбирает документ и собирает
   `meshGuid`/`materialRef`, `Prefab::CachePrefabDocument` проверяет формат и кладёт
   `PrefabTemplate` в кеш.
3. Ссылки попадают в фазу `Resources`, загрузчик их запрашивает и опрашивает.
4. `Prefab::Spawn` — только cache-hit. Если префаба нет в запросе и он не загружен
   `Prefab::LoadPrefabFile`, `Spawn` вернёт `INVALID_ENTITY_ID`.

## Редактор

Play использует тот же путь: `PlayMode.BeginPlay` снимает снимок DECS-heap/`SceneState`/
selection, затем `GameStart` запускает `Loader::Load`. `EditorUpdate` вызывает
`GameUpdate`, который не знает про загрузку. `Scene::Load` в callback создаёт editor-камеру
через `NotifySceneLoaded`. Stop не вызывает игру: `EndPlay` делает `Loader::Abort` и
восстанавливает снимок, а задача `Load` завершается по своему поколению.

Панель `Prefab Inspector` не прогревает все префабы: она следует за выделением в Asset
Browser, грузит выбранный `.prefab` через `Prefab::LoadPrefabFile` и показывает его дерево.

## Совместимость

Формат намеренно без legacy-веток: только `materialRef: MaterialRef`, только строковые
`requireMaterials`. `materialGuid`, `materialRef: string` и старые числовые массивы не
поддерживаются. Синхронного чтения текста из пака нет: текст приходит через
`Assets::RequestText`/`ReadText`, а `Scene::Load` применяет уже разобранный документ.
