options gen2
options relaxed_assign = false

module ColliderDebug shared public

//! Editor overlay for the collider components of the selected entity.
//!
//! Physics bodies exist only in Play: the component sync runs from `Physics::Update`, which is
//! attached by the game flow. Outside Play the editor therefore draws the authored shapes
//! itself, straight from the ECS components and the entity's world pose. In Play the physics
//! debug pass already draws the real bodies, so this overlay stays off.

require math
require daslib/decs
require daslib/decs_boost
require tryengine/Physics
require tryengine/Types
require tryeditor/DebugRender
require tryeditor/Hierarchy

let private SEGMENTS : int = 24
let private CAP_SEGMENTS : int = 12
let private GRID_LINES : int = 10

def private Point(pose : Transform; local : float3) : float3 {
    return pose.position + quat_mul_vec(pose.rotation, local)
}

def private Circle(pose : Transform; center : float3; axis_u : float3; axis_v : float3; radius : float) {
    var previous = Point(pose, center + axis_u * radius)
    for (i in range(1, SEGMENTS + 1)) {
        let angle = 2.0f * PI * float(i) / float(SEGMENTS)
        let current = Point(pose, center + axis_u * (radius * cos(angle)) + axis_v * (radius * sin(angle)))
        AddLine(previous, current, Color.Green)
        previous = current
    }
}

def private Edge(pose : Transform; ax : float; ay : float; az : float; bx : float; by : float; bz : float) {
    AddLine(Point(pose, float3(ax, ay, az)), Point(pose, float3(bx, by, bz)), Color.Green)
}

def private DrawBox(pose : Transform; half_extents : float3) {
    let x = half_extents.x
    let y = half_extents.y
    let z = half_extents.z
    Edge(pose, -x, -y, -z, x, -y, -z)
    Edge(pose, x, -y, -z, x, y, -z)
    Edge(pose, x, y, -z, -x, y, -z)
    Edge(pose, -x, y, -z, -x, -y, -z)
    Edge(pose, -x, -y, z, x, -y, z)
    Edge(pose, x, -y, z, x, y, z)
    Edge(pose, x, y, z, -x, y, z)
    Edge(pose, -x, y, z, -x, -y, z)
    Edge(pose, -x, -y, -z, -x, -y, z)
    Edge(pose, x, -y, -z, x, -y, z)
    Edge(pose, x, y, -z, x, y, z)
    Edge(pose, -x, y, -z, -x, y, z)
}

def private DrawSphere(pose : Transform; radius : float) {
    let zero = float3(0.0f, 0.0f, 0.0f)
    Circle(pose, zero, float3(1.0f, 0.0f, 0.0f), float3(0.0f, 1.0f, 0.0f), radius)
    Circle(pose, zero, float3(1.0f, 0.0f, 0.0f), float3(0.0f, 0.0f, 1.0f), radius)
    Circle(pose, zero, float3(0.0f, 1.0f, 0.0f), float3(0.0f, 0.0f, 1.0f), radius)
}

def private DrawCylinder(pose : Transform; half_height : float; radius : float) {
    Circle(pose, float3(0.0f, 0.0f, half_height), float3(1.0f, 0.0f, 0.0f), float3(0.0f, 1.0f, 0.0f), radius)
    Circle(pose, float3(0.0f, 0.0f, -half_height), float3(1.0f, 0.0f, 0.0f), float3(0.0f, 1.0f, 0.0f), radius)
    for (i in range(4)) {
        let angle = 0.5f * PI * float(i)
        let offset = float3(radius * cos(angle), radius * sin(angle), 0.0f)
        AddLine(Point(pose, offset + float3(0.0f, 0.0f, -half_height)),
                Point(pose, offset + float3(0.0f, 0.0f, half_height)), Color.Green)
    }
}

def private DrawCapsule(pose : Transform; half_height : float; radius : float) {
    DrawCylinder(pose, half_height, radius)
    // Two cap arcs in the XZ and YZ planes, matching the physics capsule's hemisphere ends.
    for (plane in range(2)) {
        for (i in range(1, CAP_SEGMENTS + 1)) {
            let angle = PI * float(i) / float(CAP_SEGMENTS)
            let previous_angle = PI * float(i - 1) / float(CAP_SEGMENTS)
            let c = radius * cos(angle)
            let s = radius * sin(angle)
            let pc = radius * cos(previous_angle)
            let ps = radius * sin(previous_angle)
            var top_previous = float3(0.0f, 0.0f, 0.0f)
            var top = float3(0.0f, 0.0f, 0.0f)
            var bottom_previous = float3(0.0f, 0.0f, 0.0f)
            var bottom = float3(0.0f, 0.0f, 0.0f)
            if (plane == 0) {
                top_previous = float3(pc, 0.0f, half_height + ps)
                top = float3(c, 0.0f, half_height + s)
                bottom_previous = float3(pc, 0.0f, -half_height - ps)
                bottom = float3(c, 0.0f, -half_height - s)
            } else {
                top_previous = float3(0.0f, pc, half_height + ps)
                top = float3(0.0f, c, half_height + s)
                bottom_previous = float3(0.0f, pc, -half_height - ps)
                bottom = float3(0.0f, c, -half_height - s)
            }
            AddLine(Point(pose, top_previous), Point(pose, top), Color.Green)
            AddLine(Point(pose, bottom_previous), Point(pose, bottom), Color.Green)
        }
    }
}

def private DrawPlane(pose : Transform; half_extents : float3) {
    let hx = half_extents.x
    let hy = half_extents.y
    let step_x = 2.0f * hx / float(GRID_LINES)
    let step_y = 2.0f * hy / float(GRID_LINES)
    for (i in range(GRID_LINES + 1)) {
        let x = -hx + step_x * float(i)
        let y = -hy + step_y * float(i)
        AddLine(Point(pose, float3(x, -hy, 0.0f)), Point(pose, float3(x, hy, 0.0f)), Color.Green)
        AddLine(Point(pose, float3(-hx, y, 0.0f)), Point(pose, float3(hx, y, 0.0f)), Color.Green)
    }
}

//! Draws every collider component of the selected entity at its world pose. The physics shape
//! convention is the engine's: Z is up, capsule and cylinder stand along Z, and the plane lies
//! in local XY. All colliders are drawn, so a misconfiguration is visible before Play.
def public DrawSelectedCollider() {
    if (selected_eid == INVALID_ENTITY_ID || !decs::is_alive(selected_eid)) {
        return
    }
    query(selected_eid) $(_world : Transform; boxCollider : BoxCollider) {
        DrawBox(_world, boxCollider.half_extents)
    }
    query(selected_eid) $(_world : Transform; sphereCollider : SphereCollider) {
        DrawSphere(_world, sphereCollider.radius)
    }
    query(selected_eid) $(_world : Transform; capsuleCollider : CapsuleCollider) {
        DrawCapsule(_world, capsuleCollider.half_height, capsuleCollider.radius)
    }
    query(selected_eid) $(_world : Transform; cylinderCollider : CylinderCollider) {
        DrawCylinder(_world, cylinderCollider.half_height, cylinderCollider.radius)
    }
    query(selected_eid) $(_world : Transform; planeCollider : PlaneCollider) {
        DrawPlane(_world, planeCollider.half_extents)
    }
}
