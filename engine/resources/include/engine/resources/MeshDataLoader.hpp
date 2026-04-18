#pragma once

#include <fstream>

#include "engine/core/ResourceManager.hpp"
#include "engine/resources/Types.hpp"

namespace tryengine::resources {

class MeshDataLoader {
public:
    using result_type = std::shared_ptr<MeshData>;

    explicit MeshDataLoader(core::ResourceManager& resM) : res(&resM) {}

    result_type operator()(uint64_t id, const std::string& path) const {
        std::ifstream is(path, std::ios::binary);
        if (!is.is_open())
            return nullptr;

        try {
            uint32_t num_vertices = 0;
            uint32_t num_indices = 0;

            if (!is.read(reinterpret_cast<char*>(&num_vertices), sizeof(uint32_t)))
                return nullptr;
            if (!is.read(reinterpret_cast<char*>(&num_indices), sizeof(uint32_t)))
                return nullptr;

            auto mesh_data = std::make_shared<MeshData>();

            // Резервируем память (resize заполняет её нулями, что чуть медленнее,
            // но необходимо для прямого доступа через data())
            mesh_data->vertexBuffer.resize(num_vertices);
            mesh_data->indexBuffer.resize(num_indices);

            is.read(reinterpret_cast<char*>(mesh_data->vertexBuffer.data()), num_vertices * sizeof(Vertex));
            is.read(reinterpret_cast<char*>(mesh_data->indexBuffer.data()), num_indices * sizeof(uint32_t));

            if (!is) {
                // Если прочитали меньше, чем ожидали
                return nullptr;
            }

            return mesh_data;
        } catch (const std::bad_alloc&) {
            // Логируем ошибку нехватки памяти
            return nullptr;
        }
    }

private:
    core::ResourceManager* res;
};
}  // namespace tryengine::resources