#include "core/camera.h"

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_keyboard.h>
#include <SDL3/SDL_mouse.h>
#include <SDL3/SDL_scancode.h>

void UpdateCamera(Camera& cam, bool& running, double deltaTime) {
    static float lastX = 0.0f;
    static float lastY = 0.0f;

    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_EVENT_QUIT) {
            running = false;
        } else if (event.type == SDL_EVENT_MOUSE_MOTION) {
            if (cam.firstMouse) {
                lastX = (float)event.motion.x;
                lastY = (float)event.motion.y;
                cam.firstMouse = false;
                // SDL_SetWindowRelativeMouseMode(window, true);
            }

            float xoffset = event.motion.xrel;
            float yoffset = -event.motion.yrel;  // инвертируем, т.к. Y идет сверху вниз

            xoffset *= cam.sensitivity;
            yoffset *= cam.sensitivity;

            cam.yaw += xoffset;
            cam.pitch += yoffset;

            // Ограничиваем pitch, чтобы не "сделать сальто"
            if (cam.pitch > 89.0f) cam.pitch = 89.0f;
            if (cam.pitch < -89.0f) cam.pitch = -89.0f;

            // Пересчитываем вектор направления камеры
            glm::vec3 front;
            front.x = cos(glm::radians(cam.yaw)) * cos(glm::radians(cam.pitch));
            front.y = sin(glm::radians(cam.pitch));
            front.z = sin(glm::radians(cam.yaw)) * cos(glm::radians(cam.pitch));
            cam.front = glm::normalize(front);
        }
    }

    // Управление клавишами
    const bool* keys = SDL_GetKeyboardState(nullptr);
    float currentSpeed = cam.speed * (float)deltaTime;

    if (keys[SDL_SCANCODE_W]) cam.pos += currentSpeed * cam.front;
    if (keys[SDL_SCANCODE_S]) cam.pos -= currentSpeed * cam.front;
    if (keys[SDL_SCANCODE_A]) cam.pos -= glm::normalize(glm::cross(cam.front, cam.up)) * currentSpeed;
    if (keys[SDL_SCANCODE_D]) cam.pos += glm::normalize(glm::cross(cam.front, cam.up)) * currentSpeed;
    if (keys[SDL_SCANCODE_E]) cam.pos += currentSpeed * cam.up;
    if (keys[SDL_SCANCODE_Q]) cam.pos -= currentSpeed * cam.up;
}
