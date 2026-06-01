#pragma once

#include <memory>

#include "daScript/simulate/simulate.h"
#include "editor/Editor.hpp"
#include "engine/core/Engine.hpp"
#include "engine/core/InputState.hpp"
#include "engine/graphics/RenderSystem.hpp"

namespace tryeditor {
class EditorApp {
public:
    EditorApp() = default;
    EditorApp(const EditorApp&) = delete;
    EditorApp& operator=(const EditorApp&) = delete;
    void Init();
    void Run();
    void Shutdown();

private:
    void UpdateInput();
    void CheckBaseProjectData(ImportSystem& import_system);
    std::unique_ptr<tryengine::graphics::GraphicsContext> graphics_context_;
    std::unique_ptr<tryengine::graphics::RenderSystem> render_system_;
    std::unique_ptr<tryengine::core::Engine> engine_;
    std::unique_ptr<Editor> editor_;
    tryengine::core::InputState input_state_;

    das::Context* das_ctx = nullptr;
    das::SimFunction* fn_game_update = nullptr;
};
}  // namespace tryeditor
