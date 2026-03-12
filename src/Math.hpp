#include <glm/ext/vector_common.hpp>
#include <glm/fwd.hpp>

#include "EngineTypes.hpp"
struct Ray {
    glm::vec3 origin;
    glm::vec3 direction;
    glm::vec3 invDirection;  // 1.0f / direction (для скорости)
};

bool RayIntersectsAABB(const Ray& ray, const AABBComponent& aabb, float& tResult) {
    float t1 = (aabb.worldMin.x - ray.origin.x) * ray.invDirection.x;
    float t2 = (aabb.worldMax.x - ray.origin.x) * ray.invDirection.x;
    float tmin = glm::min(t1, t2);
    float tmax = glm::max(t1, t2);

    for (int i = 1; i < 3; ++i) {
        t1 = (aabb.worldMin[i] - ray.origin[i]) * ray.invDirection[i];
        t2 = (aabb.worldMax[i] - ray.origin[i]) * ray.invDirection[i];
        tmin = glm::max(tmin, glm::min(t1, t2));
        tmax = glm::min(tmax, glm::max(t1, t2));
    }

    tResult = tmin;
    return tmax >= glm::max(0.0f, tmin);
}
Ray GetRayFromMouse(const CameraComponent& cam, const TransformComponent& camTransform, float mouseX, float mouseY,
                    float screenW, float screenH) {
    // Преобразуем экранные координаты в NDC (-1 to 1)
    float x = (2.0f * mouseX) / screenW - 1.0f;
    float y = 1.0f - (2.0f * mouseY) / screenH;

    glm::mat4 invProj =
        glm::inverse(glm::perspective(glm::radians(cam.fov), screenW / screenH, cam.nearPlane, cam.farPlane));
    glm::mat4 invView = glm::inverse(cam.viewMatrix);

    glm::vec4 rayClip = glm::vec4(x, y, -1.0f, 1.0f);
    glm::vec4 rayEye = invProj * rayClip;
    rayEye = glm::vec4(rayEye.x, rayEye.y, -1.0f, 0.0f);

    glm::vec3 rayWorld = glm::normalize(glm::vec3(invView * rayEye));

    return {camTransform.position, rayWorld, 1.0f / rayWorld};
}
