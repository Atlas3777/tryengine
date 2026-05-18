#pragma once

#include "editor/asset_factories/IAssetFactory.hpp"
#include "engine/graphics/MaterialSystem.hpp"

namespace tryeditor {

class ShaderAssetFactory : public BaseAssetFactory<tryengine::graphics::ShaderAsset> {
public:
    explicit ShaderAssetFactory(ImportSystem& import_system) : import_system_(import_system) {}

    [[nodiscard]] std::string GetAssetType() const override { return "shader"; }
    [[nodiscard]] std::string GetActionName() const override { return "Shader Asset"; }
    [[nodiscard]] std::string GetExtension() const override { return ".shader"; }
    [[nodiscard]] std::string GetDefaultName() const override {return "new_shader";};
    [[nodiscard]] tryengine::graphics::ShaderAsset CreateDefaultAsset() const override {
        tryengine::graphics::ShaderAsset a{};
        return a;
    }

private:
    ImportSystem& import_system_;
};

}  // namespace tryeditor