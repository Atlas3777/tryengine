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
  (`RegisterLooseArtifact`). Editor-артефакты (`artifacts/editor/<main>/<sub>`) регистрирует
  `RegisterEditorArtifactPaths` (`RegisterLooseArtifactPath`): их `sub` — вид артефакта
  (10/11), один на все ассеты, а не идентичность, поэтому id выводится из пути.
  `ResolvePath` — единственный способ получить id из авторского пути;
  `RegisterMaterialFile` не вычисляет id сам.
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
- Единая точка чтения ассета — `ReadAssetBytes` (`AssetBytes.hpp`): `ResourceManager`
  сам выбирает `LooseFileLocation` или `PakChunkLocation`, поэтому Content/Scene/Prefab/
  Assets.das не знают, откуда читается файл, и loose/pak-ветки не расходятся. Текст
  скриптам отдают разовые запросы `TextRequests`: `Assets::RequestText(path)` возвращает
  **handle запроса**, а не id ассета, и текст живёт до `TakeText`/`CancelText`. Кеша
  текста нет: разобранный документ — единственная его форма, которая остаётся.
  Синхронного чтения нет: `TextIsReady`/`TextIsFailed` опрашиваются (так работает
  `Loader::Load`), а `Assets::ReadText` — корутина для `Scene::LoadAsync`.
  `Scene::Load` применяет уже разобранный `Content::Document` и диск не читает.
- Готовые документы и префабы кеширует не C++ и не `Content`, а `tryengine/AssetCache.das`:
  одна ledger-таблица на все виды плюс типизированная таблица значений в модуле вида.
  Поэтому «ресурс» в `ResourceManager` — это то, что нужно кадру (Mesh/Material/Shader/
  Texture), а всё, что нужно DECS и скриптам, живёт в скриптовом кеше.
- Сцена (`engine_source/daslang/Scene.das`) хранит авторские ссылки: компонент
  `materialRef: MaterialRef`, список `requireMaterials: string[]`. `GetSceneHeader` и
  `Content::CollectDependencies` резолвят их в u64, поэтому `BuildSystem` и pak про формат ссылки
  не знают. Legacy-форматов (`materialGuid`, `materialRef: string`) нет: старые сцены
  отвергаются/не получают материал.

## Текстура

- `.png`/`.jpg` — импортируемый ассет: guid из `.meta`, один runtime-артефакт
  `runtime/<main_guid>/0` в формате `TextureBinary` (шапка 96 Б + таблица мипов + данные),
  `sub_assets` пустой, поэтому runtime-id совпадает с main guid.
- Артефакт готовит `TextureProcessor` (BC-сжатие через rdo_bc и несжатые 8-битные форматы),
  формат/мипы/размер/сэмплер берутся из `TextureImportSettings` в `.meta`.
- `AssetPak` несёт текстуры по guid. Текстуры, на которые не ссылается ни материал, ни сцена
  (UI-спрайты), попадают в common-пак через `BuildSettings::ui_textures`, который
  `CreateBuild` заполняет из `UiConfig::textures`.

## Шрифт игрового UI

- Импортируемый `.ttf` — обычный ассет: guid из `.meta`, один runtime-артефакт
  `runtime/<main_guid>/0` в формате `FontBinary` (шапка + глифы + вложенный
  `TextureBinary`), `sub_assets` пустой, поэтому runtime-id совпадает с main guid.
- `AssetPak.fonts`/`fonts_data` кладут этот артефакт в common-пак; список заполняет
  `BuildSystem` из `BuildSettings.fonts` (UiSystem config), потому что ни сцена, ни префаб
  на шрифт не ссылаются. Loader — `FontLoader` в graphics, тип `Font` в `ResourceManager`.
