#pragma once

#include <EASTL/string.h>

#include "engine/async/Task.hpp"
#include "engine/resources/AsyncFileManager.hpp"

namespace tryengine::resources {
inline async::Task<eastl::vector<uint8_t>> ReadFullFileNonCopy(AsyncFileManager& async_file_manager,
                                                                eastl::string_view file_path) {
    auto stat_handle = async_file_manager.GetStatAsync(file_path);
    co_await stat_handle;
    uint64_t file_size = stat_handle.GetFileSize();

    FileHandle file_handle = async_file_manager.ReadChunkAsync(file_path, 0, file_size);
    co_await file_handle;
    co_return file_handle.TakeData();
}

inline async::Task<eastl::vector<uint8_t>> ReadFullFileCopy(AsyncFileManager& async_file_manager,
                                                             eastl::string_view file_path) {
    auto stat_handle = async_file_manager.GetStatAsyncCopy(file_path);
    co_await stat_handle;
    uint64_t file_size = stat_handle.GetFileSize();

    FileHandle file_handle = async_file_manager.ReadChunkAsync(stat_handle.GetPath(), 0, file_size);
    co_await file_handle;
    co_return file_handle.TakeData();
}
}  // namespace tryengine::resources