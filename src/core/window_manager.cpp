#include "window_manager.hpp"

SDL_Window* WindowManager::window = nullptr;

bool WindowManager::Initialize(int width, int height, const char* title) {
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        return false;
    }

    window = SDL_CreateWindow(title, width, height, 0);
    if (!window) {
        SDL_Quit();
        return false;
    }

    return true;
}

void WindowManager::Terminate() {
    if (window) {
        SDL_DestroyWindow(window);
        window = nullptr;
    }
    SDL_Quit();
}

SDL_Window* WindowManager::GetWindow() { return window; }
