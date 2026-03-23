#include "engine/graphics/RenderSystem.hpp"

namespace engine::graphics {
void RenderSystem::BeginFrame() {}
void RenderSystem::EndFrame() {}

void RenderSystem::RenderScene(entt::registry& reg, entt::entity camera, RenderTarget* target) {
    // this->RenderPreprocessor->BuildView(reg);


    auto camView = reg.view<editor::EditorCameraTag, Transform, Camera>();
    entt::entity camEnt = camView.front();

    if (camEnt == entt::null) return;

    auto& camTransform = camView.get<Transform>(camEnt);
    auto& camera = camView.get<Camera>(camEnt);

    float aspect = static_cast<float>(target->GetWidth()) / static_cast<float>(target->GetHeight());
    glm::mat4 projMat = glm::perspective(glm::radians(camera.fov), aspect, camera.nearPlane, camera.farPlane);
    glm::mat4 viewMat = camera.viewMatrix;
    glm::vec3 camPos = camTransform.position;

    SDL_GPURenderPass* scenePass = renderer.BeginRenderPass(cmd, *target, {0.69f, 0.77f, 0.87f, 1.0f});

    engine::LightUniforms lightData{};
    lightData.lightPos = glm::vec4(2.0f, 30.0f, 3.0f, 1.0f);
    lightData.lightColor = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
    lightData.viewPos = glm::vec4(camPos, 1.0f);

    SDL_PushGPUFragmentUniformData(cmd, 0, &lightData, sizeof(engine::LightUniforms));

    SDL_BindGPUGraphicsPipeline(scenePass, renderer.GetDefaultPipeline());

    for (auto view_entities = reg.view<Transform, MeshRenderer>(); auto entity : view_entities) {
        auto& transform = view_entities.get<Transform>(entity);
        auto& [mesh] = view_entities.get<MeshRenderer>(entity);

        if (mesh == nullptr) {
            continue;
        }

        struct alignas(16) CombinedUBO {
            glm::mat4 view;
            glm::mat4 proj;
            glm::mat4 model;
            glm::mat4 normalMatrix;
        } ubo{};

        ubo.view = viewMat;
        ubo.proj = projMat;
        ubo.model = transform.worldMatrix;
        ubo.normalMatrix = glm::inverseTranspose(ubo.model);

        SDL_PushGPUVertexUniformData(cmd, 0, &ubo, sizeof(CombinedUBO));
        SDL_GPUTextureSamplerBinding tsb = {mesh->texture->handle, renderer.GetCommonSampler()};
        SDL_BindGPUFragmentSamplers(scenePass, 0, &tsb, 1);

        SDL_GPUBufferBinding vb = {mesh->vertexBuffer, 0};
        SDL_BindGPUVertexBuffers(scenePass, 0, &vb, 1);

        SDL_GPUBufferBinding ib = {mesh->indexBuffer, 0};
        SDL_BindGPUIndexBuffer(scenePass, &ib, SDL_GPU_INDEXELEMENTSIZE_32BIT);

        SDL_DrawGPUIndexedPrimitives(scenePass, mesh->numIndices, 1, 0, 0, 0);
    }
    SDL_EndGPURenderPass(scenePass);
}
}  // namespace engine::graphics
