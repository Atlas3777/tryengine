#include "editor/import/GlslShaderImporter.hpp"

#include <cereal/archives/json.hpp>
#include <fstream>
#include <iostream>
#include <random>

#include "editor/meta/MetaSerializer.hpp"

namespace tryeditor {

bool GlslShaderImporter::GenerateArtifact(const AssetContext& asset_context, AssetMetaHeader& header, const GlslShaderImportSettings& settings) {

    std::string guid_str = std::to_string(header.guid);
    std::filesystem::path asset_artifact_dir = asset_context.artifacts_dir / guid_str;

    if (!std::filesystem::exists(asset_artifact_dir)) {
        std::filesystem::create_directories(asset_artifact_dir);
    }

    std::string spv_filename = guid_str + ".spv";
    std::filesystem::path output_path = asset_artifact_dir / spv_filename;

    // 4. Компиляция
    if (!CompileGLSLToSPIRV(asset_context.asset_path, output_path)) {
        std::cerr << "[ShaderImporter] Failed to compile: " << asset_context.asset_path << std::endl;
        return false;
    }

    std::cout << "[ShaderImporter] Compiled " << asset_context.asset_path.filename() << " -> "
              << output_path.relative_path() << std::endl;

    return true;
}

bool GlslShaderImporter::CompileGLSLToSPIRV(const std::filesystem::path& input, const std::filesystem::path& output) {
    // Нам нужно вызвать glslangValidator.
    // Убедись, что он есть в PATH или укажи полный путь.
    // Флаг -V означает генерацию SPIR-V
    std::string command = "glslangValidator -V \"" + input.string() + "\" -o \"" + output.string() + "\"";

    int result = std::system(command.c_str());
    return result == 0;
}

}  // namespace tryeditor