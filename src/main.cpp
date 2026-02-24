#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlgpu3.h>

#include <algorithm>
#include <entt/entt.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <vector>

#include "Engine.hpp"
#include "EngineConfig.hpp"
#include "EngineTypes.hpp"
#include "ResourceManager.hpp"
#include "core/camera.hpp"
#include "render/renderer.hpp"

int main(int argc, char* argv[]) {
    const auto config = EngineConfig::Parse(argc, argv);
    Engine engine(config);
    engine.MountHardware();

    Renderer renderer;
    renderer.Init(engine.GetGraphicsContext().GetDevice());

    ResourceManager resources(engine.GetGraphicsContext().GetDevice());
    std::vector<Mesh*> boxMeshes = resources.LoadModel("assets/fantasy_game_inn/scene.gltf");
    std::vector<Mesh*> redBoxM = resources.LoadModel("assets/red_cube/red_cube.gltf");
    std::vector<Mesh*> matilda = resources.LoadModel("assets/matilda/scene.gltf");

    entt::registry reg;

    if (!boxMeshes.empty() && !redBoxM.empty()) {
        auto e1 = reg.create();
        reg.emplace<MeshComponent>(e1, boxMeshes[0]);
        reg.emplace<TransformComponent>(e1, glm::vec3(-31.f, -5.0f, -7), glm::vec3(0, 0, 0), glm::vec3(1));

        for (uint i = 0; i < matilda.size(); ++i) {
            auto e2 = reg.create();
            reg.emplace<MeshComponent>(e2, matilda[i]);
            reg.emplace<TransformComponent>(e2, glm::vec3(2, 0, -5), glm::vec3(0, 0, 0), glm::vec3(0.01f));
        }

        auto e3 = reg.create();
        reg.emplace<MeshComponent>(e3, redBoxM[0]);
        reg.emplace<TransformComponent>(e3, glm::vec3(-2, 1, -6), glm::vec3(0, 0, 0), glm::vec3(1, 1, 1));
    }

    Camera camera;
    camera.pos = glm::vec3(0.0f, 0.0f, -3.0f);

    uint64_t lastTime = SDL_GetTicksNS();
    double fpsTimer = 0.0;
    int frameCount = 0;

    // SDL_SetWindowRelativeMouseMode(windowManager.GetWindow(), true);

    while (engine.context->running) {
        engine.ProcessInput();

        uint64_t currentTime = SDL_GetTicksNS();
        double deltaTime = static_cast<double>(currentTime - lastTime) / 1000000000.0;
        lastTime = currentTime;
        // double totalTime = static_cast<double>(currentTime) / 1000000000.0;

        fpsTimer += deltaTime;
        frameCount++;

        if (fpsTimer >= 1.0) {
            SDL_Log("FPS: %d (ms per frame: %.3f)", frameCount, 1000.0 / frameCount);
            fpsTimer -= 1.0;
            frameCount = 0;
        }

        if (engine.context->isCursorCaptured) {
            camera.yaw += engine.context->mouseDeltaX * camera.sensitivity;
            camera.pitch -= engine.context->mouseDeltaY * camera.sensitivity;
            camera.pitch = std::clamp(camera.pitch, -89.0f, 89.0f);
        }
        UpdateCamera(camera, deltaTime);

        engine.Render(reg, [&](SDL_GPUCommandBuffer* cmd, RenderTarget* target) {
            SDL_GPURenderPass* scenePass = renderer.BeginRenderPass(cmd, *target, {0.69f, 0.77f, 0.87f, 1.0f});
            SDL_BindGPUGraphicsPipeline(scenePass, renderer.GetDefaultPipeline());

            float aspect = (float)target->GetWidth() / (float)target->GetHeight();
            glm::mat4 proj = glm::perspective(glm::radians(70.0f), aspect, 0.1f, 100.0f);
            glm::mat4 view = glm::lookAt(camera.pos, camera.pos + camera.front, camera.up);

            LightUniforms lightData{};
            lightData.lightPos = glm::vec4(2.0f, 30.0f, 3.0f, 1.0f);   // Лампочка справа-сверху
            lightData.lightColor = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);  // Белый свет
            lightData.viewPos = glm::vec4(camera.pos, 1.0f);           // Позиция камеры (на будущее)

            // Отправляем данные во фрагментный шейдер (Slot 0 соответствует binding 0 в шейдере)
            SDL_PushGPUFragmentUniformData(cmd, 0, &lightData, sizeof(LightUniforms));

            auto view_entities = reg.view<TransformComponent, MeshComponent>();

            for (auto entity : view_entities) {
                auto& transform = view_entities.get<TransformComponent>(entity);
                auto& meshComp = view_entities.get<MeshComponent>(entity);

                if (meshComp.mesh == nullptr) {
                    continue;
                }
                glm::mat4 model = transform.GetModelMatrix();
                // model = glm::rotate(model, (float)totalTime, glm::vec3(0.0f, 1.0f, 0.0f));
                glm::mat4 normalMatrix = glm::inverseTranspose(model);
                UniformBufferObject ubo{};
                ubo.proj = proj;
                ubo.view = view;
                ubo.model = model;
                ubo.normalMatrix = normalMatrix;
                SDL_PushGPUVertexUniformData(cmd, 0, &ubo, sizeof(UniformBufferObject));

                SDL_GPUTextureSamplerBinding tsb = {meshComp.mesh->texture->handle, renderer.GetCommonSampler()};
                SDL_BindGPUFragmentSamplers(scenePass, 0, &tsb, 1);

                SDL_GPUBufferBinding vb = {meshComp.mesh->vertexBuffer, 0};
                SDL_BindGPUVertexBuffers(scenePass, 0, &vb, 1);

                SDL_GPUBufferBinding ib = {meshComp.mesh->indexBuffer, 0};
                SDL_BindGPUIndexBuffer(scenePass, &ib, SDL_GPU_INDEXELEMENTSIZE_32BIT);

                SDL_DrawGPUIndexedPrimitives(scenePass, meshComp.mesh->numIndices, 1, 0, 0, 0);
            }
            SDL_EndGPURenderPass(scenePass);
        });
    }

    SDL_WaitForGPUIdle(engine.GetGraphicsContext().GetDevice());
    // ImGui_ImplSDLGPU3_Shutdown();
    // ImGui_ImplSDL3_Shutdown();
    // ImGui::DestroyContext();
    resources.Cleanup();  // Удаляет меши и текстуры
    renderer.Cleanup();   // Удаляет пайплайн, семплер, текстуру глубины
    return 0;
}
