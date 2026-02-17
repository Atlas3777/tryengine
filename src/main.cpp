#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <entt/entt.hpp>
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

    entt::registry reg;

    std::vector<Mesh*> boxMeshes = resources.LoadModel("assets/fantasy_game_inn/scene.gltf");
    std::vector<Mesh*> redBoxM = resources.LoadModel("assets/red_cube/red_cube.gltf");
    std::vector<Mesh*> g = resources.LoadModel("assets/matilda/scene.gltf");

    if (!boxMeshes.empty() && !redBoxM.empty()) {
        // Создаем первую сущность (Box 1)
        auto e1 = reg.create();
        reg.emplace<MeshComponent>(e1, boxMeshes[0]);
        reg.emplace<TransformComponent>(e1, nullptr, glm::vec3(-25.0f, -5.0f, 0), glm::vec3(0, 0, 0), glm::vec3(1.f));

        // // Создаем вторую сущность (Box 2)
        // for (uint i = 0; i < g.size(); ++i) {
        auto e2 = reg.create();
        reg.emplace<MeshComponent>(e2, g[0]);
        reg.emplace<TransformComponent>(e2, nullptr, glm::vec3(2, 0, -5), glm::vec3(0, 0, 0), glm::vec3(0.01f));
        // }

        // Создаем третью сущность (Red Box)
        auto e3 = reg.create();
        reg.emplace<MeshComponent>(e3, redBoxM[0]);
        reg.emplace<TransformComponent>(e3, nullptr, glm::vec3(-2, 1, -6), glm::vec3(0, 0, 0), glm::vec3(1, 1, 1));
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
        lightData.lightPos = glm::vec4(2.0f, 30.0f, 3.0f, 1.0f);   // Лампочка справа-сверху
        lightData.lightColor = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);  // Белый свет
        lightData.viewPos = glm::vec4(camera.pos, 1.0f);           // Позиция камеры (на будущее)

        // Отправляем данные во фрагментный шейдер (Slot 0 соответствует binding 0 в шейдере)
        SDL_PushGPUFragmentUniformData(ctx.cmd, 0, &lightData, sizeof(LightUniforms));

        auto view_entities = reg.view<TransformComponent, MeshComponent>();

        for (auto entity : view_entities) {
            auto& transform = view_entities.get<TransformComponent>(entity);
            auto& meshComp = view_entities.get<MeshComponent>(entity);

            if (!meshComp.mesh) continue;
            glm::mat4 model = transform.GetModelMatrix();
            // model = glm::rotate(model, (float)totalTime, glm::vec3(0.0f, 1.0f, 0.0f));
            glm::mat4 normalMatrix = glm::inverseTranspose(model);
            UniformBufferObject ubo{};
            ubo.proj = proj;
            ubo.view = view;
            ubo.model = model;
            ubo.normalMatrix = normalMatrix;
            SDL_PushGPUVertexUniformData(ctx.cmd, 0, &ubo, sizeof(UniformBufferObject));

            // 2. Текстуры (лучше вынести загрузку из цикла рендера!)
            // Texture* debugTex = resources.LoadTexture("assets/test.png");
            SDL_GPUTextureSamplerBinding tsb = {meshComp.mesh->texture->handle, renderer.GetCommonSampler()};
            // SDL_GPUTextureSamplerBinding tsb = {debugTex->handle, renderer.GetCommonSampler()};
            SDL_BindGPUFragmentSamplers(ctx.pass, 0, &tsb, 1);

            // 3. Drawing
            SDL_GPUBufferBinding vb = {meshComp.mesh->vertexBuffer, 0};
            SDL_BindGPUVertexBuffers(ctx.pass, 0, &vb, 1);

            SDL_GPUBufferBinding ib = {meshComp.mesh->indexBuffer, 0};
            SDL_BindGPUIndexBuffer(ctx.pass, &ib, SDL_GPU_INDEXELEMENTSIZE_32BIT);

            SDL_DrawGPUIndexedPrimitives(ctx.pass, meshComp.mesh->numIndices, 1, 0, 0, 0);
        }
        renderer.EndFrame(ctx);
    }

    resources.Cleanup();  // Удаляет меши и текстуры
    renderer.Cleanup();   // Удаляет пайплайн, семплер, текстуру глубины
    windowManager.Terminate();
    return 0;
}
