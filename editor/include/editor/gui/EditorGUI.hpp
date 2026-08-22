#pragma once

#include "editor/import/ImportSystem.hpp"
#include "engine/core/Engine.hpp"
#include "engine/graphics/GraphicsContext.hpp"

namespace tryengine::graphics {
class RenderSystem;
}

namespace tryeditor {

class EditorGUI {
public:
    EditorGUI(tryengine::core::Engine& engine, tryengine::graphics::GraphicsContext& context);
    ~EditorGUI();

    void RecordPanelsGpuCommands(bool& is_playing);
    void RenderToSwapchain(SDL_GPUTexture* swapchain_texture, SDL_GPUCommandBuffer* cmd);

private:
    void DrawDockSpace();
    void DrawPlayToolbar(bool& is_playing);
    void DrawMainMenu();

    tryengine::core::Engine& engine_;
};
}  // namespace tryeditor