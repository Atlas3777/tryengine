#include "core/window_manager.hpp"

bool WindowManager::Initialize(int width, int height, const char* title) {
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        return false;
    }
    w = width;
    h = height;
    window = SDL_CreateWindow(title, width, height, 0);
    device = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_SPIRV, true, nullptr);

    if (device && window) {
        SDL_ClaimWindowForGPUDevice(device, window);
    }

    SDL_GPUPresentMode presentMode = SDL_GPU_PRESENTMODE_VSYNC;  // По умолчанию
    // if (SDL_WindowSupportsGPUPresentMode(device, window, SDL_GPU_PRESENTMODE_IMMEDIATE)) {
    //     presentMode = SDL_GPU_PRESENTMODE_IMMEDIATE;
    // } else if (SDL_WindowSupportsGPUPresentMode(device, window, SDL_GPU_PRESENTMODE_MAILBOX)) {
    //     presentMode = SDL_GPU_PRESENTMODE_MAILBOX;
    // }
    // SDL_SetGPUSwapchainParameters(device, window, SDL_GPU_SWAPCHAINCOMPOSITION_SDR, presentMode);
    return true;
}

void WindowManager::Terminate() {
    // Сначала отвязываем окно от GPU и удаляем девайс
    if (device && window) {
        SDL_ReleaseWindowFromGPUDevice(device, window);
    }
    if (device) {
        SDL_DestroyGPUDevice(device);
        device = nullptr;
    }
    // Затем удаляем само окно и выходим
    if (window) {
        SDL_DestroyWindow(window);
        window = nullptr;
    }
    SDL_Quit();
}

SDL_Window* WindowManager::GetWindow() { return window; }
