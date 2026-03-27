#pragma once
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <cereal/cereal.hpp>

namespace glm {

template<class Archive>
void serialize(Archive& archive, vec3& v) {
    archive(v.x, v.y, v.z);
}

template<class Archive>
void serialize(Archive& archive, quat& q) {
    archive(q.x, q.y, q.z, q.w);
}

template<class Archive>
void serialize(Archive& archive, mat4& m) {
    // Матрицу можно сохранить как 4 колонки vec4
    archive(m[0], m[1], m[2], m[3]);
}

// Если используешь vec4, добавь и его
template<class Archive>
void serialize(Archive& archive, vec4& v) {
    archive(v.x, v.y, v.z, v.w);
}
}