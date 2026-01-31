#include <assimp/postprocess.h>
#include <assimp/scene.h>

#include <assimp/Importer.hpp>
#include <unordered_map>

#include "EngineTypes.h"
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

    // Храним дефолтную белую текстуру, чтобы не пересоздавать
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
        SDL_GPUTransferBufferCreateInfo tInfo = {SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD, 4};
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
            return textureCache[path];  // Уже загружена, возвращаем ссылку
        }

        int w, h, c;
        unsigned char* data = stbi_load(path.c_str(), &w, &h, &c, STBI_rgb_alpha);
        if (!data) {
            SDL_Log("Failed to load texture: %s", path.c_str());
            return nullptr;  // Или вернуть дефолтную розовую текстуру
        }

        Texture* tex = new Texture();
        tex->width = w;
        tex->height = h;
        tex->path = path;

        // Создаем текстуру в GPU
        SDL_GPUTextureCreateInfo info{};
        info.type = SDL_GPU_TEXTURETYPE_2D;
        info.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
        info.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
        info.width = w;
        info.height = h;
        info.layer_count_or_depth = 1;
        info.num_levels = 1;

        tex->handle = SDL_CreateGPUTexture(device, &info);

        // Заливаем данные (Upload)
        // ВНИМАНИЕ: Для краткости создаем временный transfer buffer.
        // В продакшене лучше иметь один общий буфер.
        size_t dataSize = w * h * 4;
        SDL_GPUTransferBufferCreateInfo tInfo = {SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD, (Uint32)dataSize};
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

    std::vector<Mesh*> LoadModel(const std::string& path) {
        Assimp::Importer importer;
        const aiScene* scene =
            importer.ReadFile(path, aiProcess_Triangulate | aiProcess_FlipUVs | aiProcess_GenNormals);

        std::vector<Mesh*> loadedMeshes;
        if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode) {
            SDL_Log("Assimp error: %s", importer.GetErrorString());
            return loadedMeshes;
        }

        std::filesystem::path modelPath(path);
        std::string directory = modelPath.parent_path().string();
        if (!directory.empty()) directory += "/";

        for (unsigned int i = 0; i < scene->mNumMeshes; i++) {
            aiMesh* aiMesh = scene->mMeshes[i];
            Mesh* myMesh = new Mesh();

            // --- ПОЛУЧАЕМ ЦВЕТ МАТЕРИАЛА ---
            aiMaterial* material = scene->mMaterials[aiMesh->mMaterialIndex];
            aiColor4D diffuseColor(1.0f, 1.0f, 1.0f, 1.0f);

            // Пытаемся взять Base Color (PBR) или Diffuse (Legacy)
            if (AI_SUCCESS != material->Get(AI_MATKEY_BASE_COLOR, diffuseColor)) {
                material->Get(AI_MATKEY_COLOR_DIFFUSE, diffuseColor);
            }

            std::vector<Vertex> vertices;
            std::vector<Uint32> indices;

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

                // --- ИСПРАВЛЕНИЕ 1: Vertex Colors ---
                // Low poly часто хранит цвета здесь
                if (aiMesh->HasVertexColors(0)) {
                    vert.r = aiMesh->mColors[0][v].r;
                    vert.g = aiMesh->mColors[0][v].g;
                    vert.b = aiMesh->mColors[0][v].b;
                    vert.a = aiMesh->mColors[0][v].a;
                } else {
                    // Если вертексных цветов нет, используем цвет материала
                    vert.r = diffuseColor.r;
                    vert.g = diffuseColor.g;
                    vert.b = diffuseColor.b;
                    vert.a = diffuseColor.a;
                }

                if (aiMesh->mTextureCoords[0]) {
                    vert.u = aiMesh->mTextureCoords[0][v].x;
                    vert.v = aiMesh->mTextureCoords[0][v].y;
                }
                vertices.push_back(vert);
            }

            // ... (Код индексов тот же) ...
            for (unsigned int f = 0; f < aiMesh->mNumFaces; f++) {
                aiFace face = aiMesh->mFaces[f];
                for (unsigned int j = 0; j < face.mNumIndices; j++) indices.push_back(face.mIndices[j]);
            }
            myMesh->numIndices = indices.size();

            // --- ИСПРАВЛЕНИЕ 2: Загрузка текстуры или ФОЛЛБЭК ---
            aiString texPath;
            bool hasTexture = false;

            // Сначала ищем BASE COLOR текстуру (gltf pbr), потом DIFFUSE
            if (material->GetTexture(aiTextureType_BASE_COLOR, 0, &texPath) == AI_SUCCESS ||
                material->GetTexture(aiTextureType_DIFFUSE, 0, &texPath) == AI_SUCCESS) {
                // Важный момент: если текстура "встроена" (embedded), путь будет начинаться с "*"
                if (texPath.data[0] == '*') {
                    // Это встроенная текстура, тут нужен отдельный код разбора aiScene::mTextures
                    // Для простоты пока скипнем и дадим белую, если не хочешь возиться с буферами в памяти
                    SDL_Log("Embedded textures not fully supported yet in this snippet: %s", texPath.C_Str());
                } else {
                    std::string fullTexPath = directory + texPath.C_Str();
                    myMesh->texture = LoadTexture(fullTexPath);
                    if (myMesh->texture) hasTexture = true;
                }
            }

            // ЕСЛИ ТЕКСТУРЫ НЕТ — назначаем БЕЛУЮ ЗАГЛУШКУ
            // Это критически важно. Если тут будет nullptr, ты потом подставишь ErrorTexture (фиолетовую).
            // А нам нужно, чтобы умножался цвет вершины (vert.r/g/b) на 1.0 (белый).
            if (!hasTexture) {
                myMesh->texture = whiteTexture;
            }

            // ... (Дальше создание буферов GPU то же самое) ...
            size_t vSize = vertices.size() * sizeof(Vertex);
            size_t iSize = indices.size() * sizeof(Uint32);
            // ... copy paste твоего кода создания буферов ...

            SDL_GPUBufferCreateInfo bInfo = {SDL_GPU_BUFFERUSAGE_VERTEX, (Uint32)vSize};
            myMesh->vertexBuffer = SDL_CreateGPUBuffer(device, &bInfo);

            bInfo.usage = SDL_GPU_BUFFERUSAGE_INDEX;
            bInfo.size = (Uint32)iSize;
            myMesh->indexBuffer = SDL_CreateGPUBuffer(device, &bInfo);

            SDL_GPUTransferBufferCreateInfo tInfo = {SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD, (Uint32)(vSize + iSize)};
            SDL_GPUTransferBuffer* tBuf = SDL_CreateGPUTransferBuffer(device, &tInfo);
            Uint8* ptr = (Uint8*)SDL_MapGPUTransferBuffer(device, tBuf, false);
            memcpy(ptr, vertices.data(), vSize);
            memcpy(ptr + vSize, indices.data(), iSize);
            SDL_UnmapGPUTransferBuffer(device, tBuf);

            SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(device);
            SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(cmd);

            SDL_GPUTransferBufferLocation src = {tBuf, 0};
            SDL_GPUBufferRegion dst = {myMesh->vertexBuffer, 0, (Uint32)vSize};
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

    SDL_GPUTexture* CreateErrorTexture(SDL_GPUDevice* device) {
        // 1. Данные: 2x2 пикселя (RGBA)
        // Фиолетовый: 255, 0, 255, 255 | Черный: 0, 0, 0, 255
        uint8_t pixels[] = {255, 0, 255, 255, 0, 0, 0, 255, 0, 0, 0, 255, 255, 0, 255, 255};

        SDL_GPUTextureCreateInfo texInfo = {};
        texInfo.type = SDL_GPU_TEXTURETYPE_2D;
        texInfo.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
        texInfo.width = 2;
        texInfo.height = 2;
        texInfo.layer_count_or_depth = 1;
        texInfo.num_levels = 1;
        texInfo.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;

        SDL_GPUTexture* errorTex = SDL_CreateGPUTexture(device, &texInfo);

        // 2. Загрузка данных на GPU
        SDL_GPUTransferBufferCreateInfo transferInfo = {};
        transferInfo.size = sizeof(pixels);
        transferInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
        SDL_GPUTransferBuffer* uploadBuffer = SDL_CreateGPUTransferBuffer(device, &transferInfo);

        // Копируем в буфер
        uint8_t* data = (uint8_t*)SDL_MapGPUTransferBuffer(device, uploadBuffer, false);
        SDL_memcpy(data, pixels, sizeof(pixels));
        SDL_UnmapGPUTransferBuffer(device, uploadBuffer);

        // Копируем из буфера в текстуру
        SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(device);
        SDL_GPUCopyPass* copyPass = SDL_BeginGPUCopyPass(cmd);

        SDL_GPUTextureTransferInfo source = {};
        source.transfer_buffer = uploadBuffer;
        source.offset = 0;
        SDL_GPUTextureRegion dest = {errorTex, 0, 0, 0, 0, 0, 2, 2, 1};
        SDL_UploadToGPUTexture(copyPass, &source, &dest, false);

        SDL_EndGPUCopyPass(copyPass);
        SDL_SubmitGPUCommandBuffer(cmd);

        SDL_ReleaseGPUTransferBuffer(device, uploadBuffer);

        return errorTex;
    }
};
