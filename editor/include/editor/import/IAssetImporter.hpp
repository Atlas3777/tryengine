#pragma once
#include <filesystem>
#include <string>

#include "editor/meta/AssetMetaHeader.hpp"

namespace tryeditor {

class IAssetImporter {
public:
    virtual ~IAssetImporter() = default;

    virtual std::string GetName() const = 0;

    virtual uint64_t GenerateMeta(const std::filesystem::path& assetPath, const std::filesystem::path& metaPath) = 0;

    virtual AssetMetaHeader ReadIdentification(const std::filesystem::path& metaPath) = 0;

    virtual bool GenerateArtifact(const std::filesystem::path& assetPath, const std::filesystem::path& metaPath,
                                  const std::filesystem::path& artifactDir,
                                  const std::filesystem::path& cacheDir,
                                  const std::filesystem::path& projectAssetsDir) = 0;

    // --- Управление иерархией ---
    // Переопредели и верни true в GltfImporter
    virtual bool HasHierarchy() const { return false; }

    // Вся логика генерации hierarchy.json теперь живет в конкретном импортере
    virtual void GenerateHierarchy(const std::filesystem::path& assetPath, const std::filesystem::path& cacheDir) {}
};

}  // namespace tryeditor