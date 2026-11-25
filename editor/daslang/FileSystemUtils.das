options gen2
options relaxed_assign = false

module FileSystemUtils shared public

require TryEditor

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
    resize(g_query_cache, 6)  // engine, game, content, data, engine shaders, game shaders
}

// kind — это домен хранения (engine/game/content/data/engine_shaders/game_shaders), а не
// подпапка. Фильтр по расширению применяется ко всем папкам домена, поэтому сцены и префабы
// ищутся по всему content, а не только в content/scenes.
def public AssetDomainOfKind(kind : string) : StorageDomain {
    if (kind == "engine") {
        return StorageDomain.Engine
    } elif (kind == "game") {
        return StorageDomain.Game
    } elif (kind == "content") {
        return StorageDomain.Content
    } elif (kind == "engine_shaders") {
        return StorageDomain.EngineShaders
    } elif (kind == "game_shaders") {
        return StorageDomain.GameShaders
    }
    return StorageDomain.Data
}

// Один обход дерева наполняет кеш домена по ключу расширения.
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

// Список домена по фильтру расширения. Для отрисовки списка предпочитай ForEachFile —
// он не создаёт копию на каждый кадр.
def public FindFiles(domain : StorageDomain; ext_key : uint64) : array<AssetFileRef> {
    EnsureQuery(domain, ext_key)
    return <- clone(g_query_cache[int(domain)][ext_key].refs)
}

// Файл по ссылке из ForEachFile/FindFiles. Строки берутся из живого дерева,
// поэтому значение годится только сразу: держать его дольше, чем жив узел, нельзя —
// копию не делаем намеренно.
def public TreeFile(domain : StorageDomain; ref : AssetFileRef) : AssetItem {
    assume folders = get_folders(domain)
    let f = folders[ref.folder].files[ref.file]
    return <- AssetItem(name = f.name, path = f.path, guid = f.guid, ext = f.ext)
}

// Сбросить кеш запросов (например, после пересборки деревьев).
def public InvalidateFileCache() {
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
