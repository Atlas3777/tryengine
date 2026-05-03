#pragma once

#include "editor/EditorContext.hpp"
#include "editor/Spawner.hpp"
#include "editor/gui/IPanel.hpp"
#include "editor/import/ImportSystem.hpp"
#include "engine/core/Engine.hpp"
#include "engine/graphics/GraphicsContext.hpp"

namespace tryeditor {
class AssetInspectorManager;
class AssetsFactoryManager;
class EditorGUI {
public:
    EditorGUI(tryengine::core::Engine& engine,
        tryengine::graphics::GraphicsContext& context,
        ImportSystem& import_system, Spawner& spawner,
        EditorContext& editor_context,
        AssetsFactoryManager& factory_manager,
        AssetInspectorManager& inspector_manager);
    ~EditorGUI();
    void UpdatePanels(const tryengine::core::Engine& engine) const;
    void RecordPanelsGpuCommands(const tryengine::core::Engine& engine, bool& is_playing);
    void RenderToPanel(SDL_GPUCommandBuffer* cmd, tryengine::graphics::RenderSystem& render_system,
                       const tryengine::core::Engine& engine) const;
    void RenderPanelsToSwapchain(SDL_GPUTexture* swapchainTexture, SDL_GPUCommandBuffer* cmd);

private:
    void DrawDockSpace();
    void DrawPlayToolbar(bool& is_playing);
    void DrawMainMenu();

    tryengine::core::Engine& engine_;
    EditorContext& editor_context_;

    std::vector<std::unique_ptr<IPanel>> panels_;
};
}  // namespace tryeditor
