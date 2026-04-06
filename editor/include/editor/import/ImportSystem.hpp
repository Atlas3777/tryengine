#pragma once
#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>

#include "IAssetImporter.hpp"

namespace editor {

class ImportSystem {
public:
    void Refresh();

    template <typename T>
    void RegisterImporter(const std::string& extension) {
        importers_[extension] = std::make_unique<T>();
    }

    uint64_t GetId(const std::string& path) const { return path_to_id_.at(path); };

    std::filesystem::path GetHierarchyPath(uint64_t id) const {
        auto p = cache_directory_;
        p /= std::to_string(id);
        p /= "hierarchy.json";
        return p;
    }

private:
    const std::filesystem::path root_path_ = std::filesystem::current_path() / "game";
    const std::filesystem::path assets_directory_ = root_path_ / "assets";
    const std::filesystem::path artefacts_directory_ = root_path_ / "artefacts";
    const std::filesystem::path cache_directory_ = root_path_ / ".cache";

    // Исправлено: храним интерфейсы через unique_ptr
    std::unordered_map<std::string, std::unique_ptr<IAssetImporter>> importers_;

    // Словари для хранения путей (для Asset Browser'a)
    std::unordered_map<uint64_t, std::string> id_to_path_;
    std::unordered_map<std::string, uint64_t> path_to_id_;
};

}  // namespace editor