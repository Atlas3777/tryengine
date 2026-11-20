# Loader

Загрузчик контента: читает документы, разворачивает бандлы, компилирует префабы, собирает
зависимости и ждёт меши/материалы. Сцены он **не применяет** и не решает, что видит игрок:
это делает игра через `Scene::Load`.

## Слои

| Слой | Отвечает за | Не делает |
| --- | --- | --- |
| `game/scripts/Game.das` | порядок загрузки, loading-сцена, момент входа в target-сцену | не парсит контент и не ждёт ресурсы вручную |
| `tryengine/Loader.das` | очередь документов, бандлы, префабы, меши/материалы, прогресс | не применяет сцены, не знает про loading-screen |
| `tryengine/Content.das` | путь по виду документа, stem, `Find`, разбор ConfigFormat, общие `AssetRefs`, кеш документов | не грузит ресурсы и не спавнит |
| `tryengine/Assets.das` | loose-файл или чанк пака, асинхронный текст, регистрация пути из диалога | не знает форматов документов |
| `tryengine/Bundle.das` | формат `.bundle` и список префабов | не загружает префабы |
| `tryengine/Scene.das` | применение сцены, кеш-независимый `Scene::Load`, сохранение | не читает текст в `Load` |
| `tryengine/Prefab.das` | проверка формата, компилированный кеш, `Spawn` | не делает I/O в `Spawn` |

`GameApp::Init` только монтирует паки и `script_pak`; контент не прогревается.

## Builder

```das
var request <- LoaderRequest()
request.SetBlocking(true)
request.AddBundle("data/bundles/startup.bundle")
request.AddScene("content/main.scene")
request.AddPrefab("content/props/sword.prefab")

Loader::Invoke(request, @@OnLoadingFinished)
```

- `AddBundle` — путь к `.bundle` относительно `game/`: `data/bundles/startup.bundle`.
- `AddScene` — предзагрузка сцены: текст и её меши/материалы. Перехода нет.
- `AddPrefab` — путь к `.prefab` относительно `game/`: `content/props/sword.prefab`.
- Пути в запросе всегда начинаются с `content/` или `data/`; `game/` подразумевается.
  Расширение указывается явно, загрузчик его не дописывает.
- `SetBlocking` — контракт для игры: пока `Loader::IsBlocking()`, gameplay не продолжается.
  Это не блокировка потока: `Loader::Update` всё равно вызывается каждый кадр.
- `Loader::Invoke(request, callback)` вызывает callback один раз, когда запрос дошёл до
  `Ready`. Ошибка в callback не приходит: её читают через `IsFailed()`/`Error()`.

## Состояние

```das
Loader::Phase()                          // Idle, Bundles, Scenes, Prefabs, Assets, Ready, Failed
Loader::PhaseName(Loader::Phase())       // "Bundles", "Scenes", "Prefabs", "Assets", ...
Loader::Progress()                       // общий прогресс 0..1
Loader::Progress(LoaderPhase.Prefabs)    // float2(done, total) — например, 10 из 23
Loader::IsActive()
Loader::IsReady()
Loader::IsFailed()
Loader::IsBlocking()
Loader::Error()
```

`Progress(phase) : float2` — это состояние конкретной фазы: `x` готово, `y` всего. Во время
фазы `Prefabs` `total` растёт, когда бандлы добавляют префабы, поэтому `10/23` — нормальная
картина. Фазы идут в порядке бандлы → сцены → префабы → меши/материалы; пустая фаза
пропускается. `Current/Total/Headline/Status` и события `LoaderEvent` удалены: HUD собирает
подпись из `PhaseName` и `Progress`.

## Loading-сцена и вход в target

Loading-сцена — обычный сценарий игры, а не состояние загрузчика:

```das
[export]
def GameStart() {
    StartTask(BootContent())
}

[async]
def private BootContent() : void {
    let text : string <- await(Assets::ReadText(LOADING_SCENE))
    Content::Cache(ContentKind.Scene, LOADING_SCENE, text)
    Scene::Load(LOADING_SCENE, @@OnLoadingScene)   // DECS-мир готов, callback вызван

    var request <- LoaderRequest()
    request.SetBlocking(true)
    request.AddBundle(STARTUP_BUNDLE)
    request.AddScene(STARTUP_SCENE)
    Loader::Invoke(request, @@OnLoadingFinished)
}

def private OnLoadingFinished() {
    // запрос готов; игра решает, когда входить в target-сцену
}
```

Пока loading-сцена на экране, `GameUpdate` вызывает `Loader::Update()` и читает
`Loader::Progress(LoaderPhase.Prefabs)` и т.п. Когда запрос готов, игра применяет
target-сцену синхронно:

```das
if (!Scene::Load(STARTUP_SCENE, @@StartGameScene)) {
    Loader::Fail("startup scene failed to load")
} else {
    Loader::Finish()
}
```

`Scene::Load(path, callback)` — синхронное применение уже разобранного документа;
callback вызывается после создания DECS-мира новой сцены. Это scene-specific hook: здесь
спавнятся объекты, выбирается камера, запускается музыка.

`Scene::Load` не читает диск. Текст должен быть получен заранее:
- `Scene::LoadAsync(path, callback)` — редакторский Open Scene: асинхронный текст +
  синхронное применение;
- `LoaderRequest::AddScene` — текст сцены в `Content`-кеше, применение позже;
- boot-корутина игры — `Assets::ReadText` + `Content::Cache` для loading-сцены.

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
2. `Loader::Update` читает текст, `Content::Cache` разбирает документ и собирает
   `meshGuid`/`materialRef`, `Prefab::CachePrefabDocument` проверяет формат и кладёт
   `PrefabData` в кеш.
3. Ссылки попадают в фазу `Assets`, загрузчик их запрашивает и опрашивает.
4. `Prefab::Spawn` — только cache-hit. Если префаба нет в запросе и он не загружен
   `Prefab::LoadPrefabFile`, `Spawn` вернёт `INVALID_ENTITY_ID`.

## Редактор

Play использует тот же путь: `PlayMode.BeginPlay` снимает снимок DECS-heap/`SceneState`/
selection, затем `GameStart` запускает boot-корутину. `EditorUpdate` вызывает `GameUpdate`,
`Loader::Update` читает тексты (loose в редакторе, pak в игре), а `Scene::Load` создаёт
editor-камеру через `NotifySceneLoaded`. Stop вызывает `Game::StopGame` (гасит boot-корутину
и `Loader::Abort`) и восстанавливает снимок.

Панель `Prefab Inspector` не прогревает все префабы: она следует за выделением в Asset
Browser, грузит выбранный `.prefab` через `Prefab::LoadPrefabFile` и показывает его дерево.

## Совместимость

Формат намеренно без legacy-веток: только `materialRef: MaterialRef`, только строковые
`requireMaterials`. `materialGuid`, `materialRef: string` и старые числовые массивы не
поддерживаются. Синхронного чтения текста из пака нет: текст всегда приходит через
`Assets::ReadText`, а `Scene::Load` применяет уже разобранный документ.
