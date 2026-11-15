options gen2

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

// Файл в дереве: индексы папки и файла в get_folders(domain). В кеше нет ни строк, ни
// указателей — имя и путь всегда читаются из живого дерева, поэтому запись кеша не может
// пережить свой узел: ревизия дерева меняется вместе с узлами.
struct AssetFileRef {
    folder : int
    file : int
}

struct FilesQuery {
    revision : uint64
    files : array<AssetFileRef>
}

var private g_query_cache : table<string; FilesQuery>

def private QueryKey(domain : StorageDomain; ext_key : uint64) : string {
    return "{int(domain)}:{ext_key:d}"
}

// Индексы всех файлов домена с данным расширением (EXT_ANY — все файлы). Обход
// повторяется только при смене ревизии дерева: кеш помнит ревизию, с которой посчитан,
// поэтому "все материалы в data" — это один обход на изменение дерева, а не на кадр.
def public FindFiles(domain : StorageDomain; ext_key : uint64) : array<AssetFileRef> {
    let key = QueryKey(domain, ext_key)
    let revision = get_tree_revision(domain)

    if (key_exists(g_query_cache, key) && g_query_cache[key].revision == revision) {
        return <- clone(g_query_cache[key].files)
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

    g_query_cache[key] = FilesQuery(revision = revision, files <- found)
    return <- clone(g_query_cache[key].files)
}

// Файлы домена по расширению строкой (".mat") или просто именем типа ("mat").
def public FilesByExt(domain : StorageDomain; ext : string) : array<AssetItem> {
    let refs = FindFiles(domain, asset_ext_key(ext))
    assume folders = get_folders(domain)

    var items : array<AssetItem>
    items.reserve(length(refs))
    for (r in refs) {
        let f = folders[r.folder].files[r.file]
        items |> push(AssetItem(name = f.name, path = f.path, guid = f.guid, ext = f.ext))
    }
    return <- items
}

// Все материалы авторитетного корня data: у них нет .meta и guid, ссылка строится по пути.
def public DataMaterials() : array<AssetItem> {
    return <- FilesByExt(StorageDomain.Data, ".mat")
}

// Сцены и префабы — композиции из content.
def public ContentScenes() : array<AssetItem> {
    return <- FilesByExt(StorageDomain.Content, ".scene")
}

// Сбросить кеш запросов (например, после пересборки деревьев).
def public InvalidateFileCache() {
    clear(g_query_cache)
}

// Полная пересборка деревьев из диска: пока файлового вотчера нет, это единственный способ
// увидеть новые файлы. После неё ревизии другие, а старые ключи кеша никому не нужны.
def public RefreshAssetTrees() {
    rebuild_asset_trees()
    clear(g_query_cache)
}
