#include "EditorLayer.hpp"

#include "Engine.hpp"
#include "EngineTypes.hpp"
#include "GraphicsContext.hpp"
#include "RenderTarget.hpp"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_sdlgpu3.h"

EditorLayer::EditorLayer(GraphicsContext& context) {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    // io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;

    ImGui::StyleColorsDark();

    // Настройка бэккендов
    ImGui_ImplSDL3_InitForSDLGPU(context.GetWindow());

    ImGui_ImplSDLGPU3_InitInfo init_info = {};
    init_info.Device = context.GetDevice();
    init_info.ColorTargetFormat = SDL_GetGPUSwapchainTextureFormat(context.GetDevice(), context.GetWindow());
    init_info.MSAASamples = SDL_GPU_SAMPLECOUNT_1;
    init_info.PresentMode = SDL_GPU_PRESENTMODE_VSYNC;

    ImGui_ImplSDLGPU3_Init(&init_info);
}

EditorLayer::~EditorLayer() {
    ImGui_ImplSDLGPU3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
}

void EditorLayer::RecordRenderGUICommands(RenderTarget& renderTarget, entt::registry& reg, Engine& engine) {
    ImGui_ImplSDLGPU3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();

    ImGuiViewport* viewport = ImGui::GetMainViewport();

    // Настройка DockSpace (занимает всё окно)
    ImGui::SetNextWindowPos(viewport->Pos);
    ImGui::SetNextWindowSize(viewport->Size);
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

    // Окно вьюпорта сцены
    ImGui::Begin("Scene");
    ImVec2 sceneViewportSize = ImGui::GetContentRegionAvail();

    if (sceneViewportSize.x > 0 && sceneViewportSize.y > 0) {
        uint32_t newWidth = (uint32_t)sceneViewportSize.x;
        uint32_t newHeight = (uint32_t)sceneViewportSize.y;

        if (newWidth != renderTarget.GetWidth() || newHeight != renderTarget.GetHeight()) {
            renderTarget.Resize(newWidth, newHeight);
        }

        ImGui::Image((ImTextureID)renderTarget.GetColor(), sceneViewportSize);
    }
    ImGui::End();

    // if (m_ShowDemoWindow) {
    ImGui::ShowDemoWindow();
    // }

    ImGui::Begin("Engine Control");

    // Checkbox возвращает true, если по нему кликнули
    if (ImGui::Checkbox("VSync Enable", &engine.settings.vSyncEnable)) {
        // Эта строка выполнится только ОДИН РАЗ в момент клика
        engine.PushCommand(CmdSetVSync(engine.settings.vSyncEnable));
    }

    ImGui::Text("FPS: %d", engine.time.currentFPS);
    ImGui::End();
    // Иерархия сцены
    ImGui::Begin("Scene Hierarchy");
    auto view_entities = reg.view<TransformComponent, MeshComponent>();

    int node_id = 0;
    for (auto entity : view_entities) {
        auto& transform = view_entities.get<TransformComponent>(entity);

        char label[64];
        sprintf(label, "Entity %d", node_id++);

        if (ImGui::TreeNode(label)) {
            ImGui::DragFloat3("Position", &transform.position.x, 0.1f);
            ImGui::DragFloat3("Rotation", &transform.rotation.x, 1.0f);
            ImGui::DragFloat3("Scale", &transform.scale.x, 0.05f);
            ImGui::TreePop();
        }
        ImGui::Separator();
    }
    ImGui::End();
    ImGui::Render();
}
