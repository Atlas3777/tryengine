#pragma once
#include <filesystem>

#include "editor/import/IAssetImporter.hpp"

namespace tryeditor {

class ImageImporter : public IAssetImporter {
public:
    uint64_t GenerateMeta(const std::filesystem::path& assetPath, const std::filesystem::path& metaPath) override;

    AssetHeader ReadIdentification(const std::filesystem::path& metaPath) override;

    bool GenerateArtifact(const std::filesystem::path& assetPath, const std::filesystem::path& metaPath,
                          const std::filesystem::path& artifactDir, const std::filesystem::path& cacheDir,
                          const std::filesystem::path& projectAssetsDir) override;
};

}  // namespace tryeditor