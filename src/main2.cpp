#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <vector>

#include "EngineTypes.h"
#include "ResourceManager.h"
#include "core/camera.h"
#include "render/renderer.h"

// static Vertex vertices[] = {
//     {-0.5f, 0.5f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f},
//     {0.5f, 0.5f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f, 0.0f, 1.0f},   // Право-верх
//     {0.5f, -0.5f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0.0f, 1.0f},  // Право-низ
//     {-0.5f, -0.5f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f}  // Лево-низ
// };

// static Uint16 indices[] = {
//     0, 1, 2,  // Первый треугольник
//     0, 2, 3   // Второй треугольник
// };

constexpr float WindowWidth = 1920 / 1.5f;
constexpr float WindowHeight = 1080 / 1.5f;

int main(int argc, char* argv[]) {
    if (!SDL_Init(SDL_INIT_VIDEO)) return -1;

    SDL_Window* window = SDL_CreateWindow("tryengine", WindowWidth, WindowHeight, 0);
    SDL_GPUDevice* device = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_SPIRV, true, nullptr);
    SDL_ClaimWindowForGPUDevice(device, window);

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
    SDL_GPUTexture* depthTexture = renderer.CreateDepthTexture(*device, WindowWidth, WindowHeight);

    SDL_GPUVertexBufferDescription vertexBufferDescriptions[1];
    renderer.SetupVertexBufferDescription(vertexBufferDescriptions);
    SDL_GPUVertexAttribute vertexAttributes[4];
    renderer.SetupVertexAttributes(vertexAttributes);
    SDL_GPUColorTargetDescription colorTargetDescriptions[1];
    renderer.SetupColorTargetDescription(colorTargetDescriptions, device, window);

    SDL_GPUGraphicsPipeline* pipeline = renderer.CreateGraphicsPipeline(
        *device, vertexShader, fragmentShader, vertexBufferDescriptions, vertexAttributes, colorTargetDescriptions);

    SDL_ReleaseGPUShader(device, vertexShader);
    SDL_ReleaseGPUShader(device, fragmentShader);

    ResourceManager resources(device);

    auto errorTexture = resources.CreateErrorTexture(device);

    std::vector<GameObject> scene;

    std::vector<Mesh*> boxMeshes = resources.LoadModel("assets/box/scene.gltf");
    std::vector<Mesh*> redBoxM = resources.LoadModel("assets/red_cube/red_cube.gltf");

    if (!boxMeshes.empty()) {
        Mesh* boxMesh = boxMeshes[0];
        Mesh* redBox = redBoxM[0];
        // Добавляем объекты
        scene.push_back({boxMesh, {0, 0, -5}, {0, 0, 0}, {0.01f, 0.01f, 0.01f}});   // Центр
        scene.push_back({boxMesh, {2, 0, -5}, {0, 45, 0}, {0.01f, 0.01f, 0.01f}});  // Справа
        scene.push_back({redBox, {-2, 1, -6}, {45, 0, 0}, {1, 1, 1}});              // Слева
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

    uint64_t lastTime = SDL_GetTicksNS();

    bool running = true;
    while (running) {
        uint64_t currentTime = SDL_GetTicksNS();
        double deltaTime = (currentTime - lastTime) / 1000000000.0;
        lastTime = currentTime;

        UpdateCamera(camera, running, deltaTime);
        SDL_SetWindowRelativeMouseMode(window, true);

        // Начало отрисовки кадра
        SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(device);
        SDL_GPUTexture* swapchainTexture;
        Uint32 w, h;

        if (!SDL_WaitAndAcquireGPUSwapchainTexture(cmd, window, &swapchainTexture, &w, &h)) {
            // Если не удалось получить текстуру (например, окно свернуто), просто
            // сабмитим пустой буфер
            SDL_SubmitGPUCommandBuffer(cmd);
            continue;
        }

        // Настройка прохода рендера (Render Pass)
        SDL_GPUColorTargetInfo colorTargetInfo{};
        colorTargetInfo.texture = swapchainTexture;
        colorTargetInfo.clear_color = {0.5f, 0.5f, 0.8f, 1.0f};
        colorTargetInfo.load_op = SDL_GPU_LOADOP_CLEAR;  // <--- ОЧИСТКА ЭКРАНА
        colorTargetInfo.store_op = SDL_GPU_STOREOP_STORE;

        SDL_GPUDepthStencilTargetInfo depthTargetInfo{};
        depthTargetInfo.texture = depthTexture;
        depthTargetInfo.clear_depth = 1.0f;
        depthTargetInfo.load_op = SDL_GPU_LOADOP_CLEAR;  // <--- ОЧИСТКА ГЛУБИНЫ
        depthTargetInfo.store_op = SDL_GPU_STOREOP_STORE;
        depthTargetInfo.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
        depthTargetInfo.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;

        SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(cmd, &colorTargetInfo, 1, &depthTargetInfo);

        // Биндим пайплайн (один раз, если он общий для всех кубов)
        SDL_BindGPUGraphicsPipeline(pass, pipeline);

        // Матрицы камеры (View/Proj общие для всего кадра)
        glm::mat4 view = glm::lookAt(camera.pos, camera.pos + camera.front, camera.up);
        glm::mat4 proj = glm::perspective(glm::radians(70.0f), (float)WindowWidth / WindowHeight, 0.1f, 100.0f);

        LightUniforms lightData{};
        lightData.lightPos = glm::vec4(2.0f, 4.0f, 3.0f, 1.0f);    // Лампочка справа-сверху
        lightData.lightColor = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);  // Белый свет
        lightData.viewPos = glm::vec4(camera.pos, 1.0f);           // Позиция камеры (на будущее)

        // Отправляем данные во фрагментный шейдер (Slot 0 соответствует binding 0 в шейдере)
        SDL_PushGPUFragmentUniformData(cmd, 0, &lightData, sizeof(LightUniforms));
        // РИСУЕМ ВСЕ ОБЪЕКТЫ
        for (auto& obj : scene) {
            if (!obj.mesh) continue;

            // 1. Uniforms (MVP)
            glm::mat4 model = obj.GetModelMatrix();
            model = glm::rotate(model, SDL_GetTicks() / 1000.0f, glm::vec3(0.0f, 1.0f, 0.0f));
            UniformBufferObject ubo{};
            ubo.proj = proj;
            ubo.view = view;
            ubo.model = model;

            SDL_PushGPUVertexUniformData(cmd, 0, &ubo, sizeof(UniformBufferObject));

            SDL_GPUTexture* textureToBind = (obj.mesh->texture) ? obj.mesh->texture->handle : errorTexture;

            SDL_GPUTextureSamplerBinding tsb = {textureToBind, commonSampler};
            SDL_BindGPUFragmentSamplers(pass, 0, &tsb, 1);

            // 3. Вершины и Индексы
            SDL_GPUBufferBinding vb = {obj.mesh->vertexBuffer, 0};
            SDL_BindGPUVertexBuffers(pass, 0, &vb, 1);

            SDL_GPUBufferBinding ib = {obj.mesh->indexBuffer, 0};
            SDL_BindGPUIndexBuffer(pass, &ib, SDL_GPU_INDEXELEMENTSIZE_32BIT);  // Убедись что индексы Uint32

            // 4. Draw call
            SDL_DrawGPUIndexedPrimitives(pass, obj.mesh->numIndices, 1, 0, 0, 0);
        }

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
