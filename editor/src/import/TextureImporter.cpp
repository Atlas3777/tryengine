#include "editor/import/TextureImporter.hpp"

#include <cereal/archives/json.hpp>
#include <fstream>
#include <iostream>
#include <random>

#include "editor/meta/TextureImportSettings.hpp"
#include "engine/resources/Types.hpp"
#include "stb_image.h"

namespace tryeditor {

uint64_t TextureImporter::GenerateMeta(const std::filesystem::path& assetPath, const std::filesystem::path& metaPath) {
    AssetMetaHeader header;
    static std::random_device rd;
    static std::mt19937_64 gen(rd());
    static std::uniform_int_distribution<uint64_t> dis;

    TextureImportSettings settings;

    header.guid = dis(gen);
    header.asset_type = "texture";
    header.importer_type = this->GetName();



    try {
        std::ofstream os(metaPath);
        if (os.is_open()) {
            cereal::JSONOutputArchive archive(os);
            archive(cereal::make_nvp("header", header));
            archive(cereal::make_nvp("settings", settings));
        } else {
            return 0;
        }
    } catch (const std::exception& e) {
        std::cerr << "[TextureImporter] Failed to save meta: " << e.what() << std::endl;
        return 0;
    }
    return header.guid;
}

AssetMetaHeader TextureImporter::ReadIdentification(const std::filesystem::path& metaPath) {
    AssetMetaHeader header{};
    if (!std::filesystem::exists(metaPath))
        return header;

    try {
        std::ifstream is(metaPath);
        cereal::JSONInputArchive archive(is);
        archive(cereal::make_nvp("header", header));
    } catch (...) {
    }
    return header;
}

bool TextureImporter::GenerateArtifact(const std::filesystem::path& assetPath, const std::filesystem::path& metaPath,
                                     const std::filesystem::path& artifact_dir, const std::filesystem::path& cacheDir,
                                     const std::filesystem::path& projectAssetsDir) {
    AssetMetaHeader header = ReadIdentification(metaPath);
    if (header.guid == 0)
        return false;

    int width, height, channels;
    // Загружаем как RGBA (4 канала) для единообразия в движке
    unsigned char* pixels = stbi_load(assetPath.string().c_str(), &width, &height, &channels, 4);

    if (!pixels) {
        std::cerr << "[TextureImporter] STBI fail: " << stbi_failure_reason() << " for " << assetPath << std::endl;
        return false;
    }

    // Создаем директорию для артефакта, если её нет (обычно делает ImportSystem)
    std::filesystem::create_directories(artifact_dir);

    // Имя файла артефакта. Можно оставить расширение .tex, как в GLTF текстурах
    std::string artifactName = std::to_string(header.guid) + ".tex";
    std::filesystem::path outPath = artifact_dir / artifactName;

    std::ofstream os(outPath, std::ios::binary);
    if (os.is_open()) {
        tryengine::resources::TextureHeader texHeader;
        texHeader.width = (uint32_t) width;
        texHeader.height = (uint32_t) height;
        texHeader.channels = 4;
        texHeader.data_size = (uint32_t) (width * height * 4);

        os.write(reinterpret_cast<const char*>(&texHeader), sizeof(tryengine::resources::TextureHeader));
        os.write(reinterpret_cast<const char*>(pixels), texHeader.data_size);
    } else {
        stbi_image_free(pixels);
        return false;
    }

    stbi_image_free(pixels);

    // Файл hierarchy.json в cacheDir не создаем по вашему условию
    return true;
}

}  // namespace tryeditor