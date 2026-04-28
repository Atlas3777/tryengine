#pragma once
#include <filesystem>

namespace tryeditor {

struct AssetContext {
    std::filesystem::path asset_path;
    std::filesystem::path meta_path;
    std::filesystem::path project_assets_dir;  // game/assets или engine_content/assets
    std::filesystem::path artifacts_dir;       // Куда класть артефакты
    std::filesystem::path cache_dir;           // Куда класть кэш
};
}  // namespace editor