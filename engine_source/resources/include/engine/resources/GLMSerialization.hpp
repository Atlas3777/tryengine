#pragma once

#include <array>
#include <daScript/misc/vectypes.h>
#include <daScript/simulate/runtime_matrices.h>
#include <glaze/glaze.hpp>

// daslang-типы векторов и матриц в JSON пишутся плоскими массивами float.

template <>
struct glz::meta<das::float3> {
    using T = das::float3;
    static constexpr auto read = [](T& v, const std::array<float, 3>& arr) { v = T(arr[0], arr[1], arr[2]); };
    static constexpr auto write = [](const T& v) { return std::array<float, 3>{v.x, v.y, v.z}; };
    static constexpr auto value = glz::custom<read, write>;
};

template <>
struct glz::meta<das::float4> {
    using T = das::float4;
    static constexpr auto read = [](T& v, const std::array<float, 4>& arr) { v = T(arr[0], arr[1], arr[2], arr[3]); };
    static constexpr auto write = [](const T& v) { return std::array<float, 4>{v.x, v.y, v.z, v.w}; };
    static constexpr auto value = glz::custom<read, write>;
};

template <>
struct glz::meta<das::float4x4> {
    using T = das::float4x4;
    static constexpr auto read = [](T& m, const std::array<float, 16>& arr) {
        for (int row = 0; row < 4; ++row)
            m.m[row] = das::float4(arr[row * 4 + 0], arr[row * 4 + 1], arr[row * 4 + 2], arr[row * 4 + 3]);
    };
    static constexpr auto write = [](const T& m) {
        std::array<float, 16> arr{};
        for (int row = 0; row < 4; ++row) {
            arr[row * 4 + 0] = m.m[row].x;
            arr[row * 4 + 1] = m.m[row].y;
            arr[row * 4 + 2] = m.m[row].z;
            arr[row * 4 + 3] = m.m[row].w;
        }
        return arr;
    };
    static constexpr auto value = glz::custom<read, write>;
};
