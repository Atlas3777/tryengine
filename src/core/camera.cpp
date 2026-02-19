#include "core/camera.hpp"

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_keyboard.h>
#include <SDL3/SDL_mouse.h>
#include <SDL3/SDL_scancode.h>

#include <cmath>

#include "core/camera.hpp"

void UpdateCamera(Camera& cam, double deltaTime) {
    // 1. Обновляем вектор направления (front) из углов yaw/pitch
    glm::vec3 front;
    front.x = std::cos(glm::radians(cam.yaw)) * std::cos(glm::radians(cam.pitch));
    front.y = std::sin(glm::radians(cam.pitch));
    front.z = std::sin(glm::radians(cam.yaw)) * std::cos(glm::radians(cam.pitch));
    cam.front = glm::normalize(front);

    // 2. Рассчитываем локальный базис
    glm::vec3 right = glm::normalize(glm::cross(cam.front, cam.up));
    glm::vec3 localUp = glm::normalize(glm::cross(right, cam.front));

    // 3. Управление перемещением (опрос клавиш)
    const bool* keys = SDL_GetKeyboardState(nullptr);
    float currentSpeed = cam.speed * (float)deltaTime;

    if (keys[SDL_SCANCODE_W]) cam.pos += currentSpeed * cam.front;
    if (keys[SDL_SCANCODE_S]) cam.pos -= currentSpeed * cam.front;
    if (keys[SDL_SCANCODE_A]) cam.pos -= right * currentSpeed;
    if (keys[SDL_SCANCODE_D]) cam.pos += right * currentSpeed;
    if (keys[SDL_SCANCODE_E]) cam.pos += currentSpeed * localUp;
    if (keys[SDL_SCANCODE_Q]) cam.pos -= currentSpeed * localUp;
}
