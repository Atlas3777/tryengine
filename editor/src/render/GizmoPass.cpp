#include "editor/render/GizmoPass.hpp"

#include <cmath>
#include <cstddef>
#include <daScript/simulate/aot_builtin_matrix.h>

#include "engine/core/Engine.hpp"
#include "engine/core/ScriptSystem.hpp"
#include "engine/graphics/RasterState.hpp"
#include "engine/graphics/RenderCommon.hpp"
#include "engine/graphics/rg/RenderGraph.hpp"

namespace tryeditor {

using namespace tryengine::graphics;

namespace {

static_assert(sizeof(GizmoDrawState) == 48, "GizmoDrawState must match the [cpp_layout] struct in Gizmo.das");
static_assert(offsetof(GizmoDrawState, position) == 0);
static_assert(offsetof(GizmoDrawState, size) == 12);
static_assert(offsetof(GizmoDrawState, rotation) == 16);
static_assert(offsetof(GizmoDrawState, mode) == 32);
static_assert(offsetof(GizmoDrawState, hover) == 36);
static_assert(offsetof(GizmoDrawState, active) == 40);
static_assert(offsetof(GizmoDrawState, visible) == 44);
static_assert(sizeof(GizmoVertex) == 48, "GizmoVertex must match Gizmo.slang (three float4 members)");
static_assert(offsetof(GizmoVertex, position) == 0);
static_assert(offsetof(GizmoVertex, normal) == 16);
static_assert(offsetof(GizmoVertex, color) == 32);

// Id ручек, общие с Gizmo.das. Оси — 0..2, центр — 3, плоскости — ось их нормали.
constexpr int32_t kModeTranslate = 0;
constexpr int32_t kModeRotate = 1;
constexpr int32_t kModeScale = 2;

constexpr int32_t kHandleCenter = 3;
constexpr int32_t kHandlePlaneXY = 4;  // нормаль Z
constexpr int32_t kHandlePlaneYZ = 5;  // нормаль X
constexpr int32_t kHandlePlaneXZ = 6;  // нормаль Y

struct Color {
    float r, g, b, a;
};

constexpr Color kAxisColor[3] = {
    {0.94f, 0.26f, 0.30f, 1.0f},  // X
    {0.36f, 0.80f, 0.38f, 1.0f},  // Y
    {0.30f, 0.52f, 0.98f, 1.0f},  // Z
};
constexpr Color kHighlight{1.00f, 0.82f, 0.24f, 1.0f};
constexpr Color kCenter{0.86f, 0.88f, 0.92f, 1.0f};

// Размеры ручек в юнитах гизмо. Gizmo.das проверяет попадание ровно по этим числам:
// один юнит меша равен одному юниту аналитического picking-пространства.
constexpr float kAxisStart = 0.01f;
constexpr float kShaftEnd = 0.80f;
constexpr float kShaftRadius = 0.016f;
constexpr float kArrowTip = 1.00f;
constexpr float kArrowBase = 0.76f;
constexpr float kArrowRadius = 0.066f;
constexpr float kScaleCube = 0.90f;
constexpr float kScaleCubeHalf = 0.062f;
constexpr float kRingRadius = 0.85f;
constexpr float kRingTube = 0.028f;
// Дальняя половина кольца полупрозрачна: гизмо рисуется без depth-теста, и так объект
// просвечивает сквозь заднюю дугу, а передняя остаётся чёткой.
constexpr float kRingBackAlpha = 0.30f;
constexpr float kPlaneLow = 0.01f;
constexpr float kPlaneHigh = 0.5f;
constexpr float kPlaneBorder = 0.010f;
constexpr float kCenterCube = 0.072f;

constexpr float kPi = 3.14159265358979323846f;

das::float3 Make3(float x, float y, float z) {
    return das::float3(x, y, z);
}

das::float3 Add(const das::float3& a, const das::float3& b) {
    return Make3(a.x + b.x, a.y + b.y, a.z + b.z);
}

das::float3 Sub(const das::float3& a, const das::float3& b) {
    return Make3(a.x - b.x, a.y - b.y, a.z - b.z);
}

das::float3 Mul(const das::float3& v, float s) {
    return Make3(v.x * s, v.y * s, v.z * s);
}

float Dot(const das::float3& a, const das::float3& b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

das::float3 Cross(const das::float3& a, const das::float3& b) {
    return Make3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x);
}

das::float3 Normalize(const das::float3& v) {
    const float length_sq = Dot(v, v);
    if (length_sq < 1e-12f)
        return Make3(0.0f, 0.0f, 1.0f);
    return Mul(v, 1.0f / std::sqrt(length_sq));
}

// Любая устойчивая пара, перпендикулярная единичной оси.
void AxisBasis(const das::float3& axis, das::float3& u, das::float3& v) {
    if (std::fabs(axis.z) < 0.9f)
        u = Normalize(Cross(axis, Make3(0.0f, 0.0f, 1.0f)));
    else
        u = Normalize(Cross(axis, Make3(1.0f, 0.0f, 0.0f)));
    v = Cross(axis, u);
}

das::float3 AxisUnit(int axis) {
    switch (axis) {
        case 0:
            return Make3(1.0f, 0.0f, 0.0f);
        case 1:
            return Make3(0.0f, 1.0f, 0.0f);
        default:
            return Make3(0.0f, 0.0f, 1.0f);
    }
}

//! Направление ручки вдоль оси: +1 или -1, смотря с какой стороны ось обращена к камере.
//! Так стрелки, кубы и плоскости всегда оказываются на ближней к камере стороне гизмо.
float AxisSign(const das::float3& camera_dir, int axis) {
    return Dot(camera_dir, AxisUnit(axis)) < 0.0f ? -1.0f : 1.0f;
}

Color HandleColor(int32_t handle, const GizmoDrawState& state) {
    if (handle == state.hover || handle == state.active)
        return kHighlight;
    if (handle == kHandleCenter)
        return kCenter;
    if (handle >= kHandlePlaneXY && handle <= kHandlePlaneXZ) {
        static constexpr int kPlaneNormalAxis[3] = {2, 0, 1};
        return kAxisColor[kPlaneNormalAxis[handle - kHandlePlaneXY]];
    }
    if (handle >= 0 && handle <= 2)
        return kAxisColor[handle];
    return kCenter;
}

// Строит гизмо в локальном пространстве ручек и выдаёт мировые вершины. Нормали
// поворачиваются тем же кватернионом, что и позиции: `size` — равномерный масштаб, поэтому
// inverse transpose не нужен.
class GizmoMesh {
public:
    GizmoMesh(const GizmoDrawState& state, eastl::vector<GizmoVertex>& out) : state_(state), out_(out) {}

    void Quad(const das::float3& a, const das::float3& b, const das::float3& c, const das::float3& d,
              const das::float3& normal, const Color& color) {
        Emit(a, normal, color);
        Emit(b, normal, color);
        Emit(c, normal, color);
        Emit(a, normal, color);
        Emit(c, normal, color);
        Emit(d, normal, color);
    }

    void Cylinder(const das::float3& from, const das::float3& to, float radius, const Color& color, int segments) {
        const das::float3 axis = Normalize(Sub(to, from));
        das::float3 u, v;
        AxisBasis(axis, u, v);

        for (int i = 0; i < segments; ++i) {
            const float a0 = 2.0f * kPi * static_cast<float>(i) / static_cast<float>(segments);
            const float a1 = 2.0f * kPi * static_cast<float>(i + 1) / static_cast<float>(segments);
            const das::float3 n0 = Add(Mul(u, std::cos(a0)), Mul(v, std::sin(a0)));
            const das::float3 n1 = Add(Mul(u, std::cos(a1)), Mul(v, std::sin(a1)));

            const das::float3 from0 = Add(from, Mul(n0, radius));
            const das::float3 from1 = Add(from, Mul(n1, radius));
            const das::float3 to0 = Add(to, Mul(n0, radius));
            const das::float3 to1 = Add(to, Mul(n1, radius));

            Emit(from0, n0, color);
            Emit(from1, n1, color);
            Emit(to1, n1, color);
            Emit(from0, n0, color);
            Emit(to1, n1, color);
            Emit(to0, n0, color);
        }

        Cap(from, Mul(axis, -1.0f), u, v, radius, color, segments);
        Cap(to, axis, u, v, radius, color, segments);
    }

    void Cone(const das::float3& base, const das::float3& tip, float radius, const Color& color, int segments) {
        const das::float3 axis = Normalize(Sub(tip, base));
        const float height = std::sqrt(Dot(Sub(tip, base), Sub(tip, base)));
        das::float3 u, v;
        AxisBasis(axis, u, v);

        for (int i = 0; i < segments; ++i) {
            const float a0 = 2.0f * kPi * static_cast<float>(i) / static_cast<float>(segments);
            const float a1 = 2.0f * kPi * static_cast<float>(i + 1) / static_cast<float>(segments);
            const das::float3 d0 = Add(Mul(u, std::cos(a0)), Mul(v, std::sin(a0)));
            const das::float3 d1 = Add(Mul(u, std::cos(a1)), Mul(v, std::sin(a1)));
            const das::float3 n0 = Normalize(Add(Mul(d0, height), Mul(axis, radius)));
            const das::float3 n1 = Normalize(Add(Mul(d1, height), Mul(axis, radius)));

            Emit(Add(base, Mul(d0, radius)), n0, color);
            Emit(Add(base, Mul(d1, radius)), n1, color);
            Emit(tip, axis, color);
        }

        Cap(base, Mul(axis, -1.0f), u, v, radius, color, segments);
    }

    // Половина кольца, обращённая к камере (`front`) или от неё. Кольца рисуются без
    // depth-теста, поэтому дальние половины идут первыми и полупрозрачно: иначе задняя
    // половина одного кольца перекрывает переднюю половину другого и гизмо читается
    // как клубок. `camera_dir` — единичное направление на камеру в локальных осях гизмо.
    void TorusHalf(const das::float3& center, const das::float3& axis, float radius, float tube, const Color& color,
                   const das::float3& camera_dir, bool front, int major, int minor) {
        das::float3 u, v;
        AxisBasis(axis, u, v);

        for (int i = 0; i < major; ++i) {
            const float a0 = 2.0f * kPi * static_cast<float>(i) / static_cast<float>(major);
            const float a1 = 2.0f * kPi * static_cast<float>(i + 1) / static_cast<float>(major);
            const das::float3 d0 = Add(Mul(u, std::cos(a0)), Mul(v, std::sin(a0)));
            const das::float3 d1 = Add(Mul(u, std::cos(a1)), Mul(v, std::sin(a1)));

            // Отрезок, пересекающий горизонт, попадает в обе половины: передняя рисуется
            // поверх задней, поэтому шва на стыке не видно.
            const float s0 = Dot(d0, camera_dir);
            const float s1 = Dot(d1, camera_dir);
            if (front ? (s0 < 0.0f && s1 < 0.0f) : (s0 > 0.0f && s1 > 0.0f))
                continue;

            const das::float3 c0 = Add(center, Mul(d0, radius));
            const das::float3 c1 = Add(center, Mul(d1, radius));

            for (int j = 0; j < minor; ++j) {
                const float b0 = 2.0f * kPi * static_cast<float>(j) / static_cast<float>(minor);
                const float b1 = 2.0f * kPi * static_cast<float>(j + 1) / static_cast<float>(minor);

                const das::float3 n00 = Add(Mul(d0, std::cos(b0)), Mul(axis, std::sin(b0)));
                const das::float3 n01 = Add(Mul(d0, std::cos(b1)), Mul(axis, std::sin(b1)));
                const das::float3 n10 = Add(Mul(d1, std::cos(b1)), Mul(axis, std::sin(b1)));
                const das::float3 n11 = Add(Mul(d1, std::cos(b0)), Mul(axis, std::sin(b0)));

                const das::float3 p00 = Add(c0, Mul(n00, tube));
                const das::float3 p01 = Add(c0, Mul(n01, tube));
                const das::float3 p11 = Add(c1, Mul(n10, tube));
                const das::float3 p10 = Add(c1, Mul(n11, tube));

                Emit(p00, n00, color);
                Emit(p01, n01, color);
                Emit(p11, n10, color);
                Emit(p00, n00, color);
                Emit(p11, n10, color);
                Emit(p10, n11, color);
            }
        }
    }

    void Box(const das::float3& center, float half, const Color& color) {
        const das::float3 x(half, 0.0f, 0.0f);
        const das::float3 y(0.0f, half, 0.0f);
        const das::float3 z(0.0f, 0.0f, half);

        Face(Add(center, x), y, z, x, color);
        Face(Sub(center, x), z, y, Mul(x, -1.0f), color);
        Face(Add(center, y), z, x, y, color);
        Face(Sub(center, y), x, z, Mul(y, -1.0f), color);
        Face(Add(center, z), x, y, z, color);
        Face(Sub(center, z), y, x, Mul(z, -1.0f), color);
    }

private:
    void Cap(const das::float3& center, const das::float3& normal, const das::float3& u, const das::float3& v,
             float radius, const Color& color, int segments) {
        for (int i = 0; i < segments; ++i) {
            const float a0 = 2.0f * kPi * static_cast<float>(i) / static_cast<float>(segments);
            const float a1 = 2.0f * kPi * static_cast<float>(i + 1) / static_cast<float>(segments);
            Emit(center, normal, color);
            Emit(Add(center, Mul(Add(Mul(u, std::cos(a0)), Mul(v, std::sin(a0))), radius)), normal, color);
            Emit(Add(center, Mul(Add(Mul(u, std::cos(a1)), Mul(v, std::sin(a1))), radius)), normal, color);
        }
    }

    void Face(const das::float3& center, const das::float3& u, const das::float3& v, const das::float3& normal,
              const Color& color) {
        const das::float3 a = Sub(Sub(center, u), v);
        const das::float3 b = Sub(Add(center, u), v);
        const das::float3 c = Add(Add(center, u), v);
        const das::float3 d = Add(Sub(center, u), v);
        Quad(a, b, c, d, normal, color);
    }

    void Emit(const das::float3& local_position, const das::float3& local_normal, const Color& color) {
        const das::float3 world_position =
            Add(state_.position, das::quat_mul_vec(state_.rotation, Mul(local_position, state_.size)));
        const das::float3 world_normal = das::quat_mul_vec(state_.rotation, Normalize(local_normal));

        GizmoVertex vertex{};
        vertex.position = das::float4(world_position.x, world_position.y, world_position.z, 1.0f);
        vertex.normal = das::float4(world_normal.x, world_normal.y, world_normal.z, 0.0f);
        vertex.color = das::float4(color.r, color.g, color.b, color.a);
        out_.push_back(vertex);
    }

    const GizmoDrawState& state_;
    eastl::vector<GizmoVertex>& out_;
};

struct PlaneHandle {
    int32_t handle;
    int32_t normal_axis;
    int32_t u_axis;
    int32_t v_axis;
};

constexpr PlaneHandle kPlaneHandles[3] = {
    {kHandlePlaneXY, 2, 0, 1},
    {kHandlePlaneYZ, 0, 1, 2},
    {kHandlePlaneXZ, 1, 0, 2},
};

struct PlaneDrawInfo {
    const PlaneHandle* plane;
    das::float3 normal;
    das::float3 u;
    das::float3 v;
    float su;
    float sv;
    float distance;
};

void BuildPlaneHandles(GizmoMesh& mesh, const GizmoDrawState& state, const das::float3& camera_dir) {
    // Квадрат каждой плоскости лежит в той четверти своей плоскости, которая обращена к
    // камере. Иначе при взгляде с обратной стороны он уходит за центр, его режут стрелки, и
    // соседние плоскости накладываются друг на друга в одной точке экрана.
    PlaneDrawInfo planes[3];
    for (int i = 0; i < 3; ++i) {
        const PlaneHandle& plane = kPlaneHandles[i];
        PlaneDrawInfo& info = planes[i];
        info.plane = &plane;
        info.normal = AxisUnit(plane.normal_axis);
        info.u = AxisUnit(plane.u_axis);
        info.v = AxisUnit(plane.v_axis);
        info.su = Dot(camera_dir, info.u) < 0.0f ? -1.0f : 1.0f;
        info.sv = Dot(camera_dir, info.v) < 0.0f ? -1.0f : 1.0f;

        // Проекция центра квадрата на направление к камере: чем больше, тем ближе к ней.
        const float middle = (kPlaneLow + kPlaneHigh) * 0.5f;
        const das::float3 center = Add(Mul(info.u, info.su * middle), Mul(info.v, info.sv * middle));
        info.distance = Dot(center, camera_dir);
    }

    // Дальние квадраты рисуются первыми: ближний ложится поверх, а не под соседей.
    for (int i = 1; i < 3; ++i) {
        const PlaneDrawInfo key = planes[i];
        int j = i - 1;
        while (j >= 0 && planes[j].distance > key.distance) {
            planes[j + 1] = planes[j];
            --j;
        }
        planes[j + 1] = key;
    }

    for (const PlaneDrawInfo& info : planes) {
        const Color color = HandleColor(info.plane->handle, state);
        const bool highlighted = info.plane->handle == state.hover || info.plane->handle == state.active;

        const float u0 = info.su * kPlaneLow;
        const float u1 = info.su * kPlaneHigh;
        const float v0 = info.sv * kPlaneLow;
        const float v1 = info.sv * kPlaneHigh;

        const das::float3 lo_lo = Add(Mul(info.u, u0), Mul(info.v, v0));
        const das::float3 hi_lo = Add(Mul(info.u, u1), Mul(info.v, v0));
        const das::float3 hi_hi = Add(Mul(info.u, u1), Mul(info.v, v1));
        const das::float3 lo_hi = Add(Mul(info.u, u0), Mul(info.v, v1));

        Color fill = color;
        fill.a = highlighted ? 0.38f : 0.15f;
        mesh.Quad(lo_lo, hi_lo, hi_hi, lo_hi, info.normal, fill);

        Color border = color;
        border.a = highlighted ? 1.0f : 0.85f;
        const das::float3 wu = Mul(info.u, info.su * kPlaneBorder);
        const das::float3 wv = Mul(info.v, info.sv * kPlaneBorder);

        // Четыре компланарные полосы; после заливки, чтобы рамка оставалась чёткой.
        mesh.Quad(Sub(lo_lo, wv), Sub(hi_lo, wv), Add(hi_lo, wv), Add(lo_lo, wv), info.normal, border);
        mesh.Quad(Add(hi_hi, wv), Add(lo_hi, wv), Sub(lo_hi, wv), Sub(hi_hi, wv), info.normal, border);
        mesh.Quad(Sub(lo_lo, wu), Sub(lo_hi, wu), Add(lo_hi, wu), Add(lo_lo, wu), info.normal, border);
        mesh.Quad(Add(hi_hi, wu), Add(hi_lo, wu), Sub(hi_lo, wu), Sub(hi_hi, wu), info.normal, border);
    }
}

void BuildTranslate(GizmoMesh& mesh, const GizmoDrawState& state, const das::float3& camera_dir) {
    BuildPlaneHandles(mesh, state, camera_dir);

    for (int axis = 0; axis < 3; ++axis) {
        const Color color = HandleColor(axis, state);
        // Стрелка растёт в сторону камеры: направление оси переворачивается вместе с
        // плоскостями, и ручка не уходит за пивот.
        const das::float3 direction = Mul(AxisUnit(axis), AxisSign(camera_dir, axis));
        mesh.Cylinder(Mul(direction, kAxisStart), Mul(direction, kShaftEnd), kShaftRadius, color, 20);
        mesh.Cone(Mul(direction, kArrowBase), Mul(direction, kArrowTip), kArrowRadius, color, 24);
    }
}

void BuildRotate(GizmoMesh& mesh, const GizmoDrawState& state, const das::float3& camera_dir) {
    // Дальние половины колец должны уйти под ближние независимо от поворота объекта.
    const int32_t highlighted = state.active >= 0 && state.active <= 2 ? state.active : state.hover;
    int axes[3] = {0, 1, 2};
    // Подсвеченное кольцо рисуется последним, чтобы его не резали соседние.
    if (highlighted >= 0 && highlighted <= 2) {
        int slot = 0;
        for (int axis = 0; axis < 3; ++axis)
            if (axis != highlighted)
                axes[slot++] = axis;
        axes[2] = highlighted;
    }

    for (int pass = 0; pass < 2; ++pass) {
        const bool front = pass == 1;
        for (int i = 0; i < 3; ++i) {
            const int axis = axes[i];
            Color color = HandleColor(axis, state);
            if (!front) {
                // Задняя дуга подсвеченного кольца ярче остальных: иначе hover теряется
                // на дальней стороне.
                color.a = axis == highlighted ? 0.55f : kRingBackAlpha;
            }
            mesh.TorusHalf(Make3(0.0f, 0.0f, 0.0f), AxisUnit(axis), kRingRadius, kRingTube, color, camera_dir, front,
                           128, 10);
        }
    }
}

void BuildScale(GizmoMesh& mesh, const GizmoDrawState& state, const das::float3& camera_dir) {
    for (int axis = 0; axis < 3; ++axis) {
        const Color color = HandleColor(axis, state);
        // Оси масштаба переворачиваются так же, как стрелки перемещения.
        const das::float3 direction = Mul(AxisUnit(axis), AxisSign(camera_dir, axis));
        mesh.Cylinder(Mul(direction, kAxisStart), Mul(direction, kScaleCube), kShaftRadius * 0.75f, color, 16);
        mesh.Box(Mul(direction, kScaleCube), kScaleCubeHalf, color);
    }

    const Color center = HandleColor(kHandleCenter, state);
    mesh.Box(Make3(0.0f, 0.0f, 0.0f), kCenterCube, center);
}

void BuildGizmo(const GizmoDrawState& state, const das::float3& camera_position, eastl::vector<GizmoVertex>& out) {
    // Направление на камеру в локальных осях гизмо: от него зависят переворот стрелок,
    // кубов, плоскостей и порядок половин колец.
    const das::float3 to_camera =
        das::quat_mul_vec(das::quat_conjugate(state.rotation), Sub(camera_position, state.position));
    const das::float3 camera_dir = Normalize(to_camera);

    GizmoMesh mesh(state, out);
    switch (state.mode) {
        case kModeRotate:
            BuildRotate(mesh, state, camera_dir);
            break;
        case kModeScale:
            BuildScale(mesh, state, camera_dir);
            break;
        default:
            BuildTranslate(mesh, state, camera_dir);
            break;
    }
}

struct alignas(16) GizmoCameraGPU {
    das::float4x4 view;
    das::float4x4 proj;
    das::float4 position;
};

struct GizmoPushConstants {
    VkDeviceAddress vertices;
    VkDeviceAddress camera;
};

}  // namespace

void GizmoPass::Collect(CollectContext& ctx) {
    auto state =
        ctx.engine.Get<tryengine::core::ScriptSystem>().SimpleReturnUnsafe<GizmoDrawState*>("GetGizmoDrawState");
    if (!state.has_value() || *state == nullptr) {
        state_.visible = 0;
        return;
    }
    state_ = *(*state);
}

void GizmoPass::Setup(RenderGraphBuilder& builder) {
    color_ = builder.Write(RGTag_v<"SceneColor">, RGUsageHint::ColorAttachment);
}

void GizmoPass::Execute(RGExecuteContext& ctx) {
    if (!state_.visible || state_.size <= 0.0f)
        return;

    const auto* extent = ctx.cpu_bb->GetAs<VkExtent2D>(RGTag_v<"ViewportSize">);
    const auto* camera_cpu = ctx.cpu_bb->GetAs<CameraData>(RGTag_v<"Camera">);
    if (!extent || !camera_cpu)
        return;
    if (camera_cpu->proj.m[0].x == 0.0f)
        return;

    vertices_.clear();
    BuildGizmo(state_, camera_cpu->position, vertices_);
    if (vertices_.empty())
        return;

    GizmoCameraGPU camera{};
    camera.view = camera_cpu->view;
    camera.proj = camera_cpu->proj;
    camera.position = das::float4(camera_cpu->position.x, camera_cpu->position.y, camera_cpu->position.z, 1.0f);

    auto camera_alloc = ctx.AllocateTransient(&camera, sizeof(camera), alignof(GizmoCameraGPU));
    // std430 требует 16-байтовое начало для массива структур с float4; типы daslang
    // выровнены всего по 4, поэтому выравнивание задано явно.
    auto vertices_alloc = ctx.AllocateTransient(vertices_.data(), sizeof(GizmoVertex) * vertices_.size(), 16);

    RGColorAttachment color_att{};
    color_att.handle = color_;
    color_att.load_op = VK_ATTACHMENT_LOAD_OP_LOAD;
    color_att.store_op = VK_ATTACHMENT_STORE_OP_STORE;

    const VkRect2D render_area{{0, 0}, *extent};
    ctx.BeginRendering(eastl::span(&color_att, 1), nullptr, render_area);

    SetViewportScissor(ctx.cmd_buffer, *extent);
    // Гизмо — оверлей: глубину не читает и не пишет.
    ApplyRasterState(ctx.cmd_buffer, {.cull_mode = VK_CULL_MODE_NONE,
                                      .depth_test = false,
                                      .depth_write = false,
                                      .depth_compare = VK_COMPARE_OP_ALWAYS});

    // Премультиплицированная альфа держит альфу сцены в единице: на этом держится текстура вьюпорта.
    constexpr VkBool32 blend_enable = VK_TRUE;
    vkCmdSetColorBlendEnableEXT(ctx.cmd_buffer, 0, 1, &blend_enable);
    constexpr VkColorBlendEquationEXT blend_equation{
        .srcColorBlendFactor = VK_BLEND_FACTOR_ONE,
        .dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        .colorBlendOp = VK_BLEND_OP_ADD,
        .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
        .dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        .alphaBlendOp = VK_BLEND_OP_ADD,
    };
    vkCmdSetColorBlendEquationEXT(ctx.cmd_buffer, 0, 1, &blend_equation);

    Shader* shader = shader_.Get();
    VkShaderStageFlagBits stages[] = {VK_SHADER_STAGE_VERTEX_BIT, VK_SHADER_STAGE_FRAGMENT_BIT};
    VkShaderEXT shaders[] = {shader->vertex_shader, shader->fragment_shader};
    vkCmdBindShadersEXT(ctx.cmd_buffer, 2, stages, shaders);

    GizmoPushConstants pc{.vertices = vertices_alloc.bda_address, .camera = camera_alloc.bda_address};
    vkCmdPushConstants(ctx.cmd_buffer, shader->layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                       sizeof(GizmoPushConstants), &pc);

    vkCmdDraw(ctx.cmd_buffer, static_cast<uint32_t>(vertices_.size()), 1, 0, 0);

    ctx.EndRendering();
}

}  // namespace tryeditor
