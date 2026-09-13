// #pragma once
//
// #include "editor/PlayModeState.hpp"
// #include "editor/import/ImportSystem.hpp"
// #include "engine/core/Engine.hpp"
// #include "engine/graphics/GraphicsContext.hpp"
//
// namespace tryeditor {
//
// class EditorGUI {
// public:
//     EditorGUI(tryengine::core::Engine& engine, const tryengine::graphics::GraphicsContext& context);
//     ~EditorGUI();
//
//     void RecordPanelsGpuCommands(PlayModeState& state);
//     void RenderToSwapchain(SDL_GPUTexture* swapchain_texture, SDL_GPUCommandBuffer* cmd);
//
// private:
//     void DrawDockSpace();
//     void DrawPlayToolbar(PlayModeState& state);
//     void DrawMainMenu();
//
//     tryengine::core::Engine& engine_;
// };
// }  // namespace tryeditor