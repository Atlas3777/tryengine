#pragma once

#include "engine/resources/ResourceManager.hpp"

namespace engine::resources {

class GltfLoader {
    explicit GltfLoader(ResourceManager& resM):res(&resM) {

    }


   private:
    ResourceManager* res;
};

}  // namespace engine::resource