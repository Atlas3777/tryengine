#pragma once

#include <SDL3/SDL.h>

#include <string>

class WindowManager {
   public:
    WindowManager() = default;
    ~WindowManager() { Terminate(); }  // Авто-очистка

    // Запрещаем копирование, так как управляем уникальными ресурсами (GPU Device)
    WindowManager(const WindowManager&) = delete;
    WindowManager& operator=(const WindowManager&) = delete;

    bool Initialize(int width, int height, const std::string& title);
    void Terminate();

    // Геттеры
    SDL_Window* GetWindow() const { return m_window; }
    SDL_GPUDevice* GetDevice() const { return m_device; }

    // Инкапсулируем размер
    void GetSize(int& width, int& height) const;

    void SetFullscreen(bool fullscreen);
    void SetVSync(bool enabled);

   private:
    SDL_Window* m_window = nullptr;
    SDL_GPUDevice* m_device = nullptr;

    int m_width = 0;
    int m_height = 0;
    bool m_vsyncEnabled = true;
};
