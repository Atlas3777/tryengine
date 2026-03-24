#pragma once
#include <memory>

#include "editor/Editor.hpp"
#include "engine/core/Engine.hpp"
#include "engine/core/InputTypes.hpp"
#include "engine/graphics/RenderSystem.hpp"

namespace editor {
class EditorApp {
    private:
    engine::core::InputState inputState;
    void UpdateInput();
    std::unique_ptr<Editor> editor;
    std::unique_ptr<engine::core::Engine> engine;
    std::unique_ptr<engine::graphics::GraphicsContext> graphicsContext;
    std::unique_ptr<engine::graphics::RenderTarget> renderTarget;
    std::unique_ptr<engine::graphics::RenderSystem> renderSystem;
public:
    void Init();
    void Run();
    void Shutdown();
};
}
