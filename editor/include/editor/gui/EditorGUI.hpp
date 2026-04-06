#pragma once

#include "editor/Spawner.hpp"
#include "editor/gui/IPanel.hpp"
#include "editor/import/ImportSystem.hpp"
#include "engine/core/Engine.hpp"
#include "engine/graphics/GraphicsContext.hpp"

namespace engine::core {
class AssetDatabase;
}
namespace editor {
class EditorGUI {
public:
    EditorGUI(const engine::graphics::GraphicsContext& context, ImportSystem& import_system, Spawner& spawner);
    ~EditorGUI();
    void RecordPanelsGpuCommands(const engine::core::Engine& engine);
    void RenderToPanel(SDL_GPUCommandBuffer* cmd, engine::graphics::RenderSystem& render_system,
                       const engine::core::Engine& engine) const;
    void RenderPanelsToSwapchain(SDL_GPUTexture* swapchainTexture, SDL_GPUCommandBuffer* cmd);

private:
    void DrawDockSpace();
    std::vector<std::unique_ptr<IPanel>> panels_;
};
}  // namespace editor
