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
            }

            float xoffset = event.motion.xrel;
            float yoffset = -event.motion.yrel;

            xoffset *= cam.sensitivity;
            yoffset *= cam.sensitivity;

            cam.yaw += xoffset;
            cam.pitch += yoffset;

            if (cam.pitch > 89.0f) cam.pitch = 89.0f;
            if (cam.pitch < -89.0f) cam.pitch = -89.0f;

            glm::vec3 front;
            front.x = cos(glm::radians(cam.yaw)) * cos(glm::radians(cam.pitch));
            front.y = sin(glm::radians(cam.pitch));
            front.z = sin(glm::radians(cam.yaw)) * cos(glm::radians(cam.pitch));
            cam.front = glm::normalize(front);
        }
    }

    // === Пересчёт локального базиса камеры ===
    // Правый вектор: перпендикулярен направлению взгляда и мировому "вверх"
    glm::vec3 right = glm::normalize(glm::cross(cam.front, cam.up));

    // Локальный "вверх": перпендикулярен направлению взгляда и правому вектору
    glm::vec3 localUp;
    float alignment = glm::abs(glm::dot(cam.front, cam.up));
    // if (alignment > 0.99f) {
    //     // Защита от вырождения при взгляде строго вверх/вниз
    //     localUp = glm::normalize(cam.up);
    // } else {
    localUp = glm::normalize(glm::cross(right, cam.front));
    // }

    // === Управление перемещением ===
    const bool* keys = SDL_GetKeyboardState(nullptr);
    float currentSpeed = cam.speed * (float)deltaTime;

    if (keys[SDL_SCANCODE_W]) cam.pos += currentSpeed * cam.front;
    if (keys[SDL_SCANCODE_S]) cam.pos -= currentSpeed * cam.front;
    if (keys[SDL_SCANCODE_A]) cam.pos -= right * currentSpeed;  // Используем precomputed right
    if (keys[SDL_SCANCODE_D]) cam.pos += right * currentSpeed;
    if (keys[SDL_SCANCODE_E]) cam.pos += currentSpeed * localUp;  // Движение относительно взгляда
    if (keys[SDL_SCANCODE_Q]) cam.pos -= currentSpeed * localUp;
}
