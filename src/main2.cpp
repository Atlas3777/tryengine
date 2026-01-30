#include "ResourceManager.h"
#include "EngineTypes.h"
#include "core/camera.h"
#include "render/renderer.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <cstdio>
#include <vector>

static Vertex vertices[] = {
    {-0.5f,  0.5f, 0.0f,  1.0f, 0.0f, 0.0f, 1.0f,  0.0f, 0.0f, 1.0f, 0.0f, 1.0f}, // Лево-верх
    { 0.5f,  0.5f, 0.0f,  0.0f, 1.0f, 0.0f, 1.0f,  1.0f, 0.0f, 1.0f, 0.0f, 1.0f}, // Право-верх
    { 0.5f, -0.5f, 0.0f,  0.0f, 0.0f, 1.0f, 1.0f,  1.0f, 1.0f, 1.0f, 0.0f, 1.0f}, // Право-низ
    {-0.5f, -0.5f, 0.0f,  1.0f, 1.0f, 0.0f, 1.0f,  0.0f, 1.0f, 1.0f, 0.0f, 1.0f}  // Лево-низ
};

static Uint16 indices[] = {
    0, 1, 2, // Первый треугольник
    0, 2, 3  // Второй треугольник
};


constexpr float WindowWidth = 960;
constexpr float WindowHeight = 540;

int main(int argc, char *argv[]) {
    if (!SDL_Init(SDL_INIT_VIDEO)) return -1;

    SDL_Window *window = SDL_CreateWindow("tryengine", WindowWidth, WindowHeight, 0);
    SDL_GPUDevice *device = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_SPIRV, true, nullptr);
    SDL_ClaimWindowForGPUDevice(device, window);

    int width, height, channels;
        unsigned char *imageData = stbi_load("assets/test.png", &width, &height, &channels, STBI_rgb_alpha);
        if (!imageData) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to load image: %s", stbi_failure_reason());
            return -1;
        }
        SDL_GPUTextureCreateInfo texInfo{};
        texInfo.type = SDL_GPU_TEXTURETYPE_2D;
        texInfo.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
        texInfo.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;// будем семплить
        texInfo.width = (Uint32)width;
        texInfo.height = (Uint32)height;
        texInfo.layer_count_or_depth = 1;
        texInfo.num_levels = 1;
        texInfo.sample_count = SDL_GPU_SAMPLECOUNT_1;
        texInfo.props = 0;

        SDL_GPUTexture *gpuTexture = SDL_CreateGPUTexture(device, &texInfo);
        if (!gpuTexture) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "CreateGPUTexture failed");
            stbi_image_free(imageData);
            return -1;
        }

        // create a transfer buffer just for the image upload
        SDL_GPUTransferBufferCreateInfo transferTexInfo{};
        transferTexInfo.size = (Uint64)width * (Uint64)height * 4; // RGBA8
        transferTexInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
        SDL_GPUTransferBuffer *transferBufferTex = SDL_CreateGPUTransferBuffer(device, &transferTexInfo);

        // fill transfer buffer with image data
        Uint8 *mapTex = (Uint8*)SDL_MapGPUTransferBuffer(device, transferBufferTex, false);
        SDL_memcpy(mapTex, imageData, (size_t)transferTexInfo.size);
        SDL_UnmapGPUTransferBuffer(device, transferBufferTex);

        // we no longer need CPU-side pixels after copying to transfer buffer
        stbi_image_free(imageData);
        imageData = NULL;


    SDL_GPUSamplerCreateInfo samplerInfo = {};
    samplerInfo.min_filter = SDL_GPU_FILTER_LINEAR;
    samplerInfo.mag_filter = SDL_GPU_FILTER_LINEAR;
    samplerInfo.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_LINEAR;
    samplerInfo.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
    samplerInfo.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;

    SDL_GPUSampler* commonSampler = SDL_CreateGPUSampler(device, &samplerInfo);

    Renderer renderer;
    auto vertexShader = renderer.CreateVertexShader(*device);
    auto fragmentShader = renderer.CreateFragmentShader(*device);
    SDL_GPUTexture *depthTexture = renderer.CreateDepthTexture(*device, WindowWidth, WindowHeight);

    SDL_GPUVertexBufferDescription vertexBufferDescriptions[1];
    renderer.SetupVertexBufferDescription(vertexBufferDescriptions);
    SDL_GPUVertexAttribute vertexAttributes[4];
    renderer.SetupVertexAttributes(vertexAttributes);
    SDL_GPUColorTargetDescription colorTargetDescriptions[1];
    renderer.SetupColorTargetDescription(colorTargetDescriptions, device, window);

    SDL_GPUGraphicsPipeline *pipeline = renderer.CreateGraphicsPipeline(
        *device, vertexShader, fragmentShader, vertexBufferDescriptions,
        vertexAttributes, colorTargetDescriptions);

    SDL_ReleaseGPUShader(device, vertexShader);
    SDL_ReleaseGPUShader(device, fragmentShader);



    SDL_GPUBufferCreateInfo bufferInfo{};
        bufferInfo.size = sizeof(vertices);
        bufferInfo.usage = SDL_GPU_BUFFERUSAGE_VERTEX;
        auto vertexBuffer = SDL_CreateGPUBuffer(device, &bufferInfo);


        SDL_GPUBufferCreateInfo indexBufInfo{};
        indexBufInfo.size = sizeof(indices);
        indexBufInfo.usage = SDL_GPU_BUFFERUSAGE_INDEX;
        auto indexBuffer = SDL_CreateGPUBuffer(device, &indexBufInfo);

        SDL_GPUTransferBufferCreateInfo transferInfo{};
        transferInfo.size = sizeof(vertices) + sizeof(indices);
        transferInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
        auto transferBuffer = SDL_CreateGPUTransferBuffer(device, &transferInfo);

        Uint8* mapData = (Uint8*)SDL_MapGPUTransferBuffer(device, transferBuffer, false);
        SDL_memcpy(mapData, vertices, sizeof(vertices));
        SDL_memcpy(mapData + sizeof(vertices), indices, sizeof(indices));

        SDL_UnmapGPUTransferBuffer(device, transferBuffer);

        // Копирование из Transfer в Vertex Buffer
        SDL_GPUCommandBuffer *uploadCmd = SDL_AcquireGPUCommandBuffer(device);

        SDL_GPUCopyPass *copyPass = SDL_BeginGPUCopyPass(uploadCmd);

        SDL_GPUTransferBufferLocation srcVert{transferBuffer, 0};
        SDL_GPUBufferRegion dstVert{vertexBuffer, 0, sizeof(vertices)};
        SDL_UploadToGPUBuffer(copyPass, &srcVert, &dstVert, true);

        SDL_GPUTransferBufferLocation srcIdx{transferBuffer, sizeof(vertices)};
        SDL_GPUBufferRegion dstIdx{indexBuffer, 0, sizeof(indices)};
        SDL_UploadToGPUBuffer(copyPass, &srcIdx, &dstIdx, true);

        SDL_GPUTextureTransferInfo srcTexInfo{ transferBufferTex, 0, 0, 0 }; // pixels_per_row / rows_per_layer = 0 => tightly packed
        SDL_GPUTextureRegion dstTexRegion{ gpuTexture, 0, 0, 0, 0, 0, (Uint32)width, (Uint32)height, 1 };
        SDL_UploadToGPUTexture(copyPass, &srcTexInfo, &dstTexRegion, true);

        SDL_EndGPUCopyPass(copyPass);






    ResourceManager resources(device);

    auto errorTexture = resources.CreateErrorTexture(device);

    std::vector<GameObject> scene;

    std::vector<Mesh *> boxMeshes = resources.LoadModel("assets/box/scene.gltf");

    if (!boxMeshes.empty()) {
        Mesh *boxMesh = boxMeshes[0];
        // Добавляем объекты
        scene.push_back({boxMesh, {0, 0, -5}, {0, 0, 0}, {0.05f, 0.05f, 0.05f}}); // Центр
        // scene.push_back({boxMesh, {2, 0, -5}, {0, 45, 0}, {0.5, 0.5, 0.5}}); // Справа
        // scene.push_back({boxMesh, {-2, 1, -6}, {45, 0, 0}, {1, 1, 1}}); // Слева
    } else {
        SDL_Log("Warning: No meshes loaded!");
    }


    Camera camera;
    camera.pos = glm::vec3(0.0f, 0.0f, -3.0f);
    camera.front = glm::vec3(0.0f, 0.0f, -1.0f);
    camera.up = glm::vec3(0.0f, 1.0f, 0.0f);
    camera.yaw = -90.0f;
    camera.sensitivity = 0.05f;
    camera.speed = 2.5f;

    SDL_SetWindowRelativeMouseMode(window, true);
    uint64_t lastTime = SDL_GetTicksNS();

    SDL_SubmitGPUCommandBuffer(uploadCmd);


    bool running = true;
    while (running) {
        uint64_t currentTime = SDL_GetTicksNS();
        double deltaTime = (currentTime - lastTime) / 1000000000.0;
        lastTime = currentTime;

        UpdateCamera(camera, running, deltaTime);

        // Начало отрисовки кадра
        SDL_GPUCommandBuffer *cmd = SDL_AcquireGPUCommandBuffer(device);
        SDL_GPUTexture *swapchainTexture;
        Uint32 w, h;

        if (!SDL_WaitAndAcquireGPUSwapchainTexture(cmd, window, &swapchainTexture, &w, &h)) {
             // Если не удалось получить текстуру (например, окно свернуто), просто сабмитим пустой буфер
            SDL_SubmitGPUCommandBuffer(cmd);
            continue;
        }

        // Настройка прохода рендера (Render Pass)
        SDL_GPUColorTargetInfo colorTargetInfo{};
        colorTargetInfo.texture = swapchainTexture;
        colorTargetInfo.clear_color = {0.5f, 0.5f, 0.2f, 1.0f};
        colorTargetInfo.load_op = SDL_GPU_LOADOP_CLEAR; // <--- ОЧИСТКА ЭКРАНА
        colorTargetInfo.store_op = SDL_GPU_STOREOP_STORE;

        SDL_GPUDepthStencilTargetInfo depthTargetInfo{};
        depthTargetInfo.texture = depthTexture;
        depthTargetInfo.clear_depth = 1.0f;
        depthTargetInfo.load_op = SDL_GPU_LOADOP_CLEAR; // <--- ОЧИСТКА ГЛУБИНЫ
        depthTargetInfo.store_op = SDL_GPU_STOREOP_STORE;
        depthTargetInfo.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
        depthTargetInfo.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;

        UniformBufferObject uVertexBuffer{};
            glm::mat4 model =
                glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, -2.0f));
            model = model * glm::rotate(glm::mat4(1.0f),
                // SDL_GetTicks() / 1000.0f,
                0.0f,
                                        glm::vec3(1.0f, 0.0f, 0.0f));
            glm::mat4 proj = glm::perspective(
                glm::radians(70.0f), static_cast<float>(width) / height, 0.1f, 100.0f);
            glm::mat4 view = glm::lookAt(camera.pos, camera.pos + camera.front, camera.up);

            uVertexBuffer.mvp = proj * view * model;

            SDL_PushGPUVertexUniformData(cmd, 0, &uVertexBuffer, sizeof(UniformBufferObject));



        SDL_GPURenderPass *pass = SDL_BeginGPURenderPass(cmd, &colorTargetInfo, 1, &depthTargetInfo);

        // Биндим пайплайн (один раз, если он общий для всех кубов)
        SDL_BindGPUGraphicsPipeline(pass, pipeline);

        // Матрицы камеры (View/Proj общие для всего кадра)
        view = glm::lookAt(camera.pos, camera.pos + camera.front, camera.up);
        proj = glm::perspective(glm::radians(70.0f), (float)WindowWidth / WindowHeight, 0.1f, 100.0f);

        // РИСУЕМ ВСЕ ОБЪЕКТЫ
        for (auto &obj : scene) {
            if (!obj.mesh) continue;

            // 1. Uniforms (MVP)
            glm::mat4 model = obj.GetModelMatrix();
            UniformBufferObject ubo{};
            ubo.mvp = proj * view * model;

            SDL_PushGPUVertexUniformData(cmd, 0, &ubo, sizeof(UniformBufferObject));

            SDL_GPUTexture* textureToBind = (obj.mesh->texture) ? obj.mesh->texture->handle : errorTexture;

            SDL_GPUTextureSamplerBinding tsb = {textureToBind, commonSampler};
            SDL_BindGPUFragmentSamplers(pass, 0, &tsb, 1);

            // 3. Вершины и Индексы
            SDL_GPUBufferBinding vb = {obj.mesh->vertexBuffer, 0};
            SDL_BindGPUVertexBuffers(pass, 0, &vb, 1);

            SDL_GPUBufferBinding ib = {obj.mesh->indexBuffer, 0};
            SDL_BindGPUIndexBuffer(pass, &ib, SDL_GPU_INDEXELEMENTSIZE_32BIT); // Убедись что индексы Uint32

            // 4. Draw call
            SDL_DrawGPUIndexedPrimitives(pass, obj.mesh->numIndices, 1, 0, 0, 0);
        }


                SDL_GPUTextureSamplerBinding tsb{.texture = gpuTexture, .sampler = commonSampler};
                SDL_BindGPUFragmentSamplers(pass, 0, &tsb, 1);

                SDL_GPUBufferBinding vertexBinding{vertexBuffer, 0};
                SDL_BindGPUVertexBuffers(pass, 0, &vertexBinding, 1);

                SDL_GPUBufferBinding indexBinding{indexBuffer, 0};
                SDL_BindGPUIndexBuffer(pass, &indexBinding, SDL_GPU_INDEXELEMENTSIZE_16BIT);


                SDL_DrawGPUIndexedPrimitives(pass, 6, 1, 0, 0, 0);


        SDL_EndGPURenderPass(pass);
        SDL_SubmitGPUCommandBuffer(cmd);
    }
    // --- КОНЕЦ ЦИКЛА ---

    // ОЧИСТКА РЕСУРСОВ (Строго после цикла!)

    // 1. Удаляем семплер
    SDL_ReleaseGPUSampler(device, commonSampler);

    // 2. Удаляем ресурсы движка (Меши и Текстуры)
    resources.Cleanup();

    // 3. Удаляем системные штуки
    SDL_ReleaseGPUTexture(device, depthTexture);
    SDL_ReleaseGPUGraphicsPipeline(device, pipeline);

    SDL_ReleaseWindowFromGPUDevice(device, window);
    SDL_DestroyGPUDevice(device);
    SDL_DestroyWindow(window);
    SDL_Quit();

    return 0;
}
