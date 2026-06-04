#include <entt/entity/registry.hpp>
#include "engine/core/Components.hpp"
#include "engine/graphics/RenderSystem.hpp"

inline uint64_t MakeSortingKey(uint8_t pass_layer, uint16_t pipeline_id, uint16_t material_id, uint16_t mesh_id, uint16_t depth = 0) {
    return (static_cast<uint64_t>(pass_layer) << 62) |
           (static_cast<uint64_t>(pipeline_id)  << 48) |
           (static_cast<uint64_t>(material_id)  << 32) |
           (static_cast<uint64_t>(mesh_id)      << 16) |
           (static_cast<uint64_t>(depth));
}

namespace tryengine::graphics {

void SubmitSceneFromEnTT(entt::registry& reg, entt::entity camera_entity, RenderSystem& render_system) {
    render_system.ClearQueue();

    // Извлекаем данные камеры
    auto cam_view = reg.view<Camera, Transform>();
    if (!cam_view.contains(camera_entity)) return;

    auto& cam_transform = cam_view.get<Transform>(camera_entity);
    auto& camera = cam_view.get<Camera>(camera_entity);

    // Допустим, аспект получаем из фиксированного/текущего разрешения
    float aspect = 16.0f / 9.0f; 
    
    CameraData camera_data;
    camera_data.view = camera.view_matrix;
    camera_data.proj = glm::perspective(glm::radians(camera.fov), aspect, camera.near_plane, camera.far_plane);
    camera_data.position = cam_transform.position;

    // Извлекаем источники света из ECS (если они там есть)
    // На данный момент можно сделать пустой вектор или собрать сущности с LightComponent
    std::vector<Light> scene_lights;
    AmbientSettings ambient; 

    // Проходим по рендер-сущностям и формируем команды отрисовки
    auto renderable_view = reg.view<Transform, MeshFilter, MeshRenderer>();
    for (auto entity : renderable_view) {
        auto& transform = renderable_view.get<Transform>(entity);
        auto& mesh_filter = renderable_view.get<MeshFilter>(entity);
        auto& mesh_renderer = renderable_view.get<MeshRenderer>(entity);

        if (!mesh_renderer.material || !mesh_renderer.material->shader || !mesh_filter.mesh)
            continue;

        // Конструируем дескриптор пайплайна для кэша
        PipelineDescriptor desc;
        desc.fragment_shader = mesh_renderer.material->shader->fragment_shader;
        desc.vertex_shader = mesh_renderer.material->shader->vertex_shader;
        auto* pipeline = render_system.GetPipelineManager()->GetOrCreatePipeline(desc);

        if (!pipeline) continue;

        // Генерируем уникальные ID для ключа сортировки (в реальном движке это индексы в менеджерах ресурсов)
        uint16_t pipeline_id = desc.GetHashCode() & 0xFFFF;
        uint16_t material_id = reinterpret_cast<uintptr_t>(mesh_renderer.material.handle().get()) & 0xFFFF;
        uint16_t mesh_id     = reinterpret_cast<uintptr_t>(mesh_filter.mesh.handle().get()) & 0xFFFF;

        DrawCommand cmd;
        // Задаем ключ: Слой Opaque(0), далее сортировка по Пайплайну -> Материалу -> Мешу
        cmd.sorting_key = MakeSortingKey(0, pipeline_id, material_id, mesh_id);
        
        cmd.vertex_buffer = mesh_filter.mesh->vertex_buffer;
        cmd.index_buffer = mesh_filter.mesh->index_buffer;
        cmd.num_indices = mesh_filter.mesh->num_indices;
        cmd.pipeline = pipeline;
        cmd.material = mesh_renderer.material.handle().get();
        cmd.model_matrix = transform.world_matrix;

        render_system.Submit(cmd);
    }
}

} // namespace tryengine::graphics