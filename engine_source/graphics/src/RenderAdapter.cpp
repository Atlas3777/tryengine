#include "engine/graphics/RenderAdapter.hpp"

#include <EASTL/span.h>

#include "engine/core/Engine.hpp"
#include "engine/core/ScriptSystem.hpp"
#include "engine/graphics/RenderSystem.hpp"
#include "engine/core/HlslppFormatter.hpp"

inline uint64_t MakeSortingKey(uint8_t pass_layer, uint16_t pipeline_id, uint16_t material_id, uint16_t mesh_id,
                               uint16_t depth = 0) {
    return (static_cast<uint64_t>(pass_layer) << 62) | (static_cast<uint64_t>(pipeline_id) << 48) |
           (static_cast<uint64_t>(material_id) << 32) | (static_cast<uint64_t>(mesh_id) << 16) |
           (static_cast<uint64_t>(depth));
}

namespace tryengine::graphics {

struct DaslangDrawCall {
    hlslpp::float4x4 world_matrix;
    uint64_t mesh;
    uint64_t material;
};

static_assert(sizeof(DaslangDrawCall) == 80);

void RenderAdapter::CollectDrawable(core::Engine& engine, RenderSystem& render_system) {
    CollectLight(engine, render_system);
    CollectDrawCommand(engine, render_system);
}

void RenderAdapter::CollectLight(core::Engine& engine, RenderSystem& render_system) {
    auto result = engine.Get<core::ScriptSystem>().SimpleReturnUnsafe<das::Array*>("get_lights");

    if (!result.has_value())
        LogError(result.error().Message());

    const auto array = *result;
    auto* points = reinterpret_cast<PointLightGPU*>(array->data);

    render_system.lights_queue_ = eastl::span(points, array->size);
}

void RenderAdapter::CollectDrawCommand(core::Engine& engine, RenderSystem& render_system) {
    render_system.ClearQueue();

    auto result = engine.Get<core::ScriptSystem>().SimpleReturnUnsafe<das::Array*>("get_render_obj");

    if (!result.has_value())
        LogError(result.error().Message());

    const auto array = *result;
    DaslangDrawCall* draws = reinterpret_cast<DaslangDrawCall*>(array->data);
    eastl::span draw_span(draws, array->size);
    // LogInfo("drawcall's size = {}", array->size);

    for (const auto& draw_call : draw_span) {
        auto mesh = slot_map_mesh.get(draw_call.mesh);
        auto material = slot_map_material.get(draw_call.material);

        if (!mesh || !mesh->IsReady())
            continue;

        if (!material || !material->IsReady())
            continue;

        if (!material->Get()->shader_handle)
            continue;

        bool all_textures_ready = true;

        if (material->Get()->textures.empty()) {
            all_textures_ready = false;
        }

        for (const auto& tex_handle : material->Get()->textures) {
            if (!tex_handle || !tex_handle.IsReady()) {
                all_textures_ready = false;
                break;
            }
        }

        if (!all_textures_ready)
            continue;

        PipelineDescriptor desc;
        desc.fragment_shader = material->Get()->shader().fragment_shader;
        desc.vertex_shader = material->Get()->shader().vertex_shader;
        desc.vertex_format = mesh->Get()->v_format;
        desc.cull_mode = SDL_GPU_CULLMODE_BACK;
        auto* pipeline = render_system.GetPipelineManager()->GetOrCreatePipeline(desc);

        if (!pipeline)
            continue;

        // Генерируем уникальные ID для ключа сортировки
        uint16_t pipeline_id = desc.GetHashCode() & 0xFFFF;
        uint16_t material_id = reinterpret_cast<uintptr_t>(material->Get()) & 0xFFFF;
        uint16_t mesh_id = reinterpret_cast<uintptr_t>(mesh->Get()) & 0xFFFF;

        DrawCommand cmd;
        cmd.sorting_key = MakeSortingKey(0, pipeline_id, material_id, mesh_id);

        cmd.vertex_buffer = mesh->Get()->vertex_buffer;
        cmd.index_buffer = mesh->Get()->index_buffer;
        cmd.num_indices = mesh->Get()->num_indices;
        cmd.index_format = mesh->Get()->i_format;
        cmd.pipeline = pipeline;
        cmd.material = material->Get();
        cmd.model_matrix = draw_call.world_matrix;

        render_system.Submit(cmd);
    }
}
};  // namespace tryengine::graphics