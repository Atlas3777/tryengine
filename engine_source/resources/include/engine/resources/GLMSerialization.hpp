#pragma once

#include <glaze/glaze.hpp>
#include <glm/gtc/quaternion.hpp>
#include "engine/core/Components.hpp"

template <>
struct glz::meta<glm::vec3> {
    using T = glm::vec3;
    static constexpr auto value = glz::object(
        "x", glz::custom<[](const T& m) { return m.x; }, [](T& m, const float v) -> void { m.x = v; }>,
        "y", glz::custom<[](const T& m) { return m.y; }, [](T& m, const float v) -> void { m.y = v; }>,
        "z", glz::custom<[](const T& m) { return m.z; }, [](T& m, const float v) -> void { m.z = v; }>
    );
};

template <>
struct glz::meta<glm::vec4> {
    using T = glm::vec4;
    static constexpr auto value = glz::object(
        "x", glz::custom<[](const T& m) { return m.x; }, [](T& m, const float v) -> void { m.x = v; }>,
        "y", glz::custom<[](const T& m) { return m.y; }, [](T& m, const float v) -> void { m.y = v; }>,
        "z", glz::custom<[](const T& m) { return m.z; }, [](T& m, const float v) -> void { m.z = v; }>,
        "w", glz::custom<[](const T& m) { return m.w; }, [](T& m, const float v) -> void { m.w = v; }>
    );
};

template <>
struct glz::meta<glm::quat> {
    using T = glm::quat;
    static constexpr auto value = glz::object(
        "x", glz::custom<[](const T& m) { return m.x; }, [](T& m, const float v) -> void { m.x = v; }>,
        "y", glz::custom<[](const T& m) { return m.y; }, [](T& m, const float v) -> void { m.y = v; }>,
        "z", glz::custom<[](const T& m) { return m.z; }, [](T& m, const float v) -> void { m.z = v; }>,
        "w", glz::custom<[](const T& m) { return m.w; }, [](T& m, const float v) -> void { m.w = v; }>
    );
};

template <>
struct glz::meta<glm::mat4> {
    using T = glm::mat4;

    static constexpr auto col0 = glz::custom<
        [](const T& m) -> const glm::vec4& { return m[0]; },
        [](T& m, const glm::vec4& v) -> void { m[0] = v; }
    >;
    static constexpr auto col1 = glz::custom<
        [](const T& m) -> const glm::vec4& { return m[1]; },
        [](T& m, const glm::vec4& v) -> void { m[1] = v; }
    >;
    static constexpr auto col2 = glz::custom<
        [](const T& m) -> const glm::vec4& { return m[2]; },
        [](T& m, const glm::vec4& v) -> void { m[2] = v; }
    >;
    static constexpr auto col3 = glz::custom<
        [](const T& m) -> const glm::vec4& { return m[3]; },
        [](T& m, const glm::vec4& v) -> void { m[3] = v; }
    >;

    static constexpr auto value = glz::object(
        "col0", col0,
        "col1", col1,
        "col2", col2,
        "col3", col3
    );
};

namespace tryengine {
}

template <>
struct glz::meta<tryengine::Transform> {
    using T = tryengine::Transform;
    static constexpr auto value = glz::object(
        "position", &T::position,
        "rotation", &T::rotation,
        "scale", &T::scale
    );
};