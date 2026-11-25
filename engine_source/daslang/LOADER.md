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
| `tryengine/Content.das` | путь по виду документа, stem, `Find`, разбор ConfigFormat, общие `Dependencies` | не кеширует и не грузит ресурсы |
| `tryengine/AssetCache.das` | ledger состояний, разовые чтения текста, компиляция значения, `AssetKindOps` для видов | не знает форматов документов |
| `tryengine/Assets.das` | loose-файл или чанк пака, разовый `RequestText`/`TextIsReady`/`TakeText` и корутина `ReadText` | не знает форматов документов, не кеширует текст |
| `tryengine/Bundle.das` | формат `.bundle` и список префабов | не загружает префабы |
| `tryengine/Scene.das` | синхронное применение сцены, сохранение | не читает текст в `Load` |
| `tryengine/Prefab.das` | проверка формата, компиляция, `Spawn` | не делает I/O в `Spawn` |

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
- `AddAsset(kind, path)` — любой вид, зарегистрированный в `AssetCache`
  (`request.AddAsset("material", "data/materials/standard.mat")`). Для нового вида не
  нужны ни поле в запросе, ни правка загрузчика: он публикует `AssetKindOps` из `[init]`,
  а `kind` в запросе — та же строка.
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

`game/data/bundles/*.bundle` — **манифест загрузки**, не рантайм-объект. Он описывает, что
прочитать и как назвать результат; экран видит только плоскую карту алиасов.

```
version: u32 = 3
name: string = "hud"
parent: string = "data/bundles/ui.bundle"   // один родитель, "" у корня
prefabs: string[] = [
    "content/props/sword.prefab"
]
assets: BundleAsset[] = [
    {
        name: string = "logo"
        kind: string = "texture"
        ref: string = "guid:14429000468236573201"
    }
]
```

`AddBundle("data/bundles/hud.bundle")` находит файл, читает его текст в фазе `Bundles` и
ставит родителя (`parent`) в ту же фазу; затем `Bundle::Validate` проверяет, что вся цепочка
загружена и без циклов. Префабы бандла идут в фазу `Prefabs`.

`assets` — авторские имена экрана. `kind` — `texture`, `font`, `material` или `mesh`;
`ref` — `guid:<id>` (импортированный ассет) или авторский путь (`.mat`). `Content::Parse`
собирает ids в `Content::Dependencies.textures`/`fonts`; загрузчик запрашивает их в фазе
`Resources` вместе с мешами и материалами.

После `Ready` `Bundle::Flatten` идёт по цепочке родителей, кладёт корень первым и
перекрывает его ребёнком; получается таблица `ключ -> (kind, id)`, которая регистрируется
в `tryengine/AssetMap` и живёт там как дескриптор. Callback запроса получает
`maps : table<string; AssetMap>`; экран берёт свою карту через
`Loader::MapOf(maps, kHudBundle)` и дальше зовёт `assets.Texture("logo")`. Ни `Bundle::Get`,
ни пути в разметке нет. CF-документ после разбора выбрасывается; в `AssetCache` остаётся
готовый манифест, в `AssetMap` — плоские алиасы.

Пути бандлов экранов — константы в файлах экранов: `kMenuBundle` (`main_menu.bundle`),
`kHudBundle`, `kPauseBundle`, `kShopBundle`. `main_menu`/`hud`/`pause`/`shop` объявляют
`parent = "data/bundles/ui.bundle"`; `ui.bundle` — корень со шрифтом. Базовый материал UI в
бандле не нужен: `UiConfig.material_path` фиксирован, boot-запрос добавляет его как ресурс
(`AddResource("material", AssetPathId(UiMaterialPath()))`), а `UiSystem` только ждёт
готовности.

## Где грузятся префабы

1. `AddPrefab`/`AddBundle` ставят `.prefab` в очередь. `AssetCache::Request` проверяет
   ledger: если запись уже есть (Loading/Ready/Failed), текст не читается и не парсится —
   задача только прогоняет шаг вида.
2. `AssetCache::Advance` забирает текст (`Assets::TakeText`), `Content::Parse` разбирает
   документ и собирает `model` (гид меша и явные оверрайды материала), compile-функция вида
   (`Prefab::CompilePrefab`) проверяет формат и разрешает поля узлов в готовые компоненты
   DECS: `PrefabTemplate` хранит `ComponentValue` на узел, а CF-дерево выбрасывается. Текст
   освобождается вместе с запросом: в памяти остаётся только готовое значение.
3. Ссылки (`AssetCache::Dependencies`) попадают в фазу `Resources`, загрузчик их
   запрашивает и опрашивает. Материал по умолчанию лежит в артефакте меша, поэтому он
   известен только после готовности меша: фаза добирает такие материалы в очередь и не
   завершается, пока они не готовы. К моменту callback готово всё, на что сущность будет
   ссылаться, и `Prefab::Spawn`/`Scene::Load` ставят указатели сразу.
4. `Prefab::Spawn` — только cache-hit. Если префаба нет в запросе и он не загружен
   `Prefab::LoadPrefabFile`, `Spawn` вернёт `INVALID_ENTITY_ID`.

Ссылки на готовые ресурсы (после загрузчика) берутся через `Hold*`, а не `RequestLoad*`:
сцена, префаб и UI только увеличивают refcount. Если ассет не был предзагружен, это
ошибка `[SCENE ERROR]`/`[PREFAB ERROR]`/`[UI ERROR]`, а не повод прочитать диск из кадра.
`RequestLoad*` в рантайме делает только `Loader` (и редакторские пути
`Scene::LoadAsync`/`Prefab::SpawnPrefab`, которым разрешено грузить по действию
пользователя).

Повторный `Loader::Load` (Play, рестарт, повторный вход в сцену) для неизменённого
контента не делает ни одного чтения: `AssetCache::Request` видит готовые записи, меши и
материалы уже готовы в `ResourceManager`, и запрос завершается на первом же шаге задачи.
Свежесть контента обеспечивают редакторские Save-пути: они заново кладут значение в
`AssetCache` (`Refresh` или `Store`).

## Редактор

Play использует тот же путь: `PlayMode.BeginPlay` снимает снимок DECS-heap/`SceneState`/
selection, затем `GameStart` запускает `Loader::Load`. `EditorUpdate` вызывает
`GameUpdate`, который не знает про загрузку.

В Edit режиме редактор **не** грузит игровые бандлы и не рисует экраны: панели **Screens**
больше нет, вьюпорт объявляет пустой UI-кадр. Ленивого запроса из `UiSystem` нет: UI читает
только то, что уже принёс загрузчик. `Scene::Load` в callback создаёт editor-камеру
через `NotifySceneLoaded`. Stop не вызывает игру: `EndPlay` делает `Loader::Abort` и
восстанавливает снимок, а задача `Load` завершается по своему поколению.

Никто не прогревает все префабы: `Prefab::LoadPrefabFile` грузит один выбранный `.prefab`
по требованию. Панель `Prefab Inspector` выключена (её дерево строилось на CF-документе,
которого в компилированном префабе больше нет) и будет переделана под готовые компоненты.

## Совместимость

Формат намеренно без legacy-веток: только `model: ModelRender` с полем `material`
(строка — путь к `.mat`), только строковые `requireMaterials`. `meshGuid`,
`materialRef` как отдельные компоненты, `guid:<id>` в поле материала и старые числовые
массивы не поддерживаются. Синхронного чтения текста из пака нет: текст приходит через
`Assets::RequestText`/`ReadText` (handle запроса, не id ассета), а `Scene::Load`
применяет уже разобранный документ.
