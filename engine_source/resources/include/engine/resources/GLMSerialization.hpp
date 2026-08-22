#pragma once

#include <hlsl++/vector_float.h>
#include <hlsl++/matrix_float.h>
#include <hlsl++/quaternion.h>
#include <glaze/glaze.hpp>
#include <array>

// hlslpp::float3
template <>
struct glz::meta<hlslpp::float3> {
    using T = hlslpp::float3;
    static constexpr auto read = [](T& v, const std::array<float, 3>& arr) {
        hlslpp::load(v, arr.data());
    };
    static constexpr auto write = [](const T& v) {
        std::array<float, 3> arr{};
        hlslpp::store(arr.data(), v);
        return arr;
    };
    static constexpr auto value = glz::custom<read, write>;
};

// hlslpp::float4
template <>
struct glz::meta<hlslpp::float4> {
    using T = hlslpp::float4;
    static constexpr auto read = [](T& v, const std::array<float, 4>& arr) {
        hlslpp::load(v, arr.data());
    };
    static constexpr auto write = [](const T& v) {
        std::array<float, 4> arr{};
        hlslpp::store(arr.data(), v);
        return arr;
    };
    static constexpr auto value = glz::custom<read, write>;
};

// hlslpp::quaternion
template <>
struct glz::meta<hlslpp::quaternion> {
    using T = hlslpp::quaternion;
    static constexpr auto read = [](T& q, const std::array<float, 4>& arr) {
        hlslpp::load(q, arr.data());
    };
    static constexpr auto write = [](const T& q) {
        std::array<float, 4> arr{};
        hlslpp::store(arr.data(), q);
        return arr;
    };
    static constexpr auto value = glz::custom<read, write>;
};

// hlslpp::float4x4
template <>
struct glz::meta<hlslpp::float4x4> {
    using T = hlslpp::float4x4;
    static constexpr auto read = [](T& m, const std::array<float, 16>& arr) {
        hlslpp::load(m, arr.data());
    };
    static constexpr auto write = [](const T& m) {
        std::array<float, 16> arr{};
        hlslpp::store(arr.data(), m);
        return arr;
    };
    static constexpr auto value = glz::custom<read, write>;
};