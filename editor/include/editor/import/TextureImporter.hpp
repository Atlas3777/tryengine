#pragma once
#include <filesystem>

#include "editor/import/IAssetImporter.hpp"

namespace tryeditor {

class TextureImporter : public IAssetImporter {
public:
    std::string GetName() const override { return "TextureImporter"; };
    bool HasHierarchy() const override { return false; };

    uint64_t GenerateMeta(const std::filesystem::path& assetPath, const std::filesystem::path& metaPath) override;

    AssetMetaHeader ReadIdentification(const std::filesystem::path& metaPath) override;

    bool GenerateArtifact(const std::filesystem::path& assetPath, const std::filesystem::path& metaPath,
                          const std::filesystem::path& artifact_dir, const std::filesystem::path& cacheDir,
                          const std::filesystem::path& projectAssetsDir) override;
};

}  // namespace tryeditor