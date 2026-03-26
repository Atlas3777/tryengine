#include "engine/graphics/RenderSystem.hpp"

#include <glm/gtc/matrix_inverse.inl>

#include "engine/core/Components.hpp"
#include "engine/graphics/Types.hpp"

namespace engine::graphics {

void RenderSystem::RenderScene(entt::registry& reg, entt::entity camera_entity, RenderTarget* target, SDL_GPUCommandBuffer* cmd) {
    // this->RenderPreprocessor->BuildView(reg);

    auto camView = reg.view<Camera, Transform>();

    auto& camTransform = camView.get<Transform>(camera_entity);
    auto& cameraComp = camView.get<Camera>(camera_entity);

    float aspect = static_cast<float>(target->GetWidth()) / static_cast<float>(target->GetHeight());
    glm::mat4 projMat = glm::perspective(glm::radians(cameraComp.fov), aspect, cameraComp.near_plane, cameraComp.far_plane);
    glm::mat4 viewMat = cameraComp.view_matrix;
    glm::vec3 camPos = camTransform.position;

    // SDL_GPURenderPass* scenePass = renderer.BeginRenderPass(cmd, *target, {0.69f, 0.77f, 0.87f, 1.0f});
    SDL_GPUColorTargetInfo colorInfo{};
    colorInfo.texture = target->GetColor();
    colorInfo.clear_color = {0.69f, 0.77f, 0.87f, 1.0f};
    colorInfo.load_op = SDL_GPU_LOADOP_CLEAR;
    colorInfo.store_op = SDL_GPU_STOREOP_STORE;

    SDL_GPUDepthStencilTargetInfo depthInfo{};
    depthInfo.texture = target->GetDepth();
    depthInfo.clear_depth = 1.0f;
    depthInfo.load_op = SDL_GPU_LOADOP_CLEAR;
    depthInfo.store_op = SDL_GPU_STOREOP_STORE;
    depthInfo.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
    depthInfo.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;
    SDL_GPURenderPass* scenePass = SDL_BeginGPURenderPass(cmd, &colorInfo, 1, &depthInfo);

    struct LightUniforms {
        glm::vec4 lightPos;    // Позиция света (w не используем)
        glm::vec4 lightColor;  // Цвет света (w можно использовать как интенсивность)
        glm::vec4 viewPos;     // Позиция камеры (понадобится для бликов/Specular, добавим сразу)
    } lightData{};
    lightData.lightPos = glm::vec4(2.0f, 30.0f, 3.0f, 1.0f);
    lightData.lightColor = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
    lightData.viewPos = glm::vec4(camPos, 1.0f);

    SDL_PushGPUFragmentUniformData(cmd, 0, &lightData, sizeof(LightUniforms));

    SDL_BindGPUGraphicsPipeline(scenePass, renderer_->GetDefaultPipeline());

    for (auto view_entities = reg.view<Transform, MeshFilter>(); auto entity : view_entities) {
        auto& transform = view_entities.get<Transform>(entity);
        auto& meshRenderer = view_entities.get<MeshFilter>(entity);


        struct alignas(16) CombinedUBO {
            glm::mat4 view;
            glm::mat4 proj;
            glm::mat4 model;
            glm::mat4 normalMatrix;
        } ubo{};

        ubo.view = viewMat;
        ubo.proj = projMat;
        ubo.model = transform.world_matrix;
        ubo.normalMatrix = glm::inverseTranspose(ubo.model);

        // SDL_PushGPUVertexUniformData(cmd, 0, &ubo, sizeof(CombinedUBO));
        // SDL_GPUTextureSamplerBinding tsb = {renderer_->, renderer_->GetCommonSampler()};
        // SDL_BindGPUFragmentSamplers(scenePass, 0, &tsb, 1);

        SDL_GPUBufferBinding vb = {meshRenderer.mesh->vertex_buffer, 0};
        SDL_BindGPUVertexBuffers(scenePass, 0, &vb, 1);

        SDL_GPUBufferBinding ib = {meshRenderer.mesh->index_buffer, 0};
        SDL_BindGPUIndexBuffer(scenePass, &ib, SDL_GPU_INDEXELEMENTSIZE_32BIT);

        SDL_DrawGPUIndexedPrimitives(scenePass, meshRenderer.mesh->num_indices, 1, 0, 0, 0);
    }
    SDL_EndGPURenderPass(scenePass);
}
}  // namespace engine::graphics
