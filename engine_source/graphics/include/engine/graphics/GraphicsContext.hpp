#pragma once

#include <EASTL/string.h>
#include <SDL3/SDL.h>

namespace tryengine::graphics {

class GraphicsContext {
public:
    GraphicsContext(uint32_t width, uint32_t height, eastl::string_view title);
    ~GraphicsContext();

    GraphicsContext(const GraphicsContext&) = delete;
    GraphicsContext& operator=(const GraphicsContext&) = delete;

    [[nodiscard]] SDL_Window* GetWindow() const { return window_; }
    [[nodiscard]] SDL_GPUDevice* GetDevice() const { return device_; }

private:
    SDL_Window* window_ = nullptr;
    SDL_GPUDevice* device_ = nullptr;
};
}  // namespace tryengine::graphics
