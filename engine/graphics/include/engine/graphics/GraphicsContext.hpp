#pragma once

#include <SDL3/SDL.h>

#include <string>
namespace engine::graphics {
class GraphicsContext {
   public:
    GraphicsContext() = default;
    ~GraphicsContext() { Terminate(); }

    // Запрещаем копирование, так как управляем уникальными ресурсами (GPU Device)
    GraphicsContext(const GraphicsContext&) = delete;
    GraphicsContext& operator=(const GraphicsContext&) = delete;

    bool Initialize(int width, int height, const std::string& title);
    // SDL_GPUTexture* AcquireSwapchainTexture(SDL_GPUCommandBuffer* cmd) const;

    void Terminate();

    // Геттеры
    SDL_Window* GetWindow() const { return m_window; }
    SDL_GPUDevice* GetDevice() const { return m_device; }

   private:
    SDL_Window* m_window = nullptr;
    SDL_GPUDevice* m_device = nullptr;
};
} // namespace engine
