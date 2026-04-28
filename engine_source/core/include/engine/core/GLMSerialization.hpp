#pragma once
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <cereal/cereal.hpp>

namespace glm {

template<class Archive>
void serialize(Archive& archive, vec3& v) {
    archive(
        cereal::make_nvp("x", v.x),
        cereal::make_nvp("y", v.y),
        cereal::make_nvp("z", v.z)
    );
}

template<class Archive>
void serialize(Archive& archive, vec4& v) {
    archive(
        cereal::make_nvp("x", v.x),
        cereal::make_nvp("y", v.y),
        cereal::make_nvp("z", v.z),
        cereal::make_nvp("w", v.w)
    );
}

template<class Archive>
void serialize(Archive& archive, quat& q) {
    // Для кватерниона лучше оставить стандартные компоненты,
    // но дать им имена, чтобы не запутаться в порядке x,y,z,w
    archive(
        cereal::make_nvp("x", q.x),
        cereal::make_nvp("y", q.y),
        cereal::make_nvp("z", q.z),
        cereal::make_nvp("w", q.w)
    );
}

template<class Archive>
void serialize(Archive& archive, mat4& m) {
    // Матрицу удобнее всего читать как массив колонок
    archive(
        cereal::make_nvp("col0", m[0]),
        cereal::make_nvp("col1", m[1]),
        cereal::make_nvp("col2", m[2]),
        cereal::make_nvp("col3", m[3])
    );
}

} // namespace glm