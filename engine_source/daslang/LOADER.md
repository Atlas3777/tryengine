# Loader

Загрузчик контента: читает документы, разворачивает бандлы, компилирует префабы, собирает
зависимости и ждёт меши/материалы. Сцены он **не применяет** и не решает, что видит игрок:
это делает игра через `Scene::Load`.

## Слои

| Слой | Отвечает за | Не делает |
| --- | --- | --- |
| `game/scripts/Boot.das` | стартовая политика, loading-сцена, запрос к Loader, момент входа в target | не парсит контент и не ждёт ресурсы вручную |
| `game/scripts/Game.das` | gameplay и игровые часы | не управляет загрузкой |
| `tryengine/Loader.das` | очередь документов, бандлы, префабы, меши/материалы, прогресс | не применяет сцены, не знает про loading-screen |
| `tryengine/Content.das` | путь по виду документа, stem, `Find`, разбор ConfigFormat, общие `AssetRefs`, кеш документов | не грузит ресурсы и не спавнит |
| `tryengine/Assets.das` | loose-файл или чанк пака, `RequestText`/`TextIsReady`/`TakeText` и корутина `ReadText`, регистрация пути из диалога | не знает форматов документов |
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

`game/scripts/Boot.das` — единственное место, где живёт стартовая политика: пути
`STARTUP_SCENE`, `LOADING_SCENE`, `STARTUP_BUNDLE`, время `MIN_LOADING_SECONDS` и
`WAIT_FOR_SPACE`. `Game.das` их не знает.

```das
// GameStart
RegisterGameTime()
Boot::Start(@@OnGameSceneLoaded)

// Boot::Start
g_loading_text_id = Assets::RequestText(LOADING_SCENE)

// Boot::Update, фаза ReadingLoadingScene
if (Assets::TextIsReady(g_loading_text_id)) {
    let text <- Assets::TakeText(g_loading_text_id)
    Content::Cache(ContentKind.Scene, LOADING_SCENE, text)
    Scene::Load(LOADING_SCENE)                     // DECS-мир loading-сцены готов

    var request <- LoaderRequest()
    request.SetBlocking(true)
    request.AddBundle(STARTUP_BUNDLE)
    request.AddScene(STARTUP_SCENE)
    Loader::Invoke(request, @@OnContentReady)
}

def private OnContentReady() {
    // запрос готов; вход в target-сцену — решение игры
}
```

`Boot::Update` вызывается из `GameUpdate` и возвращает `true`, когда кадр принадлежит
gameplay. Пока идёт чтение, `Loader::Update()` не вызывается; пока запрос активен, именно
`Boot::Update` гоняет `Loader::Update` и читает его состояние. Когда запрос готов, boot
держит loading-сцену `MIN_LOADING_SECONDS` (и Space, если `WAIT_FOR_SPACE`), затем
синхронно применяет target:

```das
if (!Scene::Load(STARTUP_SCENE, on_playing)) {
    FailBoot("startup scene failed to apply")
} else {
    Loader::Finish()
}
```

`Scene::Load(path, callback)` — синхронное применение уже разобранного документа;
callback вызывается после создания DECS-мира новой сцены. Это scene-specific hook: здесь
спавнятся объекты, выбирается камера, запускается музыка. Loading-сцена и target-сцена сами
содержат свои камеры; игровой код их только двигает.

`Scene::Load` не читает диск. Текст должен быть получен заранее:
- `Scene::LoadAsync(path, callback)` — редакторский Open Scene: асинхронный текст +
  синхронное применение;
- `LoaderRequest::AddScene` — текст сцены в `Content`-кеше, применение позже;
- boot игры — `Assets::RequestText` + `TextIsReady`/`TakeText`, затем `Content::Cache` для loading-сцены.

У boot-потока нет ни корутины, ни стоп-флага, ни счётчика поколений: `Boot::Update` — это
опрос. Остановка сессии — задача редактора: `PlayMode.EndPlay` делает `Loader::Abort()` и
перестаёт вызывать `GameUpdate`, поэтому boot больше не трогает сцену. Текст, который
остался запрошенным в момент Stop, освобождает следующий `Boot::Start`.

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
selection, затем `GameStart` запускает `Boot`. `EditorUpdate` вызывает `GameUpdate`,
`Boot::Update` гоняет `Loader::Update` (loose в редакторе, pak в игре), а `Scene::Load`
создаёт editor-камеру через `NotifySceneLoaded`. Stop не вызывает игру: `EndPlay` делает
`Loader::Abort` и восстанавливает снимок, а boot просто перестаёт опрашиваться.

Панель `Prefab Inspector` не прогревает все префабы: она следует за выделением в Asset
Browser, грузит выбранный `.prefab` через `Prefab::LoadPrefabFile` и показывает его дерево.

## Совместимость

Формат намеренно без legacy-веток: только `materialRef: MaterialRef`, только строковые
`requireMaterials`. `materialGuid`, `materialRef: string` и старые числовые массивы не
поддерживаются. Синхронного чтения текста из пака нет: текст всегда приходит через
`Assets::ReadText`, а `Scene::Load` применяет уже разобранный документ.
