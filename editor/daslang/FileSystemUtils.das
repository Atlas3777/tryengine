options gen2
options relaxed_assign = false

module FileSystemUtils shared public

require TryEditor
require strings
require daslib/decs
require daslib/static_let
require tryengine/AssetKind
require tryengine/Serializable
require tryengine/ConfigFormatBoost

// Editor-side asset wrappers. MaterialRef lives in tryengine/Types because the renderer
// reads it; scene/prefab wrappers are only picker metadata here.
[asset(kind = "content", title = "Scene", ext = ".scene"), DecsSerializable]
struct SceneRef {
    path : string
}

[asset(kind = "content", title = "Prefab", ext = ".prefab"), DecsSerializable]
struct PrefabRef {
    path : string
}

// Без фильтра по типу: все файлы корня.
let EXT_ANY : uint64 = 0ul

// Файл в дереве: индексы папки и файла в get_folders(domain). Ссылка, а не значение: имя и
// путь всегда читаются из живого дерева, поэтому такая запись не может пережить свой узел.
struct AssetFileRef {
    folder : int
    file : int
}

// Результат обхода дерева и ревизия, при которой он снят.
struct FilesQuery {
    revision : uint64
    refs : array<AssetFileRef>
}

// Кеш: по таблице на домен (индекс — int(StorageDomain)), ключ — ключ расширения.
// Ни строк в ключе, ни аллокаций на попадание в кеш.
var private g_query_cache : array<table<uint64; FilesQuery>>

[init]
def private InitQueryCache() {
    resize(g_query_cache, 4)  // engine, game, content, data
}

// Ревизия этого счётчика меняется при RefreshAssetTrees. Типизированные кеши ниже живут
// в static_let-переменных по одному на тип и не могут быть очищены снаружи, поэтому они
// сверяют ещё и эпоху, а не только ревизию конкретного дерева.
var private g_asset_cache_epoch : uint64 = 0ul

// kind — это домен хранения (engine/game/content/data), а не подпапка. Фильтр по
// расширению применяется ко всем папкам домена, поэтому сцены и префабы ищутся по
// всему content, а не только в content/scenes.
def public AssetDomainOfKind(kind : string) : StorageDomain {
    if (kind == "engine") {
        return StorageDomain.Engine
    } elif (kind == "game") {
        return StorageDomain.Game
    } elif (kind == "content") {
        return StorageDomain.Content
    }
    return StorageDomain.Data
}

// Заголовок popup'а: явный title из аннотации, иначе имя расширения без точки.
def public AssetTitleOf(info : AssetTypeInfo) : string {
    if (!empty(info.title)) {
        return info.title
    }
    let dot = find(info.ext, ".")
    return dot >= 0 ? slice(info.ext, dot + 1, length(info.ext)) : info.ext
}

// Один обход дерева для обоих кешей: и доменного, и типизированного.
def private FillQuery(domain : StorageDomain; ext_key : uint64; var refs : array<AssetFileRef>) {
    clear(refs)
    assume folders = get_folders(domain)
    for (f in range(length(folders))) {
        for (i in range(length(folders[f].files))) {
            if (ext_key != EXT_ANY && folders[f].files[i].ext != ext_key) {
                continue
            }
            refs |> push(AssetFileRef(folder = f, file = i))
        }
    }
}

// Наполняет кеш, если для этой ревизии дерева обхода ещё не было.
def private EnsureQuery(domain : StorageDomain; ext_key : uint64) {
    let d = int(domain)
    let revision = get_tree_revision(domain)

    if (key_exists(g_query_cache[d], ext_key) && g_query_cache[d][ext_key].revision == revision) {
        return
    }

    var found : array<AssetFileRef>
    FillQuery(domain, ext_key, found)
    g_query_cache[d][ext_key] <- FilesQuery(revision = revision, refs <- found)
}

// Обход файлов домена по кешу: блок получает ссылку, массив не копируется, аллокаций нет.
// Обход дерева повторяется только при смене ревизии, дальше это чтение готового массива.
def public ForEachFile(domain : StorageDomain; ext_key : uint64; blk : block<(ref : AssetFileRef) : void>) {
    EnsureQuery(domain, ext_key)
    for (r in g_query_cache[int(domain)][ext_key].refs) {
        invoke(blk, r)
    }
}

// Типизированный обход: тип-обёртка несёт [asset(kind, ext)], а кеш и ревизия дерева
// живут в static_let этого инстанцирования. Вызывающий код не хранит ни список, ни
// версию дерева — повторный вызов сам перечитает дерево, если оно изменилось.
def public ForEachAsset(t : type<auto(TT)>; blk : block<(ref : AssetFileRef) : void>) {
    let info = AssetInfoOf(type<TT>)
    if (!info.has) {
        return
    }

    let domain = AssetDomainOfKind(info.kind)
    let ext_key = asset_ext_key(info.ext)

    static_let("{typeinfo typename(type<TT>)}`asset_files", $ {
        var state : FilesQuery
        var filled : bool
        var epoch : uint64
    })

    let revision = get_tree_revision(domain)
    if (!filled || epoch != g_asset_cache_epoch || state.revision != revision) {
        filled = true
        epoch = g_asset_cache_epoch
        state.revision = revision
        FillQuery(domain, ext_key, state.refs)
    }

    for (r in state.refs) {
        invoke(blk, r)
    }
}

// Тот же результат списком, если вызывающему действительно нужен массив. Отдаём копию
// индексов: 8 байт на файл, строк в нём нет. Для отрисовки списка предпочитай
// ForEachAsset — он не создаёт копию на каждый кадр.
def public FindAssets(t : type<auto(TT)>) : array<AssetFileRef> {
    var out : array<AssetFileRef>
    ForEachAsset(type<TT>) $(r) {
        out |> push(r)
    }
    return <- out
}

// Старый вариант без типа: домен и ключ расширения задаются вручную. Нужен браузеру
// ассетов, который показывает произвольный домен без фильтра по типу.
def public FindFiles(domain : StorageDomain; ext_key : uint64) : array<AssetFileRef> {
    EnsureQuery(domain, ext_key)
    return <- clone(g_query_cache[int(domain)][ext_key].refs)
}

// Файл по ссылке из ForEachFile/ForEachAsset/FindFiles. Строки берутся из живого дерева,
// поэтому значение годится только сразу: держать его дольше, чем жив узел, нельзя —
// копию не делаем намеренно.
def public TreeFile(domain : StorageDomain; ref : AssetFileRef) : AssetItem {
    assume folders = get_folders(domain)
    let f = folders[ref.folder].files[ref.file]
    return <- AssetItem(name = f.name, path = f.path, guid = f.guid, ext = f.ext)
}

// Сбросить кеш запросов (например, после пересборки деревьев).
def public InvalidateFileCache() {
    g_asset_cache_epoch++
    for (cache in g_query_cache) {
        clear(cache)
    }
}

// Полная пересборка деревьев из диска: пока файлового вотчера нет, это единственный способ
// увидеть новые файлы. После неё ревизии другие, а старые записи кеша никому не нужны.
def public RefreshAssetTrees() {
    rebuild_asset_trees()
    InvalidateFileCache()
}
