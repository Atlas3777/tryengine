#include <stb_image.h>
#include <tiny_gltf.h>

#include <cstring>
#include <filesystem>
#include <glm/gtc/type_ptr.hpp>

#include "ResourceManager.hpp"
#include "SDL3/SDL_log.h"

ResourceManager::ResourceManager(SDL_GPUDevice* dev) : device(dev) {
    whiteTexture = CreateOnePixelTexture(255, 255, 255, 255);
}

void ResourceManager::Cleanup() {
    for (auto& pair : textureCache) {
        if (pair.second->handle) SDL_ReleaseGPUTexture(device, pair.second->handle);
        delete pair.second;
    }
    textureCache.clear();

    for (auto& pair : meshCache) {
        if (pair.second->vertexBuffer) SDL_ReleaseGPUBuffer(device, pair.second->vertexBuffer);
        if (pair.second->indexBuffer) SDL_ReleaseGPUBuffer(device, pair.second->indexBuffer);
        delete pair.second;
    }
    meshCache.clear();
}

Texture* ResourceManager::CreateOnePixelTexture(Uint8 r, Uint8 g, Uint8 b, Uint8 a) {
    Texture* tex = new Texture();
    tex->width = 1;
    tex->height = 1;
    tex->path = "internal_1x1";

    SDL_GPUTextureCreateInfo info{};
    info.type = SDL_GPU_TEXTURETYPE_2D;
    info.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    info.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
    info.width = 1;
    info.height = 1;
    info.layer_count_or_depth = 1;
    info.num_levels = 1;

    tex->handle = SDL_CreateGPUTexture(device, &info);

    Uint8 pixels[4] = {r, g, b, a};
    SDL_GPUTransferBufferCreateInfo tInfo = {.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD, .size = 4, .props = 0};
    SDL_GPUTransferBuffer* tBuf = SDL_CreateGPUTransferBuffer(device, &tInfo);

    Uint8* map = (Uint8*)SDL_MapGPUTransferBuffer(device, tBuf, false);
    std::memcpy(map, pixels, 4);
    SDL_UnmapGPUTransferBuffer(device, tBuf);

    SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(device);
    SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(cmd);
    SDL_GPUTextureTransferInfo src = {tBuf, 0, 0, 0};
    SDL_GPUTextureRegion dst = {tex->handle, 0, 0, 0, 0, 0, 1, 1, 1};
    SDL_UploadToGPUTexture(copy, &src, &dst, false);
    SDL_EndGPUCopyPass(copy);
    SDL_SubmitGPUCommandBuffer(cmd);
    SDL_ReleaseGPUTransferBuffer(device, tBuf);

    return tex;
}

Texture* ResourceManager::LoadTexture(const std::string& path) {
    if (textureCache.find(path) != textureCache.end()) {
        return textureCache[path];
    }

    int w, h, c;
    unsigned char* data = stbi_load(path.c_str(), &w, &h, &c, STBI_rgb_alpha);
    if (!data) {
        SDL_Log("Failed to load texture: %s", path.c_str());
        return nullptr;
    }

    Texture* tex = new Texture();
    tex->width = static_cast<Uint32>(w);
    tex->height = static_cast<Uint32>(h);
    tex->path = path;

    SDL_GPUTextureCreateInfo info{};
    info.type = SDL_GPU_TEXTURETYPE_2D;
    info.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    info.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
    info.width = tex->width;
    info.height = tex->height;
    info.layer_count_or_depth = 1;
    info.num_levels = 1;

    tex->handle = SDL_CreateGPUTexture(device, &info);

    Uint32 dataSize = tex->width * tex->height * 4;
    SDL_GPUTransferBufferCreateInfo tInfo = {.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD, .size = dataSize};
    SDL_GPUTransferBuffer* tBuf = SDL_CreateGPUTransferBuffer(device, &tInfo);

    Uint8* map = (Uint8*)SDL_MapGPUTransferBuffer(device, tBuf, false);
    std::memcpy(map, data, dataSize);
    SDL_UnmapGPUTransferBuffer(device, tBuf);

    SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(device);
    SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(cmd);
    SDL_GPUTextureTransferInfo src = {tBuf, 0, 0, 0};
    SDL_GPUTextureRegion dst = {tex->handle, 0, 0, 0, 0, 0, tex->width, tex->height, 1};
    SDL_UploadToGPUTexture(copy, &src, &dst, false);
    SDL_EndGPUCopyPass(copy);
    SDL_SubmitGPUCommandBuffer(cmd);

    SDL_ReleaseGPUTransferBuffer(device, tBuf);
    stbi_image_free(data);

    textureCache[path] = tex;
    return tex;
}

glm::mat4 GetNodeMatrix(const tinygltf::Node& node) {
    if (node.matrix.size() == 16) {
        return glm::make_mat4(node.matrix.data());
    }

    glm::mat4 transform = glm::mat4(1.0f);

    if (node.translation.size() == 3) {
        transform = glm::translate(
            transform, glm::vec3((float)node.translation[0], (float)node.translation[1], (float)node.translation[2]));
    }
    if (node.rotation.size() == 4) {
        glm::quat q = glm::make_quat(node.rotation.data());
        transform = transform * glm::mat4_cast(q);
    }
    if (node.scale.size() == 3) {
        transform = glm::scale(transform, glm::vec3((float)node.scale[0], (float)node.scale[1], (float)node.scale[2]));
    }
    return transform;
}

// Рекурсивная функция обхода дерева узлов

void ResourceManager::ProcessNode(const tinygltf::Model& model, int nodeIdx, const glm::mat4& parentTransform,
                                  const std::string& directory, std::vector<Mesh*>& loadedMeshes) {
    const auto& node = model.nodes[nodeIdx];
    glm::mat4 globalTransform = parentTransform * GetNodeMatrix(node);

    if (node.mesh >= 0) {
        const auto& gltfMesh = model.meshes[node.mesh];

        for (const auto& primitive : gltfMesh.primitives) {
            if (primitive.mode != TINYGLTF_MODE_TRIANGLES) continue;

            // --- 1. Сначала определяем цвет материала ---
            float matR = 1.0f, matG = 1.0f, matB = 1.0f, matA = 1.0f;
            std::string texturePath = "";

            if (primitive.material >= 0) {
                const auto& mat = model.materials[primitive.material];
                const auto& pbr = mat.pbrMetallicRoughness;

                // Читаем Base Color Factor (RGBA)
                matR = (float)pbr.baseColorFactor[0];
                matG = (float)pbr.baseColorFactor[1];
                matB = (float)pbr.baseColorFactor[2];
                matA = (float)pbr.baseColorFactor[3];

                // Проверяем текстуру
                if (pbr.baseColorTexture.index >= 0) {
                    const auto& tex = model.textures[pbr.baseColorTexture.index];
                    const auto& img = model.images[tex.source];
                    if (!img.uri.empty()) texturePath = directory + img.uri;
                }
            }

            // --- 2. Доступ к атрибутам вершин ---
            std::vector<Vertex> vertices;
            std::vector<Uint32> indices;

            const auto& posAcc = model.accessors[primitive.attributes.at("POSITION")];
            const auto& posView = model.bufferViews[posAcc.bufferView];
            const float* posData = reinterpret_cast<const float*>(
                &model.buffers[posView.buffer].data[posView.byteOffset + posAcc.byteOffset]);

            const float* normData = nullptr;
            if (primitive.attributes.contains("NORMAL")) {
                const auto& acc = model.accessors[primitive.attributes.at("NORMAL")];
                normData = reinterpret_cast<const float*>(
                    &model.buffers[model.bufferViews[acc.bufferView].buffer]
                         .data[model.bufferViews[acc.bufferView].byteOffset + acc.byteOffset]);
            }

            const float* uvData = nullptr;
            if (primitive.attributes.contains("TEXCOORD_0")) {
                const auto& acc = model.accessors[primitive.attributes.at("TEXCOORD_0")];
                uvData = reinterpret_cast<const float*>(
                    &model.buffers[model.bufferViews[acc.bufferView].buffer]
                         .data[model.bufferViews[acc.bufferView].byteOffset + acc.byteOffset]);
            }

            const float* colorData = nullptr;
            int colorCompCount = 0;
            if (primitive.attributes.contains("COLOR_0")) {
                const auto& acc = model.accessors[primitive.attributes.at("COLOR_0")];
                colorData = reinterpret_cast<const float*>(
                    &model.buffers[model.bufferViews[acc.bufferView].buffer]
                         .data[model.bufferViews[acc.bufferView].byteOffset + acc.byteOffset]);
                colorCompCount = (acc.type == TINYGLTF_TYPE_VEC3) ? 3 : 4;
            }

            glm::mat3 normalMatrix = glm::transpose(glm::inverse(glm::mat3(globalTransform)));

            // --- 3. Сборка вершин ---
            for (size_t v = 0; v < posAcc.count; ++v) {
                Vertex vert{};
                glm::vec4 worldPos =
                    globalTransform * glm::vec4(posData[v * 3 + 0], posData[v * 3 + 1], posData[v * 3 + 2], 1.0f);
                vert.x = worldPos.x;
                vert.y = worldPos.y;
                vert.z = worldPos.z;

                if (normData) {
                    glm::vec3 n = glm::normalize(
                        normalMatrix * glm::vec3(normData[v * 3 + 0], normData[v * 3 + 1], normData[v * 3 + 2]));
                    vert.nx = n.x;
                    vert.ny = n.y;
                    vert.nz = n.z;
                }

                if (uvData) {
                    vert.u = uvData[v * 2 + 0];
                    vert.v = uvData[v * 2 + 1];
                }

                // Логика цвета: приоритет у COLOR_0, если его нет — берем цвет материала
                if (colorData) {
                    vert.r = colorData[v * colorCompCount + 0] * matR;
                    vert.g = colorData[v * colorCompCount + 1] * matG;
                    vert.b = colorData[v * colorCompCount + 2] * matB;
                    vert.a = (colorCompCount == 4) ? colorData[v * 4 + 3] * matA : matA;
                } else {
                    vert.r = matR;
                    vert.g = matG;
                    vert.b = matB;
                    vert.a = matA;
                }

                vertices.push_back(vert);
            }

            // --- 4. Индексы и загрузка на GPU (как раньше) ---
            if (primitive.indices >= 0) {
                const auto& acc = model.accessors[primitive.indices];
                const auto& view = model.bufferViews[acc.bufferView];
                const unsigned char* dataPtr = &model.buffers[view.buffer].data[view.byteOffset + acc.byteOffset];
                for (size_t i = 0; i < acc.count; ++i) {
                    if (acc.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT)
                        indices.push_back(reinterpret_cast<const uint16_t*>(dataPtr)[i]);
                    else if (acc.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT)
                        indices.push_back(reinterpret_cast<const uint32_t*>(dataPtr)[i]);
                }
            }

            Mesh* myMesh = new Mesh();
            myMesh->numIndices = (Uint32)indices.size();
            Uint32 vSize = vertices.size() * sizeof(Vertex);
            Uint32 iSize = indices.size() * sizeof(Uint32);

            myMesh->vertexBuffer = SDL_CreateGPUBuffer(
                device, new SDL_GPUBufferCreateInfo{.usage = SDL_GPU_BUFFERUSAGE_VERTEX, .size = vSize});
            myMesh->indexBuffer = SDL_CreateGPUBuffer(
                device, new SDL_GPUBufferCreateInfo{.usage = SDL_GPU_BUFFERUSAGE_INDEX, .size = iSize});

            SDL_GPUTransferBuffer* tBuf = SDL_CreateGPUTransferBuffer(
                device, new SDL_GPUTransferBufferCreateInfo{.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
                                                            .size = vSize + iSize});
            Uint8* ptr = (Uint8*)SDL_MapGPUTransferBuffer(device, tBuf, false);
            memcpy(ptr, vertices.data(), vSize);
            memcpy(ptr + vSize, indices.data(), iSize);
            SDL_UnmapGPUTransferBuffer(device, tBuf);

            SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(device);
            SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(cmd);
            SDL_UploadToGPUBuffer(copy, new SDL_GPUTransferBufferLocation{tBuf, 0},
                                  new SDL_GPUBufferRegion{myMesh->vertexBuffer, 0, vSize}, false);
            SDL_UploadToGPUBuffer(copy, new SDL_GPUTransferBufferLocation{tBuf, vSize},
                                  new SDL_GPUBufferRegion{myMesh->indexBuffer, 0, iSize}, false);
            SDL_EndGPUCopyPass(copy);
            SDL_SubmitGPUCommandBuffer(cmd);
            SDL_ReleaseGPUTransferBuffer(device, tBuf);

            // Назначение текстуры
            if (!texturePath.empty()) {
                myMesh->texture = LoadTexture(texturePath);
            }
            if (!myMesh->texture) {
                myMesh->texture = whiteTexture;
            }

            loadedMeshes.push_back(myMesh);
        }
    }

    for (int childIdx : node.children) {
        ProcessNode(model, childIdx, globalTransform, directory, loadedMeshes);
    }
}
std::vector<Mesh*> ResourceManager::LoadModel(const std::string& path) {
    std::vector<Mesh*> loadedMeshes;
    tinygltf::Model model;
    tinygltf::TinyGLTF loader;
    std::string err, warn;

    bool ret = path.ends_with(".glb") ? loader.LoadBinaryFromFile(&model, &err, &warn, path)
                                      : loader.LoadASCIIFromFile(&model, &err, &warn, path);

    if (!ret) return loadedMeshes;

    std::filesystem::path modelPath(path);
    std::string directory = modelPath.parent_path().string();
    if (!directory.empty()) directory += "/";

    // Начинаем обход с корня сцены
    const tinygltf::Scene& scene = model.scenes[model.defaultScene >= 0 ? model.defaultScene : 0];
    for (int nodeIdx : scene.nodes) {
        ProcessNode(model, nodeIdx, glm::mat4(1.0f), directory, loadedMeshes);
    }

    return loadedMeshes;
}
