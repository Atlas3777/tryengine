#include "engine/core/AssetDatabase.hpp"
#include <iostream>
#include <charconv>

namespace tryengine::core {

void AssetDatabase::Refresh() {
    id_to_path_.clear();

    if (!std::filesystem::exists(artifacts_dir_)) {
        return;
    }

    // Проходим по папкам GUID (например, Game/Artifacts/12345...)
    for (const auto& guidDirEntry : std::filesystem::directory_iterator(artifacts_dir_)) {
        if (!guidDirEntry.is_directory()) continue;

        // Проходим по файлам внутри папки GUID
        for (const auto& directory_entry : std::filesystem::directory_iterator(guidDirEntry.path())) {
            if (!directory_entry.is_regular_file()) continue;

            const auto& filePath = directory_entry.path();

            // Получаем имя файла без расширения (это наш AssetID)
            std::string fileName = filePath.stem().string();

            AssetID assetId = 0;
            auto [ptr, ec] = std::from_chars(fileName.data(), fileName.data() + fileName.size(), assetId);

            if (ec == std::errc()) {
                // Регистрируем путь относительно корня проекта
                std::filesystem::path relPath = std::filesystem::relative(filePath, root_path_);
                id_to_path_[assetId] = relPath.string();
            } else {
                // Если имя файла — не число, пропускаем или логируем ошибку
                std::cerr << "Warning: File " << fileName << " is not a valid AssetID (not a number)" << std::endl;
            }
        }
    }

    // // Отладка
    // std::cout << "--- Registered Assets ---" << std::endl;
    // for (const auto& [id, path] : id_to_path_) {
    //     std::cout << "ID: " << id << " -> " << path << std::endl;
    // }
}
} // namespace tryengine::core