#pragma once

#include <SDL3/SDL.h>

class WindowManager {
   public:
    bool Initialize(int width, int height, const char* title);
    void SetFullscreen(bool fullscreen);
    void Terminate();
    SDL_Window* GetWindow();
    SDL_GPUDevice* GetDevice();
    int w;
    int h;

   private:
    SDL_Window* window;
    SDL_GPUDevice* device;
};
