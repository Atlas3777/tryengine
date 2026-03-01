#include "GraphicsContext.hpp"

#include <string>

bool GraphicsContext::Initialize(int width, int height, const std::string& title) {
    SDL_SetHint(SDL_HINT_VIDEO_WAYLAND_ALLOW_LIBDECOR, "1");

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
        SDL_Log("SDL_Init failed: %s", SDL_GetError());
        return false;
    }

    SDL_WindowFlags flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY;

    m_window = SDL_CreateWindow(title.c_str(), width, height, flags);
    if (!m_window) {
        SDL_Log("Could not create window: %s", SDL_GetError());
        return false;
    }

    m_device = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_SPIRV, true, nullptr);
    if (!m_device) {
        SDL_Log("Failed to create GPU Device: %s", SDL_GetError());
        return false;
    }

    if (!SDL_ClaimWindowForGPUDevice(m_device, m_window)) {
        SDL_Log("GPU Claim window failed: %s", SDL_GetError());
        return false;
    }

    // SDL_GPUPresentMode mode = SDL_GPU_PRESENTMODE_MAILBOX;
    // SDL_SetGPUSwapchainParameters(m_device, m_window, SDL_GPU_SWAPCHAINCOMPOSITION_SDR, mode);
    return true;
}

void GraphicsContext::Terminate() {
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
