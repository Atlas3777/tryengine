#include "editor/import/GltfUpAxis.hpp"

#include <cmath>

namespace tryeditor {
namespace {

//! Произведение Гамильтона: R(q1 * q2) = R(q1) ∘ R(q2), как `quat_mul` в daScript.
das::float4 MulQuat(das::float4 a, das::float4 b) {
    return das::float4(a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y, a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
                       a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w, a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z);
}

das::float3 Cross(das::float3 a, das::float3 b) {
    return das::float3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x);
}

das::float3 Scale(das::float3 v, float s) {
    return das::float3(v.x * s, v.y * s, v.z * s);
}

das::float3 Add(das::float3 a, das::float3 b) {
    return das::float3(a.x + b.x, a.y + b.y, a.z + b.z);
}

//! Вращение единичным кватернионом: `v + w·t + qv × t`, где `t = 2·(qv × v)` — та же формула,
//! что у `quat_mul_vec` в daScript.
das::float3 Rotate(das::float4 q, das::float3 v) {
    const das::float3 qv(q.x, q.y, q.z);
    const das::float3 t = Scale(Cross(qv, v), 2.0f);
    return Add(Add(v, Scale(t, q.w)), Cross(qv, t));
}

das::float4 NormalizeQuat(das::float4 q) {
    const float length_sq = q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w;
    if (length_sq <= 0.0f)
        return das::float4(0.0f, 0.0f, 0.0f, 1.0f);

    const float inv_length = 1.0f / std::sqrt(length_sq);
    return das::float4(q.x * inv_length, q.y * inv_length, q.z * inv_length, q.w * inv_length);
}

//! Поворот исходного пространства в Z-up движка. Угол всегда 90°, поэтому половина угла — 45°:
//! sin и cos равны √2/2, и кватернион получается точным, без тригонометрии в рантайме.
das::float4 UpAxisQuat(GltfUpAxis axis) {
    constexpr float kHalfSqrt2 = 0.70710678118654752f;
    switch (axis) {
        case GltfUpAxis::Y:
            // +90° вокруг X: Y -> Z, Z -> −Y (поворот Blender/FBX-импортёров из Y-up).
            return das::float4(kHalfSqrt2, 0.0f, 0.0f, kHalfSqrt2);
        case GltfUpAxis::X:
            // −90° вокруг Y: X -> Z, Z -> −X.
            return das::float4(0.0f, -kHalfSqrt2, 0.0f, kHalfSqrt2);
        case GltfUpAxis::Z:
            break;
    }
    return das::float4(0.0f, 0.0f, 0.0f, 1.0f);
}

}  // namespace

eastl::string_view GltfUpAxisName(GltfUpAxis axis) {
    switch (axis) {
        case GltfUpAxis::Y:
            return "Y";
        case GltfUpAxis::Z:
            return "Z";
        case GltfUpAxis::X:
            return "X";
    }
    return "Y";
}

bool GltfUpAxisFromName(eastl::string_view name, GltfUpAxis& out) {
    // Имена в .meta пишет `string(enum)` daslang'а, то есть с большой буквы; строчные
    // принимаем, потому что файл правят руками и в нём стоит "y" с прошлой версии.
    if (name == "Y" || name == "y") {
        out = GltfUpAxis::Y;
        return true;
    }
    if (name == "Z" || name == "z") {
        out = GltfUpAxis::Z;
        return true;
    }
    if (name == "X" || name == "x") {
        out = GltfUpAxis::X;
        return true;
    }
    return false;
}

void ConvertGltfTransformToEngineUp(GltfUpAxis axis, tryengine::Transform& transform) {
    if (axis == GltfUpAxis::Z)
        return;

    const das::float4 conversion = UpAxisQuat(axis);
    transform.position = Rotate(conversion, transform.position);
    transform.rotation = NormalizeQuat(MulQuat(conversion, transform.rotation));
}

}  // namespace tryeditor
