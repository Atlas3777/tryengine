#pragma once
#include <memory>

#include "Spawner.hpp"
#include "editor/Editor.hpp"
#include "engine/core/Engine.hpp"
#include "engine/core/InputTypes.hpp"
#include "engine/graphics/RenderSystem.hpp"

namespace editor {
class EditorApp {
public:
    void Init();
    void Run();
    void Shutdown();

private:
    void UpdateInput();

    std::unique_ptr<engine::graphics::GraphicsContext> graphics_context_;
    std::unique_ptr<engine::graphics::RenderSystem> render_system_;
    std::unique_ptr<engine::core::Engine> engine_;

    std::unique_ptr<Editor> editor_;

    engine::core::InputState input_state_;
};
}  // namespace editor
