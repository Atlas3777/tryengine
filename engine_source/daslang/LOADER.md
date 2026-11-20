# Loader

Явная загрузка: игра строит `LoaderRequest`, вызывает `Loader::Invoke`, а затем сама
переходит на сцену через `Scene::Load(path, callback)`. Движок не выбирает сцены и не
запускает загрузку сам.

## Слои

| Слой | Отвечает за | Не делает |
| --- | --- | --- |
| `game/scripts/Game.das` | какой loading-screen, какие бандлы/сцены/префабы, blocking, когда входить | не читает диск и не парсит контент |
| `tryengine/Loader.das` | тексты, бандлы, зависимости, прогресс и счётчики | не переключает target-сцену |
| `tryengine/Bundle.das` | формат `.bundle`, список префабов | не загружает ресурсы |
| `tryengine/Scene.das` | кеш текста, применение сцены, callback | не ждёт асинхронную загрузку |
| `tryengine/Prefab.das` | парсинг, кеш, `Spawn` из кеша | не делает I/O в `Spawn` |

`GameApp::Init` только монтирует паки и `script_pak`; контент не прогревается.

## Builder

```das
var request <- LoaderRequest()
request.SetLoadingScene("game/content/loading.scene") // "" — не менять сцену
request.SetBlocking(true)
request.AddBundle("game/data/bundles/startup.bundle")
request.AddScene("game/content/main.scene")
request.AddPrefab("game/content/props/sword.prefab")
request.AddStep("server-handshake") // внешний этап; CompleteStep("...") завершает

Loader::Invoke(request)
```

- `AddBundle` — полный путь `.bundle` из `game/data` (расширение можно опустить).
  Короткие имена и пути относительно `game/data` не резолвятся.
- `AddScene` — предзагрузка сцены: текст и её меши/материалы. Перехода нет.
- `AddPrefab` — полный путь `.prefab` из `game/content` (расширение можно опустить).
  Короткие имена и пути относительно `game/content` не резолвятся.
- `SetLoadingScene` — единственная сцена, которую применяет сам загрузчик, когда её текст
  готов. Если не задана, загрузчик ничего не переключает.
- `SetBlocking` — контракт для игры: пока `Loader::IsBlocking()`, gameplay не продолжается.
  Это не блокировка потока: `Loader::Update` всё равно вызывается каждый кадр. Для фоновой
  предзагрузки ставь `blocking = false` и не задавай `loading_scene`, иначе загрузчик
  переключит видимую сцену.

## Состояние

```das
Loader::Phase()       // Idle, LoadingScene, Bundles, Scenes, Prefabs, Assets, Steps, Ready, Failed
Loader::Headline()    // "Loading scene", "Bundles", "Scenes", "Prefabs", "Assets", "Steps", "Ready"
Loader::Current()     // сколько готово в текущей фазе
Loader::Total()       // сколько всего в текущей фазе
Loader::Status()      // "Prefabs 9/29"
Loader::Progress()    // 0..1
Loader::IsReady()
Loader::IsFailed()
Loader::Error()
```

Фазы идут в этом порядке: loading-сцена → бандлы → сцены → префабы → меши/материалы →
внешние steps. `Current/Total` относится к текущей фазе; `Progress` — общий. Внешний шаг
добавляется `LoaderRequest::AddStep("...")` и завершается `Loader::CompleteStep("...")`;
пока он не завершён, `Ready` не наступает.

## Переход на сцену

```das
if (Loader::IsReady()) {
    if (Scene::Load("game/content/main.scene", @@StartMainMenuScene)) {
        Loader::Finish()
    } else {
        Loader::Fail("main scene failed to load")
    }
}
```

`Scene::Load(path, callback)` — синхронное применение заранее прочитанного текста. Callback
вызывается после того, как DECS-мир новой сцены уже создан. Это scene-specific hook: здесь
спавнятся объекты, выбирается камера, запускается музыка. Отдельного события загрузки для
этого не нужно.

Текст сцены, прочитанный `Loader`, лежит в кеше `Scene` (`Scene::CacheText`), поэтому
`Scene::Load` может применяться позже, в любом кадре. `Loader::Finish` не чистит этот кеш;
`Loader::Abort` тоже. Очистить — `Scene::ClearCachedTexts`.

## События

`Loader::RegisterLoaderListener` и `LoaderEvent` (`LoadingSceneStarted`, `BundlesLoaded`,
`ScenesLoaded`, `PrefabsLoaded`, `AssetsReady`, `Ready`, `Failed`) — необязательные
уведомления для HUD/анимации. Scene-specific код должен жить в callback `Scene::Load`.

## Бандлы

`game/data/**/*.bundle`:

```
version: u32 = 1
name: string = "startup"
prefabs: string[] = [
    "game/content/prefabl.prefab"
    "game/content/props/sword.prefab"
]
```

`Loader::AddBundle("game/data/bundles/startup.bundle")` находит файл по пути, читает его
текст в фазе `Bundles`, добавляет префабы в общий список и тем самым увеличивает `Total`
фазы `Prefabs`. `prefabs` — полные пути `.prefab`; короткие имена не резолвятся. Бандл —
это только группировка префабов, не отдельный кеш ресурсов.

## Где грузятся префабы

1. `AddPrefab`/`AddBundle` добавляют текст префаба в фазу `Prefabs`.
2. `Loader::Update` читает текст, вызывает `Prefab::CachePrefabText`, собирает
   `meshGuid`/`materialRef` через `Prefab::CollectRefs`.
3. Ссылки попадают в фазу `Assets`, загрузчик их запрашивает и опрашивает.
4. `Prefab::Spawn` — только cache-hit. Если префаба нет в запросе, `Spawn` вернёт
   `INVALID_ENTITY_ID`.

## Где сцена тянет зависимости

Сцена не грузит себя сама. В фазе `Scenes` `Loader` читает текст, складывает его в
`Scene::CacheText`, вызывает `Scene::CollectRefs` и добавляет меши/материалы в общий
`Assets`. Потом игра вызывает `Scene::Load`, который применяет уже готовые данные.

`Scene::Load` без `Loader` возможен только для loose-файла (редактор) или уже
закешированного текста; в игре без загрузчика текст пака не появится.

## Редактор

Play использует тот же путь: `PlayMode.BeginPlay` снимает снимок DECS-heap/`SceneState`/
selection, затем `GameStart` строит `LoaderRequest`. `EditorUpdate` вызывает `GameUpdate`,
`Loader::Update` читает тексты (loose в редакторе, pak в игре), а `Scene::Load` снова
создаёт editor-камеру через `NotifySceneLoaded`. `SceneView` смотрит editor-камерой,
`SceneGameView` — game-камерой из loading/target-сцены. Stop вызывает `Loader::Abort` и
восстанавливает снимок.

## Совместимость

Формат намеренно без legacy-веток: только `materialRef: MaterialRef`, только строковые
`requireMaterials`. `materialGuid`, `materialRef: string` и старые числовые массивы не
поддерживаются.
