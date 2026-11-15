options gen2
options relaxed_assign = false

module FileSystemUtils shared public

require TryEditor
require strings

// Ключи расширений: в узле дерева лежит тот же 8-байтовый ключ, поэтому поиск по типу —
// одно сравнение uint64 на файл, без ends_with по строке.
let EXT_MAT = asset_ext_key(".mat")
let EXT_SCENE = asset_ext_key(".scene")
let EXT_PREFAB = asset_ext_key(".prefab")

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

// Наполняет кеш, если для этой ревизии дерева обхода ещё не было.
def private EnsureQuery(domain : StorageDomain; ext_key : uint64) {
    let d = int(domain)
    let revision = get_tree_revision(domain)

    if (key_exists(g_query_cache[d], ext_key) && g_query_cache[d][ext_key].revision == revision) {
        return
    }

    var found : array<AssetFileRef>
    assume folders = get_folders(domain)
    for (f in range(length(folders))) {
        for (i in range(length(folders[f].files))) {
            if (ext_key != EXT_ANY && folders[f].files[i].ext != ext_key) {
                continue
            }
            found |> push(AssetFileRef(folder = f, file = i))
        }
    }

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

// Тот же результат списком — когда его надо придержать между кадрами (BuildManager).
// Отдаём копию массива индексов: 8 байт на файл, строк в нём нет.
def public FindFiles(domain : StorageDomain; ext_key : uint64) : array<AssetFileRef> {
    EnsureQuery(domain, ext_key)
    return <- clone(g_query_cache[int(domain)][ext_key].refs)
}

// Файл по ссылке из ForEachFile/FindFiles. Строки берутся из живого дерева, поэтому значение
// годится только сразу: держать его дольше, чем жив узел, нельзя — копию не делаем намеренно.
def public TreeFile(domain : StorageDomain; ref : AssetFileRef) : AssetItem {
    assume folders = get_folders(domain)
    let f = folders[ref.folder].files[ref.file]
    return <- AssetItem(name = f.name, path = f.path, guid = f.guid, ext = f.ext)
}

// Сбросить кеш запросов (например, после пересборки деревьев).
def public InvalidateFileCache() {
    for (i in range(length(g_query_cache))) {
        clear(g_query_cache[i])
    }
}

// Полная пересборка деревьев из диска: пока файлового вотчера нет, это единственный способ
// увидеть новые файлы. После неё ревизии другие, а старые записи кеша никому не нужны.
def public RefreshAssetTrees() {
    rebuild_asset_trees()
    InvalidateFileCache()
}
