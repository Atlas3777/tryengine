#include "../../include/editor/render/DebugPass.hpp"

#include "engine/core/Engine.hpp"
#include "engine/core/ScriptSystem.hpp"
#include "engine/graphics/RasterState.hpp"
#include "engine/graphics/RenderCommon.hpp"
#include "engine/graphics/rg/RenderGraph.hpp"
#include "engine/physics/PhysicsWorld.hpp"

namespace tryeditor {

using namespace tryengine::graphics;

namespace {

static_assert(sizeof(tryengine::physics::PhysicsDebugLine) == sizeof(DebugLine),
              "PhysicsDebugLine and DebugLine must share the 32-byte layout");
static_assert(alignof(tryengine::physics::PhysicsDebugLine) == alignof(DebugLine));

struct DebugPushConstants {
    VkDeviceAddress lines;   // 8 байт
    VkDeviceAddress camera;  // 8 байт
    float screen_width;      // 4 байта
    float screen_height;     // 4 байта
    float thickness;         // 4 байта
    uint32_t _pad;           // 4 байта -> Итого 32 байта
};

}  // namespace

void DebugPass::Collect(CollectContext& ctx) {
    lines_.clear();
    auto debug_res = ctx.engine.Get<tryengine::core::ScriptSystem>().SimpleReturnUnsafe<"GetDebugLines", das::Array*>();
    if (debug_res.has_value() && *debug_res != nullptr) {
        const auto array = *debug_res;
        DebugLine* points = reinterpret_cast<DebugLine*>(array->data);
        lines_ = eastl::vector<DebugLine>(points, points + array->size);
    }

    // Физика владеет своими коллайдерами и отдаёт готовые линии: DebugPass только
    // перекладывает их в тот же буфер, что и отладочные линии скрипта.
    if (auto* physics = ctx.engine.TryGet<tryengine::physics::PhysicsWorld>()) {
        eastl::vector<tryengine::physics::PhysicsDebugLine> physics_lines;
        physics->AppendDebugLines(physics_lines);
        for (const auto& source : physics_lines) {
            DebugLine line;
            line.start = source.start;
            line.r = source.color[0];
            line.g = source.color[1];
            line.b = source.color[2];
            line.a = source.color[3];
            line.end = source.end;
            line._pad = 0;
            lines_.push_back(line);
        }
    }
}

void DebugPass::Setup(RenderGraphBuilder& builder) {
    color_ = builder.Write(RGTag_v<"SceneColor">, RGUsageHint::ColorAttachment);
    depth_ = builder.Write(RGTag_v<"SceneDepth">, RGUsageHint::DepthStencilAttachment);
}

void DebugPass::Execute(RGExecuteContext& ctx) {
    if (lines_.empty())
        return;

    const auto* extent = ctx.cpu_bb->GetAs<VkExtent2D>(RGTag_v<"ViewportSize">);
    const auto* camera_cpu = ctx.cpu_bb->GetAs<CameraData>(RGTag_v<"Camera">);
    TRY_ASSERT(extent && camera_cpu, "ViewportSize/Camera missing in CPUBlackboard!");

    CameraGPU camera_gpu{.view = camera_cpu->view, .proj = camera_cpu->proj};
    auto camera_alloc = ctx.AllocateTransient(&camera_gpu, sizeof(CameraGPU), alignof(CameraGPU));
    auto lines_alloc = ctx.AllocateTransient(lines_.data(), sizeof(DebugLine) * lines_.size(), alignof(DebugLine));

    RGColorAttachment color_att{};
    color_att.handle = color_;
    color_att.load_op = VK_ATTACHMENT_LOAD_OP_LOAD;
    color_att.store_op = VK_ATTACHMENT_STORE_OP_STORE;

    RGDepthStencilAttachment depth_att{};
    depth_att.handle = depth_;
    depth_att.depth_load_op = VK_ATTACHMENT_LOAD_OP_LOAD;
    depth_att.depth_store_op = VK_ATTACHMENT_STORE_OP_STORE;

    const VkRect2D render_area{{0, 0}, *extent};
    ctx.BeginRendering(eastl::span(&color_att, 1), &depth_att, render_area);

    SetViewportScissor(ctx.cmd_buffer, *extent);
    // Тест глубины включен, запись отключена
    ApplyRasterState(
        ctx.cmd_buffer,
        {.cull_mode = VK_CULL_MODE_NONE, .depth_write = false, .depth_compare = VK_COMPARE_OP_LESS_OR_EQUAL});

    Shader* shader = shader_.Get();
    VkShaderStageFlagBits stages[] = {VK_SHADER_STAGE_VERTEX_BIT, VK_SHADER_STAGE_FRAGMENT_BIT};
    VkShaderEXT shaders[] = {shader->vertex_shader, shader->fragment_shader};
    vkCmdBindShadersEXT(ctx.cmd_buffer, 2, stages, shaders);

    DebugPushConstants pc{.lines = lines_alloc.bda_address,
                          .camera = camera_alloc.bda_address,
                          .screen_width = static_cast<float>(extent->width),
                          .screen_height = static_cast<float>(extent->height),
                          .thickness = thickness,  // БЫЛО: константа 4.0f
                          ._pad = 0};

    vkCmdPushConstants(ctx.cmd_buffer, shader->layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                       sizeof(DebugPushConstants), &pc);

    vkCmdDraw(ctx.cmd_buffer, static_cast<uint32_t>(lines_.size() * 6), 1, 0, 0);

    ctx.EndRendering();
}

}  // namespace tryeditor