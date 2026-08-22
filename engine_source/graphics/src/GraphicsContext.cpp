#include "engine/graphics/GraphicsContext.hpp"

#include <cassert>

#include "engine/core/Assert.hpp"

namespace tryengine::graphics {

GraphicsContext::GraphicsContext(const uint32_t width, const uint32_t height, const eastl::string_view title) {
    if (!TRY_VERIFY(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS), "SDL_Init failed: {}", SDL_GetError())) {
        return;
    }

    LogInfo(TRY_VARS(width, height));

    constexpr SDL_WindowFlags flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY;
    m_window = SDL_CreateWindow(title.data(), static_cast<int>(width), static_cast<int>(height), flags);
    TRY_CHECK(m_window, "Не удалось создать окно SDL: {}", SDL_GetError());

    m_device = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_SPIRV, true, nullptr);
    TRY_CHECK(m_device, "Не удалось создать GPU Device: {}", SDL_GetError());

    TRY_CHECK(SDL_ClaimWindowForGPUDevice(m_device, m_window), "Не удалось привязать окно к GPU Device: {}", SDL_GetError());

    // SDL_GPUPresentMode mode = SDL_GPU_PRESENTMODE_IMMEDIATE;
    // SDL_GPUPresentMode mode = SDL_GPU_PRESENTMODE_IMMEDIATE;
    // SDL_SetGPUSwapchainParameters(m_device, m_window, SDL_GPU_SWAPCHAINCOMPOSITION_SDR, mode);
}

GraphicsContext::~GraphicsContext() {
    if (m_device) {
        if (m_window) {
            SDL_ReleaseWindowFromGPUDevice(m_device, m_window);
        }
        SDL_DestroyGPUDevice(m_device);
        m_device = nullptr;
    }

    if (m_window) {
        SDL_DestroyWindow(m_window);
        m_window = nullptr;
    }

    SDL_Quit();
}
}  // namespace tryengine::graphics
