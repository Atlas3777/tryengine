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

    [[nodiscard]] SDL_Window* GetWindow() const { return m_window; }
    [[nodiscard]] SDL_GPUDevice* GetDevice() const { return m_device; }

private:
    SDL_Window* m_window = nullptr;
    SDL_GPUDevice* m_device = nullptr;
};
}  // namespace tryengine::graphics
