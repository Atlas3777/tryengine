#include "editor/render/GridPass.hpp"

#include <daScript/simulate/aot_builtin_matrix.h>

#include "engine/graphics/RasterState.hpp"
#include "engine/graphics/RenderCommon.hpp"
#include "engine/graphics/rg/RenderGraph.hpp"

namespace tryeditor {

using namespace tryengine::graphics;

namespace {

// Зеркало GridCamera из Grid.slang: матрицы движка (row-vector) плюс их обратные, чтобы
// шейдер не инвертировал 4x4 на каждый пиксель.
struct alignas(16) GridCameraGPU {
    das::float4x4 view;
    das::float4x4 proj;
    das::float4x4 view_inverse;
    das::float4x4 proj_inverse;
    das::float3 position;
    float _pad;
};

static_assert(sizeof(GridCameraGPU) == 272, "GridCameraGPU должен совпадать с GridCamera из Grid.slang");

// Зеркало GridPushConstants: смещения совпадают с std430-блоком из SPIR-V.
struct GridPushConstants {
    VkDeviceAddress camera;
    float screen_width;
    float screen_height;
    float cell_size;
    float min_pixels_between_cells;
    float grid_size;
    float _pad;
};

static_assert(sizeof(GridPushConstants) == 32, "GridPushConstants должен совпадать с Grid.slang");

}  // namespace

void GridPass::Setup(RenderGraphBuilder& builder) {
    color_ = builder.Write(RGTag_v<"SceneColor">, RGUsageHint::ColorAttachment);
    depth_ = builder.Write(RGTag_v<"SceneDepth">, RGUsageHint::DepthStencilAttachment);
}

void GridPass::Execute(RGExecuteContext& ctx) {
    Shader* shader = shader_.Get();
    if (!shader)
        return;

    const auto* extent = ctx.cpu_bb->GetAs<VkExtent2D>(RGTag_v<"ViewportSize">);
    const auto* camera_cpu = ctx.cpu_bb->GetAs<CameraData>(RGTag_v<"Camera">);
    TRY_ASSERT(extent && camera_cpu, "ViewportSize/Camera missing in CPUBlackboard!");
    if (!extent || !camera_cpu)
        return;

    // До первой загрузки сцены editor-камеры нет, и в CPU-борде лежит нулевая матрица
    // проекции: инвертировать её нельзя, иначе весь вьюпорт заполнится NaN.
    if (camera_cpu->proj.m[0].x == 0.0f)
        return;

    GridCameraGPU camera{};
    camera.view = camera_cpu->view;
    camera.proj = camera_cpu->proj;
    camera.view_inverse = das::float4x4_inverse(camera_cpu->view);
    camera.proj_inverse = das::float4x4_inverse(camera_cpu->proj);
    camera.position = camera_cpu->position;
    auto camera_alloc = ctx.AllocateTransient(&camera, sizeof(camera), alignof(GridCameraGPU));

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
    // Глубину сетка только читает: SV_Depth из шейдера прячет её за геометрией сцены,
    // но сама она ничего не заслоняет — отладочные линии и picking идут следом и не
    // должны упираться в плоскость сетки.
    ApplyRasterState(
        ctx.cmd_buffer,
        {.cull_mode = VK_CULL_MODE_NONE, .depth_write = false, .depth_compare = VK_COMPARE_OP_LESS_OR_EQUAL});

    // Линии полупрозрачные, цвет premultiplied: так альфа-канал буфера сцены остаётся
    // единицей (вьюпорт рисуется этой же текстурой через ImGui).
    constexpr VkBool32 blend_enable = VK_TRUE;
    vkCmdSetColorBlendEnableEXT(ctx.cmd_buffer, 0, 1, &blend_enable);
    constexpr VkColorBlendEquationEXT blend_equation{
        .srcColorBlendFactor = VK_BLEND_FACTOR_ONE,
        .dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        .colorBlendOp = VK_BLEND_OP_ADD,
        .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
        .dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        .alphaBlendOp = VK_BLEND_OP_ADD,
    };
    vkCmdSetColorBlendEquationEXT(ctx.cmd_buffer, 0, 1, &blend_equation);

    VkShaderStageFlagBits stages[] = {VK_SHADER_STAGE_VERTEX_BIT, VK_SHADER_STAGE_FRAGMENT_BIT};
    VkShaderEXT shaders[] = {shader->vertex_shader, shader->fragment_shader};
    vkCmdBindShadersEXT(ctx.cmd_buffer, 2, stages, shaders);

    GridPushConstants pc{.camera = camera_alloc.bda_address,
                         .screen_width = static_cast<float>(extent->width),
                         .screen_height = static_cast<float>(extent->height),
                         .cell_size = cell_size,
                         .min_pixels_between_cells = min_pixels_between_cells,
                         .grid_size = grid_size,
                         ._pad = 0.0f};

    vkCmdPushConstants(ctx.cmd_buffer, shader->layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                       sizeof(GridPushConstants), &pc);

    // Три вершины полноэкранного треугольника, буферов нет.
    vkCmdDraw(ctx.cmd_buffer, 3, 1, 0, 0);

    ctx.EndRendering();
}

}  // namespace tryeditor
