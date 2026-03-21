#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include "SDL3/SDL_gpu.h"
#include "engine/core/CoreTypes.hpp"

namespace tinygltf {
struct Accessor;
class Model;
}
namespace engine::resources {
class ResourceManager {
   private:
    SDL_GPUDevice* device;
    std::unordered_map<std::string, core::Texture*> textureCache;
    std::unordered_map<std::string, core::Mesh*> meshCache;
    core::Texture* whiteTexture = nullptr;

    // Внутренний метод создания базовой текстуры
    core::Texture* CreateOnePixelTexture(Uint8 r, Uint8 g, Uint8 b, Uint8 a);

    void ProcessNode(const tinygltf::Model& model, int nodeIdx, const core::mat4& parentTransform,
                     const std::string& directory, std::vector<core::Mesh*>& loadedMeshes);

    void ComputeLocalAABB(const tinygltf::Accessor& posAcc, const glm::mat4& nodeTransform, vec3& outMin, vec3& outMax);

   public:
    ResourceManager(SDL_GPUDevice* dev);
    ~ResourceManager() { Cleanup(); }

    void Cleanup();

    // Загрузка текстуры (с проверкой кеша)
    core::Texture* LoadTexture(const std::string& path);

    // Загрузка модели (Mesh)
    std::vector<core::Mesh*> LoadModel(const std::string& path);
};
}  // namespace engine
