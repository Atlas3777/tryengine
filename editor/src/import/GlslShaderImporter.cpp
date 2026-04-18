#include <cereal/archives/json.hpp>
#include <fstream>
#include <iostream>
#include <random>

#include "editor/import/GlslShaderImporter.hpp"
#include "editor/meta/ModelAssetMap.hpp"  // Используем ту же структуру для кэша

namespace tryeditor {

uint64_t GlslShaderImporter::GenerateMeta(const std::filesystem::path& assetPath, const std::filesystem::path& metaPath) {
    AssetMetaHeader header{};
    static std::random_device rd;
    static std::mt19937_64 gen(rd());
    static std::uniform_int_distribution<uint64_t> dis;

    header.guid = dis(gen);
    header.asset_type = "glsl_shader";
    header.importer_type = "native";

    std::ofstream os(metaPath);
    if (os.is_open()) {
        cereal::JSONOutputArchive archive(os);
        archive(cereal::make_nvp("header", header));
    }
    return header.guid;
}

AssetMetaHeader GlslShaderImporter::ReadIdentification(const std::filesystem::path& metaPath) {
    AssetMetaHeader header{};
    std::ifstream is(metaPath);
    if (is.is_open()) {
        cereal::JSONInputArchive archive(is);
        archive(cereal::make_nvp("header", header));
    }
    return header;
}

bool GlslShaderImporter::GenerateArtifact(const std::filesystem::path& assetPath, const std::filesystem::path& metaPath,
                                      const std::filesystem::path& artifactDir,
                                      const std::filesystem::path& cacheDir,
                                      const std::filesystem::path& projectAssetsDir) {

    AssetMetaHeader header = ReadIdentification(metaPath);

    // Имя выходного файла артефакта
    std::string spvName = std::to_string(header.guid) + ".spv";
    std::filesystem::path outputPath = artifactDir / spvName;

    // 1. Компиляция
    if (!CompileGLSLToSPIRV(assetPath, outputPath)) {
        std::cerr << "[ShaderImporter] Failed to compile: " << assetPath << std::endl;
        return false;
    }


    std::cout << "[ShaderImporter] Compiled " << assetPath.filename() << " -> " << spvName << std::endl;
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

} // namespace tryeditor