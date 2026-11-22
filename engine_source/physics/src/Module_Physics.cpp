#include <algorithm>
#include <cstdint>
#include <daScript/daScript.h>

#include "engine/core/TryengineContext.hpp"
#include "engine/physics/PhysicsWorld.hpp"

namespace {

using tryengine::physics::BodyDesc;
using tryengine::physics::ContactEvent;
using tryengine::physics::MotionType;
using tryengine::physics::PhysicsWorld;
using tryengine::physics::ShapeType;

// Индексы приходят из перечислений в tryengine/Physics.das; порядок должен совпадать.
static_assert(static_cast<int32_t>(ShapeType::Box) == 0);
static_assert(static_cast<int32_t>(ShapeType::Plane) == 4);
static_assert(static_cast<int32_t>(MotionType::Static) == 0);
static_assert(static_cast<int32_t>(MotionType::Dynamic) == 2);

PhysicsWorld& World(das::Context* ctx) {
    auto* try_ctx = static_cast<tryengine::core::TryengineContext*>(ctx);
    TRY_ASSERT(try_ctx, "tryPhysics: context is not TryengineContext");
    return try_ctx->engine.Get<PhysicsWorld>();
}

int32_t ClampShape(int32_t shape) {
    return std::clamp(shape, static_cast<int32_t>(ShapeType::Box), static_cast<int32_t>(ShapeType::Plane));
}

int32_t ClampMotion(int32_t motion) {
    return std::clamp(motion, static_cast<int32_t>(MotionType::Static), static_cast<int32_t>(MotionType::Dynamic));
}

das::float3 Zero3() {
    return das::float3(0.0f, 0.0f, 0.0f);
}

das::float4 Identity4() {
    return das::float4(0.0f, 0.0f, 0.0f, 1.0f);
}

uint64_t PhysicsCreateBody(int32_t shape, int32_t motion, das::float3 position, das::float4 rotation,
                           das::float3 half_extents, float radius, float half_height, float mass, float friction,
                           float restitution, das::float3 linear_velocity, das::float3 angular_velocity,
                           uint64_t entity, bool sensor, das::Context* ctx) {
    BodyDesc desc;
    desc.shape = static_cast<ShapeType>(ClampShape(shape));
    desc.motion = static_cast<MotionType>(ClampMotion(motion));
    desc.position = position;
    desc.rotation = rotation;
    desc.half_extents = half_extents;
    desc.radius = radius;
    desc.half_height = half_height;
    desc.mass = mass;
    desc.friction = friction;
    desc.restitution = restitution;
    desc.linear_velocity = linear_velocity;
    desc.angular_velocity = angular_velocity;
    desc.entity = entity;
    desc.sensor = sensor;
    return World(ctx).CreateBody(desc);
}

void PhysicsDestroyBody(uint64_t handle, das::Context* ctx) {
    World(ctx).DestroyBody(handle);
}

void PhysicsClear(das::Context* ctx) {
    World(ctx).Clear();
}

void PhysicsStep(float dt, das::Context* ctx) {
    World(ctx).Step(dt);
}

void PhysicsSetGravity(das::float3 gravity, das::Context* ctx) {
    World(ctx).SetGravity(gravity);
}

das::float3 PhysicsGetGravity(das::Context* ctx) {
    return World(ctx).GetGravity();
}

void PhysicsSetFixedStep(float step, das::Context* ctx) {
    World(ctx).SetFixedStep(step);
}

void PhysicsSetMaxSubsteps(int32_t steps, das::Context* ctx) {
    World(ctx).SetMaxSubsteps(static_cast<uint32_t>(std::max(1, steps)));
}

bool PhysicsIsBodyAlive(uint64_t handle, das::Context* ctx) {
    return World(ctx).IsBodyAlive(handle);
}

int32_t PhysicsBodyCount(das::Context* ctx) {
    return static_cast<int32_t>(World(ctx).BodyCount());
}

uint64_t PhysicsBodyAt(int32_t index, das::Context* ctx) {
    if (index < 0)
        return 0;
    return World(ctx).BodyAt(static_cast<uint32_t>(index));
}

uint64_t PhysicsBodyEntity(uint64_t handle, das::Context* ctx) {
    return World(ctx).BodyEntity(handle);
}

int32_t PhysicsBodyMotion(uint64_t handle, das::Context* ctx) {
    return static_cast<int32_t>(World(ctx).BodyMotion(handle));
}

das::float3 PhysicsGetBodyPosition(uint64_t handle, das::Context* ctx) {
    das::float3 out = Zero3();
    World(ctx).GetBodyPosition(handle, out);
    return out;
}

das::float4 PhysicsGetBodyRotation(uint64_t handle, das::Context* ctx) {
    das::float4 out = Identity4();
    World(ctx).GetBodyRotation(handle, out);
    return out;
}

bool PhysicsSetBodyPosition(uint64_t handle, das::float3 position, das::Context* ctx) {
    return World(ctx).SetBodyPosition(handle, position);
}

bool PhysicsSetBodyRotation(uint64_t handle, das::float4 rotation, das::Context* ctx) {
    return World(ctx).SetBodyRotation(handle, rotation);
}

bool PhysicsSetBodyTransform(uint64_t handle, das::float3 position, das::float4 rotation, das::Context* ctx) {
    return World(ctx).SetBodyTransform(handle, position, rotation);
}

das::float3 PhysicsGetLinearVelocity(uint64_t handle, das::Context* ctx) {
    das::float3 out = Zero3();
    World(ctx).GetLinearVelocity(handle, out);
    return out;
}

bool PhysicsSetLinearVelocity(uint64_t handle, das::float3 velocity, das::Context* ctx) {
    return World(ctx).SetLinearVelocity(handle, velocity);
}

das::float3 PhysicsGetAngularVelocity(uint64_t handle, das::Context* ctx) {
    das::float3 out = Zero3();
    World(ctx).GetAngularVelocity(handle, out);
    return out;
}

bool PhysicsSetAngularVelocity(uint64_t handle, das::float3 velocity, das::Context* ctx) {
    return World(ctx).SetAngularVelocity(handle, velocity);
}

bool PhysicsApplyImpulse(uint64_t handle, das::float3 impulse, das::Context* ctx) {
    return World(ctx).ApplyImpulse(handle, impulse);
}

bool PhysicsApplyImpulseAt(uint64_t handle, das::float3 impulse, das::float3 point, das::Context* ctx) {
    return World(ctx).ApplyImpulseAt(handle, impulse, point);
}

bool PhysicsSetMotionType(uint64_t handle, int32_t motion, das::Context* ctx) {
    return World(ctx).SetMotionType(handle, static_cast<MotionType>(ClampMotion(motion)));
}

int32_t PhysicsContactCount(das::Context* ctx) {
    return static_cast<int32_t>(World(ctx).ContactCount());
}

uint64_t PhysicsContactBodyA(int32_t index, das::Context* ctx) {
    ContactEvent event;
    if (index < 0 || !World(ctx).ContactAt(static_cast<uint32_t>(index), event))
        return 0;
    return event.body_a;
}

uint64_t PhysicsContactBodyB(int32_t index, das::Context* ctx) {
    ContactEvent event;
    if (index < 0 || !World(ctx).ContactAt(static_cast<uint32_t>(index), event))
        return 0;
    return event.body_b;
}

das::float3 PhysicsContactPoint(int32_t index, das::Context* ctx) {
    ContactEvent event;
    if (index < 0 || !World(ctx).ContactAt(static_cast<uint32_t>(index), event))
        return Zero3();
    return event.point;
}

das::float3 PhysicsContactNormal(int32_t index, das::Context* ctx) {
    ContactEvent event;
    if (index < 0 || !World(ctx).ContactAt(static_cast<uint32_t>(index), event))
        return Zero3();
    return event.normal;
}

float PhysicsContactDepth(int32_t index, das::Context* ctx) {
    ContactEvent event;
    if (index < 0 || !World(ctx).ContactAt(static_cast<uint32_t>(index), event))
        return 0.0f;
    return event.depth;
}

void PhysicsSetDebugDraw(bool enabled, das::Context* ctx) {
    World(ctx).SetDebugDraw(enabled);
}

bool PhysicsDebugDraw(das::Context* ctx) {
    return World(ctx).DebugDrawEnabled();
}

}  // namespace

class Module_Physics : public das::Module {
public:
    Module_Physics() : das::Module("tryPhysics") {
        das::ModuleLibrary lib(this);
        lib.addBuiltInModule();

        das::addExtern<DAS_BIND_FUN(PhysicsCreateBody)>(*this, lib, "PhysicsCreateBody", das::SideEffects::accessGlobal,
                                                        "PhysicsCreateBody");
        das::addExtern<DAS_BIND_FUN(PhysicsDestroyBody)>(*this, lib, "PhysicsDestroyBody",
                                                         das::SideEffects::accessGlobal, "PhysicsDestroyBody");
        das::addExtern<DAS_BIND_FUN(PhysicsClear)>(*this, lib, "PhysicsClear", das::SideEffects::accessGlobal,
                                                   "PhysicsClear");
        das::addExtern<DAS_BIND_FUN(PhysicsStep)>(*this, lib, "PhysicsStep", das::SideEffects::accessGlobal,
                                                  "PhysicsStep");
        das::addExtern<DAS_BIND_FUN(PhysicsSetGravity)>(*this, lib, "PhysicsSetGravity", das::SideEffects::accessGlobal,
                                                        "PhysicsSetGravity");
        das::addExtern<DAS_BIND_FUN(PhysicsGetGravity)>(*this, lib, "PhysicsGetGravity", das::SideEffects::accessGlobal,
                                                        "PhysicsGetGravity");
        das::addExtern<DAS_BIND_FUN(PhysicsSetFixedStep)>(*this, lib, "PhysicsSetFixedStep",
                                                          das::SideEffects::accessGlobal, "PhysicsSetFixedStep");
        das::addExtern<DAS_BIND_FUN(PhysicsSetMaxSubsteps)>(*this, lib, "PhysicsSetMaxSubsteps",
                                                            das::SideEffects::accessGlobal, "PhysicsSetMaxSubsteps");
        das::addExtern<DAS_BIND_FUN(PhysicsIsBodyAlive)>(*this, lib, "PhysicsIsBodyAlive",
                                                         das::SideEffects::accessGlobal, "PhysicsIsBodyAlive");
        das::addExtern<DAS_BIND_FUN(PhysicsBodyCount)>(*this, lib, "PhysicsBodyCount", das::SideEffects::accessGlobal,
                                                       "PhysicsBodyCount");
        das::addExtern<DAS_BIND_FUN(PhysicsBodyAt)>(*this, lib, "PhysicsBodyAt", das::SideEffects::accessGlobal,
                                                    "PhysicsBodyAt");
        das::addExtern<DAS_BIND_FUN(PhysicsBodyEntity)>(*this, lib, "PhysicsBodyEntity", das::SideEffects::accessGlobal,
                                                        "PhysicsBodyEntity");
        das::addExtern<DAS_BIND_FUN(PhysicsBodyMotion)>(*this, lib, "PhysicsBodyMotion", das::SideEffects::accessGlobal,
                                                        "PhysicsBodyMotion");
        das::addExtern<DAS_BIND_FUN(PhysicsGetBodyPosition)>(*this, lib, "PhysicsGetBodyPosition",
                                                             das::SideEffects::accessGlobal, "PhysicsGetBodyPosition");
        das::addExtern<DAS_BIND_FUN(PhysicsGetBodyRotation)>(*this, lib, "PhysicsGetBodyRotation",
                                                             das::SideEffects::accessGlobal, "PhysicsGetBodyRotation");
        das::addExtern<DAS_BIND_FUN(PhysicsSetBodyPosition)>(*this, lib, "PhysicsSetBodyPosition",
                                                             das::SideEffects::accessGlobal, "PhysicsSetBodyPosition");
        das::addExtern<DAS_BIND_FUN(PhysicsSetBodyRotation)>(*this, lib, "PhysicsSetBodyRotation",
                                                             das::SideEffects::accessGlobal, "PhysicsSetBodyRotation");
        das::addExtern<DAS_BIND_FUN(PhysicsSetBodyTransform)>(
            *this, lib, "PhysicsSetBodyTransform", das::SideEffects::accessGlobal, "PhysicsSetBodyTransform");
        das::addExtern<DAS_BIND_FUN(PhysicsGetLinearVelocity)>(
            *this, lib, "PhysicsGetLinearVelocity", das::SideEffects::accessGlobal, "PhysicsGetLinearVelocity");
        das::addExtern<DAS_BIND_FUN(PhysicsSetLinearVelocity)>(
            *this, lib, "PhysicsSetLinearVelocity", das::SideEffects::accessGlobal, "PhysicsSetLinearVelocity");
        das::addExtern<DAS_BIND_FUN(PhysicsGetAngularVelocity)>(
            *this, lib, "PhysicsGetAngularVelocity", das::SideEffects::accessGlobal, "PhysicsGetAngularVelocity");
        das::addExtern<DAS_BIND_FUN(PhysicsSetAngularVelocity)>(
            *this, lib, "PhysicsSetAngularVelocity", das::SideEffects::accessGlobal, "PhysicsSetAngularVelocity");
        das::addExtern<DAS_BIND_FUN(PhysicsApplyImpulse)>(*this, lib, "PhysicsApplyImpulse",
                                                          das::SideEffects::accessGlobal, "PhysicsApplyImpulse");
        das::addExtern<DAS_BIND_FUN(PhysicsApplyImpulseAt)>(*this, lib, "PhysicsApplyImpulseAt",
                                                            das::SideEffects::accessGlobal, "PhysicsApplyImpulseAt");
        das::addExtern<DAS_BIND_FUN(PhysicsSetMotionType)>(*this, lib, "PhysicsSetMotionType",
                                                           das::SideEffects::accessGlobal, "PhysicsSetMotionType");
        das::addExtern<DAS_BIND_FUN(PhysicsContactCount)>(*this, lib, "PhysicsContactCount",
                                                          das::SideEffects::accessGlobal, "PhysicsContactCount");
        das::addExtern<DAS_BIND_FUN(PhysicsContactBodyA)>(*this, lib, "PhysicsContactBodyA",
                                                          das::SideEffects::accessGlobal, "PhysicsContactBodyA");
        das::addExtern<DAS_BIND_FUN(PhysicsContactBodyB)>(*this, lib, "PhysicsContactBodyB",
                                                          das::SideEffects::accessGlobal, "PhysicsContactBodyB");
        das::addExtern<DAS_BIND_FUN(PhysicsContactPoint)>(*this, lib, "PhysicsContactPoint",
                                                          das::SideEffects::accessGlobal, "PhysicsContactPoint");
        das::addExtern<DAS_BIND_FUN(PhysicsContactNormal)>(*this, lib, "PhysicsContactNormal",
                                                           das::SideEffects::accessGlobal, "PhysicsContactNormal");
        das::addExtern<DAS_BIND_FUN(PhysicsContactDepth)>(*this, lib, "PhysicsContactDepth",
                                                          das::SideEffects::accessGlobal, "PhysicsContactDepth");
        das::addExtern<DAS_BIND_FUN(PhysicsSetDebugDraw)>(*this, lib, "PhysicsSetDebugDraw",
                                                          das::SideEffects::accessGlobal, "PhysicsSetDebugDraw");
        das::addExtern<DAS_BIND_FUN(PhysicsDebugDraw)>(*this, lib, "PhysicsDebugDraw", das::SideEffects::accessGlobal,
                                                       "PhysicsDebugDraw");

        verifyAotReady();
    }
};

REGISTER_DYN_MODULE(Module_Physics, Module_Physics);
REGISTER_MODULE(Module_Physics);
