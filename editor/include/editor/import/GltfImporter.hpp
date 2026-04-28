#pragma once

#include <filesystem>
#include <vector>

#include "editor/import/IAssetImporter.hpp"
#include "editor/meta/AssetMetaHeader.hpp"
#include "editor/meta/ModelAssetMap.hpp"

struct tg3_model;

namespace tryeditor {
class ImportSystem;
class AssetsFactoryManager;

struct GltfImportSettings {
    bool extract_materials = true;

    template <class Archive>
    void serialize(Archive& archive) {
        archive(cereal::make_nvp("extract_materials", extract_materials));
    }
};
class GltfImporter : public BaseTypedImporter<GltfImportSettings> {
public:
    GltfImporter(AssetsFactoryManager& assets_factory, ImportSystem& import_system)
        : assets_factory_(assets_factory), import_system_(import_system) {};
    ~GltfImporter() override = default;

    std::string GetName() const override { return "GltfImporter"; }
    std::string GetAssetType() const override { return "gltf"; }

    AssetMetaHeader GenerateMeta(const std::filesystem::path& asset_path, const std::filesystem::path& meta_path);

    bool GenerateArtifact(const AssetContext& asset_context, const GltfImportSettings& settings) override;

private:
    // --- Внутренние этапы импорта ---
    std::vector<uint64_t> ProcessMaterials(const tg3_model* m, uint64_t main_uuid,
                                           const std::filesystem::path& projectAssetsDir,
                                           const std::filesystem::path& assetStem, ModelAssetMap& asset_map);

    void ProcessTextures(const tg3_model* m, uint64_t main_uuid, const std::filesystem::path& artifactDir,
                         const std::filesystem::path& projectAssetsDir, const std::filesystem::path& assetStem,
                         ModelAssetMap& asset_map);

    std::vector<std::vector<uint64_t>> ProcessMeshes(const tg3_model* m, uint64_t main_uuid,
                                                                   const std::filesystem::path& artifact_dir,
                                                                   ModelAssetMap& asset_map);

    void ProcessNodes(const tg3_model* m, const std::vector<std::vector<uint64_t>>& mesh_primitive_guids,
                      const std::vector<uint64_t>& material_guids, ModelAssetMap& asset_map);

    AssetsFactoryManager& assets_factory_;
    ImportSystem& import_system_;
};

}  // namespace tryeditor