#pragma once
#include "editor/Editor.hpp"
#include "engine/Engine.hpp"

#include <memory>

namespace editor {
class EditorApp {
    private:
    std::unique_ptr<Editor> editor;
    std::unique_ptr<engine::Engine> engine;
    std::unique_ptr<engine::GraphicsContext> graphicsContext;
    std::unique_ptr<engine::RenderTarget> target;
public:
    void Init();
    void Run();
    void Shutdown();
};
}
