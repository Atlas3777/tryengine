#pragma once

#include <tiny_gltf.h>

#include <string>
#include <unordered_map>
#include <vector>

#include "EngineTypes.hpp"
#include "SDL3/SDL_gpu.h"

class ResourceManager {
   private:
    SDL_GPUDevice* device;
    std::unordered_map<std::string, Texture*> textureCache;
    std::unordered_map<std::string, Mesh*> meshCache;
    Texture* whiteTexture = nullptr;

    // Внутренний метод создания базовой текстуры
    Texture* CreateOnePixelTexture(Uint8 r, Uint8 g, Uint8 b, Uint8 a);

    void ProcessNode(const tinygltf::Model& model, int nodeIdx, const glm::mat4& parentTransform,
                     const std::string& directory, std::vector<Mesh*>& loadedMeshes);

   public:
    ResourceManager(SDL_GPUDevice* dev);
    ~ResourceManager() { Cleanup(); }

    void Cleanup();

    // Загрузка текстуры (с проверкой кеша)
    Texture* LoadTexture(const std::string& path);

    // Загрузка модели (Mesh)
    std::vector<Mesh*> LoadModel(const std::string& path);
};
