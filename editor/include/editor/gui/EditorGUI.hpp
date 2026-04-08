#pragma once

#include "editor/Spawner.hpp"
#include "editor/gui/IPanel.hpp"
#include "editor/import/ImportSystem.hpp"
#include "engine/core/Engine.hpp"
#include "engine/graphics/GraphicsContext.hpp"

namespace tryeditor {
class EditorGUI {
public:
    EditorGUI(const tryengine::graphics::GraphicsContext& context, ImportSystem& import_system, Spawner& spawner);
    ~EditorGUI();
    void UpdatePanels(const tryengine::core::Engine& engine) const;
    void RecordPanelsGpuCommands(const tryengine::core::Engine& engine);
    void RenderToPanel(SDL_GPUCommandBuffer* cmd, tryengine::graphics::RenderSystem& render_system,
                       const tryengine::core::Engine& engine) const;
    void RenderPanelsToSwapchain(SDL_GPUTexture* swapchainTexture, SDL_GPUCommandBuffer* cmd);

private:
    void DrawDockSpace();
    std::vector<std::unique_ptr<IPanel>> panels_;
};
}  // namespace tryeditor
