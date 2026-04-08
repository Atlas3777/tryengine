#pragma once

#include <fstream>
#include <memory>

#include "engine/core/ResourceManager.hpp"
#include "engine/resources/Types.hpp"

namespace tryengine::resources {

class TTextureLoader {
public:
    using result_type = std::shared_ptr<TextureData>;

    explicit TTextureLoader(core::ResourceManager& resM) : res(resM) {}

    result_type operator()(uint64_t id, const std::string& path) const {
        std::ifstream is(path, std::ios::binary);
        if (!is.is_open())
            return nullptr;

        try {
            TextureHeader header{};
            if (!is.read(reinterpret_cast<char*>(&header), sizeof(TextureHeader)))
                return nullptr;

            auto tex_data = std::make_shared<TextureData>();
            tex_data->width = header.width;
            tex_data->height = header.height;
            tex_data->channels = header.channels;

            // Резервируем память под пиксели
            tex_data->pixels.resize(header.dataSize);

            if (!is.read(reinterpret_cast<char*>(tex_data->pixels.data()), header.dataSize))
                return nullptr;

            return tex_data;
        } catch (const std::bad_alloc&) {
            return nullptr;
        }
    }

private:
    core::ResourceManager& res;
};
}  // namespace tryengine::resources