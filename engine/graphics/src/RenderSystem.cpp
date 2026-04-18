#include "engine/graphics/RenderSystem.hpp"

#include <glm/gtc/matrix_inverse.inl>

#include "engine/core/Components.hpp"
#include "engine/graphics/MaterialSystem.hpp"
#include "engine/graphics/Types.hpp"

namespace tryengine::graphics {

void RenderSystem::RenderScene(entt::registry& reg, entt::entity camera_entity, RenderTarget* target,
                               SDL_GPUCommandBuffer* cmd) const {
    auto cam_view = reg.view<Camera, Transform>();
    auto& cam_transform = cam_view.get<Transform>(camera_entity);
    auto& camera = cam_view.get<Camera>(camera_entity);

    float aspect = static_cast<float>(target->GetWidth()) / static_cast<float>(target->GetHeight());
    glm::mat4 projMat = glm::perspective(glm::radians(camera.fov), aspect, camera.near_plane, camera.far_plane);
    glm::mat4 viewMat = camera.view_matrix;
    glm::vec3 camPos = cam_transform.position;

    SDL_GPUColorTargetInfo color_info{};
    color_info.texture = target->GetColor();
    color_info.clear_color = {0.69f, 0.77f, 0.87f, 1.0f};
    color_info.load_op = SDL_GPU_LOADOP_CLEAR;
    color_info.store_op = SDL_GPU_STOREOP_STORE;

    SDL_GPUDepthStencilTargetInfo depth_info{};
    depth_info.texture = target->GetDepth();
    depth_info.clear_depth = 1.0f;
    depth_info.load_op = SDL_GPU_LOADOP_CLEAR;
    depth_info.store_op = SDL_GPU_STOREOP_STORE;
    depth_info.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
    depth_info.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;

    SDL_GPURenderPass* scene_pass = SDL_BeginGPURenderPass(cmd, &color_info, 1, &depth_info);

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
        auto& mesh_renderer = view_entities.get<MeshRenderer>(entity);

        Shader* master_material = mesh_renderer.material->shader;
        PipelineDescriptor pipeline_descriptor;
        pipeline_descriptor.fragment_shader = mesh_renderer.material->shader->fragment_shader;
        pipeline_descriptor.vertex_shader = mesh_renderer.material->shader->vertex_shader;

        auto pipeline = pipeline_manager_->GetOrCreatePipeline(pipeline_descriptor);

        // Пропускаем, если материалы не назначены или меша нет
        if (!master_material || !pipeline || !mesh_filter.mesh)
            continue;

        auto& layout = master_material->layout;
        auto material = mesh_renderer.material;

        // --- БИНДИМ ПАЙПЛАЙН ИЗ МАТЕРИАЛА ---
        SDL_BindGPUGraphicsPipeline(scene_pass, pipeline);

        // --- VERTEX UNIFORMS (Слот 0 вершинного шейдера) ---
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
        SDL_PushGPUVertexUniformData(cmd, 0, &ubo, sizeof(CombinedUBO));

        // --- FRAGMENT UNIFORMS (Слот 1 фрагментного шейдера - данные материала) ---
        if (layout.uniform_buffer_size > 0) {
            SDL_PushGPUFragmentUniformData(cmd, layout.uniform_binding_slot, material->uniform_buffer.data(),
                                           layout.uniform_buffer_size);
        }

        // --- ТЕКСТУРЫ И СЭМПЛЕРЫ ---
        for (const auto& tex : material->textures) {
            SDL_GPUTextureSamplerBinding tsb = {tex.second.handle, tex.second.sampler};
            SDL_BindGPUFragmentSamplers(scene_pass, tex.first, &tsb, 1);
        }

        // --- ОТРИСОВКА ---
        SDL_GPUBufferBinding vb = {mesh_filter.mesh->vertex_buffer, 0};
        SDL_BindGPUVertexBuffers(scene_pass, 0, &vb, 1);

        SDL_GPUBufferBinding ib = {mesh_filter.mesh->index_buffer, 0};
        SDL_BindGPUIndexBuffer(scene_pass, &ib, SDL_GPU_INDEXELEMENTSIZE_32BIT);

        SDL_DrawGPUIndexedPrimitives(scene_pass, mesh_filter.mesh->num_indices, 1, 0, 0, 0);
    }

    SDL_EndGPURenderPass(scene_pass);
}
}  // namespace tryengine::graphics
