#pragma once

#include <EASTL/string.h>
#include <cstring>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <dirent.h>
#include <sys/types.h>
#endif

namespace tryengine::filesystem {

namespace detail {

template <typename Context, typename F>
void IterateDirectoryRecursive(eastl::string& current_path, Context parent_ctx, F&& callback) {
    const size_t base_len = current_path.length();

#if defined(_WIN32)
    current_path += "\\*";
    WIN32_FIND_DATAA find_data;
    HANDLE hFind = FindFirstFileA(current_path.c_str(), &find_data);
    current_path.resize(base_len);

    if (hFind == INVALID_HANDLE_VALUE)
        return;

    do {
        const char* name = find_data.cFileName;
        if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0)
            continue;

        current_path += "\\";
        current_path += name;

        const bool is_dir = (find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;

        // Колбэк обрабатывает элемент и возвращает новый контекст для его детей
        Context child_ctx = callback(current_path, name, is_dir, parent_ctx);

        if (is_dir) {
            IterateDirectoryRecursive(current_path, child_ctx, callback);
        }

        current_path.resize(base_len);
    } while (FindNextFileA(hFind, &find_data));

    FindClose(hFind);
#else
    DIR* dir = opendir(current_path.c_str());
    if (!dir)
        return;

    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        const char* name = entry->d_name;
        if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0)
            continue;

        if (!current_path.empty() && current_path.back() != '/') {
            current_path += "/";
        }
        current_path += name;

        const bool is_dir = (entry->d_type == DT_DIR);

        Context child_ctx = callback(current_path, name, is_dir, parent_ctx);

        if (is_dir) {
            IterateDirectoryRecursive(current_path, child_ctx, callback);
        }

        current_path.resize(base_len);
    }
    closedir(dir);
#endif
}

}  // namespace detail
template <typename Context, typename F>
requires std::invocable<F, const eastl::string&, const char*, bool, Context>
void Iterate(const eastl::string_view path, Context root_ctx, F&& callback) {
    eastl::string path_buffer(path.data(), path.size());
    detail::IterateDirectoryRecursive(path_buffer, root_ctx, eastl::forward<F>(callback));
}

}  // namespace tryengine::filesystem