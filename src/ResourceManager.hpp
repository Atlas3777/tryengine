#include <assimp/postprocess.h>
#include <assimp/scene.h>

#include <assimp/Importer.hpp>
#include <unordered_map>

#include "EngineTypes.hpp"
#include "SDL3/SDL_gpu.h"
#include "SDL3/SDL_log.h"
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include <filesystem>

class ResourceManager {
   private:
    SDL_GPUDevice* device;
    std::unordered_map<std::string, Texture*> textureCache;
    std::unordered_map<std::string, Mesh*> meshCache;

    Texture* whiteTexture = nullptr;

   public:
    ResourceManager(SDL_GPUDevice* dev) : device(dev) {
        // Сразу создаем белую текстуру при старте
        whiteTexture = CreateOnePixelTexture(255, 255, 255, 255);
    }
    void Cleanup() {
        for (auto& pair : textureCache) {
            SDL_ReleaseGPUTexture(device, pair.second->handle);
            delete pair.second;
        }
        for (auto& pair : meshCache) {
            SDL_ReleaseGPUBuffer(device, pair.second->vertexBuffer);
            SDL_ReleaseGPUBuffer(device, pair.second->indexBuffer);
            delete pair.second;
        }
    }
    // Вспомогательный метод для создания однопиксельной текстуры
    Texture* CreateOnePixelTexture(Uint8 r, Uint8 g, Uint8 b, Uint8 a) {
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

        // Упрощенная заливка для 1 пикселя
        SDL_GPUTransferBufferCreateInfo tInfo = {.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD, .size = 4, .props = 0};
        SDL_GPUTransferBuffer* tBuf = SDL_CreateGPUTransferBuffer(device, &tInfo);
        Uint8* map = (Uint8*)SDL_MapGPUTransferBuffer(device, tBuf, false);
        memcpy(map, pixels, 4);
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

    // Загрузка текстуры (с проверкой кеша)
    Texture* LoadTexture(const std::string& path) {
        if (textureCache.find(path) != textureCache.end()) {
            return textureCache[path];
        }

        int w, h, c;
        unsigned char* data = stbi_load(path.c_str(), &w, &h, &c, STBI_rgb_alpha);
        if (!data) {
            SDL_Log("Failed to load texture: %s", path.c_str());
            return nullptr;
        }
        Uint32 width = (w > 0) ? static_cast<Uint32>(w) : 0;
        Uint32 height = (h > 0) ? static_cast<Uint32>(h) : 0;

        Texture* tex = new Texture();
        tex->width = width;
        tex->height = height;
        tex->path = path;

        // Создаем текстуру в GPU
        SDL_GPUTextureCreateInfo info{};
        info.type = SDL_GPU_TEXTURETYPE_2D;
        info.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
        info.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
        info.width = width;
        info.height = height;
        info.layer_count_or_depth = 1;
        info.num_levels = 1;

        tex->handle = SDL_CreateGPUTexture(device, &info);

        // Заливаем данные (Upload)
        // ВНИМАНИЕ: Для краткости создаем временный transfer buffer.
        // В продакшене лучше иметь один общий буфер.
        Uint32 dataSize = width * height * 4;
        SDL_GPUTransferBufferCreateInfo tInfo = {
            .usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD, .size = dataSize, .props = 0};
        SDL_GPUTransferBuffer* tBuf = SDL_CreateGPUTransferBuffer(device, &tInfo);

        Uint8* map = (Uint8*)SDL_MapGPUTransferBuffer(device, tBuf, false);
        memcpy(map, data, dataSize);
        SDL_UnmapGPUTransferBuffer(device, tBuf);

        SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(device);
        SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(cmd);

        SDL_GPUTextureTransferInfo src = {tBuf, 0, 0, 0};
        SDL_GPUTextureRegion dst = {tex->handle, 0, 0, 0, 0, 0, (Uint32)w, (Uint32)h, 1};
        SDL_UploadToGPUTexture(copy, &src, &dst, false);

        SDL_EndGPUCopyPass(copy);
        SDL_SubmitGPUCommandBuffer(cmd);
        SDL_ReleaseGPUTransferBuffer(device, tBuf);
        stbi_image_free(data);

        textureCache[path] = tex;
        return tex;
    }

    // Загрузка модели (Mesh)
    // Возвращает массив мешей, так как один файл может содержать несколько частей

    // Загрузка модели (Mesh) со склейкой по материалам (Static Batching)
    std::vector<Mesh*> LoadModel(const std::string& path) {
        Assimp::Importer importer;

        // ВАЖНО: Добавлен aiProcess_PreTransformVertices!
        // Он "запекает" все трансформации нодов (позицию, поворот, масштаб)
        // прямо в вершины. Без него склеенные детали могут оказаться не на своих местах.
        const aiScene* scene = importer.ReadFile(
            path, aiProcess_Triangulate | aiProcess_FlipUVs | aiProcess_GenNormals | aiProcess_PreTransformVertices);

        std::vector<Mesh*> loadedMeshes;
        if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode) {
            SDL_Log("Assimp error: %s", importer.GetErrorString());
            return loadedMeshes;
        }

        std::filesystem::path modelPath(path);
        std::string directory = modelPath.parent_path().string();
        if (!directory.empty()) directory += "/";

        // Структура для временного хранения склеенных данных
        struct MergedMeshData {
            std::vector<Vertex> vertices;
            std::vector<Uint32> indices;
        };

        // Ключ - индекс материала, Значение - склеенные вершины и индексы
        std::unordered_map<unsigned int, MergedMeshData> groupedMeshes;

        // --- ШАГ 1: Группировка данных по материалам ---
        for (unsigned int i = 0; i < scene->mNumMeshes; i++) {
            aiMesh* aiMesh = scene->mMeshes[i];
            unsigned int matIndex = aiMesh->mMaterialIndex;

            // Получаем или создаем группу для этого материала
            MergedMeshData& group = groupedMeshes[matIndex];

            // Запоминаем текущее количество вершин в группе.
            // Это нужно, чтобы правильно сдвинуть индексы для новых треугольников!
            Uint32 vertexOffset = static_cast<Uint32>(group.vertices.size());

            // --- Извлекаем цвет материала (фоллбэк, если нет vertex colors) ---
            aiMaterial* material = scene->mMaterials[matIndex];
            aiColor4D diffuseColor(1.0f, 1.0f, 1.0f, 1.0f);
            if (AI_SUCCESS != material->Get(AI_MATKEY_BASE_COLOR, diffuseColor)) {
                material->Get(AI_MATKEY_COLOR_DIFFUSE, diffuseColor);
            }

            // 1. Копируем вершины
            for (unsigned int v = 0; v < aiMesh->mNumVertices; v++) {
                Vertex vert{};
                vert.x = aiMesh->mVertices[v].x;
                vert.y = aiMesh->mVertices[v].y;
                vert.z = aiMesh->mVertices[v].z;

                if (aiMesh->HasNormals()) {
                    vert.nx = aiMesh->mNormals[v].x;
                    vert.ny = aiMesh->mNormals[v].y;
                    vert.nz = aiMesh->mNormals[v].z;
                }

                if (aiMesh->HasVertexColors(0)) {
                    vert.r = aiMesh->mColors[0][v].r;
                    vert.g = aiMesh->mColors[0][v].g;
                    vert.b = aiMesh->mColors[0][v].b;
                    vert.a = aiMesh->mColors[0][v].a;
                } else {
                    vert.r = diffuseColor.r;
                    vert.g = diffuseColor.g;
                    vert.b = diffuseColor.b;
                    vert.a = diffuseColor.a;
                }

                if (aiMesh->mTextureCoords[0]) {
                    vert.u = aiMesh->mTextureCoords[0][v].x;
                    vert.v = aiMesh->mTextureCoords[0][v].y;
                }
                group.vertices.push_back(vert);
            }

            // 2. Копируем индексы со сдвигом (vertexOffset)
            for (unsigned int f = 0; f < aiMesh->mNumFaces; f++) {
                aiFace face = aiMesh->mFaces[f];
                for (unsigned int j = 0; j < face.mNumIndices; j++) {
                    group.indices.push_back(face.mIndices[j] + vertexOffset);
                }
            }
        }

        // --- ШАГ 2: Создание GPU буферов для каждой склеенной группы ---
        for (auto& [matIndex, group] : groupedMeshes) {
            Mesh* myMesh = new Mesh();
            myMesh->numIndices = static_cast<Uint32>(group.indices.size());

            aiMaterial* material = scene->mMaterials[matIndex];
            aiString texPath;
            bool hasTexture = false;

            if (material->GetTexture(aiTextureType_BASE_COLOR, 0, &texPath) == AI_SUCCESS ||
                material->GetTexture(aiTextureType_DIFFUSE, 0, &texPath) == AI_SUCCESS) {
                if (texPath.data[0] == '*') {
                    SDL_Log("Embedded textures not fully supported yet: %s", texPath.C_Str());
                } else {
                    std::string fullTexPath = directory + texPath.C_Str();
                    myMesh->texture = LoadTexture(fullTexPath);
                    if (myMesh->texture) hasTexture = true;
                }
            }

            if (!hasTexture) {
                myMesh->texture = whiteTexture;
            }

            // Выделяем память и отправляем единый буфер в GPU
            Uint32 vSize = static_cast<Uint32>(group.vertices.size()) * sizeof(Vertex);
            Uint32 iSize = static_cast<Uint32>(group.indices.size()) * sizeof(Uint32);

            SDL_GPUBufferCreateInfo bInfo = {.usage = SDL_GPU_BUFFERUSAGE_VERTEX, .size = vSize, .props = 0};
            myMesh->vertexBuffer = SDL_CreateGPUBuffer(device, &bInfo);

            bInfo.usage = SDL_GPU_BUFFERUSAGE_INDEX;
            bInfo.size = iSize;
            myMesh->indexBuffer = SDL_CreateGPUBuffer(device, &bInfo);

            SDL_GPUTransferBufferCreateInfo tInfo = {
                .usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD, .size = vSize + iSize, .props = 0};
            SDL_GPUTransferBuffer* tBuf = SDL_CreateGPUTransferBuffer(device, &tInfo);
            Uint8* ptr = (Uint8*)SDL_MapGPUTransferBuffer(device, tBuf, false);

            memcpy(ptr, group.vertices.data(), vSize);
            memcpy(ptr + vSize, group.indices.data(), iSize);
            SDL_UnmapGPUTransferBuffer(device, tBuf);

            SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(device);
            SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(cmd);

            SDL_GPUTransferBufferLocation src = {tBuf, 0};
            SDL_GPUBufferRegion dst = {myMesh->vertexBuffer, 0, vSize};
            SDL_UploadToGPUBuffer(copy, &src, &dst, false);

            src.offset = vSize;
            dst.buffer = myMesh->indexBuffer;
            dst.size = iSize;
            SDL_UploadToGPUBuffer(copy, &src, &dst, false);

            SDL_EndGPUCopyPass(copy);
            SDL_SubmitGPUCommandBuffer(cmd);
            SDL_ReleaseGPUTransferBuffer(device, tBuf);

            loadedMeshes.push_back(myMesh);
        }

        return loadedMeshes;
    }
};
