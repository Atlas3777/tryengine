#include "engine/graphics/RenderSystem.hpp"
#include <glm/gtc/matrix_inverse.hpp>
#include <algorithm>

namespace tryengine::graphics {

RenderSystem::RenderSystem(SDL_GPUDevice* device) : device_(device) {
    pipeline_manager_ = std::make_unique<PipelineManager>(device);
}

void RenderSystem::ClearQueue() {
    draw_queue_.clear();
}

void RenderSystem::Submit(const DrawCommand& cmd) {
    draw_queue_.push_back(cmd);
}

void RenderSystem::ExecuteCommands(SDL_GPUCommandBuffer* cmd_buffer,
                                   RenderTarget& target,
                                   const CameraData& camera,
                                   const std::vector<Light>& lights) {
    // --- НАСТРОЙКА ТАРГЕТОВ ---
    auto& clear_color = ambient.clear_color;
    SDL_GPUColorTargetInfo color_info{};
    color_info.texture = target.GetColor();
    color_info.clear_color = {clear_color.r, clear_color.g, clear_color.b, clear_color.a};
    color_info.load_op = SDL_GPU_LOADOP_CLEAR;
    color_info.store_op = SDL_GPU_STOREOP_STORE;

    SDL_GPUDepthStencilTargetInfo depth_info{};
    depth_info.texture = target.GetDepth();
    depth_info.clear_depth = 1.0f;
    depth_info.load_op = SDL_GPU_LOADOP_CLEAR;
    depth_info.store_op = SDL_GPU_STOREOP_STORE;

    // Начинаем проход рендера. Теперь экран очистится в любом случае!
    SDL_GPURenderPass* scene_pass = SDL_BeginGPURenderPass(cmd_buffer, &color_info, 1, &depth_info);

    // Если рисовать нечего — просто закрываем проход рендера и выходим (экран останется чистым и серым)
    if (draw_queue_.empty()) {
        SDL_EndGPURenderPass(scene_pass);
        return;
    }

    // --- СОРТИРОВКА ОЧЕРЕДИ ---
    std::sort(draw_queue_.begin(), draw_queue_.end(), [](const DrawCommand& a, const DrawCommand& b) {
        return a.sorting_key < b.sorting_key;
    });

    // --- ПОДГОТОВКА ГЛОБАЛЬНОГО ОСВЕЩЕНИЯ (Фрагментный слот 3) ---
    GlobalLightUniforms global_light_data{};
    global_light_data.ambient_color = ambient.ambient_color;
    global_light_data.view_pos = glm::vec4(camera.position, 1.0f);

    uint32_t active_lights = std::min(lights.size(), MAX_LIGHTS);
    global_light_data.light_count = active_lights;

    for (uint32_t i = 0; i < active_lights; ++i) {
        const auto& src_light = lights[i];
        if (src_light.type == LightType::Directional) {
            global_light_data.lights[i].position_type = glm::vec4(src_light.position, 0.0f); // 0.0 = Dir
        } else {
            global_light_data.lights[i].position_type = glm::vec4(src_light.position, 1.0f); // 1.0 = Point
        }

        // Умножаем цвет на интенсивность прямо тут перед отправкой на GPU
        glm::vec3 final_color = src_light.color * src_light.intensity;
        global_light_data.lights[i].color_radius = glm::vec4(final_color, src_light.radius);
    }

    // ВАЖНО: Передаем в слот 3 (соответствует set = 3 в шейдере)
    SDL_PushGPUFragmentUniformData(cmd_buffer, 3, &global_light_data, sizeof(GlobalLightUniforms));

    // --- ОТРИСОВКА ОЧЕРЕДИ С КЭШИРОВАНИЕМ СОСТОЯНИЙ ---
    SDL_GPUGraphicsPipeline* current_pipeline = nullptr;
    Material* current_material = nullptr;
    SDL_GPUBuffer* current_vertex_buffer = nullptr;
    SDL_GPUBuffer* current_index_buffer = nullptr;

    for (const auto& command : draw_queue_) {
        if (!command.pipeline || !command.vertex_buffer || !command.material) continue;

        if (command.pipeline != current_pipeline) {
            SDL_BindGPUGraphicsPipeline(scene_pass, command.pipeline);
            current_pipeline = command.pipeline;
            current_material = nullptr;
            current_vertex_buffer = nullptr;
            current_index_buffer = nullptr;
        }

        // Vertex Uniforms (Слот 0 вершинного шейдера)
        struct alignas(16) CombinedUBO {
            glm::mat4 view;
            glm::mat4 proj;
            glm::mat4 model;
            glm::mat4 normalMatrix;
        } ubo{};
        ubo.view = camera.view;
        ubo.proj = camera.proj;
        ubo.model = command.model_matrix;
        ubo.normalMatrix = glm::inverseTranspose(ubo.model);
        SDL_PushGPUVertexUniformData(cmd_buffer, 0, &ubo, sizeof(CombinedUBO));

        // Смена Материала (Слот 1 фрагментного шейдера)
        if (command.material != current_material) {
            auto shader = command.material->shader;

            if (shader->layout.uniform_buffer_size > 0) {
                SDL_PushGPUFragmentUniformData(cmd_buffer, shader->layout.uniform_binding_slot,
                                               command.material->uniform_buffer.data(),
                                               shader->layout.uniform_buffer_size);
            }

            for (const auto& binding : command.material->textures) {
                SDL_GPUTextureSamplerBinding tsb = {binding.texture.handle, binding.texture.sampler};
                SDL_BindGPUFragmentSamplers(scene_pass, binding.slot, &tsb, 1);
            }
            current_material = command.material;
        }

        if (command.vertex_buffer != current_vertex_buffer) {
            SDL_GPUBufferBinding vb = {command.vertex_buffer, 0};
            SDL_BindGPUVertexBuffers(scene_pass, 0, &vb, 1);
            current_vertex_buffer = command.vertex_buffer;
        }

        if (command.index_buffer != current_index_buffer) {
            SDL_GPUBufferBinding ib = {command.index_buffer, 0};
            SDL_BindGPUIndexBuffer(scene_pass, &ib, SDL_GPU_INDEXELEMENTSIZE_32BIT);
            current_index_buffer = command.index_buffer;
        }

        SDL_DrawGPUIndexedPrimitives(scene_pass, command.num_indices, 1, 0, 0, 0);
    }

    SDL_EndGPURenderPass(scene_pass);
}

}  // namespace tryengine::graphics