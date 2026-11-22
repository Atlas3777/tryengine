#include "engine/physics/PhysicsWorld.hpp"

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <mutex>

// Jolt requires its umbrella header before any other Jolt header. The block is kept
// out of clang-format's include sorting so the order cannot be rearranged by accident.
// clang-format off
#include <Jolt/Jolt.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyInterface.h>
#include <Jolt/Physics/Collision/BroadPhase/BroadPhaseLayer.h>
#include <Jolt/Physics/Collision/ContactListener.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/CylinderShape.h>
#include <Jolt/Physics/Collision/Shape/PlaneShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/EPhysicsUpdateError.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/RegisterTypes.h>
// clang-format on

#include "engine/core/Assert.hpp"
#include "engine/core/Log.hpp"
#include "engine/core/SlotMap.hpp"

namespace tryengine::physics {

namespace {

using tryengine::core::LogCategory;
using tryengine::core::LogError;
using tryengine::core::LogInfo;

constexpr uint32_t kMaxContactEvents = 256;
constexpr float kMaxFrameDt = 0.25f;
constexpr float kMinShapeSize = 0.001f;
constexpr float kPi = 3.14159265358979323846f;

bool SamePosition(const das::float3& a, const das::float3& b) {
    return a.x == b.x && a.y == b.y && a.z == b.z;
}

bool SameRotation(const das::float4& a, const das::float4& b) {
    // Jolt normalizes on write, so compare the orientation instead of the four components.
    // q and -q are the same rotation.
    const float dot = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
    return std::fabs(dot) >= 1.0f - 1e-5f;
}

namespace Layers {
constexpr JPH::ObjectLayer kNonMoving = 0;
constexpr JPH::ObjectLayer kMoving = 1;
constexpr JPH::ObjectLayer kCount = 2;
}  // namespace Layers

namespace BroadPhaseLayers {
constexpr JPH::BroadPhaseLayer kNonMoving(0);
constexpr JPH::BroadPhaseLayer kMoving(1);
constexpr uint32_t kCount = 2;
}  // namespace BroadPhaseLayers

class ObjectLayerPairFilterImpl final : public JPH::ObjectLayerPairFilter {
public:
    bool ShouldCollide(JPH::ObjectLayer layer1, JPH::ObjectLayer layer2) const override {
        if (layer1 == Layers::kNonMoving)
            return layer2 == Layers::kMoving;
        if (layer1 == Layers::kMoving)
            return true;
        return false;
    }
};

class BPLayerInterfaceImpl final : public JPH::BroadPhaseLayerInterface {
public:
    BPLayerInterfaceImpl() {
        object_to_broadphase_[Layers::kNonMoving] = BroadPhaseLayers::kNonMoving;
        object_to_broadphase_[Layers::kMoving] = BroadPhaseLayers::kMoving;
    }

    JPH::uint GetNumBroadPhaseLayers() const override {
        return BroadPhaseLayers::kCount;
    }

    JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer layer) const override {
        return object_to_broadphase_[layer];
    }

private:
    JPH::BroadPhaseLayer object_to_broadphase_[Layers::kCount];
};

class ObjectVsBroadPhaseLayerFilterImpl final : public JPH::ObjectVsBroadPhaseLayerFilter {
public:
    bool ShouldCollide(JPH::ObjectLayer layer, JPH::BroadPhaseLayer broadphase_layer) const override {
        if (layer == Layers::kNonMoving)
            return broadphase_layer == BroadPhaseLayers::kMoving;
        return true;
    }
};

void JoltTrace(const char* fmt, ...) {
    char buffer[1024];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    LogInfo(LogCategory::Physics, "{}", buffer);
}

#ifdef JPH_ENABLE_ASSERTS
bool JoltAssertFailed(const char* expression, const char* message, const char* file, JPH::uint line) {
    LogError(LogCategory::Physics, "Jolt assert '{}' at {}:{} {}", expression, file, line,
             message != nullptr ? message : "");
    return false;
}
#endif

//! Глобальные Jolt-объекты создаются один раз на процесс. PhysicsWorld может
//! пересоздаваться (hot reload, тестовый контекст), а RegisterTypes — нет.
void EnsureJoltStarted() {
    static bool started = false;
    if (started)
        return;

    JPH::RegisterDefaultAllocator();
    JPH::Trace = JoltTrace;
#ifdef JPH_ENABLE_ASSERTS
    JPH::AssertFailed = JoltAssertFailed;
#endif
    JPH::Factory::sInstance = new JPH::Factory();
    JPH::RegisterTypes();
    started = true;
}

JPH::Vec3 ToVec3(const das::float3& v) {
    return JPH::Vec3(v.x, v.y, v.z);
}

JPH::RVec3 ToRVec3(const das::float3& v) {
    return JPH::RVec3(v.x, v.y, v.z);
}

JPH::Quat ToQuat(const das::float4& q) {
    const float length_sq = q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w;
    if (!std::isfinite(length_sq) || length_sq < 1.0e-12f)
        return JPH::Quat::sIdentity();
    const float inv_length = 1.0f / std::sqrt(length_sq);
    return JPH::Quat(q.x * inv_length, q.y * inv_length, q.z * inv_length, q.w * inv_length);
}

das::float3 FromVec3(const JPH::Vec3& v) {
    return das::float3(v.GetX(), v.GetY(), v.GetZ());
}

das::float3 FromRVec3(const JPH::RVec3& v) {
    return das::float3(static_cast<float>(v.GetX()), static_cast<float>(v.GetY()), static_cast<float>(v.GetZ()));
}

das::float4 FromQuat(const JPH::Quat& q) {
    const JPH::Vec4 v = q.GetXYZW();
    return das::float4(v.GetX(), v.GetY(), v.GetZ(), v.GetW());
}

JPH::EMotionType ToJoltMotion(MotionType motion) {
    switch (motion) {
        case MotionType::Static:
            return JPH::EMotionType::Static;
        case MotionType::Kinematic:
            return JPH::EMotionType::Kinematic;
        case MotionType::Dynamic:
            return JPH::EMotionType::Dynamic;
    }
    return JPH::EMotionType::Static;
}

JPH::Ref<JPH::Shape> MakeShape(const BodyDesc& desc) {
    const float radius = std::max(kMinShapeSize, std::abs(desc.radius));
    const float half_height = std::max(kMinShapeSize, std::abs(desc.half_height));
    // Ось Y — локальная ось капсулы и цилиндра в Jolt. Движок живёт с Z вверх,
    // поэтому поворачиваем форму один раз здесь; тело уже получает обычный rotation.
    const JPH::Quat y_to_z = JPH::Quat::sRotation(JPH::Vec3::sAxisX(), 0.5f * kPi);

    switch (desc.shape) {
        case ShapeType::Box: {
            const JPH::Vec3 half(std::max(kMinShapeSize, std::abs(desc.half_extents.x)),
                                 std::max(kMinShapeSize, std::abs(desc.half_extents.y)),
                                 std::max(kMinShapeSize, std::abs(desc.half_extents.z)));
            return new JPH::BoxShape(half);
        }
        case ShapeType::Sphere:
            return new JPH::SphereShape(radius);
        case ShapeType::Capsule:
            return new JPH::RotatedTranslatedShape(JPH::Vec3::sZero(), y_to_z,
                                                   new JPH::CapsuleShape(half_height, radius));
        case ShapeType::Cylinder:
            return new JPH::RotatedTranslatedShape(JPH::Vec3::sZero(), y_to_z,
                                                   new JPH::CylinderShape(half_height, radius));
        case ShapeType::Plane: {
            const float extent = std::max(10.0f, std::abs(desc.half_extents.x));
            return new JPH::PlaneShape(JPH::Plane(JPH::Vec3(0.0f, 0.0f, 1.0f), 0.0f), nullptr, extent);
        }
    }
    return nullptr;
}

struct Color {
    uint8_t r, g, b, a;
};

constexpr Color kStaticColor{130, 130, 130, 255};
constexpr Color kKinematicColor{240, 200, 70, 255};
constexpr Color kDynamicColor{70, 200, 240, 255};
constexpr Color kSensorColor{240, 80, 240, 255};
constexpr Color kContactColor{255, 80, 80, 255};

void PushLine(eastl::vector<PhysicsDebugLine>& out, const JPH::RVec3& a, const JPH::RVec3& b, const Color& color) {
    PhysicsDebugLine line;
    line.start = FromRVec3(a);
    line.end = FromRVec3(b);
    line.color[0] = color.r;
    line.color[1] = color.g;
    line.color[2] = color.b;
    line.color[3] = color.a;
    out.push_back(line);
}

JPH::RVec3 WorldPoint(const JPH::RVec3& position, const JPH::Quat& rotation, const JPH::Vec3& local) {
    const JPH::Vec3 rotated = rotation * local;
    return JPH::RVec3(position.GetX() + rotated.GetX(), position.GetY() + rotated.GetY(),
                      position.GetZ() + rotated.GetZ());
}

void DrawBox(eastl::vector<PhysicsDebugLine>& out, const JPH::RVec3& position, const JPH::Quat& rotation,
             const das::float3& half_extents, const Color& color) {
    const float x = std::max(kMinShapeSize, std::abs(half_extents.x));
    const float y = std::max(kMinShapeSize, std::abs(half_extents.y));
    const float z = std::max(kMinShapeSize, std::abs(half_extents.z));

    JPH::Vec3 corners[8];
    for (int i = 0; i < 8; ++i)
        corners[i] = JPH::Vec3((i & 1) ? x : -x, (i & 2) ? y : -y, (i & 4) ? z : -z);

    static constexpr int kEdges[12][2] = {
        {0, 1}, {2, 3}, {4, 5}, {6, 7}, {0, 2}, {1, 3}, {4, 6}, {5, 7}, {0, 4}, {1, 5}, {2, 6}, {3, 7},
    };
    for (const auto& edge : kEdges) {
        PushLine(out, WorldPoint(position, rotation, corners[edge[0]]),
                 WorldPoint(position, rotation, corners[edge[1]]), color);
    }
}

void DrawCircle(eastl::vector<PhysicsDebugLine>& out, const JPH::RVec3& position, const JPH::Quat& rotation,
                const JPH::Vec3& axis_u, const JPH::Vec3& axis_v, float radius, const Color& color) {
    constexpr int kSegments = 24;
    JPH::Vec3 previous = axis_u * radius;
    for (int i = 1; i <= kSegments; ++i) {
        const float angle = 2.0f * kPi * static_cast<float>(i) / static_cast<float>(kSegments);
        const JPH::Vec3 current = axis_u * (radius * std::cos(angle)) + axis_v * (radius * std::sin(angle));
        PushLine(out, WorldPoint(position, rotation, previous), WorldPoint(position, rotation, current), color);
        previous = current;
    }
}

void DrawSphere(eastl::vector<PhysicsDebugLine>& out, const JPH::RVec3& position, const JPH::Quat& rotation,
                float radius, const Color& color) {
    DrawCircle(out, position, rotation, JPH::Vec3::sAxisX(), JPH::Vec3::sAxisY(), radius, color);
    DrawCircle(out, position, rotation, JPH::Vec3::sAxisX(), JPH::Vec3::sAxisZ(), radius, color);
    DrawCircle(out, position, rotation, JPH::Vec3::sAxisY(), JPH::Vec3::sAxisZ(), radius, color);
}

void DrawCylinder(eastl::vector<PhysicsDebugLine>& out, const JPH::RVec3& position, const JPH::Quat& rotation,
                  float half_height, float radius, const Color& color) {
    DrawCircle(out, WorldPoint(position, rotation, JPH::Vec3(0.0f, 0.0f, half_height)), rotation, JPH::Vec3::sAxisX(),
               JPH::Vec3::sAxisY(), radius, color);
    DrawCircle(out, WorldPoint(position, rotation, JPH::Vec3(0.0f, 0.0f, -half_height)), rotation, JPH::Vec3::sAxisX(),
               JPH::Vec3::sAxisY(), radius, color);

    for (int i = 0; i < 4; ++i) {
        const float angle = 0.5f * kPi * static_cast<float>(i);
        const JPH::Vec3 offset(radius * std::cos(angle), radius * std::sin(angle), 0.0f);
        PushLine(out, WorldPoint(position, rotation, offset + JPH::Vec3(0.0f, 0.0f, -half_height)),
                 WorldPoint(position, rotation, offset + JPH::Vec3(0.0f, 0.0f, half_height)), color);
    }
}

void DrawCapsule(eastl::vector<PhysicsDebugLine>& out, const JPH::RVec3& position, const JPH::Quat& rotation,
                 float half_height, float radius, const Color& color) {
    DrawCylinder(out, position, rotation, half_height, radius, color);

    constexpr int kSegments = 12;
    for (int plane = 0; plane < 2; ++plane) {
        for (int i = 1; i < kSegments; ++i) {
            const float angle = kPi * static_cast<float>(i) / static_cast<float>(kSegments);
            const float c = radius * std::cos(angle);
            const float s = radius * std::sin(angle);
            const JPH::Vec3 top_local =
                plane == 0 ? JPH::Vec3(c, 0.0f, half_height + s) : JPH::Vec3(0.0f, c, half_height + s);
            const JPH::Vec3 bottom_local =
                plane == 0 ? JPH::Vec3(c, 0.0f, -half_height - s) : JPH::Vec3(0.0f, c, -half_height - s);
            const float previous_angle = kPi * static_cast<float>(i - 1) / static_cast<float>(kSegments);
            const float pc = radius * std::cos(previous_angle);
            const float ps = radius * std::sin(previous_angle);
            const JPH::Vec3 top_previous =
                plane == 0 ? JPH::Vec3(pc, 0.0f, half_height + ps) : JPH::Vec3(0.0f, pc, half_height + ps);
            const JPH::Vec3 bottom_previous =
                plane == 0 ? JPH::Vec3(pc, 0.0f, -half_height - ps) : JPH::Vec3(0.0f, pc, -half_height - ps);
            PushLine(out, WorldPoint(position, rotation, top_previous), WorldPoint(position, rotation, top_local),
                     color);
            PushLine(out, WorldPoint(position, rotation, bottom_previous), WorldPoint(position, rotation, bottom_local),
                     color);
        }
    }
}

void DrawPlane(eastl::vector<PhysicsDebugLine>& out, const JPH::RVec3& position, const JPH::Quat& rotation,
               float half_extent, const Color& color) {
    constexpr int kLines = 10;
    const float step = 2.0f * half_extent / static_cast<float>(kLines);
    for (int i = 0; i <= kLines; ++i) {
        const float offset = -half_extent + step * static_cast<float>(i);
        PushLine(out, WorldPoint(position, rotation, JPH::Vec3(offset, -half_extent, 0.0f)),
                 WorldPoint(position, rotation, JPH::Vec3(offset, half_extent, 0.0f)), color);
        PushLine(out, WorldPoint(position, rotation, JPH::Vec3(-half_extent, offset, 0.0f)),
                 WorldPoint(position, rotation, JPH::Vec3(half_extent, offset, 0.0f)), color);
    }
}

}  // namespace

struct PhysicsWorld::Impl {
    struct BodyRecord {
        uint64_t handle = 0;
        JPH::BodyID body_id;
        uint64_t entity = 0;
        ShapeType shape = ShapeType::Box;
        MotionType motion = MotionType::Dynamic;
        das::float3 half_extents = das::float3(0.5f, 0.5f, 0.5f);
        float radius = 0.5f;
        float half_height = 0.5f;
        bool sensor = false;
    };

    struct Listener final : JPH::ContactListener {
        Impl& owner;

        explicit Listener(Impl& in_owner) : owner(in_owner) {}

        void OnContactAdded(const JPH::Body& body1, const JPH::Body& body2, const JPH::ContactManifold& manifold,
                            JPH::ContactSettings& settings) override {
            owner.OnContactAdded(body1, body2, manifold);
        }
    };

    explicit Impl(const PhysicsWorldConfig& in_config) : config(in_config), listener(*this) {}

    void Init() {
        job_system = std::make_unique<JPH::JobSystemThreadPool>(JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers,
                                                                config.worker_threads > 0 ? config.worker_threads : -1);
        temp_allocator = std::make_unique<JPH::TempAllocatorImpl>(config.temp_allocator_bytes);

        system = std::make_unique<JPH::PhysicsSystem>();
        system->Init(config.max_bodies, 0, config.max_body_pairs, config.max_contact_constraints, bp_layer,
                     object_vs_bp, layer_pair);
        system->SetGravity(ToVec3(config.gravity));
        system->SetContactListener(&listener);
    }

    void OnContactAdded(const JPH::Body& body1, const JPH::Body& body2, const JPH::ContactManifold& manifold);

    BodyRecord* Find(uint64_t handle) {
        return bodies.get(tryengine::core::SlotMap<BodyRecord>::Key(handle));
    }

    const BodyRecord* Find(uint64_t handle) const {
        return bodies.get(tryengine::core::SlotMap<BodyRecord>::Key(handle));
    }

    PhysicsWorldConfig config;
    BPLayerInterfaceImpl bp_layer;
    ObjectVsBroadPhaseLayerFilterImpl object_vs_bp;
    ObjectLayerPairFilterImpl layer_pair;
    std::unique_ptr<JPH::TempAllocatorImpl> temp_allocator;
    std::unique_ptr<JPH::JobSystemThreadPool> job_system;
    Listener listener;
    std::unique_ptr<JPH::PhysicsSystem> system;

    tryengine::core::SlotMap<BodyRecord> bodies;
    mutable std::mutex contacts_mutex;
    eastl::vector<ContactEvent> contacts;
    float accumulator = 0.0f;
    bool debug_draw = true;
};

void PhysicsWorld::Impl::OnContactAdded(const JPH::Body& body1, const JPH::Body& body2,
                                        const JPH::ContactManifold& manifold) {
    ContactEvent event;
    event.body_a = body1.GetUserData();
    event.body_b = body2.GetUserData();
    if (event.body_a == 0 || event.body_b == 0)
        return;

    event.normal = FromVec3(manifold.mWorldSpaceNormal);
    event.depth = manifold.mPenetrationDepth;
    if (!manifold.mRelativeContactPointsOn1.empty())
        event.point = FromRVec3(manifold.GetWorldSpaceContactPointOn1(0));
    else
        event.point = FromRVec3(manifold.mBaseOffset);

    std::lock_guard lock(contacts_mutex);
    if (contacts.size() >= kMaxContactEvents)
        return;
    for (const ContactEvent& existing : contacts) {
        if ((existing.body_a == event.body_a && existing.body_b == event.body_b) ||
            (existing.body_a == event.body_b && existing.body_b == event.body_a)) {
            return;
        }
    }
    contacts.push_back(event);
}

PhysicsWorld::PhysicsWorld(const PhysicsWorldConfig& config) {
    EnsureJoltStarted();
    impl_ = std::make_unique<Impl>(config);
    impl_->config.max_substeps = std::max(1u, impl_->config.max_substeps);
    impl_->config.fixed_step = std::clamp(impl_->config.fixed_step, 1.0f / 240.0f, 1.0f / 10.0f);
    impl_->Init();
}

PhysicsWorld::~PhysicsWorld() {
    if (!impl_)
        return;
    Clear();
    impl_->system.reset();
    impl_->job_system.reset();
    impl_->temp_allocator.reset();
}

uint64_t PhysicsWorld::CreateBody(const BodyDesc& desc) {
    if (!impl_ || !impl_->system)
        return 0;

    JPH::Ref<JPH::Shape> shape = MakeShape(desc);
    if (shape == nullptr) {
        LogError(LogCategory::Physics, "PhysicsWorld: unsupported shape {}", static_cast<int>(desc.shape));
        return 0;
    }

    const auto key = impl_->bodies.emplace();
    auto* record = impl_->bodies.get(key);
    TRY_ASSERT(record, "PhysicsWorld: SlotMap returned no record for a fresh key");
    record->handle = key;
    record->entity = desc.entity;
    record->shape = desc.shape;
    record->motion = desc.motion;
    record->half_extents = desc.half_extents;
    record->radius = desc.radius;
    record->half_height = desc.half_height;
    record->sensor = desc.sensor;

    // Плоскость в Jolt обязана быть статической.
    const MotionType motion = desc.shape == ShapeType::Plane ? MotionType::Static : desc.motion;
    record->motion = motion;

    const float mass = std::max(kMinShapeSize, std::abs(desc.mass));
    JPH::BodyCreationSettings settings(shape.GetPtr(), ToRVec3(desc.position), ToQuat(desc.rotation),
                                       ToJoltMotion(motion),
                                       motion == MotionType::Static ? Layers::kNonMoving : Layers::kMoving);
    settings.mUserData = record->handle;
    settings.mFriction = std::max(0.0f, desc.friction);
    settings.mRestitution = std::max(0.0f, desc.restitution);
    settings.mLinearVelocity = ToVec3(desc.linear_velocity);
    settings.mAngularVelocity = ToVec3(desc.angular_velocity);
    settings.mIsSensor = desc.sensor;
    // A plane is always static and its shape has no finite mass properties, so Jolt must not
    // be asked to compute them. Every other body stays switchable at runtime.
    settings.mAllowDynamicOrKinematic = desc.shape != ShapeType::Plane;
    settings.mCollideKinematicVsNonDynamic = true;
    settings.mAllowSleeping = true;
    if (motion == MotionType::Dynamic) {
        settings.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
        settings.mMassPropertiesOverride.mMass = mass;
    }

    JPH::BodyInterface& body_interface = impl_->system->GetBodyInterface();
    const JPH::BodyID body_id = body_interface.CreateAndAddBody(settings, JPH::EActivation::Activate);
    if (body_id.IsInvalid()) {
        impl_->bodies.erase(key);
        LogError(LogCategory::Physics, "PhysicsWorld: Jolt refused to create a body (limit {})",
                 impl_->config.max_bodies);
        return 0;
    }

    record = impl_->bodies.get(key);
    record->body_id = body_id;

    return record->handle;
}

bool PhysicsWorld::DestroyBody(uint64_t handle) {
    if (!impl_)
        return false;
    auto* record = impl_->Find(handle);
    if (!record)
        return false;

    if (!record->body_id.IsInvalid() && impl_->system) {
        JPH::BodyInterface& body_interface = impl_->system->GetBodyInterface();
        body_interface.RemoveBody(record->body_id);
        body_interface.DestroyBody(record->body_id);
    }

    return impl_->bodies.erase(tryengine::core::SlotMap<Impl::BodyRecord>::Key(handle));
}

void PhysicsWorld::Clear() {
    if (!impl_)
        return;

    if (impl_->system) {
        JPH::BodyInterface& body_interface = impl_->system->GetBodyInterface();
        for (auto& record : impl_->bodies) {
            if (record.body_id.IsInvalid())
                continue;
            body_interface.RemoveBody(record.body_id);
            body_interface.DestroyBody(record.body_id);
            record.body_id = JPH::BodyID();
        }
    }

    impl_->bodies.clear();
    {
        std::lock_guard lock(impl_->contacts_mutex);
        impl_->contacts.clear();
    }
    impl_->accumulator = 0.0f;
}

void PhysicsWorld::Step(float dt) {
    if (!impl_ || !impl_->system || dt <= 0.0f)
        return;

    impl_->accumulator += std::min(dt, kMaxFrameDt);
    {
        std::lock_guard lock(impl_->contacts_mutex);
        impl_->contacts.clear();
    }

    uint32_t steps = 0;
    while (impl_->accumulator >= impl_->config.fixed_step && steps < impl_->config.max_substeps) {
        const JPH::EPhysicsUpdateError error =
            impl_->system->Update(impl_->config.fixed_step, 1, impl_->temp_allocator.get(), impl_->job_system.get());
        if (error != JPH::EPhysicsUpdateError::None) {
            LogError(LogCategory::Physics, "PhysicsWorld::Step: Jolt update error {}", static_cast<uint32_t>(error));
            impl_->accumulator = 0.0f;
            return;
        }
        impl_->accumulator -= impl_->config.fixed_step;
        ++steps;
    }

    if (steps == impl_->config.max_substeps)
        impl_->accumulator = 0.0f;
}

void PhysicsWorld::SetGravity(const das::float3& gravity) {
    if (!impl_ || !impl_->system)
        return;
    impl_->config.gravity = gravity;
    impl_->system->SetGravity(ToVec3(gravity));
}

das::float3 PhysicsWorld::GetGravity() const {
    if (!impl_ || !impl_->system)
        return das::float3(0.0f, 0.0f, 0.0f);
    return FromVec3(impl_->system->GetGravity());
}

void PhysicsWorld::SetFixedStep(float step) {
    if (!impl_)
        return;
    impl_->config.fixed_step = std::clamp(step, 1.0f / 240.0f, 1.0f / 10.0f);
}

void PhysicsWorld::SetMaxSubsteps(uint32_t steps) {
    if (!impl_)
        return;
    impl_->config.max_substeps = std::clamp(steps, 1u, 16u);
}

bool PhysicsWorld::IsBodyAlive(uint64_t handle) const {
    return impl_ && impl_->Find(handle) != nullptr;
}

uint32_t PhysicsWorld::BodyCount() const {
    return impl_ ? static_cast<uint32_t>(impl_->bodies.size()) : 0u;
}

uint64_t PhysicsWorld::BodyAt(uint32_t index) const {
    if (!impl_ || index >= impl_->bodies.size())
        return 0;
    return impl_->bodies.data()[index].handle;
}

uint64_t PhysicsWorld::BodyEntity(uint64_t handle) const {
    const auto* record = impl_ ? impl_->Find(handle) : nullptr;
    return record ? record->entity : 0;
}

MotionType PhysicsWorld::BodyMotion(uint64_t handle) const {
    const auto* record = impl_ ? impl_->Find(handle) : nullptr;
    return record ? record->motion : MotionType::Static;
}

bool PhysicsWorld::GetBodyPosition(uint64_t handle, das::float3& out) const {
    const auto* record = impl_ ? impl_->Find(handle) : nullptr;
    if (!record || !impl_->system || record->body_id.IsInvalid())
        return false;
    out = FromRVec3(impl_->system->GetBodyInterface().GetPosition(record->body_id));
    return true;
}

bool PhysicsWorld::GetBodyRotation(uint64_t handle, das::float4& out) const {
    const auto* record = impl_ ? impl_->Find(handle) : nullptr;
    if (!record || !impl_->system || record->body_id.IsInvalid())
        return false;
    out = FromQuat(impl_->system->GetBodyInterface().GetRotation(record->body_id));
    return true;
}

bool PhysicsWorld::SetBodyPosition(uint64_t handle, const das::float3& position) {
    auto* record = impl_ ? impl_->Find(handle) : nullptr;
    if (!record || !impl_->system || record->body_id.IsInvalid())
        return false;
    auto& body_interface = impl_->system->GetBodyInterface();
    // Static and kinematic poses are pushed every frame; skipping an unchanged pose keeps
    // the broadphase quiet and avoids waking contacts on a level that did not move.
    if (SamePosition(FromRVec3(body_interface.GetPosition(record->body_id)), position))
        return true;
    body_interface.SetPosition(record->body_id, ToRVec3(position), JPH::EActivation::Activate);
    return true;
}

bool PhysicsWorld::SetBodyRotation(uint64_t handle, const das::float4& rotation) {
    auto* record = impl_ ? impl_->Find(handle) : nullptr;
    if (!record || !impl_->system || record->body_id.IsInvalid())
        return false;
    auto& body_interface = impl_->system->GetBodyInterface();
    if (SameRotation(FromQuat(body_interface.GetRotation(record->body_id)), rotation))
        return true;
    body_interface.SetRotation(record->body_id, ToQuat(rotation), JPH::EActivation::Activate);
    return true;
}

bool PhysicsWorld::SetBodyTransform(uint64_t handle, const das::float3& position, const das::float4& rotation) {
    auto* record = impl_ ? impl_->Find(handle) : nullptr;
    if (!record || !impl_->system || record->body_id.IsInvalid())
        return false;
    auto& body_interface = impl_->system->GetBodyInterface();
    const bool same_position = SamePosition(FromRVec3(body_interface.GetPosition(record->body_id)), position);
    const bool same_rotation = SameRotation(FromQuat(body_interface.GetRotation(record->body_id)), rotation);
    if (same_position && same_rotation)
        return true;
    if (same_position) {
        body_interface.SetRotation(record->body_id, ToQuat(rotation), JPH::EActivation::Activate);
    } else if (same_rotation) {
        body_interface.SetPosition(record->body_id, ToRVec3(position), JPH::EActivation::Activate);
    } else {
        body_interface.SetPositionAndRotation(record->body_id, ToRVec3(position), ToQuat(rotation),
                                              JPH::EActivation::Activate);
    }
    return true;
}

bool PhysicsWorld::GetLinearVelocity(uint64_t handle, das::float3& out) const {
    const auto* record = impl_ ? impl_->Find(handle) : nullptr;
    if (!record || !impl_->system || record->body_id.IsInvalid())
        return false;
    out = FromVec3(impl_->system->GetBodyInterface().GetLinearVelocity(record->body_id));
    return true;
}

bool PhysicsWorld::SetLinearVelocity(uint64_t handle, const das::float3& velocity) {
    auto* record = impl_ ? impl_->Find(handle) : nullptr;
    if (!record || !impl_->system || record->body_id.IsInvalid())
        return false;
    if (record->motion == MotionType::Static)
        return false;
    impl_->system->GetBodyInterface().SetLinearVelocity(record->body_id, ToVec3(velocity));
    return true;
}

bool PhysicsWorld::GetAngularVelocity(uint64_t handle, das::float3& out) const {
    const auto* record = impl_ ? impl_->Find(handle) : nullptr;
    if (!record || !impl_->system || record->body_id.IsInvalid())
        return false;
    out = FromVec3(impl_->system->GetBodyInterface().GetAngularVelocity(record->body_id));
    return true;
}

bool PhysicsWorld::SetAngularVelocity(uint64_t handle, const das::float3& velocity) {
    auto* record = impl_ ? impl_->Find(handle) : nullptr;
    if (!record || !impl_->system || record->body_id.IsInvalid())
        return false;
    if (record->motion == MotionType::Static)
        return false;
    impl_->system->GetBodyInterface().SetAngularVelocity(record->body_id, ToVec3(velocity));
    return true;
}

bool PhysicsWorld::ApplyImpulse(uint64_t handle, const das::float3& impulse) {
    auto* record = impl_ ? impl_->Find(handle) : nullptr;
    if (!record || !impl_->system || record->body_id.IsInvalid())
        return false;
    if (record->motion != MotionType::Dynamic)
        return false;
    impl_->system->GetBodyInterface().AddImpulse(record->body_id, ToVec3(impulse));
    return true;
}

bool PhysicsWorld::ApplyImpulseAt(uint64_t handle, const das::float3& impulse, const das::float3& point) {
    auto* record = impl_ ? impl_->Find(handle) : nullptr;
    if (!record || !impl_->system || record->body_id.IsInvalid())
        return false;
    if (record->motion != MotionType::Dynamic)
        return false;
    impl_->system->GetBodyInterface().AddImpulse(record->body_id, ToVec3(impulse), ToRVec3(point));
    return true;
}

bool PhysicsWorld::SetMotionType(uint64_t handle, MotionType motion) {
    auto* record = impl_ ? impl_->Find(handle) : nullptr;
    if (!record || !impl_->system || record->body_id.IsInvalid())
        return false;
    if (record->shape == ShapeType::Plane && motion != MotionType::Static)
        return false;
    impl_->system->GetBodyInterface().SetMotionType(record->body_id, ToJoltMotion(motion), JPH::EActivation::Activate);
    record->motion = motion;
    return true;
}

uint32_t PhysicsWorld::ContactCount() const {
    if (!impl_)
        return 0;
    std::lock_guard lock(impl_->contacts_mutex);
    return static_cast<uint32_t>(impl_->contacts.size());
}

bool PhysicsWorld::ContactAt(uint32_t index, ContactEvent& out) const {
    if (!impl_)
        return false;
    std::lock_guard lock(impl_->contacts_mutex);
    if (index >= impl_->contacts.size())
        return false;
    out = impl_->contacts[index];
    return true;
}

void PhysicsWorld::SetDebugDraw(bool enabled) {
    if (impl_)
        impl_->debug_draw = enabled;
}

bool PhysicsWorld::DebugDrawEnabled() const {
    return impl_ && impl_->debug_draw;
}

void PhysicsWorld::AppendDebugLines(eastl::vector<PhysicsDebugLine>& out) const {
    if (!impl_ || !impl_->system || !impl_->debug_draw)
        return;

    out.reserve(out.size() + impl_->bodies.size() * 24 + 32);
    JPH::BodyInterface& body_interface = impl_->system->GetBodyInterface();

    for (const auto& record : impl_->bodies) {
        if (record.body_id.IsInvalid())
            continue;

        const JPH::RVec3 position = body_interface.GetPosition(record.body_id);
        const JPH::Quat rotation = body_interface.GetRotation(record.body_id);
        Color color = kStaticColor;
        if (record.sensor)
            color = kSensorColor;
        else if (record.motion == MotionType::Kinematic)
            color = kKinematicColor;
        else if (record.motion == MotionType::Dynamic)
            color = kDynamicColor;

        switch (record.shape) {
            case ShapeType::Box:
                DrawBox(out, position, rotation, record.half_extents, color);
                break;
            case ShapeType::Sphere:
                DrawSphere(out, position, rotation, std::max(kMinShapeSize, std::abs(record.radius)), color);
                break;
            case ShapeType::Capsule:
                DrawCapsule(out, position, rotation, std::max(kMinShapeSize, std::abs(record.half_height)),
                            std::max(kMinShapeSize, std::abs(record.radius)), color);
                break;
            case ShapeType::Cylinder:
                DrawCylinder(out, position, rotation, std::max(kMinShapeSize, std::abs(record.half_height)),
                             std::max(kMinShapeSize, std::abs(record.radius)), color);
                break;
            case ShapeType::Plane:
                DrawPlane(out, position, rotation, std::max(10.0f, std::abs(record.half_extents.x)), color);
                break;
        }
    }

    {
        std::lock_guard lock(impl_->contacts_mutex);
        for (const ContactEvent& contact : impl_->contacts) {
            const JPH::RVec3 point = ToRVec3(contact.point);
            constexpr float kCross = 0.15f;
            PushLine(out, point - JPH::RVec3(kCross, 0.0f, 0.0f), point + JPH::RVec3(kCross, 0.0f, 0.0f),
                     kContactColor);
            PushLine(out, point - JPH::RVec3(0.0f, kCross, 0.0f), point + JPH::RVec3(0.0f, kCross, 0.0f),
                     kContactColor);
            PushLine(out, point - JPH::RVec3(0.0f, 0.0f, kCross), point + JPH::RVec3(0.0f, 0.0f, kCross),
                     kContactColor);
        }
    }
}

}  // namespace tryengine::physics
