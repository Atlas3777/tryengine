#pragma once
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <string>
#include <unordered_map>
#include <vector>

namespace tryengine::core {

using AssetID = uint64_t;

class AssetDatabase {
public:
    AssetDatabase() = default;

    void Refresh();

    std::string GetPath(AssetID id) const {
        auto it = id_to_path_.find(id);

        if (it == id_to_path_.end()) {
            std::cerr << "ASSET DATABASE: Warning: Path to asset with id = " << id << " not found!" << std::endl;
            return "";
        }

        return it->second;
    }

private:
    const std::filesystem::path root_path_ = std::filesystem::current_path();

    // Массив директорий для поиска артефактов
    const std::vector<std::filesystem::path> artifacts_dirs_ = {
        root_path_ / "game" / "artifacts",
        root_path_ / "engine_content" / "artifacts"
    };

    std::unordered_map<AssetID, std::string> id_to_path_;
};

} // namespace tryengine::core