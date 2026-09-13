#pragma once

#include <EASTL/hash_map.h>
#include <EASTL/string.h>
#include <EASTL/vector.h>
#include <mutex>

#include "AssetLocation.hpp"
#include "AssetPakBinary.hpp"
#include "AssetRegistry.hpp"
#include "FileIterator.h"
#include "engine/async/GlobalExecutors.hpp"
#include "engine/async/RunAndForget.hpp"
#include "engine/async/Task.hpp"
#include "engine/async/WhenAll.hpp"
#include "engine/core/MakeError.hpp"
#include "engine/resources/AsyncFileManager.hpp"

namespace tryengine::resources {

class PakManager {
public:
    PakManager(AsyncFileManager& file_manager, AssetRegistry& registry)
        : file_manager_(file_manager), registry_(registry) {}

    ~PakManager() {
        std::lock_guard lock(mutex_);
        for (const auto& record : pak_records_) {
            if (record.fd_slot != UINT32_MAX) {
                file_manager_.CloseFDAsync(record.fd_slot);
            }
        }
        file_manager_.Submit();
    }

    PakManager(const PakManager&) = delete;
    PakManager& operator=(const PakManager&) = delete;

    async::Task<void> LoadAll() {
        eastl::vector<async::Task<PakHandle>> vector;
        filesystem::Iterate("content/", 0,
            [&](const eastl::string& path, const char* name, bool is_dir, int idx) {
            async::RunAndForget(async::ThreadPool(), LoadPakAsync(path));
        });
        co_return {};
    }

    async::Task<PakHandle> LoadPakAsync(const eastl::string& pak_path) {
        // 1. Проверка на уже смонтированный PAK
        {
            std::lock_guard lock(mutex_);
            auto it = mounted_paks_by_path_.find(pak_path);
            if (it != mounted_paks_by_path_.end()) {
                co_return it->second;
            }
        }

        // 2. Асинхронно открываем дескриптор файла в io_uring
        FileHandle open_handle = file_manager_.OpenAsyncCopy(pak_path, O_RDONLY);
        co_await open_handle;

        if (open_handle.IsFailed() || !open_handle.IsValid()) {
            co_return LogAndMakeError("Failed to open PAK file: {}", pak_path.c_str());
        }

        const uint32_t fd_slot = open_handle.GetSlotIndex();

        // 3. Чтение заголовка по открытому fd_slot без повторного открытия файла
        FileHandle header_handle = file_manager_.ReadChunkFDAsync(fd_slot, 0, sizeof(AssetPakHeader));
        co_await header_handle;

        if (header_handle.IsFailed()) {
            file_manager_.CloseFDAsync(fd_slot);
            co_return LogAndMakeError("Failed to read header for PAK: {}", pak_path.c_str());
        }

        auto header_res = AssetPakBinary::ReadHeader(header_handle.GetData());
        if (!header_res.has_value()) {
            file_manager_.CloseFDAsync(fd_slot);
            co_return header_res.error();
        }

        const auto header = *header_res;

        // 4. Чтение манифеста по открытому fd_slot
        uint64_t manifest_body_size = header.data_offset - sizeof(AssetPakHeader);
        FileHandle manifest_handle = file_manager_.ReadChunkFDAsync(
            fd_slot, sizeof(AssetPakHeader), manifest_body_size
        );
        co_await manifest_handle;

        if (manifest_handle.IsFailed()) {
            file_manager_.CloseFDAsync(fd_slot);
            co_return LogAndMakeError("Failed to read manifest block for PAK: {}", pak_path.c_str());
        }

        auto meta_res = AssetPakBinary::UnpackManifest(manifest_handle.GetData());
        if (!meta_res.has_value()) {
            file_manager_.CloseFDAsync(fd_slot);
            co_return meta_res.error();
        }

        const auto& manifest = *meta_res;

        // 5. Параллельная подгрузка зависимостей
        if (!manifest.dependencies.empty()) {
            eastl::vector<async::Task<PakHandle>> dep_tasks;
            dep_tasks.reserve(manifest.dependencies.size());

            for (const auto& dep_path : manifest.dependencies) {
                dep_tasks.push_back(LoadPakAsync(dep_path));
            }

            auto results_r = co_await async::WhenAll(async::ThreadPool(), std::move(dep_tasks));

            if (!results_r.has_value()) {
                LogError("Error loading dependencies for PAK {}: {}", pak_path.c_str(), results_r.error());
            } else {
                auto& results = *results_r;
                for (size_t i = 0; i < results.size(); ++i) {
                    if (!results[i].has_value()) {
                        file_manager_.CloseFDAsync(fd_slot);
                        co_return LogAndMakeError("Failed dependency '{}' for PAK '{}': {}",
                                                 manifest.dependencies[i].c_str(),
                                                 pak_path.c_str(),
                                                 results[i].error().Message());
                    }
                }
            }
        }

        // 6. Фиксация смонтированного пакета
        PakHandle new_handle = INVALID_PAK_HANDLE;
        {
            std::lock_guard lock(mutex_);
            auto it = mounted_paks_by_path_.find(pak_path);
            if (it != mounted_paks_by_path_.end()) {
                // Если за время co_await файл параллельно уже смонтировали
                file_manager_.CloseFDAsync(fd_slot);
                co_return it->second;
            }

            new_handle = static_cast<PakHandle>(pak_records_.size());
            pak_records_.push_back({pak_path, header.data_offset, fd_slot});
            mounted_paks_by_path_[pak_path] = new_handle;
        }

        // 7. Регистрация артефактов с сохранением fd_slot в PakChunkLocation
        for (const auto& entry : manifest.layout) {
            registry_.RegisterPakArtifact(
                entry.guid,
                PakChunkLocation{
                    .pak_handle = new_handle,
                    .fd_slot = fd_slot,
                    .offset = header.data_offset + entry.offset,
                    .size = entry.size
                }
            );
        }

        co_return new_handle;
    }

    struct PakRecord {
        eastl::string path;
        uint64_t data_base_offset{0};
        uint32_t fd_slot{UINT32_MAX};
    };

    PakRecord GetPakRecord(PakHandle handle) const {
        std::lock_guard lock(mutex_);
        return pak_records_[handle];
    }

private:
    AsyncFileManager& file_manager_;
    AssetRegistry& registry_;

    mutable std::mutex mutex_;
    eastl::vector<PakRecord> pak_records_;
    eastl::hash_map<eastl::string, PakHandle> mounted_paks_by_path_;
};

} // namespace tryengine::resources