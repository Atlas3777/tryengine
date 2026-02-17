#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <glm/gtc/matrix_inverse.hpp>
#include <vector>

#include "EngineTypes.hpp"
#include "ResourceManager.hpp"
#include "core/camera.hpp"
#include "core/window_manager.hpp"
#include "render/renderer.hpp"

constexpr uint WindowWidth = 1280;
constexpr uint WindowHeight = 720;

int main(int, char*[]) {
    WindowManager windowManager;
    if (!windowManager.Initialize(WindowWidth, WindowHeight, "tryengine")) {
        SDL_Log("Failed to initialize WindowManager");
        return -1;
    }

    Renderer renderer;
    renderer.Init(windowManager);

    ResourceManager resources(windowManager.device);

    std::vector<TransformComponent> scene;
    std::vector<Mesh*> boxMeshes = resources.LoadModel("assets/box/scene.gltf");
    std::vector<Mesh*> redBoxM = resources.LoadModel("assets/red_cube/red_cube.gltf");

    if (!boxMeshes.empty() && !redBoxM.empty()) {
        Mesh* boxMesh = boxMeshes[0];
        Mesh* redBox = redBoxM[0];
        scene.push_back({boxMesh, {0, 0, -5}, {222, 0, 0}, {0.01f, 0.01f, 0.01f}});
        scene.push_back({boxMesh, {2, 0, -5}, {0, 45, 0}, {0.01f, 0.01f, 0.01f}});
        scene.push_back({redBox, {-2, 1, -6}, {0, 0, 0}, {1, 1, 1}});
    } else {
        SDL_Log("Warning: No meshes loaded!");
    }

    Camera camera;
    camera.pos = glm::vec3(0.0f, 0.0f, -3.0f);

    uint64_t lastTime = SDL_GetTicksNS();
    double fpsTimer = 0.0;
    int frameCount = 0;
    bool running = true;

    SDL_SetWindowRelativeMouseMode(windowManager.GetWindow(), true);

    // --- ИГРОВОЙ ЦИКЛ ---
    while (running) {
        uint64_t currentTime = SDL_GetTicksNS();
        double deltaTime = static_cast<double>(currentTime - lastTime) / 1000000000.0;
        lastTime = currentTime;
        double totalTime = static_cast<double>(currentTime) / 1000000000.0;

        fpsTimer += deltaTime;
        frameCount++;

        if (fpsTimer >= 1.0) {
            SDL_Log("FPS: %d (ms per frame: %.3f)", frameCount, 1000.0 / frameCount);
            fpsTimer -= 1.0;
            frameCount = 0;
        }

        UpdateCamera(camera, running, deltaTime);

        auto ctx = renderer.BeginFrame();
        // Биндим пайплайн (один раз, если он общий для всех кубов)
        SDL_BindGPUGraphicsPipeline(ctx.pass, renderer.GetDefaultPipeline());

        // Матрицы камеры (View/Proj общие для всего кадра)
        glm::mat4 view = glm::lookAt(camera.pos, camera.pos + camera.front, camera.up);
        glm::mat4 proj = glm::perspective(glm::radians(70.0f), (float)WindowWidth / WindowHeight, 0.1f, 100.0f);

        LightUniforms lightData{};
        lightData.lightPos = glm::vec4(2.0f, 4.0f, 3.0f, 1.0f);    // Лампочка справа-сверху
        lightData.lightColor = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);  // Белый свет
        lightData.viewPos = glm::vec4(camera.pos, 1.0f);           // Позиция камеры (на будущее)

        // Отправляем данные во фрагментный шейдер (Slot 0 соответствует binding 0 в шейдере)
        SDL_PushGPUFragmentUniformData(ctx.cmd, 0, &lightData, sizeof(LightUniforms));

        // РИСУЕМ ВСЕ ОБЪЕКТЫ
        for (auto& obj : scene) {
            if (!obj.mesh) continue;

            // 1. Uniforms (MVP)
            glm::mat4 model = obj.GetModelMatrix();
            model = glm::rotate(model, (float)totalTime, glm::vec3(0.0f, 1.0f, 0.0f));
            glm::mat4 normalMatrix = glm::inverseTranspose(model);
            UniformBufferObject ubo{};
            ubo.proj = proj;
            ubo.view = view;
            ubo.model = model;
            ubo.normalMatrix = normalMatrix;

            SDL_PushGPUVertexUniformData(ctx.cmd, 0, &ubo, sizeof(UniformBufferObject));

            SDL_GPUTexture* textureToBind = obj.mesh->texture->handle;

            SDL_GPUTextureSamplerBinding tsb = {textureToBind, renderer.GetCommonSampler()};
            SDL_BindGPUFragmentSamplers(ctx.pass, 0, &tsb, 1);

            // 3. Вершины и Индексы
            SDL_GPUBufferBinding vb = {obj.mesh->vertexBuffer, 0};
            SDL_BindGPUVertexBuffers(ctx.pass, 0, &vb, 1);

            SDL_GPUBufferBinding ib = {obj.mesh->indexBuffer, 0};
            SDL_BindGPUIndexBuffer(ctx.pass, &ib, SDL_GPU_INDEXELEMENTSIZE_32BIT);

            // 4. Draw call
            SDL_DrawGPUIndexedPrimitives(ctx.pass, obj.mesh->numIndices, 1, 0, 0, 0);
        }
        renderer.EndFrame(ctx);
    }

    renderer.Cleanup();   // Удаляет пайплайн, семплер, текстуру глубины
    resources.Cleanup();  // Удаляет меши и текстуры
    windowManager.Terminate();
    return 0;
}
