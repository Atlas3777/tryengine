#pragma once
#include "IAssetImporter.hpp"

namespace editor {
class GltfImporter : public IAssetImporter {
public:
    uint64_t GenerateMeta(const std::filesystem::path& asset_path, const std::filesystem::path& meta_path) override;
    AssetHeader ReadIdentification(const std::filesystem::path& metaPath) override;
    bool GenerateArtifact(const std::filesystem::path& assetPath, const std::filesystem::path& metaPath,
                                       const std::filesystem::path& artifactDir,
                                       const std::filesystem::path& cacheDir,
                                       const std::filesystem::path& projectAssetsDir) override;
};
}  // namespace editor