# editor — заметки для агентов

Дополнение к корневому `AGENTS.md`. Здесь только неочевидные вещи этого каталога.

## Импорт glTF: матрицы и конвенции

- glTF хранит матрицы по столбцам для column-vector преобразований, а движок работает
  с row-vector матрицами. При разборе `node.matrix` столбцы glTF становятся строками
  матрицы вращения, трансляция — `matrix[12..14]`.
- Кватернион из матрицы вращения строится через `das::quat_from_float3x3`
  (`<daScript/simulate/aot_builtin_matrix.h>`); свой разбор или сторонняя math-библиотека
  не нужны.
- Кватернион glTF `[x, y, z, w]` совпадает с `das::float4`, поэтому в TRS-ветке он
  копируется как есть.
- UV пакуются в half через `das::das_float_to_float16` (`<daScript/misc/vectypes.h>`).
  Это округление RTNE; для UV разница с усечением несущественна, но не меняй округление
  без причины.

## Slang: компилятор и модули

- Компилятор — `ShaderCompilerSystem` (система `Engine`, только редактор). `IGlobalSession`
  в Slang не thread-safe, поэтому она создаётся по одной на контекст и защищена мьютексом;
  импортёр не должен создавать `IGlobalSession` на каждый `.slang`.
- `.slang` без точек входа `vsMain`/`fsMain` (и фолбэков) считается модулем: импорт даёт
  артефакт `.slang-module`. Модуль лежит рядом с шейдером или в одном из search paths,
  подключается через `import ModuleName;`. Объявленное в модуле API должно быть `public`.
- Шейдер компилируется из исходников модулей через `SessionDesc::searchPaths`, поэтому
  порядок импорта ассетов не важен. Реально прочитанные модули и include-файлы попадают в
  `ImportResult::dependencies`; `AssetPipeline` пишет их в штамп, и правка модуля
  переимпортирует зависимые шейдеры.


## Материалы: ConfigFormat и редактор

- Единственный формат материала — ConfigFormat: `name: string`, `shader: u64`,
  `params: Params = { ... }` (uniform-параметры UBO) и `textures: Textures = { ... }`
  (сэмплеры, значение — u64 id текстуры). Оба блока динамические: имена и типы задаёт
  рефлексия Slang, C++ конкретный набор полей не знает.
- JSON-варианта материала нет: `MaterialLoader` разбирает только `MaterialConfig`, а
  glTF-импортёр пишет суб-ассеты тем же `WriteMaterialConfig`. Меняется содержимое
  артефактов — поднимай `GltfImporter::GetVersion()`, иначе старые JSON-артефакты
  останутся на диске.
- Материал в `data/` — файл без `.meta`/GUID. Его id — `core::PathId(путь)`: одна
  функция нормализации и FNV для редактора и рантайма. Все файлы авторитетных доменов
  регистрирует `AssetPipeline::BuildTreeFromDisk` (`AssetRegistry::RegisterAuthoritative`),
  поэтому `ResolvePath` находит материал без ручной регистрации. `RegisterMaterialFile` /
  `ReloadMaterialFile` резолвят путь (fallback — регистрация) и грузят `.mat`;
  `Invalidate` откладывает старый ресурс в retired: указатели, уже отданные в кадр, не
  повисают.
- `MaterialConfig` зарегистрирован и как ресурс (`MaterialConfigLoader`): `BuildSystem`
  берёт из него GUID'ы шейдера и текстур для пака, не разбирая файл повторно.
- Схему материала редактор получает строкой: `GetMaterialSchema(shader)` возвращает
  `char*` из кучи контекста daslang (`ctx->allocateString`), для скрипта это обычный
  `string`. Вернуть C++-объект `das::string` нельзя: его тип для скрипта — handled
  `das_string`, а `cast_res<das::string>` в ABI биндинга нет. Пока шейдер не загружен,
  функция отдаёт `null`.
- Панель — `editor/daslang/Material.das`. При старте сама загружает
  `game/data/materials/standard.mat`, дальше следует за выделением (галка Follow
  selection): компонент `materialRef` (`"путь"` или `"guid:<id>"`) -> `ResolveAssetRef` ->
  путь из `AssetRegistry` (`GetAssetFilePath`) -> файл. `IsImportedAsset(id)` отличает
  артефакт импортёра от авторитетного файла: импортированный показывается read-only и не
  пишется.
- Правка применяется сразу в живой `Material`: `ApplyMaterialConfigText` парсит тот же
  ConfigFormat, который записал бы Save, и пишет значения через `SetParamRaw` в замапленный
  UBO (`ApplyMaterialConfigLive` в `MaterialLoader.hpp`). GPU-ресурсы не пересоздаются, файл
  пишется по Save. После Save перезагружать материал нельзя: указатель в компоненте
  сущности станет висячим (для смены текстур live-путь перепривязывает уже загруженные).
  Apply пишет в компонент `materialRef`, а не id: путь для `.mat`, `guid:<id>` для
  импортированного материала.
- Авторитетные файлы не имеют `.meta`: при построении деревьев `AssetPipeline`
  регистрирует их в `AssetRegistry` под id = `PathId(путь)`. Отдельного
  `RegisterLooseDirectory` в `EditorApp` больше нет — регистрация следует за сканированием
  домена, а не за списком расширений.

## Деревья ресурсов (Asset Browser)

- Корень дерева — домен `tryeditor::StorageDomain`; каталоги собраны в одной таблице
  `kStorageRoots` (`editor/include/editor/AssetPipeline.hpp`). Ассеты: `game/assets` и
  `engine_content/assets` — `.meta` с guid, артефакты импорта. Авторитетные файлы:
  `game/content` (сцены, префабы) и `game/data` (материалы и прочие атомы) — у них нет ни
  `.meta`, ни guid, ни артефактов, дерево строится одним `AssetScanner::ScanDirectory` без
  конвейера импорта.
- Узел-файл хранит `UiFile::ext` — ключ расширения на 8 байт (`editor/include/editor/AssetExt.hpp`).
  Расширение строкой не хранится: поиск "все `.mat` в `data`" — одно сравнение `uint64` на
  файл. Ключ считает `ExtKey`, он же отдаётся скрипту как `asset_ext_key`.
- `UiTree::Version()` — ревизия дерева, растёт на любом изменении. Скрипты кешируют обход
  по ней: `FileSystemUtils.das` (`FindFiles` -> `array<AssetFileRef>`) помнит ревизию
  запроса и в кеше держит только индексы узлов, поэтому кеш не может пережить свои строки.
- Файлового вотчера ещё нет (`FileWatcher` — заглушка): новые файлы появляются в деревьях
  только после `rebuild_asset_trees()` (кнопка Refresh в Asset Browser, обёртка
  `RefreshAssetTrees()`), которая заново сканирует все четыре корня.
