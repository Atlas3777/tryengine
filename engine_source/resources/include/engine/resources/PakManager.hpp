#pragma once

#include <EASTL/hash_map.h>
#include <EASTL/string.h>
#include <EASTL/vector.h>
#include <mutex>

#include "AssetLocation.hpp"
#include "AssetRegistry.hpp"
#include "binary/AssetPakBinary.hpp"
#include "engine/async/GlobalExecutors.hpp"
#include "engine/async/RunAndForget.hpp"
#include "engine/async/Task.hpp"
#include "engine/async/WhenAll.hpp"
#include "engine/core/MakeError.hpp"
#include "engine/platform/Directory.hpp"
#include "engine/resources/AsyncFileManager.hpp"
#include "engine/resources/Content.hpp"

namespace tryengine::resources {

class PakManager {
public:
    PakManager(AsyncFileManager& file_manager, AssetRegistry& registry)
        : file_manager_(file_manager), registry_(registry) {}

    ~PakManager() {
        std::lock_guard lock(mutex_);
        for (const auto& record : pak_records_)
            if (record.fd_slot != UINT32_MAX)
                file_manager_.CloseFDAsync(record.fd_slot);
        file_manager_.Submit();
    }

    PakManager(const PakManager&) = delete;
    PakManager& operator=(const PakManager&) = delete;

    // Монтирует все паки из content/ и ждёт их. Игра вызывает это до старта скриптов:
    // иначе первый же кадр просит ассет, которого ещё нет в реестре.
    async::Task<void> LoadAllAsync() {
        eastl::vector<eastl::string> pak_paths;

        filesystem::ReadDirectory("content/", [&](eastl::string_view name, filesystem::EntryKind kind) {
            // Пропускаем директории и другие записи, оставляем только файлы
            if (kind != filesystem::EntryKind::File)
                return;

            if (name.size() < 4 || std::memcmp(name.data() + name.size() - 4, ".pak", 4) != 0) {
                LogTrace(LogCategory::Importer, "Unsupported type in content");
                return;
            }

            // Формируем полный путь, т.к. ReadDirectory отдаёт только имя файла
            char fullPath[filesystem::kMaxPath];
            std::snprintf(fullPath, sizeof(fullPath), "content/%.*s", static_cast<int>(name.size()), name.data());
            pak_paths.emplace_back(fullPath);
        });

        if (pak_paths.empty()) {
            LogWarn("PakManager: no .pak files under content/");
            co_return {};
        }

        eastl::vector<async::Task<PakHandle>> tasks;
        tasks.reserve(pak_paths.size());
        for (const auto& path : pak_paths)
            tasks.push_back(LoadPakAsync(path));

        auto results = co_await async::WhenAll(async::ThreadPool(), eastl::move(tasks));
        if (!results.has_value())
            co_return LogAndMakeError("Failed to load content paks: {}", results.error().Message());

        for (size_t i = 0; i < results->size(); ++i)
            if (!(*results)[i].has_value())
                co_return LogAndMakeError("Failed to load pak '{}': {}", pak_paths[i].c_str(),
                                          (*results)[i].error().Message());

        co_return {};
    }

    // Fire-and-forget вариант для редактора и тестов; игра ждёт LoadAllAsync.
    void StartLoadAll() {
        async::RunAndForget(async::ThreadPool(), LoadAllAsync());
    }

    async::Task<PakHandle> LoadPakAsync(const eastl::string pak_path) {
        // 1. Проверка на уже смонтированный PAK
        {
            std::lock_guard lock(mutex_);
            auto it = mounted_paks_by_path_.find(pak_path);
            if (it != mounted_paks_by_path_.end())
                co_return it->second;
        }

        // 2. Асинхронно открываем дескриптор файла в io_uring
        FileHandle open_handle = file_manager_.OpenAsyncCopy(pak_path, O_RDONLY);
        co_await open_handle;

        if (open_handle.IsFailed() || !open_handle.IsValid())
            co_return LogAndMakeError("Failed to open PAK file: {}", pak_path.c_str());

        const uint32_t fd_slot = open_handle.GetSlotIndex();

        // 3. Чтение заголовка по открытому fd_slot без повторного открытия файла
        FileHandle header_handle = file_manager_.ReadChunkFDAsync(fd_slot, 0, sizeof(AssetPakHeader));
        co_await header_handle;

        if (header_handle.IsFailed()) {
            auto close_handle = file_manager_.CloseFDAsync(fd_slot);
            co_await close_handle;
            co_return LogAndMakeError("Failed to read header for PAK: {}", pak_path.c_str());
        }

        auto header_res = AssetPakBinary::ReadHeader(header_handle.GetData());
        if (!header_res.has_value()) {
            auto close_handle = file_manager_.CloseFDAsync(fd_slot);
            co_await close_handle;
            co_return LogAndMakeError("Failed AssetPakBinary::ReadHeader {}", header_res.error().Message());
        }

        const auto header = *header_res;

        // 4. Чтение манифеста по открытому fd_slot
        uint64_t manifest_body_size = header.data_offset - sizeof(AssetPakHeader);
        FileHandle manifest_handle = file_manager_.ReadChunkFDAsync(fd_slot, 0, header.data_offset);
        co_await manifest_handle;

        if (manifest_handle.IsFailed()) {
            auto close_handle = file_manager_.CloseFDAsync(fd_slot);
            co_await close_handle;
            co_return LogAndMakeError("Failed to read manifest block for PAK: {}", pak_path.c_str());
        }

        auto meta_res = AssetPakBinary::UnpackManifest(manifest_handle.GetData());
        if (!meta_res.has_value()) {
            auto close_handle = file_manager_.CloseFDAsync(fd_slot);
            co_await close_handle;
            co_return LogAndMakeError("Error AssetPakBinary::UnpackManifest - {}", meta_res.error().Message());
        }

        const auto& manifest = *meta_res;

        // 5. Параллельная подгрузка зависимостей
        if (!manifest.dependencies.empty()) {
            eastl::vector<async::Task<PakHandle>> dep_tasks;
            dep_tasks.reserve(manifest.dependencies.size());

            for (const auto& dep_path : manifest.dependencies)
                dep_tasks.push_back(LoadPakAsync(dep_path));

            auto results_r = co_await async::WhenAll(async::ThreadPool(), std::move(dep_tasks));

            if (!results_r.has_value()) {
                LogError("Error loading dependencies for PAK {}: {}", pak_path.c_str(), results_r.error());
            } else {
                auto& results = *results_r;
                for (size_t i = 0; i < results.size(); ++i) {
                    if (!results[i].has_value()) {
                        auto close_handle = file_manager_.CloseFDAsync(fd_slot);
                        co_await close_handle;
                        co_return LogAndMakeError("Failed dependency '{}' for PAK '{}': {}",
                                                  manifest.dependencies[i].c_str(), pak_path.c_str(),
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
                auto close_handle = file_manager_.CloseFDAsync(fd_slot);
                co_await close_handle;
                co_return it->second;
            }

            new_handle = static_cast<PakHandle>(pak_records_.size());
            pak_records_.push_back({pak_path, header.data_offset, fd_slot});
            mounted_paks_by_path_[pak_path] = new_handle;
        }

        // 7. Регистрация артефактов с сохранением fd_slot в PakChunkLocation
        for (const auto& entry : manifest.layout) {
            registry_.RegisterPakArtifact(entry.guid, PakChunkLocation{.pak_handle = new_handle,
                                                                       .fd_slot = fd_slot,
                                                                       .offset = header.data_offset + entry.offset,
                                                                       .size = entry.size});
        }

        // 8. Манифест content-файлов: в паке нет каталогов, поэтому листинг приходит
        // отдельной записью. Игра читает его и наполняет кеш префабов.
        for (const auto& entry : manifest.layout) {
            if (entry.guid != kContentManifestId)
                continue;

            FileHandle content_manifest_handle = file_manager_.ReadChunkFDAsync(
                fd_slot, header.data_offset + entry.offset, static_cast<uint32_t>(entry.size));
            co_await content_manifest_handle;

            if (content_manifest_handle.IsFailed()) {
                LogError("Failed to read content manifest from PAK '{}'", pak_path.c_str());
                break;
            }

            const auto data = content_manifest_handle.GetData();
            registry_.AppendContentManifest(
                eastl::string_view(reinterpret_cast<const char*>(data.data()), data.size()));
            break;
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

}  // namespace tryengine::resources