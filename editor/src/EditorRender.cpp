#include "editor/EditorRender.hpp"

#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlgpu3.h>

#include "editor/CollectDebug.hpp"
#include "editor/PlayModeState.hpp"
#include "engine/graphics/ForwardPipeline.hpp"
#include "engine/graphics/OpaqueGeometryPass.hpp"
#include "editor/CollectDebug.hpp"

namespace tryeditor {

using tryengine::graphics::RGTextureDesc;

EditorRender::EditorRender(tryengine::graphics::GraphicsContext& context,
                           tryengine::graphics::RenderGraph& render_graph)
    : rg_(render_graph), pm_(context.GetDevice()) {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = "editor/imgui.ini";
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_DockingEnable;

    ImGui::StyleColorsDark();
    ImGui_ImplSDL3_InitForSDLGPU(context.GetWindow());

    ImGui_ImplSDLGPU3_InitInfo init_info = {};
    init_info.Device = context.GetDevice();
    init_info.ColorTargetFormat = SDL_GetGPUSwapchainTextureFormat(context.GetDevice(), context.GetWindow());
    init_info.MSAASamples = SDL_GPU_SAMPLECOUNT_1;
    init_info.PresentMode = SDL_GPU_PRESENTMODE_VSYNC;
    ImGui_ImplSDLGPU3_Init(&init_info);
}

EditorRender::~EditorRender() {
    ImGui_ImplSDLGPU3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
}

void EditorRender::RecordImguiFrame(tryengine::core::Engine& engine, PlayModeState& state) {
    ImGui_ImplSDLGPU3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();

    // DrawMainMenu();
    // DrawPlayToolbar(state);
    DrawDockSpace();

    engine.Get<tryengine::core::ScriptSystem>().InvokeFunctionSafe("draw_editor");

    // RenderProfilerPanel();

    ImGui::Render();
}

void EditorRender::Render(tryengine::core::Engine& engine, tryengine::graphics::GraphicsContext& context,
                          PlayModeState& state) {
    RecordImguiFrame(engine, state);

    SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(context.GetDevice());

    SDL_GPUTexture* swap_tex = nullptr;
    uint32_t w = 0, h = 0;
    if (!SDL_WaitAndAcquireGPUSwapchainTexture(cmd, context.GetWindow(), &swap_tex, &w, &h)) {
        SDL_SubmitGPUCommandBuffer(cmd);
        return;
    }

    auto& script_system = engine.Get<tryengine::core::ScriptSystem>();
    auto camera_data = script_system.SimpleReturnUnsafe<tryengine::graphics::CameraData*>("get_camera");
    if (!camera_data.has_value())
        LogCritical("Camera data not found");

    const auto camera = *camera_data;

    tryengine::graphics::FrameRenderData frame_render_data;
    frame_render_data.point_lights = eastl::move(tryengine::graphics::CollectLight(engine));
    frame_render_data.opaque_queue = eastl::move(tryengine::graphics::OpaqueGeometryPass::CollectDrawable(engine, pm_));

    rg_.Reset();

    auto swapchain_format = SDL_GetGPUSwapchainTextureFormat(context.GetDevice(), context.GetWindow());
    RGTextureDesc swap_desc{w, h, 1, 0, swapchain_format, SDL_GPU_TEXTUREUSAGE_COLOR_TARGET, "SwapchainTexture"};
    RGResourceHandle swapchain_handle = rg_.ImportExternalTexture("Swapchain", swap_tex, swap_desc);

    struct T {
        hlslpp::float4 am_color;
        hlslpp::float4 view_pos;
    } t;
    t.am_color = hlslpp::float4(0.1f, 0.1f, 0.1f, 0);
    t.view_pos = hlslpp::float4(camera->position, 0);

    rg_.PutCPUData(RGTag_v<"GlobalLightUBO">, t);
    rg_.PutCPUData(RGTag_v<"Camera">, *camera);

    EditorFrame editor_frame;
    editor_frame.debug_lines = eastl::move(CollectDebug(engine));

    auto size_res = script_system.SimpleReturnUnsafe<das::float2>("GetEditorViewportSize");

    if (!size_res.has_value())
        LogError("aaa");

    auto size = *size_res;
    // LogTrace("x = {}, y = {}", size.x, size.y);

    EnsureDebugPipeline();

    BuildEditorRenderGraph(frame_render_data, editor_frame, swapchain_handle, size.x, size.y);

    rg_.Compile();

    rg_.Execute(cmd);

    SDL_SubmitGPUCommandBuffer(cmd);
}
void EditorRender::BuildEditorRenderGraph(const tryengine::graphics::FrameRenderData& frame_render_data,
                                          const EditorFrame& editor_frame, RGResourceHandle swapchain_handle, uint32_t width,
                                          uint32_t height) const {
    auto forward_out = tryengine::graphics::BuildForwardPipeline(rg_, frame_render_data, width, height);

    rg_.AddPass<ImGuiPassData>(
        "ImGuiPass",
        [&](tryengine::graphics::RenderGraphBuilder& builder, ImGuiPassData& data) {
            data.scene_tex_read = builder.Read(forward_out.color_target, SDL_GPU_TEXTUREUSAGE_SAMPLER);
            data.swapchain_target = builder.Write(swapchain_handle, SDL_GPU_TEXTUREUSAGE_COLOR_TARGET);
            builder.MarkSideEffect();
        },
        [](tryengine::graphics::RGExecuteContext& ctx, const ImGuiPassData& data) {
            ImDrawData* draw_data = ImGui::GetDrawData();
            if (!draw_data || draw_data->CmdListsCount == 0)
                return;

            // LogTrace("ImGui");

            ImGui_ImplSDLGPU3_PrepareDrawData(draw_data, ctx.cmd_buffer);

            SDL_GPUColorTargetInfo color_info{};
            color_info.texture = ctx.GetTexture(data.swapchain_target);
            color_info.clear_color = {0.0f, 0.0f, 0.0f, 1.0f};
            color_info.load_op = SDL_GPU_LOADOP_CLEAR;
            color_info.store_op = SDL_GPU_STOREOP_STORE;

            ctx.gpu_pass = SDL_BeginGPURenderPass(ctx.cmd_buffer, &color_info, 1, nullptr);
            ImGui_ImplSDLGPU3_RenderDrawData(draw_data, ctx.cmd_buffer, ctx.gpu_pass);
            SDL_EndGPURenderPass(ctx.gpu_pass);
            ctx.gpu_pass = nullptr;
        });

    rg_.AddPass<DebugDrawPass>(
        "DebugPass",
        [&](tryengine::graphics::RenderGraphBuilder& builder, DebugDrawPass& data) {
            data.debug_lines = eastl::span(editor_frame.debug_lines.data(), editor_frame.debug_lines.size());
            data.pipeline = debug_pipeline_;


            if (!data.debug_lines.empty()) {
                uint32_t buf_size = static_cast<uint32_t>(sizeof(DebugLine) * data.debug_lines.size());
                tryengine::graphics::RGBufferDesc desc{buf_size, SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ, "DebugLines"};
                data.lines_buffer = builder.CreateBuffer("DebugLines", desc);
            }

            data.color_target = builder.Write(forward_out.color_target);
            data.depth_target = builder.Write(forward_out.depth_target);

            builder.MarkSideEffect();
        },
        [](tryengine::graphics::RGExecuteContext& ctx, const DebugDrawPass& data) {
            // Чистая лямбда []: проверяем доступность линий и готового пайплайна
            // if (data.debug_lines.empty() || !data.pipeline) return;
            if (data.debug_lines.empty()) {
                LogError("Debug lines empty");
                return;
            }
            if (!data.pipeline) {
                LogError("Pipeline not initialize");
                return;
            }

            // LogTrace("Debug?");

            // 1. Копируем данные в Storage Buffer
            uint32_t upload_size = static_cast<uint32_t>(sizeof(DebugLine) * data.debug_lines.size());
            SDL_GPUTransferBufferCreateInfo xfer_info{SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD, upload_size};
            SDL_GPUTransferBuffer* xfer_buffer = SDL_CreateGPUTransferBuffer(ctx.device, &xfer_info);

            void* mapped = SDL_MapGPUTransferBuffer(ctx.device, xfer_buffer, false);
            std::memcpy(mapped, data.debug_lines.data(), upload_size);
            SDL_UnmapGPUTransferBuffer(ctx.device, xfer_buffer);

            SDL_GPUCopyPass* copy_pass = SDL_BeginGPUCopyPass(ctx.cmd_buffer);
            SDL_GPUTransferBufferLocation src{xfer_buffer, 0};
            SDL_GPUBufferRegion dst{ctx.GetBuffer(data.lines_buffer), 0, upload_size};
            SDL_UploadToGPUBuffer(copy_pass, &src, &dst, true);
            SDL_EndGPUCopyPass(copy_pass);
            SDL_ReleaseGPUTransferBuffer(ctx.device, xfer_buffer);

            // 2. Настраиваем Render Pass
            SDL_GPUColorTargetInfo color_info{};
            color_info.texture = ctx.GetTexture(data.color_target);
            color_info.load_op = SDL_GPU_LOADOP_LOAD;
            color_info.store_op = SDL_GPU_STOREOP_STORE;

            SDL_GPUDepthStencilTargetInfo depth_info{};
            depth_info.texture = ctx.GetTexture(data.depth_target);
            depth_info.load_op = SDL_GPU_LOADOP_LOAD;
            depth_info.store_op = SDL_GPU_STOREOP_STORE;

            ctx.gpu_pass = SDL_BeginGPURenderPass(ctx.cmd_buffer, &color_info, 1, &depth_info);

            // Используем пайплайн из data
            SDL_BindGPUGraphicsPipeline(ctx.gpu_pass, data.pipeline);

            SDL_GPUBuffer* storage_buf = ctx.GetBuffer(data.lines_buffer);
            SDL_BindGPUVertexStorageBuffers(ctx.gpu_pass, 0, &storage_buf, 1);

            auto camera_span = ctx.cpu_bb->GetSpan(tryengine::graphics::RGTag_v<"Camera">);
            if (camera_span) {
                SDL_PushGPUVertexUniformData(ctx.cmd_buffer, 0, camera_span->data(), static_cast<uint32_t>(camera_span->size_bytes()));
            }

            SDL_DrawGPUPrimitives(ctx.gpu_pass, static_cast<uint32_t>(data.debug_lines.size() * 6), 1, 0, 0);

            SDL_EndGPURenderPass(ctx.gpu_pass);
            ctx.gpu_pass = nullptr;
        });
}

void EditorRender::EnsureDebugPipeline() {
    // Если пайплайн уже создан или шейдер еще не загружен — ничего не делаем
    if (debug_pipeline_ || !debug_shader_.IsReady()) return;

    tryengine::graphics::PipelineDescriptor debug_pipeline_desc{};
    debug_pipeline_desc.vertex_shader = debug_shader_->vertex_shader;
    debug_pipeline_desc.fragment_shader = debug_shader_->fragment_shader;
    debug_pipeline_desc.vertex_format = tryengine::resources::VertexFormat::None;
    debug_pipeline_desc.cull_mode = SDL_GPU_CULLMODE_NONE;
    // debug_pipeline_desc.enable_depth_test = true;
    debug_pipeline_desc.enable_depth_test = false;
    debug_pipeline_desc.enable_depth_write = false;
    debug_pipeline_desc.depth_compare_op = SDL_GPU_COMPAREOP_LESS_OR_EQUAL;
    debug_pipeline_desc.color_target_format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    debug_pipeline_desc.depth_stencil_format = SDL_GPU_TEXTUREFORMAT_D16_UNORM;

    debug_pipeline_ = pm_.GetOrCreatePipeline(debug_pipeline_desc);
}

void EditorRender::DrawDockSpace() {
    ImGuiViewport* viewport = ImGui::GetMainViewport();

    float toolbar_height = 0.0f;  // 30.0f;

    // Сдвигаем начало DockSpace на высоту тулбара и уменьшаем его общий размер
    ImVec2 dock_pos = ImVec2(viewport->WorkPos.x, viewport->WorkPos.y + toolbar_height);
    ImVec2 dock_size = ImVec2(viewport->WorkSize.x, viewport->WorkSize.y - toolbar_height);

    ImGui::SetNextWindowPos(dock_pos);
    ImGui::SetNextWindowSize(dock_size);
    ImGui::SetNextWindowViewport(viewport->ID);

    ImGuiWindowFlags host_window_flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
                                         ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                                         ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus |
                                         ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoDocking;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));

    ImGui::Begin("MainDockSpace", nullptr, host_window_flags);
    ImGui::PopStyleVar(3);

    ImGuiID dockspace_id = ImGui::GetID("MainDockSpaceDock");
    ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_None);
    ImGui::End();
}

}  // namespace tryeditor