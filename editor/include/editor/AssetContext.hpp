#pragma once
#include <filesystem>

namespace tryeditor {

struct AssetContext {
    std::filesystem::path asset_path;
    std::filesystem::path meta_path;

    std::filesystem::path project_assets_dir;
    std::filesystem::path artifacts_dir;
    std::filesystem::path cache_dir;

    // 1. Просто возвращает путь к папке (безопасный, ничего не создаёт)
    [[nodiscard]] std::filesystem::path GetArtifactDir(uint64_t main_guid) const {
        return artifacts_dir / std::to_string(main_guid);
    }

    // 2. Гарантирует создание папки и возвращает путь к ней
    std::filesystem::path EnsureArtifactDir(uint64_t main_guid) const {
        auto dir = GetArtifactDir(main_guid);
        if (!std::filesystem::exists(dir)) {
            std::filesystem::create_directories(dir);
        }
        return dir;
    }

    // 3. Возвращает путь к ФАЙЛУ главного артефакта (и создает под него папку)
    std::filesystem::path EnsureMainArtifactPath(uint64_t main_guid) const {
        return EnsureArtifactDir(main_guid) / std::to_string(main_guid);
    }

    // 4. Возвращает путь к ФАЙЛУ субассета внутри папки главного ассета
    std::filesystem::path EnsureSubArtifactPath(uint64_t main_guid, uint64_t sub_guid) const {
        return EnsureArtifactDir(main_guid) / std::to_string(sub_guid);
    }
};
}  // namespace editor