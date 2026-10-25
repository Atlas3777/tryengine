options gen2

module FileSystemUtils shared public

require tryengine/Types
require strings
require tryeditor

// DTO для описания ассета
struct public AssetItem {
    guid : uint64
    name : string
    path : string
}

def public FindFilesByExt(ext : string) : array<AssetItem> {
    print("FindFilesByExt\n")
    return <- find_files_by_ext(get_game_folders(), ext)
}

// Поиск файлов по расширению во всех папках проекта
def public find_files_by_ext(folders : UiFolderVector; ext : string) : array<AssetItem> {
    var result : array<AssetItem>
    for (folder in folders) {
        for (file in folder.files) {
            if (ends_with(file.name, ext)) {
                push(result, AssetItem(name = file.name, path = file.path, guid = file.guid))
            }
        }
    }
    return <- result
}

// Поиск файлов по расширению внутри конкретной директории (по пути папки)
def public find_files_by_ext_in_dir(folders : UiFolderVector; folder_path : string; ext : string) : array<AssetItem> {
    var result : array<AssetItem>
    for (folder in folders) {
        if (folder.name == folder_path || folder.path == folder_path) {
            for (file in folder.files) {
                if (ends_with(file.name, ext)) {
                    push(result, AssetItem(name = file.name, path = file.path, guid = file.guid))
                }
            }
        }
    }
    print("sdf\n")
    return <- result
}