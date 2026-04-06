#pragma once
#include <filesystem>
#include <string>
#include <unordered_map>
#include <cstdint>

namespace engine::core {

using AssetID = uint64_t;

class AssetDatabase {
public:
    AssetDatabase() = default;

    void Refresh();

    // Безопасное получение пути
    std::string GetPath(AssetID id) const {
        auto it = id_to_path_.find(id);

        if (it == id_to_path_.end()) {
            std::cerr << "Warning: AssetID " << id << " not found!" << std::endl;
            return "";
        }

        std::cout << "Path: " << it->second << std::endl;
        return it->second;
    }

private:
    const std::filesystem::path root_path_ = std::filesystem::current_path();
    const std::filesystem::path artifacts_dir_ = "game/artefacts";

    std::unordered_map<AssetID, std::string> id_to_path_;
};

} // namespace engine::core