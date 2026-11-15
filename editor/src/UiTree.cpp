#include "editor/UiTree.hpp"

#include <EASTL/sort.h>
#include <cstring>

#include "editor/AssetExt.hpp"

namespace tryeditor {

using eastl::string_view;
using tryengine::core::PathId;
using tryengine::core::StringTable;
using tryengine::core::ToView;

namespace {

constexpr size_t kBuf = 1024;  // максимальная длина пути

inline string_view V(const char* s) {
    return s ? StringTable::View(s) : string_view();
}

// "parent/name" -> buf. Пустой результат => переполнение (пути > 1023 не поддерживаются).
string_view Join(char* buf, string_view parent, string_view name) {
    const size_t n = parent.size() + 1 + name.size();
    if (n >= kBuf)
        return {};
    std::memcpy(buf, parent.data(), parent.size());
    buf[parent.size()] = '/';
    if (!name.empty())
        std::memcpy(buf + parent.size() + 1, name.data(), name.size());
    buf[n] = 0;
    return string_view(buf, n);
}

}  // namespace

UiTree::UiTree(StringTable& strings) : strings_(strings) {}

UiTree::~UiTree() {
    Clear();
}

void UiTree::Clear() {
    for (UiFolder& f : folders_) {
        if (!f.name)
            continue;
        for (UiFile& file : f.files) {
            strings_.Release(file.name);
            strings_.Release(file.path);
        }
        strings_.Release(f.name);
        strings_.Release(f.path);
    }
    folders_.clear();
    free_slots_.clear();
    folder_by_path_.clear();
    ++version_;
}

void UiTree::BuildFrom(const ScanResult& scan, const eastl::hash_map<uint64_t, uint64_t>* path_to_guid) {
    Clear();
    if (scan.entries.empty())
        return;

    // Обход гарантирует: родитель раньше ребёнка, поэтому родителя всегда можно найти по пути.
    for (size_t i = 0; i < scan.entries.size(); ++i) {
        const ScanEntry& e = scan.entries[i];
        const string_view path = scan.Path(e);
        const size_t slash = path.find_last_of("\\/");
        const string_view name = (slash == string_view::npos) ? path : path.substr(slash + 1);

        if (i == 0) {  // корень
            NewFolder(kNoFolder, name, path);
            continue;
        }
        if (slash == string_view::npos)
            continue;
        const uint32_t parent = FindFolder(path.substr(0, slash));
        if (parent == kNoFolder)
            continue;

        if (e.is_dir) {
            const uint32_t idx = NewFolder(parent, name, path);
            folders_[parent].subfolders.push_back(idx);
        } else {
            uint64_t guid = 0;
            if (path_to_guid) {
                auto it = path_to_guid->find(PathId(path));
                if (it != path_to_guid->end())
                    guid = it->second;
            }
            folders_[parent].files.push_back(UiFile{guid, ExtKey(name), strings_.Intern(name), strings_.Intern(path)});
        }
    }

    // Один раз сортируем (быстрее, чем вставка по одному).
    for (UiFolder& f : folders_) {
        eastl::sort(f.subfolders.begin(), f.subfolders.end(),
                    [this](uint32_t a, uint32_t b) { return V(folders_[a].name) < V(folders_[b].name); });
        eastl::sort(f.files.begin(), f.files.end(),
                    [](const UiFile& a, const UiFile& b) { return V(a.name) < V(b.name); });
    }
    ++version_;
}

// ---------------------------------------------------------------- поиск

uint32_t UiTree::FindFolder(string_view path) const {
    auto it = folder_by_path_.find(PathId(path));
    return it == folder_by_path_.end() ? kNoFolder : it->second;
}

uint32_t UiTree::EnsureFolder(string_view path) {
    const uint32_t found = FindFolder(path);
    if (found != kNoFolder)
        return found;
    const size_t slash = path.find_last_of("\\/");
    if (slash == string_view::npos)
        return kNoFolder;
    const uint32_t parent = EnsureFolder(path.substr(0, slash));
    if (parent == kNoFolder)
        return kNoFolder;
    return AddFolder(parent, path.substr(slash + 1));
}

uint32_t UiTree::LowerBoundSub(uint32_t parent, string_view name) const {
    const auto& subs = folders_[parent].subfolders;
    uint32_t lo = 0, hi = static_cast<uint32_t>(subs.size());
    while (lo < hi) {
        const uint32_t mid = (lo + hi) / 2;
        if (V(folders_[subs[mid]].name) < name)
            lo = mid + 1;
        else
            hi = mid;
    }
    return lo;
}

uint32_t UiTree::FindChildFolder(uint32_t parent, string_view name) const {
    const uint32_t pos = LowerBoundSub(parent, name);
    const auto& subs = folders_[parent].subfolders;
    if (pos < subs.size() && V(folders_[subs[pos]].name) == name)
        return subs[pos];
    return kNoFolder;
}

void UiTree::InsertSub(uint32_t parent, uint32_t child) {
    const uint32_t pos = LowerBoundSub(parent, V(folders_[child].name));
    auto& subs = folders_[parent].subfolders;
    subs.insert(subs.begin() + pos, child);
}

void UiTree::EraseSub(uint32_t parent, uint32_t child) {
    auto& subs = folders_[parent].subfolders;
    for (size_t i = 0; i < subs.size(); ++i) {
        if (subs[i] == child) {
            subs.erase(subs.begin() + i);
            return;
        }
    }
}

uint32_t UiTree::LowerBoundFile(uint32_t folder, string_view name) const {
    const auto& files = folders_[folder].files;
    uint32_t lo = 0, hi = static_cast<uint32_t>(files.size());
    while (lo < hi) {
        const uint32_t mid = (lo + hi) / 2;
        if (V(files[mid].name) < name)
            lo = mid + 1;
        else
            hi = mid;
    }
    return lo;
}

uint32_t UiTree::FindFile(uint32_t folder, string_view name) const {
    const uint32_t pos = LowerBoundFile(folder, name);
    const auto& files = folders_[folder].files;
    if (pos < files.size() && V(files[pos].name) == name)
        return pos;
    return kNoFolder;
}

void UiTree::InsertFile(uint32_t folder, const UiFile& file) {
    const uint32_t pos = LowerBoundFile(folder, V(file.name));
    auto& files = folders_[folder].files;
    files.insert(files.begin() + pos, file);
}

// ---------------------------------------------------------------- внутреннее

uint32_t UiTree::NewFolder(uint32_t parent, string_view name, string_view path) {
    uint32_t idx;
    if (!free_slots_.empty()) {
        idx = free_slots_.back();
        free_slots_.pop_back();
    } else {
        idx = static_cast<uint32_t>(folders_.size());
        folders_.emplace_back();
    }
    UiFolder& f = folders_[idx];  // ссылка берётся ПОСЛЕ возможной реаллокации
    f.name = strings_.Intern(name);
    f.path = strings_.Intern(path);
    f.parent = parent;
    folder_by_path_[PathId(path)] = idx;
    return idx;
}

void UiTree::FreeSubtree(uint32_t idx, Changes* changes) {
    for (size_t i = 0; i < folders_[idx].subfolders.size(); ++i)
        FreeSubtree(folders_[idx].subfolders[i], changes);

    UiFolder& f = folders_[idx];
    for (UiFile& file : f.files) {
        if (changes)
            changes->push_back({PathId(V(file.path)), 0});
        strings_.Release(file.name);
        strings_.Release(file.path);
    }
    folder_by_path_.erase(PathId(V(f.path)));
    strings_.Release(f.name);
    strings_.Release(f.path);
    f = UiFolder{};
    free_slots_.push_back(idx);
}

// Папке (и всему поддереву) выставляет новый путь. Имя папки уже должно быть корректным.
void UiTree::Repath(uint32_t idx, string_view new_path, Changes* changes) {
    {
        UiFolder& f = folders_[idx];
        folder_by_path_.erase(PathId(V(f.path)));
        const char* np = strings_.Intern(new_path);
        strings_.Release(f.path);
        f.path = np;
        folder_by_path_[PathId(new_path)] = idx;
    }

    for (UiFile& file : folders_[idx].files) {
        char fb[kBuf];
        const string_view np = Join(fb, new_path, V(file.name));
        if (np.empty())
            continue;
        if (changes)
            changes->push_back({PathId(V(file.path)), PathId(np)});
        const char* interned = strings_.Intern(np);
        strings_.Release(file.path);
        file.path = interned;
    }

    for (size_t i = 0; i < folders_[idx].subfolders.size(); ++i) {
        const uint32_t child = folders_[idx].subfolders[i];
        char cb[kBuf];
        const string_view cp = Join(cb, new_path, V(folders_[child].name));
        if (!cp.empty())
            Repath(child, cp, changes);
    }
}

// ---------------------------------------------------------------- папки

uint32_t UiTree::AddFolder(uint32_t parent, string_view name) {
    if (!Alive(parent) || name.empty())
        return kNoFolder;
    const uint32_t existing = FindChildFolder(parent, name);
    if (existing != kNoFolder)
        return existing;

    char buf[kBuf];
    const string_view path = Join(buf, V(folders_[parent].path), name);
    if (path.empty())
        return kNoFolder;

    const uint32_t idx = NewFolder(parent, name, path);
    InsertSub(parent, idx);
    ++version_;
    return idx;
}

bool UiTree::RemoveFolder(uint32_t idx, Changes* changes) {
    if (idx == 0 || !Alive(idx))
        return false;
    EraseSub(folders_[idx].parent, idx);
    FreeSubtree(idx, changes);
    ++version_;
    return true;
}

bool UiTree::RenameFolder(uint32_t idx, string_view new_name, Changes* changes) {
    if (idx == 0 || !Alive(idx) || new_name.empty())
        return false;
    if (V(folders_[idx].name) == new_name)
        return true;

    const uint32_t parent = folders_[idx].parent;
    if (FindChildFolder(parent, new_name) != kNoFolder)
        return false;

    char buf[kBuf];
    const string_view path = Join(buf, V(folders_[parent].path), new_name);
    if (path.empty())
        return false;

    EraseSub(parent, idx);
    const char* nn = strings_.Intern(new_name);
    strings_.Release(folders_[idx].name);
    folders_[idx].name = nn;
    Repath(idx, path, changes);
    InsertSub(parent, idx);
    ++version_;
    return true;
}

bool UiTree::MoveFolder(uint32_t idx, uint32_t new_parent, Changes* changes) {
    if (idx == 0 || !Alive(idx) || !Alive(new_parent))
        return false;
    const uint32_t old_parent = folders_[idx].parent;
    if (old_parent == new_parent)
        return true;

    for (uint32_t p = new_parent; p != kNoFolder; p = folders_[p].parent)
        if (p == idx)
            return false;  // нельзя переместить папку внутрь самой себя

    const string_view name = V(folders_[idx].name);
    if (FindChildFolder(new_parent, name) != kNoFolder)
        return false;

    char buf[kBuf];
    const string_view path = Join(buf, V(folders_[new_parent].path), name);
    if (path.empty())
        return false;

    EraseSub(old_parent, idx);
    folders_[idx].parent = new_parent;
    Repath(idx, path, changes);
    InsertSub(new_parent, idx);
    ++version_;
    return true;
}

// ---------------------------------------------------------------- файлы

bool UiTree::AddFile(uint32_t folder, string_view name, uint64_t guid) {
    if (!Alive(folder) || name.empty())
        return false;

    const uint32_t existing = FindFile(folder, name);
    if (existing != kNoFolder) {
        folders_[folder].files[existing].guid = guid;
        ++version_;
        return true;
    }

    char buf[kBuf];
    const string_view path = Join(buf, V(folders_[folder].path), name);
    if (path.empty())
        return false;

    InsertFile(folder, UiFile{guid, ExtKey(name), strings_.Intern(name), strings_.Intern(path)});
    ++version_;
    return true;
}

bool UiTree::RemoveFile(uint32_t folder, string_view name, Changes* changes) {
    if (!Alive(folder))
        return false;
    const uint32_t pos = FindFile(folder, name);
    if (pos == kNoFolder)
        return false;

    auto& files = folders_[folder].files;
    const UiFile file = files[pos];
    if (changes)
        changes->push_back({PathId(V(file.path)), 0});
    strings_.Release(file.name);
    strings_.Release(file.path);
    files.erase(files.begin() + pos);
    ++version_;
    return true;
}

bool UiTree::RenameFile(uint32_t folder, string_view old_name, string_view new_name, Changes* changes) {
    if (!Alive(folder) || new_name.empty())
        return false;
    const uint32_t pos = FindFile(folder, old_name);
    if (pos == kNoFolder)
        return false;
    if (old_name == new_name)
        return true;
    if (FindFile(folder, new_name) != kNoFolder)
        return false;

    char buf[kBuf];
    const string_view path = Join(buf, V(folders_[folder].path), new_name);
    if (path.empty())
        return false;

    auto& files = folders_[folder].files;
    UiFile file = files[pos];
    if (changes)
        changes->push_back({PathId(V(file.path)), PathId(path)});

    const char* nn = strings_.Intern(new_name);
    const char* np = strings_.Intern(path);
    strings_.Release(file.name);
    strings_.Release(file.path);
    file.name = nn;
    file.path = np;

    files.erase(files.begin() + pos);
    InsertFile(folder, file);
    ++version_;
    return true;
}

bool UiTree::MoveFile(uint32_t from_folder, string_view name, uint32_t to_folder, Changes* changes) {
    if (!Alive(from_folder) || !Alive(to_folder))
        return false;
    const uint32_t pos = FindFile(from_folder, name);
    if (pos == kNoFolder)
        return false;
    if (from_folder == to_folder)
        return true;

    auto& src = folders_[from_folder].files;
    UiFile file = src[pos];
    const string_view fname = V(file.name);
    if (FindFile(to_folder, fname) != kNoFolder)
        return false;

    char buf[kBuf];
    const string_view path = Join(buf, V(folders_[to_folder].path), fname);
    if (path.empty())
        return false;

    if (changes)
        changes->push_back({PathId(V(file.path)), PathId(path)});
    const char* np = strings_.Intern(path);
    strings_.Release(file.path);
    file.path = np;  // имя переезжает вместе с файлом, refcount не меняется

    src.erase(src.begin() + pos);
    InsertFile(to_folder, file);
    ++version_;
    return true;
}

}  // namespace tryeditor