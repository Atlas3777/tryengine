#include "engine/graphics/RenderSystem.hpp"

#include <glm/gtc/matrix_inverse.inl>

#include "engine/core/Components.hpp"
#include "engine/graphics/MaterialSystem.hpp"
#include "engine/graphics/Types.hpp"

namespace tryengine::graphics {

void RenderSystem::RenderScene(entt::registry& reg, entt::entity camera_entity, RenderTarget* target, SDL_GPUCommandBuffer* cmd) {
    auto camView = reg.view<Camera, Transform>();
    auto& camTransform = camView.get<Transform>(camera_entity);
    auto& cameraComp = camView.get<Camera>(camera_entity);

    float aspect = static_cast<float>(target->GetWidth()) / static_cast<float>(target->GetHeight());
    glm::mat4 projMat = glm::perspective(glm::radians(cameraComp.fov), aspect, cameraComp.near_plane, cameraComp.far_plane);
    glm::mat4 viewMat = cameraComp.view_matrix;
    glm::vec3 camPos = camTransform.position;

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

    // 1. Глобальные Uniforms (Слот 0 фрагментного шейдера)
    struct LightUniforms {
        glm::vec4 lightPos;
        glm::vec4 lightColor;
        glm::vec4 viewPos;
    } lightData{};
    lightData.lightPos = glm::vec4(2.0f, 30.0f, 3.0f, 1.0f);
    lightData.lightColor = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
    lightData.viewPos = glm::vec4(camPos, 1.0f);

    SDL_PushGPUFragmentUniformData(cmd, 0, &lightData, sizeof(LightUniforms));

    // 2. Отрисовка всех сущностей с MeshFilter и MeshRenderer
    for (auto view_entities = reg.view<Transform, MeshFilter, MeshRenderer>(); auto entity : view_entities) {
        auto& transform = view_entities.get<Transform>(entity);
        auto& mesh_filter = view_entities.get<MeshFilter>(entity);
        auto& renderer_comp = view_entities.get<MeshRenderer>(entity);


        auto* mat_inst = renderer_comp.material_instance;
        // Пропускаем, если материалы не назначены или меша нет
        if (!mat_inst || !mat_inst->material || !mesh_filter.mesh) continue;
        // std::cout << "draw" << std::endl;

        auto* material = mat_inst->material;
        auto& layout = material->layout;

        // --- БИНДИМ ПАЙПЛАЙН ИЗ МАТЕРИАЛА ---
        SDL_BindGPUGraphicsPipeline(scenePass, material->pipeline);

        // --- VERTEX UNIFORMS (Слот 0 вершинного шейдера) ---
        struct alignas(16) CombinedUBO {
            glm::mat4 view;
            glm::mat4 proj;
            glm::mat4 model;
            glm::mat4 normalMatrix;
        } ubo{};
        ubo.view = viewMat;
        ubo.proj = projMat;
        // ubo.model = glm::mat4(1.0f); // Ставит объект ровно в центр мира (0,0,0) с масштабом 1
        ubo.model = transform.world_matrix;
        ubo.normalMatrix = glm::inverseTranspose(ubo.model);
        SDL_PushGPUVertexUniformData(cmd, 0, &ubo, sizeof(CombinedUBO));

        // --- FRAGMENT UNIFORMS (Слот 1 фрагментного шейдера - данные материала) ---
        if (layout.uniform_buffer_size > 0) {
            SDL_PushGPUFragmentUniformData(cmd,
                layout.uniform_binding_slot,
                mat_inst->data.uniform_buffer.data(),
                layout.uniform_buffer_size);
        }

        // --- ТЕКСТУРЫ И СЭМПЛЕРЫ ---
        for (const auto& tex : mat_inst->data.textures) {
            SDL_GPUTextureSamplerBinding tsb = { tex.texture, tex.sampler };
            SDL_BindGPUFragmentSamplers(scenePass, tex.binding_slot, &tsb, 1);
        }

        // --- ОТРИСОВКА ---
        SDL_GPUBufferBinding vb = {mesh_filter.mesh->vertex_buffer, 0};
        SDL_BindGPUVertexBuffers(scenePass, 0, &vb, 1);

        SDL_GPUBufferBinding ib = {mesh_filter.mesh->index_buffer, 0};
        SDL_BindGPUIndexBuffer(scenePass, &ib, SDL_GPU_INDEXELEMENTSIZE_32BIT);

        SDL_DrawGPUIndexedPrimitives(scenePass, mesh_filter.mesh->num_indices, 1, 0, 0, 0);
    }

    SDL_EndGPURenderPass(scenePass);
}
}  // namespace tryengine::graphics