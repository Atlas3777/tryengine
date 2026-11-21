#pragma once

#include <EASTL/string.h>
#include <EASTL/vector.h>
#include <utility>

#include "engine/async/Task.hpp"
#include "engine/core/MakeError.hpp"
#include "engine/resources/AssetLocation.hpp"
#include "engine/resources/AssetRegistry.hpp"
#include "engine/resources/AsyncFileManager.hpp"

namespace tryengine::resources {

// Байты одного ассета, где бы он ни лежал: loose-файл в редакторе или чанк
// смонтированного пака в игре. Это единственный путь чтения: типизированные кеши и
// разовые текстовые запросы ходят через него, поэтому loose- и pak-ветки больше не
// могут разойтись (раньше pak-ветка ещё и не делала Submit и ждала кадр).
inline async::Task<eastl::vector<uint8_t>> ReadAssetBytes(AsyncFileManager& file_manager, AssetRegistry& registry,
                                                          uint64_t asset_id) {
    const auto location = registry.GetAssetLocation(asset_id);
    if (!location.has_value())
        co_return LogAndMakeError("Path for guid {} not found", asset_id);

    if (const auto* loose = eastl::get_if<LooseFileLocation>(&*location)) {
        // Размер нужен заранее: io_uring читает по длине. Обе операции отправляем
        // сразу, чтобы чтение не ждало Submit из кадра приложения.
        FileHandle stat_handle = file_manager.GetStatAsync(loose->path.c_str());
        file_manager.Submit();
        co_await stat_handle;
        if (stat_handle.IsFailed())
            co_return LogAndMakeError("Failed to stat artifact path: '{}'", loose->path);

        FileHandle read_handle =
            file_manager.ReadChunkAsync(loose->path.c_str(), 0, static_cast<uint32_t>(stat_handle.GetFileSize()));
        file_manager.Submit();
        co_await read_handle;
        if (read_handle.IsFailed())
            co_return LogAndMakeError("Failed to read artifact: {}", loose->path);

        co_return eastl::move(read_handle.TakeData());
    }

    if (const auto* pak = eastl::get_if<PakChunkLocation>(&*location)) {
        FileHandle read_handle = file_manager.ReadChunkFDAsync(pak->fd_slot, pak->offset, pak->size);
        file_manager.Submit();
        co_await read_handle;
        if (read_handle.IsFailed())
            co_return LogAndMakeError("Failed to read artifact for guid: {}", asset_id);

        co_return eastl::move(read_handle.TakeData());
    }

    co_return LogAndMakeError("Unsupported asset location for guid {}", asset_id);
}

// Тот же байтовый путь, но текстом. Пустой файл — это успешное чтение; что он значит
// для конкретного формата, решает вызывающий.
inline async::Task<eastl::string> ReadAssetText(AsyncFileManager& file_manager, AssetRegistry& registry,
                                                uint64_t asset_id) {
    auto bytes = co_await ReadAssetBytes(file_manager, registry, asset_id);
    if (!bytes.has_value())
        co_return std::move(bytes).error();

    co_return eastl::string(reinterpret_cast<const char*>(bytes->data()), bytes->size());
}

}  // namespace tryengine::resources
