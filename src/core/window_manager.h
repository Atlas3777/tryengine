#ifndef WINDOW_MANAGER_H
#define WINDOW_MANAGER_H

#include <SDL3/SDL.h>

class WindowManager {
public:
    static bool Initialize(int width, int height, const char* title);
    static void Terminate();
    static SDL_Window* GetWindow();

private:
    static SDL_Window* window;
};

#endif // WINDOW_MANAGER_H