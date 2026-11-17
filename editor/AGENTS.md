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
