#pragma once

#include "engine/resources/ResourceManager.hpp"

namespace engine::resources {

class GltfLoader {
   public:
    using result_type = std::shared_ptr<MeshData>;

    explicit GltfLoader(ResourceManager& resM) : res(&resM) {}

    result_type operator()(const std::string& path) const {
        //реализовать загрузку
    }

   private:
    ResourceManager* res;
};
}  // namespace engine::resources