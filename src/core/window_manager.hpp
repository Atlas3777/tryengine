#pragma once

#include <SDL3/SDL.h>

class WindowManager {
   public:
    bool Initialize(int width, int height, const char* title);
    void Terminate();
    SDL_Window* GetWindow();
    SDL_GPUDevice* device;
    int w;
    int h;

   private:
    SDL_Window* window;
};
