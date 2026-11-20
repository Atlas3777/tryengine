#pragma once

#include <EASTL/algorithm.h>
#include <EASTL/hash_map.h>
#include <EASTL/optional.h>
#include <EASTL/sort.h>
#include <EASTL/string.h>
#include <EASTL/string_view.h>
#include <EASTL/vector.h>
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
        const uint64_t id = core::PathId(path);
        auto it = path_index_.find(id);
        if (it != path_index_.end())
            return it->second;

        // У content-файла id — это PathId(путь), и в паке он зарегистрирован под этим
        // id без пути. Таблица путей в паке не нужна: запрос по пути даёт тот же хеш.
        if (locations_.find(id) != locations_.end())
            return id;

        return eastl::nullopt;
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

    // Список авторитетных файлов, которые знает редактор. Пак листинга не хранит: у
    // content-файла id = PathId(путь), поэтому runtime находит его по хешу пути, а не по
    // таблице путей. Список нужен только сборщику редактора, чтобы обойти content/data.
    eastl::vector<eastl::string> AuthoritativePaths() const {
        std::lock_guard lock(mutex_);
        eastl::vector<eastl::string> paths;
        paths.reserve(locations_.size());

        for (const auto& entry : locations_) {
            if (const auto* loose = eastl::get_if<LooseFileLocation>(&entry.second))
                if (loose->origin == AssetOrigin::Authoritative)
                    paths.push_back(loose->path);
        }

        eastl::sort(paths.begin(), paths.end());
        paths.erase(eastl::unique(paths.begin(), paths.end()), paths.end());
        return paths;
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