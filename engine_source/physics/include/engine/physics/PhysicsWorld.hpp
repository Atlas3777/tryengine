#pragma once

#include <EASTL/vector.h>
#include <cstdint>
#include <daScript/misc/vectypes.h>
#include <memory>

namespace tryengine::physics {

//! Геометрия коллайдера. Примитивы намеренно заданы в конвенции движка (Z — вверх):
//! капсула и цилиндр внутри Jolt поворачиваются из его локальной оси Y в Z.
enum class ShapeType : uint8_t {
    Box = 0,
    Sphere,
    Capsule,
    Cylinder,
    Plane,
};

enum class MotionType : uint8_t {
    Static = 0,
    Kinematic,
    Dynamic,
};

//! Описание тела для PhysicsWorld::CreateBody. Значения по умолчанию — куб полметра
//! с массой 1 кг; так из скрипта достаточно поменять позицию и размер.
struct BodyDesc {
    ShapeType shape = ShapeType::Box;
    MotionType motion = MotionType::Dynamic;

    das::float3 position = das::float3(0.0f, 0.0f, 0.0f);
    das::float4 rotation = das::float4(0.0f, 0.0f, 0.0f, 1.0f);

    //! Половина размера для Box; для Plane — половина размера отладочной сетки.
    das::float3 half_extents = das::float3(0.5f, 0.5f, 0.5f);
    //! Радиус Sphere, Capsule и Cylinder.
    float radius = 0.5f;
    //! Половина высоты цилиндрической части Capsule и полная половина высоты Cylinder.
    float half_height = 0.5f;

    float mass = 1.0f;
    float friction = 0.5f;
    float restitution = 0.0f;

    das::float3 linear_velocity = das::float3(0.0f, 0.0f, 0.0f);
    das::float3 angular_velocity = das::float3(0.0f, 0.0f, 0.0f);

    //! EntityId, упакованный в uint64: скрипт передаёт его, чтобы Physics::Sync
    //! мог переложить позу тела в Transform, а PhysicsBody-компонент — обратно в тело.
    //! 0 — тело без сущности.
    uint64_t entity = 0;
    bool sensor = false;
};

//! Одно событие начала контакта. Векторы — в мировом пространстве движка.
struct ContactEvent {
    uint64_t body_a = 0;
    uint64_t body_b = 0;
    das::float3 point = das::float3(0.0f, 0.0f, 0.0f);
    das::float3 normal = das::float3(0.0f, 0.0f, 0.0f);
    float depth = 0.0f;
};

//! Линия отладочной отрисовки. Раскладка совпадает с tryeditor::DebugLine
//! (start, rgba, end, padding), чтобы DebugPass только копировал поля.
struct PhysicsDebugLine {
    das::float3 start = das::float3(0.0f, 0.0f, 0.0f);
    uint8_t color[4] = {255, 255, 255, 255};
    das::float3 end = das::float3(0.0f, 0.0f, 0.0f);
    uint32_t pad = 0;
};

struct PhysicsWorldConfig {
    das::float3 gravity = das::float3(0.0f, 0.0f, -9.81f);
    uint32_t max_bodies = 65536;
    uint32_t max_body_pairs = 65536;
    uint32_t max_contact_constraints = 16384;
    uint32_t temp_allocator_bytes = 32u * 1024u * 1024u;
    //! 0 — авто (hardware_concurrency - 1).
    int32_t worker_threads = 0;
    //! Фиксированный шаг симуляции: переменный dt от кадра к кадру разваливает стопки.
    float fixed_step = 1.0f / 60.0f;
    uint32_t max_substeps = 4;
};

//! Владелец Jolt PhysicsSystem. Живёт как система Engine: C++ владеет миром,
//! daslang получает ручки тел и узкий API из tryPhysics/Physics.das.
class PhysicsWorld {
public:
    explicit PhysicsWorld(const PhysicsWorldConfig& config = {});
    ~PhysicsWorld();

    PhysicsWorld(const PhysicsWorld&) = delete;
    PhysicsWorld& operator=(const PhysicsWorld&) = delete;
    PhysicsWorld(PhysicsWorld&&) = delete;
    PhysicsWorld& operator=(PhysicsWorld&&) = delete;

    //! Создаёт тело и возвращает стабильную ручку (0 при ошибке). Ручка переживает
    //! удаление других тел и не совпадает с ручкой нового тела на том же слоте.
    uint64_t CreateBody(const BodyDesc& desc);
    bool DestroyBody(uint64_t handle);

    //! Удаляет все тела, не пересоздавая сам PhysicsSystem.
    void Clear();

    //! Продвигает симуляцию на dt с фиксированным внутренним шагом.
    void Step(float dt);

    void SetGravity(const das::float3& gravity);
    das::float3 GetGravity() const;

    void SetFixedStep(float step);
    void SetMaxSubsteps(uint32_t steps);

    bool IsBodyAlive(uint64_t handle) const;
    uint32_t BodyCount() const;
    //! Позиция в dense-списке, меняется при удалении тел; храни handle, не index.
    uint64_t BodyAt(uint32_t index) const;

    uint64_t BodyEntity(uint64_t handle) const;
    MotionType BodyMotion(uint64_t handle) const;

    bool GetBodyPosition(uint64_t handle, das::float3& out) const;
    bool GetBodyRotation(uint64_t handle, das::float4& out) const;
    bool SetBodyPosition(uint64_t handle, const das::float3& position);
    bool SetBodyRotation(uint64_t handle, const das::float4& rotation);
    bool SetBodyTransform(uint64_t handle, const das::float3& position, const das::float4& rotation);

    bool GetLinearVelocity(uint64_t handle, das::float3& out) const;
    bool SetLinearVelocity(uint64_t handle, const das::float3& velocity);
    bool GetAngularVelocity(uint64_t handle, das::float3& out) const;
    bool SetAngularVelocity(uint64_t handle, const das::float3& velocity);
    bool ApplyImpulse(uint64_t handle, const das::float3& impulse);
    bool ApplyImpulseAt(uint64_t handle, const das::float3& impulse, const das::float3& point);
    bool SetMotionType(uint64_t handle, MotionType motion);

    uint32_t ContactCount() const;
    bool ContactAt(uint32_t index, ContactEvent& out) const;

    void SetDebugDraw(bool enabled);
    bool DebugDrawEnabled() const;
    //! Добавляет каркасы коллайдеров и точки контактов в out.
    void AppendDebugLines(eastl::vector<PhysicsDebugLine>& out) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace tryengine::physics
