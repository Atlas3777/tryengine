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
