#pragma once

#include <filesystem>
#include <vector>
#include "editor/import/IAssetImporter.hpp"
#include "editor/meta/AssetHeader.hpp"
#include "editor/meta/ModelAssetMap.hpp"

struct tg3_model;

namespace tryeditor {

class GltfImporter : public IAssetImporter {
public:
    GltfImporter() = default;
    ~GltfImporter() override = default;

    // --- IAssetImporter Interface ---
    uint64_t GenerateMeta(const std::filesystem::path& assetPath,
                          const std::filesystem::path& metaPath) override;

    AssetHeader ReadIdentification(const std::filesystem::path& metaPath) override;

    bool GenerateArtifact(const std::filesystem::path& assetPath,
                          const std::filesystem::path& metaPath,
                          const std::filesystem::path& artifactDir,
                          const std::filesystem::path& cacheDir,
                          const std::filesystem::path& projectAssetsDir) override;

private:
    // --- Внутренние этапы импорта ---
    void ProcessMaterials(const tg3_model* m, uint64_t main_uuid,
                          const std::filesystem::path& projectAssetsDir,
                          const std::filesystem::path& assetStem);

    void ProcessTextures(const tg3_model* m, uint64_t main_uuid,
                                   const std::filesystem::path& artifactDir,
                                   const std::filesystem::path& projectAssetsDir,
                                   const std::filesystem::path& assetStem,
                                   ModelAssetMap& asset_map);

    void ProcessMeshes(const tg3_model* m, uint64_t main_uuid,
                       const std::filesystem::path& artifactDir,
                       ModelAssetMap& asset_map,
                       std::vector<std::vector<uint64_t>>& out_mesh_primitive_guids);

    void ProcessNodes(const tg3_model* m,
                      const std::vector<std::vector<uint64_t>>& mesh_primitive_guids,
                      ModelAssetMap& asset_map);
};

}  // namespace tryeditor