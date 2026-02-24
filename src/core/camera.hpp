#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

struct Camera {
    glm::vec3 pos;
    glm::vec3 front = glm::vec3(0.0f, 0.0f, -1.f);
    glm::vec3 up = glm::vec3(0.0f, 1.0f, 0.0f);
    float yaw = -90.0f;
    float pitch;
    float speed = 2.5f;
    float sensitivity = 0.05f;
    bool firstMouse = true;
};

void UpdateCamera(Camera& cam, double deltaTime);
