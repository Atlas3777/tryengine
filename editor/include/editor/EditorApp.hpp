#pragma once
#include <memory>

#include "engine/core/Engine.hpp"
#include "editor/Editor.hpp"

namespace editor {
class EditorApp {
    private:
    std::unique_ptr<Editor> editor;
    std::unique_ptr<engine::Engine> engine;
    std::unique_ptr<engine::graphics::GraphicsContext> graphicsContext;
    std::unique_ptr<engine::graphics::RenderTarget> target;
public:
    void Init();
    void Run();
    void Shutdown();
};
}
