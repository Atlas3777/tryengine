#pragma once
#include <cereal/archives/binary.hpp>
#include <cereal/archives/json.hpp>
#include <fstream>
#include <random>

#include "editor/asset_factories/IAssetFactory.hpp"
#include "editor/import/ImportSystem.hpp"
#include "engine/graphics/MaterialSystem.hpp"

namespace tryeditor {

class ShaderAssetFactory : public IAssetFactory {
public:
    explicit ShaderAssetFactory(ImportSystem& import_system) : import_system_(import_system) {}

    uint64_t CreateDefault(const std::filesystem::path& directory) override {
        tryengine::graphics::ShaderAsset default_shader;
        return Create(directory, "NewShader.shader", default_shader);
    }

    uint64_t Create(const std::filesystem::path& directory, const std::string& name,
                    const tryengine::graphics::ShaderAsset& def) {
        const std::filesystem::path asset_path = directory / name;

        AssetMetaHeader header = CreateMeta(asset_path);
        import_system_.SaveNativeAsset<tryengine::graphics::ShaderAsset>(asset_path, def, header);

        import_system_.Refresh();

        return header.guid;
    }

    [[nodiscard]] std::string GetActionName() const override { return "Shader Asset"; }

private:
    AssetMetaHeader CreateMeta(const std::filesystem::path& asset_path) {
        const std::filesystem::path meta_path = asset_path.string() + ".meta";

        std::random_device rd;
        std::mt19937_64 id(rd());
        const uint64_t asset_id = id();

        AssetMetaHeader asset_header;
        asset_header.guid = asset_id;
        asset_header.importer_type = "native";
        asset_header.asset_type = "shader";

        MetaSerializer::WriteHeaderOnly(meta_path, asset_header);
        return asset_header;
    }


    ImportSystem& import_system_;
};

}  // namespace tryeditor