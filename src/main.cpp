#include <imgui.h>

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <entt/entt.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlgpu3.h>
#include <vector>

#include "Engine.hpp"
#include "EngineConfig.hpp"
#include "EngineTypes.hpp"
#include "GraphicsContext.hpp"
#include "Renderer.hpp"
#include "ResourceManager.hpp"
#include "systems/Systems.hpp"

int main(int argc, char* argv[]) {
    const auto config = EngineConfig::Parse(argc, argv);
    Engine engine(config);
    engine.MountHardware();

    Renderer renderer;
    renderer.Init(engine.GetGraphicsContext().GetDevice());

    ResourceManager resources(engine.GetGraphicsContext().GetDevice());
    std::vector<Mesh*> taverna = resources.LoadModel("assets/fantasy_game_inn/scene.gltf");
    std::vector<Mesh*> redBoxM = resources.LoadModel("assets/red_cube/red_cube.gltf");
    // std::vector<Mesh*> matilda = resources.LoadModel("assets/matilda/scene.gltf");
    std::vector<Mesh*> skull = resources.LoadModel("assets/skull_downloadable/scene.gltf");

    entt::registry reg;

    auto e1 = reg.create();
    reg.emplace<MeshComponent>(e1, taverna[0]);
    reg.emplace<TransformComponent>(e1, glm::vec3(-31.f, -5.0f, -7), glm::vec3(0, 0, 0), glm::vec3(1));
    reg.emplace<HierarchyComponent>(e1);

    auto e2 = reg.create();
    reg.emplace<MeshComponent>(e2, skull[0]);
    reg.emplace<TransformComponent>(e2, glm::vec3(1.f, 2.f, 3.f), glm::vec3(0, 0, 0), glm::vec3(1));
    reg.emplace<HierarchyComponent>(e2);
    // for (uint i = 0; i < matilda.size(); ++i) {
    //     auto e2 = reg.create();
    //     reg.emplace<MeshComponent>(e2, matilda[i]);
    //     reg.emplace<TransformComponent>(e2, glm::vec3(2, 0, -5), glm::vec3(0, 0, 0), glm::vec3(0.01f));
    // }

    auto e3 = reg.create();
    reg.emplace<MeshComponent>(e3, redBoxM[0]);
    reg.emplace<TransformComponent>(e3, glm::vec3(-2, 1, -6), glm::vec3(0, 0, 0), glm::vec3(1, 1, 1));
    reg.emplace<HierarchyComponent>(e3, e2, 1);

    auto mainCamera = reg.create();
    reg.emplace<TransformComponent>(mainCamera, glm::vec3(0, 2, 10), glm::vec3(0, -90, 0));
    reg.emplace<CameraComponent>(mainCamera);
    reg.emplace<EditorCameraTag>(mainCamera);

    while (engine.isRunning) {
        engine.ProcessInput();
        engine.UpdateTime();
        engine.DispatchCommands();

        UpdateEditorCameraSystem(reg, engine.time.deltaTime, engine.input);
        UpdateTransformSystem(reg);

        engine.Render(reg, [&](SDL_GPUCommandBuffer* cmd, RenderTarget* target) {
            auto camView = reg.view<EditorCameraTag, TransformComponent, CameraComponent>();
            entt::entity camEnt = camView.front();

            if (camEnt == entt::null) return;

            auto& transform = camView.get<TransformComponent>(camEnt);
            auto& camera = camView.get<CameraComponent>(camEnt);

            float aspect = (float)target->GetWidth() / (float)target->GetHeight();
            glm::mat4 projMat = glm::perspective(glm::radians(camera.fov), aspect, camera.nearPlane, camera.farPlane);
            glm::mat4 viewMat = camera.viewMatrix;
            glm::vec3 camPos = transform.position;

            SDL_GPURenderPass* scenePass = renderer.BeginRenderPass(cmd, *target, {0.69f, 0.77f, 0.87f, 1.0f});
            SDL_BindGPUGraphicsPipeline(scenePass, renderer.GetDefaultPipeline());

            LightUniforms lightData{};
            lightData.lightPos = glm::vec4(2.0f, 30.0f, 3.0f, 1.0f);
            lightData.lightColor = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
            lightData.viewPos = glm::vec4(camPos, 1.0f);

            SDL_PushGPUFragmentUniformData(cmd, 0, &lightData, sizeof(LightUniforms));

            auto view_entities = reg.view<TransformComponent, MeshComponent>();

            for (auto entity : view_entities) {
                auto& transform = view_entities.get<TransformComponent>(entity);
                auto& meshComp = view_entities.get<MeshComponent>(entity);

                if (meshComp.mesh == nullptr) {
                    continue;
                }

                struct alignas(16) CombinedUBO {
                    glm::mat4 view;
                    glm::mat4 proj;
                    glm::mat4 model;
                    glm::mat4 normalMatrix;
                } ubo;

                // Заполняем данными
                ubo.view = viewMat;
                ubo.proj = projMat;
                ubo.model = transform.worldMatrix;
                ubo.normalMatrix = glm::inverseTranspose(ubo.model);

                SDL_PushGPUVertexUniformData(cmd, 0, &ubo, sizeof(CombinedUBO));
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
    resources.Cleanup();
    renderer.Cleanup();
    return 0;
}
