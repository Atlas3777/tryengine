#include "editor/import/ImageImporter.hpp"

#include <cereal/archives/json.hpp>
#include <fstream>
#include <iostream>
#include <random>

#include "stb_image.h"

namespace editor {

namespace {
struct TextureHeader {
    uint32_t width;
    uint32_t height;
    uint32_t channels;
    uint32_t dataSize;
};
}  // namespace

uint64_t ImageImporter::GenerateMeta(const std::filesystem::path& assetPath, const std::filesystem::path& metaPath) {
    AssetHeader header{};
    static std::random_device rd;
    static std::mt19937_64 gen(rd());
    static std::uniform_int_distribution<uint64_t> dis;

    header.main_uuid = dis(gen);

    try {
        std::ofstream os(metaPath);
        if (os.is_open()) {
            cereal::JSONOutputArchive archive(os);
            archive(cereal::make_nvp("header", header));
        } else {
            return 0;
        }
    } catch (const std::exception& e) {
        std::cerr << "[ImageImporter] Failed to save meta: " << e.what() << std::endl;
        return 0;
    }
    return header.main_uuid;
}

AssetHeader ImageImporter::ReadIdentification(const std::filesystem::path& metaPath) {
    AssetHeader header{};
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

bool ImageImporter::GenerateArtifact(const std::filesystem::path& assetPath, const std::filesystem::path& metaPath,
                                     const std::filesystem::path& artifactDir, const std::filesystem::path& cacheDir,
                                     const std::filesystem::path& projectAssetsDir) {
    AssetHeader header = ReadIdentification(metaPath);
    if (header.main_uuid == 0)
        return false;

    int width, height, channels;
    // Загружаем как RGBA (4 канала) для единообразия в движке
    unsigned char* pixels = stbi_load(assetPath.string().c_str(), &width, &height, &channels, 4);

    if (!pixels) {
        std::cerr << "[ImageImporter] STBI fail: " << stbi_failure_reason() << " for " << assetPath << std::endl;
        return false;
    }

    // Создаем директорию для артефакта, если её нет (обычно делает ImportSystem)
    std::filesystem::create_directories(artifactDir);

    // Имя файла артефакта. Можно оставить расширение .tex, как в GLTF текстурах
    std::string artifactName = std::to_string(header.main_uuid) + ".tex";
    std::filesystem::path outPath = artifactDir / artifactName;

    std::ofstream os(outPath, std::ios::binary);
    if (os.is_open()) {
        TextureHeader texHeader;
        texHeader.width = (uint32_t) width;
        texHeader.height = (uint32_t) height;
        texHeader.channels = 4;
        texHeader.dataSize = (uint32_t) (width * height * 4);

        os.write(reinterpret_cast<const char*>(&texHeader), sizeof(TextureHeader));
        os.write(reinterpret_cast<const char*>(pixels), texHeader.dataSize);
    } else {
        stbi_image_free(pixels);
        return false;
    }

    stbi_image_free(pixels);

    // Файл hierarchy.json в cacheDir не создаем по вашему условию
    return true;
}

}  // namespace editor