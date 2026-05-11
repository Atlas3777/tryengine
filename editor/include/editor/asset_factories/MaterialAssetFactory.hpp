#pragma once
#include <filesystem>

#include "editor/asset_factories/IAssetFactory.hpp"
#include "editor/import/ImportSystem.hpp"
#include "editor/meta/MetaSerializer.hpp"
#include "engine/resources/MaterialAssetData.hpp"
#include "engine/core/RandomUtil.hpp"

namespace tryeditor {

class MaterialAssetFactory : public IAssetFactory {
public:
    explicit MaterialAssetFactory(ImportSystem& import_system) : import_system_(import_system) {}

    uint64_t CreateDefault(const std::filesystem::path& directory) override {
        tryengine::resources::MaterialAssetData def;
        def.name = "New Material";
        def.shader_asset_id = 0;
        return Create(directory, "NewMaterial.mat", def);
    }

    uint64_t Create(const std::filesystem::path& directory, const std::string& name,
                    const tryengine::resources::MaterialAssetData& data) {
        std::string filename = name;
        if (filename.find(".mat") == std::string::npos) filename += ".mat";

        const std::filesystem::path asset_path = directory / filename ;
        AssetMetaHeader header = CreateMeta(asset_path);
        import_system_.SaveNativeAsset(asset_path, data, header);

        return header.guid;
    }

    [[nodiscard]] std::string GetActionName() const override { return "Material Asset"; }

private:
    AssetMetaHeader CreateMeta(const std::filesystem::path& asset_path) {
        std::filesystem::path meta_path = asset_path.string() + ".meta";

        AssetMetaHeader header;
        header.guid = tryengine::core::RandomUtil::GenerateInt64();
        header.importer_type = "native";
        header.asset_type = "material";

        MetaSerializer::WriteHeaderOnly(meta_path, header);

        return header;
    }

    ImportSystem& import_system_;
};

} // namespace tryeditor