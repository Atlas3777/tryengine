#pragma once

#include <format>
#include <hlsl++.h>

// 1. Форматтер для swizzle1 (скаляры .x, .y, .z, .w)
template <int X>
struct std::formatter<hlslpp::swizzle1<X>> {
    constexpr auto parse(auto& ctx) { return ctx.begin(); }

    auto format(const hlslpp::swizzle1<X>& s, auto& ctx) const {
        return std::format_to(ctx.out(), "{}", static_cast<float>(s));
    }
};

// 2. Форматтеры для векторных свиззлов (.xy, .xyz, .xyzw)
template <int X, int Y>
struct std::formatter<hlslpp::swizzle2<X, Y>> : std::formatter<std::string_view> {
    auto format(const hlslpp::swizzle2<X, Y>& s, std::format_context& ctx) const {
        hlslpp::float2 v(s);
        return std::format_to(ctx.out(), "({}, {})", v.x, v.y);
    }
};

template <int X, int Y, int Z>
struct std::formatter<hlslpp::swizzle3<X, Y, Z>> : std::formatter<std::string_view> {
    auto format(const hlslpp::swizzle3<X, Y, Z>& s, std::format_context& ctx) const {
        hlslpp::float3 v(s);
        return std::format_to(ctx.out(), "({}, {}, {})", v.x, v.y, v.z);
    }
};

template <int X, int Y, int Z, int W>
struct std::formatter<hlslpp::swizzle4<X, Y, Z, W>> : std::formatter<std::string_view> {
    auto format(const hlslpp::swizzle4<X, Y, Z, W>& s, std::format_context& ctx) const {
        hlslpp::float4 v(s);
        return std::format_to(ctx.out(), "({}, {}, {}, {})", v.x, v.y, v.z, v.w);
    }
};

// 3. Форматтеры для базовых типов hlslpp::float1..4
template <>
struct std::formatter<hlslpp::float1> {
    constexpr auto parse(auto& ctx) { return ctx.begin(); }

    auto format(const hlslpp::float1& v, auto& ctx) const {
        return std::format_to(ctx.out(), "{}", v.x);
    }
};

template <>
struct std::formatter<hlslpp::float2> : std::formatter<std::string_view> {
    auto format(const hlslpp::float2& v, std::format_context& ctx) const {
        return std::format_to(ctx.out(), "({}, {})", v.x, v.y);
    }
};

template <>
struct std::formatter<hlslpp::float3> : std::formatter<std::string_view> {
    auto format(const hlslpp::float3& v, std::format_context& ctx) const {
        return std::format_to(ctx.out(), "({}, {}, {})", v.x, v.y, v.z);
    }
};

template <>
struct std::formatter<hlslpp::float4> : std::formatter<std::string_view> {
    auto format(const hlslpp::float4& v, std::format_context& ctx) const {
        return std::format_to(ctx.out(), "({}, {}, {}, {})", v.x, v.y, v.z, v.w);
    }
};

// 4. Многострочные форматтеры для матриц hlslpp::floatNxM

// --- Квадратные матрицы ---
template <>
struct std::formatter<hlslpp::float2x2> : std::formatter<std::string_view> {
    auto format(const hlslpp::float2x2& m, std::format_context& ctx) const {
        float f[4];
        hlslpp::store(f, m);
        return std::format_to(ctx.out(),
            "\n[{}, {}]"
            "\n[{}, {}]",
            f[0], f[1],
            f[2], f[3]);
    }
};

template <>
struct std::formatter<hlslpp::float3x3> : std::formatter<std::string_view> {
    auto format(const hlslpp::float3x3& m, std::format_context& ctx) const {
        float f[9];
        hlslpp::store(f, m);
        return std::format_to(ctx.out(),
            "\n[{}, {}, {}]"
            "\n[{}, {}, {}]"
            "\n[{}, {}, {}]",
            f[0], f[1], f[2],
            f[3], f[4], f[5],
            f[6], f[7], f[8]);
    }
};

template <>
struct std::formatter<hlslpp::float4x4> : std::formatter<std::string_view> {
    auto format(const hlslpp::float4x4& m, std::format_context& ctx) const {
        float f[16];
        hlslpp::store(f, m);
        return std::format_to(ctx.out(),
            "\n[{}, {}, {}, {}]"
            "\n[{}, {}, {}, {}]"
            "\n[{}, {}, {}, {}]"
            "\n[{}, {}, {}, {}]",
            f[0], f[1], f[2], f[3],
            f[4], f[5], f[6], f[7],
            f[8], f[9], f[10], f[11],
            f[12], f[13], f[14], f[15]);
    }
};

// --- Прямоугольные матрицы ---
template <>
struct std::formatter<hlslpp::float2x3> : std::formatter<std::string_view> {
    auto format(const hlslpp::float2x3& m, std::format_context& ctx) const {
        float f[6];
        hlslpp::store(f, m);
        return std::format_to(ctx.out(),
            "\n[{}, {}, {}]"
            "\n[{}, {}, {}]",
            f[0], f[1], f[2],
            f[3], f[4], f[5]);
    }
};

template <>
struct std::formatter<hlslpp::float3x2> : std::formatter<std::string_view> {
    auto format(const hlslpp::float3x2& m, std::format_context& ctx) const {
        float f[6];
        hlslpp::store(f, m);
        return std::format_to(ctx.out(),
            "\n[{}, {}]"
            "\n[{}, {}]"
            "\n[{}, {}]",
            f[0], f[1],
            f[2], f[3],
            f[4], f[5]);
    }
};

template <>
struct std::formatter<hlslpp::float2x4> : std::formatter<std::string_view> {
    auto format(const hlslpp::float2x4& m, std::format_context& ctx) const {
        float f[8];
        hlslpp::store(f, m);
        return std::format_to(ctx.out(),
            "\n[{}, {}, {}, {}]"
            "\n[{}, {}, {}, {}]",
            f[0], f[1], f[2], f[3],
            f[4], f[5], f[6], f[7]);
    }
};

template <>
struct std::formatter<hlslpp::float4x2> : std::formatter<std::string_view> {
    auto format(const hlslpp::float4x2& m, std::format_context& ctx) const {
        float f[8];
        hlslpp::store(f, m);
        return std::format_to(ctx.out(),
            "\n[{}, {}]"
            "\n[{}, {}]"
            "\n[{}, {}]"
            "\n[{}, {}]",
            f[0], f[1],
            f[2], f[3],
            f[4], f[5],
            f[6], f[7]);
    }
};

template <>
struct std::formatter<hlslpp::float3x4> : std::formatter<std::string_view> {
    auto format(const hlslpp::float3x4& m, std::format_context& ctx) const {
        float f[12];
        hlslpp::store(f, m);
        return std::format_to(ctx.out(),
            "\n[{}, {}, {}, {}]"
            "\n[{}, {}, {}, {}]"
            "\n[{}, {}, {}, {}]",
            f[0], f[1], f[2], f[3],
            f[4], f[5], f[6], f[7],
            f[8], f[9], f[10], f[11]);
    }
};

template <>
struct std::formatter<hlslpp::float4x3> : std::formatter<std::string_view> {
    auto format(const hlslpp::float4x3& m, std::format_context& ctx) const {
        float f[12];
        hlslpp::store(f, m);
        return std::format_to(ctx.out(),
            "\n[{}, {}, {}]"
            "\n[{}, {}, {}]"
            "\n[{}, {}, {}]"
            "\n[{}, {}, {}]",
            f[0], f[1], f[2],
            f[3], f[4], f[5],
            f[6], f[7], f[8],
            f[9], f[10], f[11]);
    }
};

// --- Векторы-столбцы (Nx1) ---
template <>
struct std::formatter<hlslpp::float2x1> : std::formatter<std::string_view> {
    auto format(const hlslpp::float2x1& m, std::format_context& ctx) const {
        float f[2];
        hlslpp::store(f, m);
        return std::format_to(ctx.out(), "\n[{}]\n[{}]", f[0], f[1]);
    }
};

template <>
struct std::formatter<hlslpp::float3x1> : std::formatter<std::string_view> {
    auto format(const hlslpp::float3x1& m, std::format_context& ctx) const {
        float f[3];
        hlslpp::store(f, m);
        return std::format_to(ctx.out(), "\n[{}]\n[{}]\n[{}]", f[0], f[1], f[2]);
    }
};

template <>
struct std::formatter<hlslpp::float4x1> : std::formatter<std::string_view> {
    auto format(const hlslpp::float4x1& m, std::format_context& ctx) const {
        float f[4];
        hlslpp::store(f, m);
        return std::format_to(ctx.out(), "\n[{}]\n[{}]\n[{}]\n[{}]", f[0], f[1], f[2], f[3]);
    }
};

// --- Векторы-строки (1xN) ---
template <>
struct std::formatter<hlslpp::float1x1> : std::formatter<std::string_view> {
    auto format(const hlslpp::float1x1& m, std::format_context& ctx) const {
        float f[1];
        hlslpp::store(f, m);
        return std::format_to(ctx.out(), "[{}]", f[0]);
    }
};

template <>
struct std::formatter<hlslpp::float1x2> : std::formatter<std::string_view> {
    auto format(const hlslpp::float1x2& m, std::format_context& ctx) const {
        float f[2];
        hlslpp::store(f, m);
        return std::format_to(ctx.out(), "[{}, {}]", f[0], f[1]);
    }
};

template <>
struct std::formatter<hlslpp::float1x3> : std::formatter<std::string_view> {
    auto format(const hlslpp::float1x3& m, std::format_context& ctx) const {
        float f[3];
        hlslpp::store(f, m);
        return std::format_to(ctx.out(), "[{}, {}, {}]", f[0], f[1], f[2]);
    }
};

template <>
struct std::formatter<hlslpp::float1x4> : std::formatter<std::string_view> {
    auto format(const hlslpp::float1x4& m, std::format_context& ctx) const {
        float f[4];
        hlslpp::store(f, m);
        return std::format_to(ctx.out(), "[{}, {}, {}, {}]", f[0], f[1], f[2], f[3]);
    }
};