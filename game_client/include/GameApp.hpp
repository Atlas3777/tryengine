#pragma once

#include <EASTL/unique_ptr.h>
#include <memory>

#include "engine/core/Engine.hpp"
#include "engine/core/InputState.hpp"
#include "engine/graphics/GraphicsContext.hpp"
#include "engine/graphics/RenderTarget.hpp"
#include "engine/resources/AsyncFileManager.hpp"

namespace trygame {

class GameApp {
public:
    GameApp() = default;
    void Init();
    void Run();
    void Shutdown();
private:
    void UpdateInput();

    bool running_;

    tryengine::core::InputState input_state_;

    std::unique_ptr<tryengine::graphics::GraphicsContext> graphics_context_;
    std::unique_ptr<tryengine::core::Engine> engine_;
    std::unique_ptr<tryengine::graphics::RenderTarget> target;
    tryengine::resources::AsyncFileManager async_file_manager_;
};

}  // namespace trygame