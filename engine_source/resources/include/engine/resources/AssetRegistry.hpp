#pragma once

#include <EASTL/hash_map.h>
#include <EASTL/optional.h>
#include <mutex>

#include "engine/core/strings/Hash.hpp"
#include "engine/resources/AssetLocation.hpp"
#include "engine/resources/AssetRef.hpp"

namespace tryengine::resources {

class IAssetMissHandler {
public:
    virtual ~IAssetMissHandler() = default;
    virtual bool Defer(uint64_t guid, eastl::function<void()> retry) = 0;
    virtual void OnRegistered(uint64_t guid) = 0;
};

// Runtime тип, который используется в билде, в отличие от asset pipeline.
// Идентичность ресурса — uint64: у импортируемого ассета это guid из .meta, у
// авторитетного файла — PathId(путь). Путь в редакторе переводится в id только здесь.
class AssetRegistry {
public:
    // Авторитетный файл (content/data): у него нет .meta и guid, путь и есть его
    // идентичность. Идемпотентно: повторная регистрация даёт тот же id.
    uint64_t RegisterAuthoritative(eastl::string_view path) {
        const uint64_t id = core::PathId(path);
        {
            std::lock_guard lock(mutex_);
            locations_[id] = LooseFileLocation{eastl::string(path.data(), path.size()), AssetOrigin::Authoritative};
            path_index_[id] = id;
        }
        if (miss_handler_)
            miss_handler_->OnRegistered(id);
        return id;
    }

    void RegisterLooseArtifact(uint64_t guid, eastl::string path) {
        // LogTrace("Loose asset {} registred, {}", guid, path);
        {
            std::lock_guard lock(mutex_);
            path_index_[core::PathId(eastl::string_view(path.data(), path.size()))] = guid;
            locations_[guid] = LooseFileLocation{std::move(path), AssetOrigin::Imported};
        }
        if (miss_handler_)
            miss_handler_->OnRegistered(guid);
    }

    // Путь -> id. Нужен редактору, чтобы резолвить авторские ссылки, не зная заранее,
    // авторитетный это файл или артефакт импорта.
    eastl::optional<uint64_t> ResolvePath(eastl::string_view path) const {
        std::lock_guard lock(mutex_);
        auto it = path_index_.find(core::PathId(path));
        if (it == path_index_.end())
            return eastl::nullopt;
        return it->second;
    }

    // Авторская ссылка -> id: "guid:<u64>" самодостаточен, всё остальное — по пути.
    // nullopt — ссылка не распознана (битый формат guid или неизвестный путь).
    eastl::optional<uint64_t> ResolveRef(eastl::string_view ref) const {
        uint64_t id = 0;
        if (IsGuidRef(ref)) {
            // guid самодостаточен: ресурс может быть ещё не зарегистрирован (импорт идёт
            // асинхронно), и RequestLoad отложится через miss handler, а не потеряется.
            if (!ParseGuidRef(ref, id) || id == 0)
                return eastl::nullopt;
            return id;
        }
        return ResolvePath(ref);
    }

    // id -> авторская ссылка. Авторитетный файл отдаётся путём, всё остальное — guid:
    // так сохранённая сцена не зависит от раскладки artifacts/.
    eastl::optional<eastl::string> AssetRefOf(uint64_t id) const {
        if (id == 0)
            return eastl::nullopt;
        std::lock_guard lock(mutex_);
        auto it = locations_.find(id);
        if (it != locations_.end()) {
            if (const auto* loose = eastl::get_if<LooseFileLocation>(&it->second))
                if (loose->origin == AssetOrigin::Authoritative)
                    return loose->path;
        }
        return MakeGuidRef(id);
    }

    // Импортируемый ресурс (в том числе запечённый в pak) — производный, его нельзя править
    // как авторитетный файл.
    bool IsImported(uint64_t id) const {
        std::lock_guard lock(mutex_);
        auto it = locations_.find(id);
        if (it == locations_.end())
            return false;
        if (const auto* loose = eastl::get_if<LooseFileLocation>(&it->second))
            return loose->origin == AssetOrigin::Imported;
        return true;
    }

    void RegisterPakArtifact(uint64_t guid, const PakChunkLocation& location) {
        // LogTrace("Pak asset {} registred", guid);
        {
            std::lock_guard lock(mutex_);
            locations_[guid] = location;
        }
        if (miss_handler_)
            miss_handler_->OnRegistered(guid);
    }

    eastl::optional<AssetLocation> GetAssetLocation(uint64_t guid) const {
        std::lock_guard lock(mutex_);
        auto it = locations_.find(guid);
        if (it != locations_.end())
            return it->second;
        return eastl::nullopt;
    }

    // Вызывать до старта потоков. В game не вызывается.
    void SetMissHandler(IAssetMissHandler* h) {
        miss_handler_ = h;
    }

    bool TryDefer(uint64_t guid, eastl::function<void()> retry) {
        return miss_handler_ && miss_handler_->Defer(guid, std::move(retry));
    }

private:
    mutable std::mutex mutex_;
    eastl::hash_map<uint64_t, AssetLocation> locations_;
    // PathId(путь) -> id. Авторитетные файлы индексируются под своим id, артефакты
    // импорта — под путём артефакта, чтобы редактор мог найти ресурс по любому пути.
    eastl::hash_map<uint64_t, uint64_t> path_index_;
    IAssetMissHandler* miss_handler_ = nullptr;
};

}  // namespace tryengine::resources