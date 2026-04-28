#pragma once
#include <vector>

#include "editor/import/IAssetImporter.hpp"

namespace tryeditor {

struct GlslShaderImportSettings {
    bool a;
    template<class Archive>
        void serialize(Archive& archive) {
        archive(cereal::make_nvp("test", a));
    }
};

class GlslShaderImporter : public BaseTypedImporter<GlslShaderImportSettings> {
public:
    std::string GetName() const override { return "ShaderSourceImporter"; }
    std::string GetAssetType() const override { return "glsl_shader"; }

    bool GenerateArtifact(const AssetContext& asset_context, const GlslShaderImportSettings& settings) override;

private:
    // Вспомогательная функция для запуска внешнего процесса (glslangValidator)
    bool CompileGLSLToSPIRV(const std::filesystem::path& input, const std::filesystem::path& output);
};

}  // namespace tryeditor