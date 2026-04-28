#include "editor/import/TextureImporter.hpp"

#include <cereal/archives/json.hpp>
#include <fstream>
#include <iostream>
#include <random>

#include "engine/resources/Types.hpp"

#define STB_IMAGE_IMPLEMENTATION
#include "editor/meta/MetaSerializer.hpp"
#include "stb_image.h"

namespace tryeditor {

// uint64_t TextureImporter::GenerateMeta(const std::filesystem::path& asset_path,
//                                        const std::filesystem::path& meta_path) {
//     AssetMetaHeader header;
//     static std::random_device rd;
//     static std::mt19937_64 gen(rd());
//     static std::uniform_int_distribution<uint64_t> dis;
//
//     TextureImportSettings settings;
//
//     header.guid = dis(gen);
//     header.asset_type = "texture";
//     header.importer_type = this->GetName();
//
//     MetaSerializer::Write(meta_path, header, settings);
//
//     return header.guid;
// }

bool TextureImporter::GenerateArtifact(const AssetContext& ctx, const TextureImportSettings& settings) {
    std::optional<AssetMetaHeader> header = MetaSerializer::ReadHeader(ctx.meta_path);
    if (header == std::nullopt)
        return false;

    int width, height, channels;
    // Загружаем как RGBA (4 канала) для единообразия в движке
    unsigned char* pixels = stbi_load(ctx.asset_path.string().c_str(), &width, &height, &channels, 4);

    if (!pixels) {
        std::cerr << "[TextureImporter] STBI fail: " << stbi_failure_reason() << " for " << ctx.artifacts_dir
                  << std::endl;
        return false;
    }

    // Создаем директорию для артефакта, если её нет (обычно делает ImportSystem)
    std::filesystem::create_directories(ctx.artifacts_dir);

    // Имя файла артефакта. Можно оставить расширение .tex, как в GLTF текстурах
    std::string artifactName = std::to_string(header->guid) + ".tex";
    std::filesystem::path outPath = ctx.artifacts_dir / artifactName;

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

    return true;
}

}  // namespace tryeditor