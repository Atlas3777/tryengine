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
#include "GraphicsContext.hpp"
#include "Renderer.hpp"
#include "ResourceManager.hpp"

void UpdateCameraSystem(entt::registry& reg, double deltaTime, const InputState& input) {
    auto view = reg.view<TransformComponent, CameraComponent>();

    for (auto entity : view) {
        auto& transform = view.get<TransformComponent>(entity);
        auto& cam = view.get<CameraComponent>(entity);

        if (!cam.isActive) continue;

        // 1. Поворот через мышь (используем mouseDelta из твоего InputState)
        if (input.isCursorCaptured) {
            transform.rotation.y += input.mouseDeltaX * cam.sensitivity;  // Yaw
            transform.rotation.x -= input.mouseDeltaY * cam.sensitivity;  // Pitch
            transform.rotation.x = std::clamp(transform.rotation.x, -89.0f, 89.0f);
        }

        // 2. Обновление векторов направления
        glm::vec3 front;
        front.x = std::cos(glm::radians(transform.rotation.y)) * std::cos(glm::radians(transform.rotation.x));
        front.y = std::sin(glm::radians(transform.rotation.x));
        front.z = std::sin(glm::radians(transform.rotation.y)) * std::cos(glm::radians(transform.rotation.x));

        cam.front = glm::normalize(front);
        cam.right = glm::normalize(glm::cross(cam.front, glm::vec3(0, 1, 0)));
        cam.up = glm::normalize(glm::cross(cam.right, cam.front));

        // 3. Движение (используем IsKeyDown из твоего InputState)
        float moveSpeed = cam.speed * static_cast<float>(deltaTime);

        if (input.IsKeyDown(SDL_SCANCODE_W)) transform.position += cam.front * moveSpeed;
        if (input.IsKeyDown(SDL_SCANCODE_S)) transform.position -= cam.front * moveSpeed;
        if (input.IsKeyDown(SDL_SCANCODE_A)) transform.position -= cam.right * moveSpeed;
        if (input.IsKeyDown(SDL_SCANCODE_D)) transform.position += cam.right * moveSpeed;
        if (input.IsKeyDown(SDL_SCANCODE_E)) transform.position += cam.up * moveSpeed;
        if (input.IsKeyDown(SDL_SCANCODE_Q)) transform.position -= cam.up * moveSpeed;
    }
}
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
    auto mainCamera = reg.create();
    reg.emplace<TransformComponent>(mainCamera, glm::vec3(0, 2, 10), glm::vec3(0, -90, 0));
    reg.emplace<CameraComponent>(mainCamera);

    while (engine.isRunning) {
        engine.ProcessInput();
        engine.UpdateTime();
        engine.DispatchCommands();

        UpdateCameraSystem(reg, engine.time.deltaTime, engine.input);
        engine.Render(reg, [&](SDL_GPUCommandBuffer* cmd, RenderTarget* target) {
            auto camView = reg.view<TransformComponent, CameraComponent>();

            glm::mat4 viewMat = glm::mat4(1.0f);
            glm::mat4 projMat = glm::mat4(1.0f);
            glm::vec3 camPos = glm::vec3(0.0f);

            for (auto entity : camView) {
                auto [transform, camera] = camView.get<TransformComponent, CameraComponent>(entity);
                if (camera.isActive) {
                    float aspect = (float)target->GetWidth() / (float)target->GetHeight();
                    projMat = glm::perspective(glm::radians(camera.fov), aspect, camera.nearPlane, camera.farPlane);
                    viewMat = glm::lookAt(transform.position, transform.position + camera.front, camera.up);
                    camPos = transform.position;
                    break;
                }
            }

            SDL_GPURenderPass* scenePass = renderer.BeginRenderPass(cmd, *target, {0.69f, 0.77f, 0.87f, 1.0f});
            SDL_BindGPUGraphicsPipeline(scenePass, renderer.GetDefaultPipeline());

            // float aspect = (float)target->GetWidth() / (float)target->GetHeight();
            // glm::mat4 proj = glm::perspective(glm::radians(70.0f), aspect, 0.1f, 100.0f);
            // glm::mat4 view = glm::lookAt(camera.pos, camera.pos + camera.front, camera.up);

            LightUniforms lightData{};
            lightData.lightPos = glm::vec4(2.0f, 30.0f, 3.0f, 1.0f);
            lightData.lightColor = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
            lightData.viewPos = glm::vec4(camPos, 1.0f);

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
                ubo.proj = projMat;
                ubo.view = viewMat;
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
    resources.Cleanup();
    renderer.Cleanup();
    return 0;
}
