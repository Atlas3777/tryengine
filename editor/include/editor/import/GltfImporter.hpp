#pragma once

#include <filesystem>
#include <vector>

#include "editor/import/IAssetImporter.hpp"
#include "editor/meta/AssetMetaHeader.hpp"
#include "editor/meta/ModelAssetMap.hpp"

struct tg3_model;

namespace tryeditor {
class AssetsFactoryManager;

class GltfImporter : public IAssetImporter {
public:
    GltfImporter(AssetsFactoryManager& assets_factory) : assets_factory_(assets_factory) {};
    ~GltfImporter() override = default;

    std::string GetName() const override { return "GltfImporter"; }

    // --- IAssetImporter Interface ---
    uint64_t GenerateMeta(const std::filesystem::path& assetPath, const std::filesystem::path& metaPath) override;

    AssetMetaHeader ReadIdentification(const std::filesystem::path& meta_path) override;

    bool GenerateArtifact(const std::filesystem::path& asset_path, const std::filesystem::path& metaPath,
                          const std::filesystem::path& artifact_dir, const std::filesystem::path& cacheDir,
                          const std::filesystem::path& project_assets_dir) override;
    bool HasHierarchy() const override { return true; };

private:
    // --- Внутренние этапы импорта ---
    std::vector<uint64_t> ProcessMaterials(const tg3_model* m, uint64_t main_uuid,
                                           const std::filesystem::path& projectAssetsDir,
                                           const std::filesystem::path& assetStem, ModelAssetMap& asset_map);

    void ProcessTextures(const tg3_model* m, uint64_t main_uuid, const std::filesystem::path& artifactDir,
                         const std::filesystem::path& projectAssetsDir, const std::filesystem::path& assetStem,
                         ModelAssetMap& asset_map);

    void ProcessMeshes(const tg3_model* m, uint64_t main_uuid, const std::filesystem::path& artifactDir,
                       ModelAssetMap& asset_map, std::vector<std::vector<uint64_t>>& out_mesh_primitive_guids);

    void ProcessNodes(const tg3_model* m, const std::vector<std::vector<uint64_t>>& mesh_primitive_guids,
                      const std::vector<uint64_t>& material_guids, ModelAssetMap& asset_map);

    AssetsFactoryManager& assets_factory_;
};

}  // namespace tryeditor