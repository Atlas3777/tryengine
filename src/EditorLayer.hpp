#pragma once

#include <entt/entt.hpp>

#include "EngineContext.hpp"
#include "EngineTypes.hpp"
#include "RenderTarget.hpp"
#include "core/GraphicsContext.hpp"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_sdlgpu3.h"

class EditorLayer {
   public:
    EditorLayer(GraphicsContext& context) {
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;  // Управление клавиатурой
        io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;      // Докинг (вкладки)
        // io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;    // ТЕ САМЫЕ ОКНА (Multi-viewports)

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

    ~EditorLayer() {
        // Переносим сюда:
        // ImGui_ImplSDLGPU3_Shutdown();
        // ImGui::DestroyContext();
        ImGui_ImplSDLGPU3_Shutdown();
        ImGui_ImplSDL3_Shutdown();
        ImGui::DestroyContext();
    }

    void RecordRenderGUICommands(RenderTarget& renderTarget, entt::registry& reg, EngineContext& context) {
        ImGui_ImplSDLGPU3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();

        ImGuiViewport* viewport = ImGui::GetMainViewport();

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

        ImGui::Begin("Scene");
        ImVec2 sceneViewportSize = ImGui::GetContentRegionAvail();

        // Проверяем, изменился ли размер и не равен ли он нулю
        if (sceneViewportSize.x > 0 && sceneViewportSize.y > 0) {
            uint32_t newWidth = (uint32_t)sceneViewportSize.x;
            uint32_t newHeight = (uint32_t)sceneViewportSize.y;
            if (newWidth != renderTarget.GetWidth() || newHeight != renderTarget.GetHeight())
                renderTarget.Resize(newWidth, newHeight);
            ImGui::Image((ImTextureID)renderTarget.GetColor(), sceneViewportSize);
        }
        ImGui::End();

        bool show_demo = true;
        if (show_demo) ImGui::ShowDemoWindow(&show_demo);

        ImGui::Begin("Engine Control");
        // ImGui::Text("FPS: %.1f", context.isCursorCaptured);
        ImGui::Checkbox("Show Demo Window", &show_demo);
        // if (ImGui::Checkbox("Enable VSync", &context.vSynvEnable)) {
        //     windowManager.SetVSync(&context.vSyncEnabled);
        // }
        ImGui::End();

        ImGui::Begin("Scene Hierarchy");

        auto view_entitiess = reg.view<TransformComponent, MeshComponent>();

        int node_id = 0;
        for (auto entity : view_entitiess) {
            auto& transform = view_entitiess.get<TransformComponent>(entity);

            // Создаем уникальное имя для каждого объекта в списке
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

        // Завершаем генерацию геометрии ImGui
        // ImGui::Render();
        // ImDrawData* draw_data = ImGui::GetDrawData();
    }
};
