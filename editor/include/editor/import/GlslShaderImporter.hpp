#pragma once
#include <vector>

#include "editor/import/IAssetImporter.hpp"

namespace tryeditor {

class GlslShaderImporter : public IAssetImporter {
public:
    std::string GetName() const override { return "ShaderSourceImporter"; }

    uint64_t GenerateMeta(const std::filesystem::path& assetPath, const std::filesystem::path& metaPath) override;

    AssetMetaHeader ReadIdentification(const std::filesystem::path& metaPath) override;

    bool GenerateArtifact(const std::filesystem::path& assetPath, const std::filesystem::path& metaPath,
                          const std::filesystem::path& artifactDir, const std::filesystem::path& cacheDir,
                          const std::filesystem::path& projectAssetsDir) override;

    bool HasHierarchy() const override { return false; };

private:
    // Вспомогательная функция для запуска внешнего процесса (glslangValidator)
    bool CompileGLSLToSPIRV(const std::filesystem::path& input, const std::filesystem::path& output);
};

}  // namespace tryeditor