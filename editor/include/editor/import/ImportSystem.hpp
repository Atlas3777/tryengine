#pragma once
#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include <optional>

#include "IAssetImporter.hpp"

namespace tryeditor {

class ImportSystem {
public:
    // Создаем ОДИН инстанс импортера для списка расширений
    template <typename T, typename... Args>
    void RegisterImporter(const std::vector<std::string>& extensions, Args&&... args) {
        auto importer = std::make_unique<T>(std::forward<Args>(args)...);
        std::string name = importer->GetName();
        IAssetImporter* ptr = importer.get();

        importers_.push_back(std::move(importer));
        importers_by_name_[name] = ptr;

        for (const auto& ext : extensions) {
            importers_by_ext_[ext] = ptr;
        }
    }

    void Refresh();

    uint64_t GetId(const std::string& path) const { return path_to_id_.at(path); }

    std::filesystem::path GetHierarchyPath(const uint64_t id) const {
        auto p = cache_directory_;
        p /= std::to_string(id);
        p /= "hierarchy.json";
        return p;
    }

    IAssetImporter* GetImporterByName(const std::string& name) const {
        auto it = importers_by_name_.find(name);
        return it != importers_by_name_.end() ? it->second : nullptr;
    }

    // Вспомогательный метод для "подглядывания" в .meta без парсинга всего файла
    std::optional<AssetMetaHeader> PeekMetaHeader(const std::filesystem::path& meta_path);
    void DeleteAsset(const std::filesystem::path& asset_path);
    void DeleteDirectory(const std::filesystem::path& dir_path);

private:
    void DeleteArtifactsAndCache(uint64_t id);


    const std::filesystem::path root_path_ = std::filesystem::current_path() / "game";
    const std::filesystem::path assets_directory_ = root_path_ / "assets";
    const std::filesystem::path artefacts_directory_ = root_path_ / "artefacts";
    const std::filesystem::path cache_directory_ = root_path_ / ".cache";

    // Хранение владельцев (unique_ptr)
    std::vector<std::unique_ptr<IAssetImporter>> importers_;

    // Быстрый доступ (raw pointers)
    std::unordered_map<std::string, IAssetImporter*> importers_by_ext_;
    std::unordered_map<std::string, IAssetImporter*> importers_by_name_;

    std::unordered_map<uint64_t, std::string> id_to_path_;
    std::unordered_map<std::string, uint64_t> path_to_id_;
};

}  // namespace tryeditor