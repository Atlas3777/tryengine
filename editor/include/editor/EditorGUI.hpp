#pragma once

#include <imgui.h>

#include <ImGuizmo.h>

#include <entt/entt.hpp>

#include "IPanel.hpp"
#include "editor/HierarchyPanel.hpp"
#include "engine/core/Engine.hpp"
#include "engine/graphics/GraphicsContext.hpp"

namespace editor {
class EditorGUI {
   public:
    EditorGUI(const engine::graphics::GraphicsContext& context);
    ~EditorGUI();
    void RecordPanelsGpuCommands(const engine::core::Engine& engine);
    void RenderToPanel(SDL_GPUCommandBuffer* cmd, engine::graphics::RenderSystem& render_system,
                       engine::core::Engine& engine) const;
    void RenderPanelsToSwapchain(SDL_GPUTexture* swapchainTexture, SDL_GPUCommandBuffer* cmd);

   private:
    void DrawDockSpace();
    std::vector<std::unique_ptr<IPanel>> panels_;
    // std::unique_ptr<HierarchyPanel> hierarchy;
    // std::unique_ptr<InspectorPanel> inspector;
};
}  // namespace editor
