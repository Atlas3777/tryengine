# resources — заметки для агентов

Дополнение к корневому `AGENTS.md`. Здесь только контракт идентичности ресурсов.

## Идентичность ассетов

- `uint64_t` — единственная идентичность в рантайме и в паке. Ресурсы двух видов:
  - импортируемый (`game/assets`, `engine_content/assets`): `.meta` с guid, суб-ассеты
    `CombineID(main_guid, имя)`, артефакт в `artifacts/<main>/<sub>`; id — guid из меты;
  - авторитетный (`game/content`, `game/data`): файл без `.meta`, артефакта и guid;
    id — `core::PathId(путь)`.
- `core::PathId` — одна функция и для редактора, и для рантайма: `\` == `/`, подряд
  идущие `/` схлопываются, ведущий `./` и хвостовой `/` отбрасываются, регистр значим.
  Значения совпадают с прежним `PathGuid`, поэтому уже сохранённые в сценах ссылки на
  `.mat` продолжают резолвиться. Отдельных `PathHash`/`PathGuid` больше нет; не заводи
  третий путь-хеш.
- Готовность ресурсов для загрузки: `ResourceIsReady*` и `ResourceIsFailed*` из
  `tryResources`. Загрузчик запрашивает id один раз, опрашивает пару ready/failed и сам
  решает, когда переключать сцену.
- `AssetRegistry` хранит `id -> AssetLocation` и `PathId(путь) -> id`. Авторитетные файлы
  регистрирует `AssetPipeline::BuildTreeFromDisk` при сканировании домена
  (`RegisterAuthoritative`), артефакты импорта — `RegisterArtifactPaths`
  (`RegisterLooseArtifact`). `ResolvePath` — единственный способ получить id из авторского
  пути; `RegisterMaterialFile` не вычисляет id сам.
- Авторская ссылка (`AssetRef.hpp`) — одна строка: голый путь для авторитетного файла или
  `guid:<u64>` для импортируемого. В компонентах/сценах лежит именно она, а не id.
  `ResolveRef` переводит её в id, `AssetRefOf` — обратно; `IsImported(id)` отличает
  производный ресурс от авторитетного. Вид ссылки — данные, а не тип поля: отдельный
  компонент под guid не нужен.
- В паке нет каталогов: `AssetPak` и `BuildSystem` работают с u64. Для content-файлов
  (`game/content`, `game/data`) id — это `PathId(путь)`, поэтому ни путь, ни таблица
  path -> id в пак не пишутся: layout уже даёт `id -> {offset, size}`, а скрипт просит файл
  по тому же авторскому пути. Манифеста content-путей и стартовой сцены в паке нет; игре не
  нужен листинг, она читает только то, что назвала. `AuthoritativePaths()` — список
  зарегистрированных loose-файлов, нужный только сборщику редактора.
- Ссылки в сцене не нужно переписывать в `guid:`: `AssetRegistry::ResolvePath` сначала
  смотрит таблицу путей, а затем пробует `PathId(путь)` в `locations_`. В игре это находит
  content-чанк, в редакторе — зарегистрированный авторский файл.
- Единая точка чтения текста для скриптов — `SourceText` + биндинги `tryResources`
  (`ResolveAssetPath`, `RequestLoadSourceText`, `SourceTextIsReady/IsFailed`,
  `GetSourceText`). `ResourceManager` сам выбирает `LooseFileLocation` или
  `PakChunkLocation`, поэтому Content/Scene/Prefab/Assets.das не знают, откуда читается
  файл. Синхронного чтения текста нет: `Assets::RequestText`/`TextIsReady`/`TakeText`
  отдают его опросом (так работает задача `Loader::Load`), а `Assets::ReadText` —
  корутина для `Scene::LoadAsync`. `Scene::Load` применяет уже разобранный
  `Content::Document` и диск не читает.
- Сцена (`engine_source/daslang/Scene.das`) хранит авторские ссылки: компонент
  `materialRef: MaterialRef`, список `requireMaterials: string[]`. `GetSceneHeader` и
  `Content::CollectDependencies` резолвят их в u64, поэтому `BuildSystem` и pak про формат ссылки
  не знают. Legacy-форматов (`materialGuid`, `materialRef: string`) нет: старые сцены
  отвергаются/не получают материал.
