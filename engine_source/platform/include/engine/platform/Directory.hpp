#pragma once

// Слой 1: ReadDirectory — один уровень.  Слой 2: Walk — рекурсивный обход, путь живёт в стековом буфере.

#include <EASTL/string_view.h>
#include <cstdint>
#include <cstring>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#endif

namespace tryengine::filesystem {

enum class EntryKind : uint8_t { File, Directory, Other };

inline constexpr size_t kMaxPath = 1024;

// path — UTF-8, null-terminated. on_entry(eastl::string_view name_utf8, EntryKind).
// Симлинки/reparse points отдаются как Other (не заходим => нет циклов).
// Возвращает false, если директорию не удалось открыть.
template <class F>
bool ReadDirectory(const char* path, F&& on_entry) {
#if defined(_WIN32)
    wchar_t wpath[kMaxPath + 4];
    const int n = MultiByteToWideChar(CP_UTF8, 0, path, -1, wpath, static_cast<int>(kMaxPath));
    if (n <= 0)
        return false;
    size_t len = static_cast<size_t>(n - 1);
    wpath[len++] = L'\\';
    wpath[len++] = L'*';
    wpath[len] = 0;

    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(wpath, &fd);
    if (h == INVALID_HANDLE_VALUE)
        return false;

    do {
        const wchar_t* wn = fd.cFileName;
        if (wn[0] == L'.' && (wn[1] == 0 || (wn[1] == L'.' && wn[2] == 0)))
            continue;

        char name[kMaxPath];
        const int m = WideCharToMultiByte(CP_UTF8, 0, wn, -1, name, static_cast<int>(sizeof(name)), nullptr, nullptr);
        if (m <= 0)
            continue;

        EntryKind kind = EntryKind::File;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)
            kind = EntryKind::Other;
        else if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            kind = EntryKind::Directory;

        on_entry(eastl::string_view(name, static_cast<size_t>(m - 1)), kind);
    } while (FindNextFileW(h, &fd));

    FindClose(h);
    return true;
#else
    DIR* dir = opendir(path);
    if (!dir)
        return false;

    while (dirent* e = readdir(dir)) {
        const char* name = e->d_name;
        if (name[0] == '.' && (name[1] == 0 || (name[1] == '.' && name[2] == 0)))
            continue;

        EntryKind kind = EntryKind::Other;
        if (e->d_type == DT_REG) {
            kind = EntryKind::File;
        } else if (e->d_type == DT_DIR) {
            kind = EntryKind::Directory;
        } else if (e->d_type == DT_UNKNOWN) {  // некоторые ФС не заполняют d_type
            struct stat st;
            if (fstatat(dirfd(dir), name, &st, AT_SYMLINK_NOFOLLOW) == 0) {
                if (S_ISREG(st.st_mode))
                    kind = EntryKind::File;
                else if (S_ISDIR(st.st_mode))
                    kind = EntryKind::Directory;
            }
        }

        on_entry(eastl::string_view(name, std::strlen(name)), kind);
    }
    closedir(dir);
    return true;
#endif
}

namespace detail {

template <class Visitor>
void WalkImpl(char* buf, size_t len, Visitor& v) {
    buf[len] = 0;
    ReadDirectory(buf, [&](eastl::string_view name, EntryKind kind) {
        if (len + 1 + name.size() >= kMaxPath)
            return;  // слишком длинный путь — пропускаем запись
        buf[len] = '/';
        std::memcpy(buf + len + 1, name.data(), name.size());
        const size_t n = len + 1 + name.size();
        buf[n] = 0;
        const eastl::string_view full(buf, n);

        if (kind == EntryKind::Directory) {
            if (v.OnDirectoryEnter(full, name)) {
                WalkImpl(buf, n, v);
                v.OnDirectoryLeave();
            }
        } else if (kind == EntryKind::File) {
            v.OnFile(full, name);
        }
    });
}

}  // namespace detail

// Visitor:
//   bool OnDirectoryEnter(eastl::string_view full_path, eastl::string_view name);  // false => не заходить
//   void OnDirectoryLeave();
//   void OnFile(eastl::string_view full_path, eastl::string_view name);
// Пути отдаются с '/' на всех платформах. Контекст ("индекс родителя") — стек внутри Visitor.
// full_path/name валидны только на время вызова.
template <class Visitor>
void Walk(eastl::string_view root, Visitor& v) {
    while (root.size() > 1 && (root.back() == '/' || root.back() == '\\'))
        root.remove_suffix(1);
    if (root.size() >= kMaxPath)
        return;
    char buf[kMaxPath];
    std::memcpy(buf, root.data(), root.size());
    detail::WalkImpl(buf, root.size(), v);
}

}  // namespace tryengine::filesystem