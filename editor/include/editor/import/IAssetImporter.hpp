#pragma once
#include <filesystem>

#include "editor/meta/AssetHeader.hpp"

namespace editor {
class IAssetImporter {
public:
    virtual ~IAssetImporter() = default;

    virtual uint64_t GenerateMeta(const std::filesystem::path& assetPath, const std::filesystem::path& metaPath) = 0;
    virtual AssetHeader ReadIdentification(const std::filesystem::path& metaPath) = 0;
    virtual bool GenerateArtifact(const std::filesystem::path& assetPath, const std::filesystem::path& metaPath,
                                    const std::filesystem::path& artifactDir,
                                    const std::filesystem::path& cacheDir,
                                    const std::filesystem::path& projectAssetsDir) = 0;
};
}  // namespace editor