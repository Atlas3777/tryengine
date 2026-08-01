#pragma once

#include <memory>

#include "editor/Editor.hpp"
#include "engine/core/Engine.hpp"
#include "engine/core/InputState.hpp"
#include "engine/graphics/RenderSystem.hpp"
#include "engine/resources/AsyncFileManager.hpp"

namespace tryeditor {
class EditorApp {
public:
    EditorApp();
    ~EditorApp();
    EditorApp(const EditorApp&) = delete;
    EditorApp& operator=(const EditorApp&) = delete;

    void Init();
    void Run();
    void Shutdown();

private:
    void UpdateInput();
    std::unique_ptr<tryengine::graphics::GraphicsContext> graphics_context_;
    std::unique_ptr<tryengine::graphics::RenderSystem> render_system_;
    std::unique_ptr<tryengine::core::Engine> engine_;
    std::unique_ptr<Editor> editor_;
    tryengine::core::InputState input_state_;
    tryengine::resources::AsyncFileManager async_file_manager_;
};
}  // namespace tryeditor
