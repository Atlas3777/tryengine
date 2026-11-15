#pragma once

#include <EASTL/hash_map.h>
#include <EASTL/string_view.h>
#include <EASTL/vector.h>
#include <cstdint>

#include "editor/AssetScanner.hpp"  // ScanResult (только для BuildFrom)
#include "engine/core/strings/StringTable.hpp"

namespace tryeditor {

inline constexpr uint32_t kNoFolder = 0xFFFFFFFFu;

// POD-структуры, которые видит daslang. Строки принадлежат StringTable и живут, пока узел в дереве.
struct UiFile {
    uint64_t guid;
    const char* name;
    const char* path;
};

struct UiFolder {
    const char* name = nullptr;  // nullptr == слот свободен
    const char* path = nullptr;
    eastl::vector<uint32_t> subfolders;  // индексы в Folders(), отсортированы по имени
    eastl::vector<UiFile> files;         // отсортированы по имени
    uint32_t parent = kNoFolder;
};

// Мутирующее дерево. Индекс папки стабилен (удалённые слоты переиспользуются).
// Указатели на строки стабильны, пока узел жив. Все мутации — ТОЛЬКО с одного потока (main),
// в момент, когда daslang не рисует дерево.
class UiTree {
public:
    // new_hash == 0 => файл удалён. Нужно, чтобы синхронизировать внешние таблицы path->guid.
    struct PathChange {
        uint64_t old_hash;
        uint64_t new_hash;
    };

    using Changes = eastl::vector<PathChange>;

    explicit UiTree(tryengine::core::StringTable& strings);
    ~UiTree();
    UiTree(const UiTree&) = delete;
    UiTree& operator=(const UiTree&) = delete;

    // Полная пересборка (первичная загрузка) из результата сканирования.
    void BuildFrom(const ScanResult& scan, const eastl::hash_map<uint64_t, uint64_t>& path_to_guid);
    void Clear();

    const eastl::vector<UiFolder>& Folders() const {
        return folders_;
    }

    uint64_t Version() const {
        return version_;
    }

    uint32_t FindFolder(eastl::string_view path) const;  // kNoFolder, если нет
    uint32_t EnsureFolder(eastl::string_view path);      // создаёт недостающие, если есть корень

    // Все операции идемпотентны/безопасны: false => ничего не изменилось.
    // Строковые аргументы НЕ должны указывать внутрь самого дерева.
    uint32_t AddFolder(uint32_t parent, eastl::string_view name);
    bool AddFile(uint32_t folder, eastl::string_view name, uint64_t guid);  // уже есть => обновит guid

    bool RemoveFolder(uint32_t idx, Changes* changes = nullptr);  // рекурсивно, корень удалить нельзя
    bool RemoveFile(uint32_t folder, eastl::string_view name, Changes* changes = nullptr);

    bool RenameFolder(uint32_t idx, eastl::string_view new_name, Changes* changes = nullptr);
    bool RenameFile(uint32_t folder, eastl::string_view old_name, eastl::string_view new_name,
                    Changes* changes = nullptr);

    bool MoveFolder(uint32_t idx, uint32_t new_parent, Changes* changes = nullptr);
    bool MoveFile(uint32_t from_folder, eastl::string_view name, uint32_t to_folder, Changes* changes = nullptr);

private:
    bool Alive(uint32_t idx) const {
        return idx < folders_.size() && folders_[idx].name != nullptr;
    }

    uint32_t NewFolder(uint32_t parent, eastl::string_view name, eastl::string_view path);
    void FreeSubtree(uint32_t idx, Changes* changes);
    void Repath(uint32_t idx, eastl::string_view new_path, Changes* changes);

    uint32_t LowerBoundSub(uint32_t parent, eastl::string_view name) const;
    uint32_t FindChildFolder(uint32_t parent, eastl::string_view name) const;
    void InsertSub(uint32_t parent, uint32_t child);
    void EraseSub(uint32_t parent, uint32_t child);

    uint32_t LowerBoundFile(uint32_t folder, eastl::string_view name) const;
    uint32_t FindFile(uint32_t folder, eastl::string_view name) const;  // kNoFolder, если нет
    void InsertFile(uint32_t folder, const UiFile& file);

    tryengine::core::StringTable& strings_;
    eastl::vector<UiFolder> folders_;  // [0] = корень
    eastl::vector<uint32_t> free_slots_;
    eastl::hash_map<uint64_t, uint32_t> folder_by_path_;  // PathHash(path) -> idx
    uint64_t version_ = 0;
};

}  // namespace tryeditor