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
  selection): компонент `materialRef` — обёртка `MaterialRef(path)` (`"путь"` или
  `"guid:<id>"`) -> `ResolveAssetRef` -> путь из `AssetRegistry` (`GetAssetFilePath`) ->
  файл. Follow selection реагирует не только на смену `selected_eid`, но и на изменение
  `materialRef.path` у той же сущности: иначе после выбора материала через инспектор
  панель продолжала показывать и править прошлый материал. `IsImportedAsset(id)`
  отличает артефакт импортёра от авторитетного файла: импортированный показывается
  read-only и не пишется.
- Правка применяется сразу в живой `Material`: `ApplyMaterialConfigText` парсит тот же
  ConfigFormat, который записал бы Save, и пишет значения через `SetParamRaw` в замапленный
  UBO (`ApplyMaterialConfigLive` в `MaterialLoader.hpp`). UBO и текстуры не пересоздаются,
  файл пишется по Save. Live-смена текстуры заменяет только `VkDescriptorSet` (старый
  остаётся в `retired_descriptor_sets` материала), иначе обновление набора, который уже
  используется записанным command buffer, даёт
  `VUID-vkUpdateDescriptorSets-None-03047`. После Save перезагружать материал нельзя:
  указатель в компоненте сущности станет висячим. Apply пишет в `materialRef.path`, а не
  id: путь для `.mat`, `guid:<id>` для импортированного материала.
- `CF(table)` забирает таблицу по move (`_object <- v`). В `WriteMaterial` поэтому
  `params` и `textures` сначала копируются в промежуточную таблицу (`copy := m.params`),
  и только она уходит в `CF`. Без копии `LiveApply`/`Save` опустошали глобальный
  `material.params`, и следующий кадр `EnsureParam` создавал поля заново с дефолтами —
  выглядело как сброс значений и серый материал.
- Авторитетные файлы не имеют `.meta`: при построении деревьев `AssetPipeline`
  регистрирует их в `AssetRegistry` под id = `PathId(путь)`. Отдельного
  `RegisterLooseDirectory` в `EditorApp` больше нет — регистрация следует за сканированием
  домена, а не за списком расширений.

## Инспектор: аннотации, asset picker, удаление компонентов

- Схема полей `[DecsSerializable]`-структур приходит из `apply` (`tryengine/Serializable`),
  вместе с аннотациями. Инспектор понимает `@hide`, `@title`, `@min`, `@max`, `@speed`,
  `@readonly`. Asset picker задаётся не аннотацией поля, а типом значения.
- Обёртка ассета — структура с полем `path` и аннотацией
  `[asset(kind = "data", title = "Material", ext = ".mat")]` (`tryengine/AssetKind`).
  `kind` — это домен хранения (`engine`/`game`/`content`/`data`), а не подпапка:
  `ext` фильтрует всё дерево домена, поэтому сцены и префабы ищутся по всему `content`.
  `title` — только подпись popup'а, можно не указывать (тогда берётся расширение).
  Инспектор находит обёртку по имени типа поля/компонента и рисует слева от input кнопку
  `...`, которая открывает `tryeditor/AssetPicker`. Отдельного реестра видов и привязки
  «имя компонента -> kind» больше нет; `AssetPicker` берёт домен и расширение из
  аннотации типа. `MaterialRef` лежит в `tryengine/Types` (его читает `Render.das`),
  `SceneRef`/`PrefabRef` — в `editor/daslang/FileSystemUtils.das`.
- Типизированный обход дерева — `FileSystemUtils.ForEachAsset<T>`: `static_let` внутри
  generic-функции держит кеш и ревизию по одному на тип, поэтому вызывающий код не хранит
  ни список файлов, ни версию дерева. Пример — `BuildManager.DrawBuildManager`.
  Для произвольного домена без типа остаётся `ForEachFile(domain, ext_key)`.
- `materialRef` теперь структура `MaterialRef(path)`, поэтому picker работает и для
  динамического компонента без отдельной регистрации. Для ручной проверки есть компонент
  `PickerDemo` (`editor/daslang/PickerTest.das`): `Add component` -> `PickerDemo`, поле
  `material` имеет тип `MaterialRef`.
- Удаление рисуется кнопкой в теле каждого компонента. Для зарегистрированных компонентов
  и шаблонов вызывается `EcsCompName.cmp_remover`; для динамических имён — прямой
  `decs::remove`. Удаление откладывается до конца обхода архетипа: `commit()` меняет
  архетип, и нельзя продолжать читать старый `arch_idx`.

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
  по ней: `FileSystemUtils.das` (`ForEachFile`/`ForEachAsset` -> `AssetFileRef`) помнит
  ревизию запроса и в кеше держит только индексы узлов, поэтому кеш не может пережить свои
  строки. `ForEachAsset<T>` дополнительно сверяет эпоху `RefreshAssetTrees`, потому что
  его `static_let`-кеш живёт по одному на тип и снаружи не очищается.
- Файлового вотчера ещё нет (`FileWatcher` — заглушка): новые файлы появляются в деревьях
  только после `rebuild_asset_trees()` (кнопка Refresh в Asset Browser, обёртка
  `RefreshAssetTrees()`), которая заново сканирует все четыре корня.

## Панели ImGui (daslang)

- Панели рисует `DrawEditor` (`editor/daslang/EditorUI.das`): одна функция на панель, свой
  `window(WIN_ID, (text = "...", ...))` внутри. Новую панель добавляй вызовом из `DrawEditor`,
  а не вложенным окном в чужую панель: z-порядок и парность `Begin`/`End` держатся на стеке
  окон, и вложенное окно работает только по совпадению.
- Внутри блока `window(...) { }` (и любого контейнера) **не пиши `return`**: выход из блока
  пропускает `End()` контейнера и разваливает стек окон. Выход — через `if`/`else`.
- Идентификатор виджета — просто имя на месте вызова: `text(PROFILER_SUMMARY, (text = s))`
  заводит в модуле глобальное состояние `PROFILER_SUMMARY` само (макрос из
  `imgui/widgets/imgui_boost_v2`). Объявлять его вручную не нужно, а повторное объявление
  отключает регистрацию виджета. Ручные объявления нужны только формам-таблицам
  (`var ... : table<int; NarrativeState>` для строки на индекс) и когда на состояние окна
  ссылаются из функций *выше* первого вызова виджета.
- Строку-список с меню и подсветкой рисуй одним `InvisibleButton` + `GetWindowDrawList`, а
  не стопкой `text`/`same_line`/`small_button`: у строки появляется одна поверхность под
  курсором и одно место под правый клик (`OpenPopup` + `BeginPopup` рядом с ней).
- Один call site `text`/`tree_node_ex`/любого `[widget]` — одно состояние на кадр. Вызов
  внутри цикла по строкам паникует (`rendered twice`). Для повторяющихся строк объявляй
  `table<string; NarrativeState>` / `TreeNodeState` и передавай `[key]`; `with_id` даёт
  уникальный ImGui ID, но не отменяет проверку call site (пример — дерево компонентов в
  `PrefabInspector.das`).
- `Hierarchy` при смене `selected_eid` (viewport, спавн префаба, Inspector, `SetSelectedEntity`)
  один кадр принудительно раскрывает цепочку родителей и выбранный узел, а если узел вне
  видимой области — скроллит к нему (`SetNextItemOpen` + `SetScrollHereY`). Force снимается
  в конце кадра, поэтому выбранный узел можно свернуть обратно.
- `Relationship` для `Hierarchy` не обязателен: сущность без компонента (или с мёртвым
  `parent`) рисуется корнем, а всё, что не достижимо по цепочке `first_child`/`next`,
  добавляется отдельными корнями. `g_drawn` за кадр не даёт показать узел дважды.
- Загрузка сцены (`Scene::NotifySceneLoaded`) обнуляет `selected_eid`: `decs::restart()`
  заменяет lookup-таблицу, и id из прошлой сцены может быть за её границей. Любой
  `query(selected_eid)` в панели сначала проверяет `is_alive(selected_eid)` —
  `for_eid_archetype` индексирует `entityLookup` без проверки границ, и исключение в
  середине `window(...)`/`child(...)` оставляет стек ImGui открытым (`Missing EndChild()`).
  По той же причине в теле контейнера не должно быть `return` (см. выше): состояние
  рисуется через `if`/`else`, а не выходом из функции.

## Текстура вьюпорта сцены (ImGuiPass)

- `SceneView`/`SceneGameView` рисуют `image(...)` по `VkDescriptorSet` из
  `GetEditorViewportTexture`/`GetGameViewportTexture`. Текстура вьюпорта лежит в
  `RGResourcePool` и получает новый `VkImageView` на каждую пересборку графа, то есть на
  каждый шаг ресайза панели. Поэтому набор кешируется по паре `(scope, tag)`, а не по view:
  старый набор уходит в `retired_sets_` и освобождается через `kMaxFramesInFlight` кадров.
  Кеш по view исчерпывал пул ImGui (64 набора) примерно за 64 шага ресайза, после чего
  бэкенд отдавал `VK_NULL_HANDLE` в `vkUpdateDescriptorSets` и приложение падало.
- `ImGui_ImplVulkan_InitInfo::CheckVkResultFn` выставляется в `ImGuiPass`: без него бэкенд
  молча глотает ошибки Vulkan, и первым симптомом становится SIGSEGV.

## Prefabs и drag&drop моделей

- Импорт glTF пишет два editor-артефакта: `10` — `ModelAssetMap` (локальные трансформы,
  дети, `scene_roots`), `11` — плоскую pre-order композицию в формате `.prefab`
  (`EditorArtifactGuid::Prefab`). В артефакте `11` лежат локальные TRS; `CompilePrefab`
  компонует из них мировые `_world` в шаблоне, поэтому спавн не зависит от прохода.
- Оба артефакта — `target = Editor` и лежат в `artifacts/editor/<guid>/`: в пак они не
  попадают. Runtime спавнит `.prefab`, а не glb; glb можно дропнуть только в редакторе.
  Ссылки на них — пути: `sub` (10/11) означает вид артефакта и одинаков у всех моделей,
  поэтому `AssetPipeline` регистрирует их как `PathId(путь)` (`RegisterEditorArtifactPaths`)
  из `res.artifacts`/штампа импорта, в том числе на старте без реимпорта. Без этой
  регистрации `Assets::RequestText` не находит путь и префаб-артефакт не спавнится.
- `tryengine/Prefab.das` — единственный парсер/спавнер префаба. В файле нет `eid`,
  `relationship` и других runtime-компонентов: хранятся только `localId` и `parentLocalId`
  у узлов с родителем (корень поля не пишет), а `first_child`/`prev`/`next` выводятся из
  порядка массива. Проверяются один корень,
  уникальность `localId` и то, что родитель записан раньше ребёнка. `transform` в префабе
  локальный; мировые TRS компонуются при компиляции и едут в шаблоне готовыми
  компонентами (`_world`). `relationship` — одна структура `Relationship` на сущность,
  а не четыре `Relationship_*` компонента: спавн пишет её одним `set_direct_at`.
- Формат сцены хранит только авторские компоненты и `parentLocalId` — индекс сущности в
  массиве `entities`, как в префабе; корневая сущность поля не пишет, и его отсутствие
  означает корень. `eid` не пишется (DECS всё равно выдаёт новый при
  создании), а `relationship` собирается при загрузке. Сохранение определяется типом:
  компонент пишется, если у его типа есть writer — встроенный скаляр или
  `[DecsSerializable]`-структура. Обычная структура (`WorldTransform`, `Relationship`,
  `LoadMarker`, `EditorCameraTag`) writer'а не имеет, поэтому scene save, prefab compile/save
  и Inspector её пропускают. `eid` — единственное структурное исключение.
- `SceneViewPort` принимает payload `Asset`: `.prefab` читается напрямую, glb/gltf — через
  `GetModelPrefabArtifactPath` (путь к `artifacts/editor/<guid>/11`). Точка спавна — луч
  камеры редактора до плоскости `Z=0`; при взгляде выше горизонта — точка перед камерой.
- Кеш префабов живёт в `tryengine/AssetCache.das` (типизированная таблица `PrefabTemplate`
  в `tryengine/Prefab.das`), а не в C++ `ResourceManager`: префаб нужен DECS в daslang, у
  content-файлов нет guid в игре, а `spawn` синхронный. На диске файл остаётся
  ConfigFormat-текстом; в памяти лежит `PrefabTemplate` — узлы `PrefabNode` с parent/sibling
  индексами, авторской локальной позицией и готовым `ComponentMap`, поэтому повторный спавн
  не читает файл, не парсит и не разбирает имена полей. CF-дерево после компиляции
  выбрасывается; строки, запечённые в байты компонентов, держит
  `PrefabTemplate.strings` (`ComponentValue` их не считает). После сохранения префаба
  `AssetCache::Store` кладёт новое значение сразу, а `Prefab::LoadPrefabFile`/`SpawnPrefab`
  делают `AssetCache::Refresh` и читают файл заново.
- Спавн идёт группами по набору компонентов (`PrefabTemplate.groups`, считается в
  `CompilePrefab`): на группу один `create_entities_from_cmp` вместо `create_entity` на
  сущность, компоненты копируются в архетип готовыми байтами, `relationship` пишется
  одним `set_direct_at`, позиция корня — тоже `set_direct_at`. `update_entity` в спавне не
  используется: он копирует всю строку сущности, чтобы поменять четыре ссылки. Порядок
  компонентов в архетипе позиционный, поэтому шаблон не должен опираться на порядок `set`
  в другом коде. `CompilePrefab` — публичная точка «разобранный документ →
  `PrefabTemplate`», через неё пробники гоняют реальный префаб из текста без файлов.
- Полного прогрева префабов больше нет: `Prefab::RefreshContent` и
  `Assets::ContentManifest` удалены. `Prefab::LoadPrefabFile(path)` читает и компилирует
  один `.prefab` через `tryengine/Content`; `Prefab::Spawn("props/sword.prefab", origin)`
  после этого — hit кеша и `EntityId`. Путь пишется относительно `content/` или полным;
  расширение обязательно. Игра читает только запрошенные префабы через
  `LoaderRequest::AddPrefab`/`AddBundle`; загрузчик кладёт значение в `AssetCache` и
  добавляет меши/материалы в прогресс.
- `SpawnPrefab(path)` (drag&drop) каждый раз читает текст через asset API и обновляет кеш,
  поэтому изменённый prefab не спавнится старым. `Prefab::SavePrefabFromEntity` пишет текст
  через `FileReader::WriteFile` (файловый менеджер сам создаёт папки), сразу кладёт его в
  кеш и обновляет дерево ассетов.
- `editor/daslang/PrefabInspector.das` выключен: панель обходила CF-дерево префаба, а его
  больше нет (компилированный префаб хранит готовые компоненты), и её модель
  взаимодействия всё равно требует переделки. Файл оставлен заглушкой, панели нет в меню
  Window. Спавн идёт через drag&drop из Asset Browser (`Prefab::SpawnPrefab`) или
  `Prefab::Spawn` как cache-hit.
- `PrefabAuthoring.SaveSelectionAsPrefab` (File → Save Selection as Prefab...) сериализует
  выделенную ветку в pre-order и выкидывает `eid` и всё, у чего нет writer'а
  (`relationship`, `_world`, `meshLoad`/`materialLoad`, `editorCameraTag`); позиция корня
  обнуляется (rotation/scale сохраняются), дерево ассетов обновляется.
- Билд кладёт **все** `.prefab` из `game/content` и все файлы `game/data` в
  `common_dto.pak`, вместе с мешами и материалами, которые эти префабы упоминают
  (и их шейдерами/текстурами): иначе `Prefab::Spawn` в игре получил бы текст без ресурсов.
  Тексты сцен — в пак своей сцены. Пак не хранит ни списка путей, ни стартовой сцены:
  layout — это `id -> {offset, size}`, а `id` content-файла равен `PathId(путь)`, поэтому
  игра находит файл по хешу того же авторского пути. `PakManager` монтирует paks до запуска
  скриптов, поэтому задача `Loader::Load` читает сцены, бандлы и префабы через тот же
  `tryengine/Assets`, что и редактор; игра не читает loose-файлы и не перечисляет контент.
- `game_client` не прогревает контент: `GameApp::Init` только монтирует паки и грузит
  `script_pak`, а `GameStart` собирает `LoaderRequest` и запускает
  `StartTask(Loader::Load(request, @@OnLoaded))`. Задача читает сцены, бандлы и префабы
  через `tryengine/Assets`; когда всё готово, `OnLoaded` синхронно применяет сцену из
  `AssetCache` и спавнит префабы. В редакторе Play тот же путь: loose-файл читается тем
  же API. `PlayMode.BeginPlay` до `GameStart` снимает снимок (DECS-heap, `SceneState`,
  выделение), а `EndPlay` делает `Loader::Abort` и возвращает снимок, включая
  несохранённые правки.

## Transform и editor camera

- `Transform` — один компонент `transform`: обычная структура `position/rotation/scale`
  с identity-дефолтами. Его правит Inspector и сохраняет сцена; `default<Transform>`
  больше не даёт нулевой scale. `_world` — производный компонент **другого типа**
  `WorldTransform` (та же тройка TRS, но без `[DecsSerializable]`):
  `tryengine/TransformSystem` разворачивает Relationship-иерархию каждый кадр, его читает
  `Render.das`. Writer'а у `WorldTransform` нет, поэтому `_world` не сохраняется и не
  виден в Inspector.
- **Имя аргумента в `query` — это имя компонента** (для не-шаблонных компонентов). Поэтому
  `query() $(transform : Transform)` видит `transform`, а `$(t : Transform)` — компонент `t`
  и молча не находит ни одной сущности. Для шаблонов (`camera : Camera`) имя не важно: они
  разворачиваются по префиксу. Опечатка не диагностируется — проверяй имена.
- `UpdateWorldTransforms` вызывает `EngineUpdate` (то есть `engine_source/daslang/Engine.das`)
  до того, как кадр дойдёт до `EditorUpdate`/`GameUpdate` и `Render`; это единственное место,
  где считается иерархия. Меняешь сцену из скрипта — правь `transform`, не `_world`.
- **Проход никогда не добавляет `_world`.** Добавление компонента переводит сущность в
  другой архетип и копирует всю строку; на первом проходе после создания это стоило
  ~6.6 µs/сущность — дороже всего остального прохода. Поэтому `transform` и `_world`
  применяются вместе в момент создания: `TransformSystem::ApplyEntityTransform(cmp, ...)`
  (editor-камера, `Hierarchy` → New Entity, `QuadTree`), `CompilePrefab` кладёт готовый
  `_world` в шаблон, `Scene::Load` — при разборе полей сущности. Сущность, созданная без
  `_world`, — ошибка создания: первый `WriteWorld` упадёт на `find_component_index`.
- `UpdateWorldTransforms` обходит лес **вниз**: корни — `relationship.parent == INVALID` или
  родитель без `transform` (сирота), дети — по `first_child`/`next`, поэтому родитель всегда
  скомпонован раньше потомка. После любого `decs::restart` (загрузка сцены, Play/Stop)
  вызывай `TransformSystem::ResetWorldTransformCaches`: `decs` переиспользует индексы
  архетипов, и закешированный индекс `_world` иначе попадёт в чужой компонент. `WriteWorld`
  дополнительно проверяет имя компонента и границы, но кеш надо сбрасывать на месте.
  `_world` пишется прямо в архетип одним `set_direct_at` и только если значение изменилось; `update_entity`/`commit` в проходе нет вообще. Цикл в
  `Relationship` защищён флагом посещения. Замер на 100 узлах (JIT): статический проход 0.49
  µs/сущность, первый проход после спавна 0.60 (кэш ещё пуст, поэтому значения пишутся один
  раз), сдвинутый корень (пересчёт всего поддерева) 0.59 µs/сущность — было 0.55 / 6.63 /
  0.72 на трёх компонентах.
- `CompilePrefab` компонует мировые TRS один раз (узлы pre-order, родитель раньше ребёнка) и
  хранит их и в `values` как `_world`, и в `PrefabNode.world`. Спавн копирует готовые байты
  и сдвигает `_world.position` каждого узла на `origin`, поэтому префаб стоит на месте сразу
  после спавна, без прохода. Сцена после `commit()` зовёт `UpdateWorldTransforms()` сама:
  редактор грузит сцену уже после `EngineUpdate`, иначе дети один кадр показывались бы в
  локальных позах.
- Формат префаба — `version: u32 = 5`, трансформ узла пишется как `transform: Transform = {
  position: ..., rotation: ..., scale: ... }`. Импортёр glTF (`GetVersion() == 9`) пишет тот
  же формат, поэтому старые артефакты переимпортируются. `Prefab::BuildPrefabTextFromEntity`
  обнуляет `transform.position` корня, rotation/scale сохраняет.
- Профиль прохода на 10 000 сущностях (JIT): скан двумя `query` ~0.08 µs/сущность, обход с
  композицией и сравнением ~0.27. Прямой обход `decsState.allArchetypes[...]` пробовали — на
  скане он *медленнее* (0.21 µs/сущность: ручной `memcpy` и поэлементные копии дороже
  страйд-итератора `query`), а скан — только ~20% стоимости прохода, так что смысла в нём
  нет. Реальная цена — вызов обхода на сущность и `SameTransform`. Если сцена вырастет, надо
  оптимизировать обход (без рекурсии, векторное сравнение) или вводить dirty-метки от
  писателей `transform`, а не менять скан.
- `EditorStart` подписывается на `RegisterSceneLoadedListener` и вызывает `LoadEmptyScene`:
  начальная пустая сцена — такой же load-event, как файл. Event создаёт editor-камеру на
  любой загрузке сцены, включая Play: камера помечена `editorCameraTag`, поэтому game
  camera query и `GameUpdate` её игнорируют, а `Mover` и Scene viewport продолжают
  работать. `GameStart` сам editor-камеру не создаёт; в standalone-игре слушателя нет,
  поэтому её там нет.
- Editor-камера помечена `editorCameraTag`, поэтому `Mover` двигает только её. `Scene.das`
  не пишет такие сущности в файл, а `UnloadActiveScene` (restart DECS) их убивает; в сцене
  всегда одна editor-камера.

## Transform gizmo

- Отрисовка и ввод разделены намеренно. `GizmoPass` (`editor/src/render/GizmoPass.cpp`,
  шейдер `engine_content/assets/shaders/Gizmo.slang`) строит треугольники стрелок,
  плоскостей и колец на CPU и рисует их поверх `SceneColor` без depth-теста; мышь он не
  читает. `editor/daslang/Gizmo.das` владеет режимом/пространством, экранно-постоянным
  размером, аналитическим picking'ом и драгом. Picking идёт по точным формам ручек в
  локальном пространстве гизмо: те же константы (`AXIS_HIT_RADIUS`, `RING_RADIUS`,
  `PLANE_HIT_LOW/HIGH`) продублированы в `GizmoPass.cpp`, поэтому меняешь форму — меняй
  обе стороны.
- Кольца rotate-гизмо делятся на переднюю и заднюю половины относительно камеры
  (`camera_dir` в локальных осях гизмо): задние дуги всех колец идут первыми с альфой
  `kRingBackAlpha` (у подсвеченной — 0.55), передние — последними, подсвеченное кольцо
  рисуется последним внутри прохода. Без depth-теста это единственный способ получить
  правильный порядок: иначе задняя половина одного кольца ложится поверх передней половины
  другого и гизмо читается как клубок. Тёмного outline-кольца нет — оно рисовалось до
  цвета и так же резало соседние кольца.
- Все осевые ручки «переворачиваются» каждый кадр: знак направления оси берётся из
  `dot(camera_dir, axis)` в локальных осях гизмо, поэтому стрелки и кубы масштаба растут
  в сторону камеры, а квадрат плоскости лежит в обращённой к ней четверти. Picking
  повторяет то же правило (`AxisSign` и `PlanePointInHandle`, `camera_dir =
  normalize(local.origin)`), иначе кликабельная область осталась бы с обратной стороны.
  Драг от знака не зависит: он работает с параметром полной прямой. Плоскости рисуются
  от дальнего к ближнему по расстоянию до камеры, чтобы ближний квадрат ложился поверх
  дальнего.
- Id ручек — общий контракт: `0..2` оси X/Y/Z, `3` центр (только uniform scale), `4`
  плоскость XY (нормаль Z), `5` YZ (нормаль X), `6` XZ (нормаль Y), `-1` — ничего.
  Раскладка Unity-подобная: у Translate нет центральной ручки (свободного перетаскивания по
  плоскости камеры), плоскости стоят впритык к центру (`kPlaneLow/kPlaneHigh` 0.02..0.42, внутренний угол у самого центра),
  у Scale нет плоскостей — только оси с кубами и центральный куб равномерного масштаба.
  Picking повторяет это: центр проверяется только в Scale, плоскости — только в Translate.
  `GizmoDrawState` в `Gizmo.das` помечен `[cpp_layout]` и зеркалится
  `tryeditor::GizmoDrawState` в `GizmoPass.hpp`; размер проверяется `static_assert` (48 байт).
- Один юнит меша гизмо равен одному юниту picking-пространства. По умолчанию размер
  экранно-постоянный: `2 * depth * tan(fov/2) * (90 / viewport_height)`, то есть ось
  примерно 90 px на любой дистанции (компактнее прежних 120 px). `Gizmo` → `Scale With Distance` (`V`) выключает это
  и даёт фиксированный мировой размер `GIZMO_WORLD_SIZE` (0.4 юнита); picking читает тот
  же `GizmoSizeFor`, поэтому попадание всегда совпадает с картинкой. Нижнего зажима у
  размера нет — при включённом масштабировании гизмо и его ручки уменьшаются вместе с
  глубиной.
- Состояние для отрисовки собирает `UpdateGizmoDrawState` из `EditorUpdate` (до рендера),
  а интеракция — `UpdateGizmoInteraction` из `DrawSceneViewPort` (после того, как ImGui
  знает реальную позицию мыши и hover). Из-за этого картинка отстаёт от курсора на один
  кадр; это осознанный компромисс, зато нет пересчёта координат окна и DPI.
- `PickingPass` спрашивает `IsGizmoDragging` и не выбирает объект, пока тянется ручка:
  иначе клик по стрелке выделял бы меш за ней. `RefreshWorldSubtree` из
  `TransformSystem.das` сразу пересчитывает `_world` изменённой ветки — ждать
  `UpdateWorldTransforms` нельзя, объект отрисовался бы на кадр позже ручки.
- Драг устроен как «полный дельта от старта», а не шаг за шагом: `GizmoTarget` хранит
  стартовые world position/rotation и локальный scale, `GizmoDelta` отдаёт world-смещение,
  world-поворот и множитель локального scale. Это же API рассчитано на будущий редактор
  коллайдеров: в `scale` можно передать экстенты бокса и применить `delta.scale`.
- Шейдер — такой же ассет, как `Grid.slang`: первый запуск редактора импортирует его и
  создаёт артефакты в `engine_content/artifacts/` (они генерируются и в git не коммитятся,
  см. корневой `AGENTS.md`). Без запущенного редактора папки с новым guid нет — это нормально.
- Горячие клавиши: `W`/`E`/`R` — move/rotate/scale, `X` — world/local, `V` — scale with
  distance, `Ctrl` при драге — снап (0.5 юнита, 15°, 0.05 scale). Пока зажат RMB (полёт
  камеры), W/E/R не переключают режим. Те же действия есть в меню `Gizmo`. Читаются они
  напрямую в `ProcessGizmoHotkeys`, а не через `EditorHotkeys`: фреймворк регистрации не
  должен быть единственной точкой отказа для базового переключения режимов. `ProcessHotkeys` не срабатывает, пока клавиатуру захватил ImGui-виджет
  (`IsEditorTextInputActive` из `Module_TryEditor.cpp`).

## Экраны игрового UI

- Панель **Screens** и Edit-превью — `editor/daslang/UiScreens.das`. Список берётся из
  `Ui::ScreenNames()` (реестр заполняют `[init]` модулей игры — по файлу на экран
  `game/scripts/Screen*.das` — поэтому имена видны до Play; в Play стартовый экран —
  `main_menu`, его ставит `GameFlow::Start`),
  выбор хранится в `preview_screen`, кадр объявляет `DrawPreviewFrame`. Вызывает его
  `EditorUpdate` в ветке `!is_playing`; в Play редактор зовёт `GameUiFrame()` — тот же экспорт,
  что и `game_client`. `Pause` замораживает всю игру, UI включённо: при неидущем `game_clock`
  редактор UI-кадр не объявляет, проход перерисовывает последний список команд, указатель
  ничего не задевает. `StepPlay` (`PlayMode.das`) объявляет ровно один кадр.
- Edit-превью рисуется в `SceneGameView` и объявляется внутри `Ui::SetPreviewMode(true)`: hover,
  press и анимации работают (ввод передаётся, `SetInputViewport` получает `IsItemHovered()`), а
  `Ui::Button` возвращает false, `@@`-действия и звуки подавлены, `Checkbox`/`Slider` не пишут в
  переменные. Это единственная защита состояния игры: сама функция экрана может писать в
  глобальные, поэтому экраны должны только читать.
- Клик по `SceneGameView` в Edit не конфликтует с редактором: гизмо и `PickingPass` читают
  мышь `SceneView` (редакторская камера), а UI живёт в game scope.
- Когда превью не выбрано, кадр всё равно открывается пустым (`Ui::Begin`+`Ui::End`): список
  команд очищается только при открытии кадра, иначе на экране замерзает последний кадр из Play.
- Строки списка — индексированные виджеты (`selectable_label(g_screen_rows[name], ...)`): один
  call site на список, состояние на строку. Обычный вызов виджета в цикле паникует
  (`rendered twice`).

## Текстуры и настройки импорта

- `TextureImporter` (`.png`/`.jpg`/`.jpeg`) — обычный импортёр новой архитектуры: настройки в
  `.meta` (`format`, `generate_mips`, `max_size`, `filter`, `wrap`), один runtime-артефакт `.tex`
  под main guid. Запекание — `TextureBaker::BakeTexture` (sync, без IO), поэтому
  `tools/agent/texture-probe.sh` гоняет тот же код без редактора. Меняешь содержимое
  артефакта — поднимай `TextureImporter::GetVersion()`.
- `TextureProcessor` теперь умеет и сжатые (BC через rdo_bc), и несжатые 8-битные форматы
  (`r8`, `rg8`, `rgba8`, sRGB) и принимает `generate_mips`/`max_size`. Для UI спрайтов
  ставьте `rgba8_srgb`, `generate_mips = false`, `wrap = clamp`.
- Панель **Import Settings** (`editor/daslang/AssetSettings.das`) показывает сводку `.meta`
  выбранного в Asset Browser файла: C++ `DescribeAssetMeta` читает `<path>.meta`, форматирует
  известные настройки (текстура, шрифт) и дописывает сырой JSON. Она только читает: правка
  `.meta` перезапекается при следующем старте редактора, отдельного «reimport one asset» в
  `AssetPipeline` пока нет.

## Шрифты игрового UI

- `FontImporter` (`.ttf`) — единственный импортёр шрифтов: `FontBaker` печёт SDF-атлас
  (`stbtt_GetGlyphSDF` + `stbrp`) в один runtime-артефакт `FontBinary`, без внешних
  инструментов и без растеризатора в рантайме. Реализации stb живут в `FontBaker.cpp`,
  поэтому `tools/font_probe.cpp` собирает тот же TU и проверяет запекание без GUI.
- Настройки запекания (`em_size`, `px_range`, `padding`, размер атласа, `ranges`) лежат в
  `.meta` ассета: перезапекание не требует правок кода. Меняешь содержимое артефакта —
  поднимай `FontImporter::GetVersion()`.
- Guid из `game/assets/fonts/<Font>.ttf.meta` попадает в `kDefaultUiFontGuid`
  (`engine_source/graphics/.../UiSystem.hpp`), а сборка кладёт артефакт в common-пак через
  `BuildSettings.fonts` (заполняется в `Module_TryEditor::CreateBuild` из `UiSystem`).
  Без этого шрифт в игре не найдётся, хотя в редакторе всё работает (loose-артефакт).
- Инструкция для человека — `game/assets/fonts/README.md`; проверка —
  `tools/agent/font-probe.sh game/assets/fonts/<Font>.ttf`.
