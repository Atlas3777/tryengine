#include "core/window_manager.hpp"

bool WindowManager::Initialize(int width, int height, const char* title) {
    // Включаем поддержку libdecor для серверных декораций в Wayland
    SDL_SetHint(SDL_HINT_VIDEO_WAYLAND_ALLOW_LIBDECOR, "1");

    // Инициализация видео-подсистемы и событий (события критичны для Wayland!)
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
        SDL_Log("SDL_Init failed: %s", SDL_GetError());
        return false;
    }

    SDL_Log("Current Video Driver: %s", SDL_GetCurrentVideoDriver());

    w = width;
    h = height;

    // Убираем SDL_WINDOW_HIDDEN. В Wayland окно и так не покажется,
    // пока вы не начнете рисовать, но флаг HIDDEN может мешать инициализации GPU.
    SDL_WindowFlags flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY;

    window = SDL_CreateWindow(title, width, height, flags);

    if (!window) {
        SDL_Log("Could not create window: %s", SDL_GetError());
        return false;
    }

    // Создаем GPU Device
    device = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_SPIRV, true, nullptr);

    if (device) {
        if (!SDL_ClaimWindowForGPUDevice(device, window)) {
            SDL_Log("GPU Claim window failed: %s", SDL_GetError());
            return false;
        }
    } else {
        SDL_Log("Failed to create GPU Device: %s", SDL_GetError());
        return false;
    }

    return true;
}

SDL_Window* WindowManager::GetWindow() { return window; }
SDL_GPUDevice* WindowManager::GetDevice() { return device; }

void WindowManager::Terminate() {
    if (device) {
        if (window) {
            SDL_ReleaseWindowFromGPUDevice(device, window);
        }
        SDL_DestroyGPUDevice(device);
        device = nullptr;
    }

    if (window) {
        SDL_DestroyWindow(window);
        window = nullptr;
    }
    SDL_Quit();
}
