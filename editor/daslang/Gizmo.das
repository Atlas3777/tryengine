options gen2
options relaxed_assign = false

module Gizmo shared public

//! Editor transform gizmo.
//!
//! Geometry and input are split on purpose:
//! - `GizmoPass` (C++) rasterizes arrows, planes and rings from a small high-level state
//!   over the scene color, with depth test disabled. It never reads the mouse.
//! - This module owns the state: mode/space, the screen-constant size, analytic hit-testing
//!   (ray against the exact handle shapes) and drag math. Picking here is exact, unlike a
//!   screen-space rectangle around a handle.
//!
//! The drag math is kept independent from `Transform`: `GizmoTarget` describes any TRS and
//! `GizmoDelta` carries a world translation, a world rotation and a per-axis scale multiplier.
//! A future collider editor can call the same `GizmoBeginDrag`/`GizmoUpdateDrag` pair with
//! collider extents in `scale` and apply the multiplier to the box size.
//!
//! Handle ids are shared with `GizmoPass.cpp`:
//!   0..2 = X/Y/Z axis, 3 = center (uniform scale only), 4 = XY plane (normal Z), 5 = YZ
//!   (normal X), 6 = XZ (normal Y). -1 means "nothing". Translate has no center handle:
//!   Unity-like free movement on the camera plane is intentionally absent.

require math
require daslib/math_boost
require daslib/decs
require daslib/decs_boost
require tryengine/Types
require tryengine/TransformSystem
require tryengine/Input
require tryengine/Profiling
require TryEditor
require tryeditor/Hierarchy
require tryeditor/EditorInput

enum GizmoMode {
    Translate,
    Rotate,
    Scale
}

enum GizmoSpace {
    World,
    Local
}

//! Mirror of `tryeditor::GizmoDrawState` in GizmoPass.hpp. The layout is fixed by
//! [cpp_layout]; keep the C++ static_assert happy when adding a field.
[cpp_layout]
struct GizmoDrawState {
    position : float3
    size : float
    rotation : float4
    mode : int
    hover : int
    active : int
    visible : int
}

struct GizmoRay {
    origin : float3
    direction : float3
}

//! Position/rotation are world-space because the gizmo lives in the world. `scale` is the
//! authored local scale: scale handles edit it directly, so a parent's scale never leaks
//! into the drag factor.
struct GizmoTarget {
    position : float3
    rotation : float4
    scale : float3
}

struct GizmoDelta {
    translation : float3 = float3(0.0f, 0.0f, 0.0f)
    rotation : float4 = float4(0.0f, 0.0f, 0.0f, 1.0f)
    scale : float3 = float3(1.0f, 1.0f, 1.0f)
    changed : bool = false
}

struct GizmoCamera {
    position : float3
    forward : float3
    right : float3
    up : float3
    fov : float
}

struct GizmoDrag {
    active : bool
    handle : int
    mode : GizmoMode
    space : GizmoSpace
    target : GizmoTarget
    basis : float4
    size : float
    plane_normal : float3
    start_value : float
    start_point : float3
    start_scale : float3
}

// Handle ids (see the comment at the top of the module).
let private HANDLE_CENTER : int = 3
let private HANDLE_PLANE_XY : int = 4
let private HANDLE_PLANE_YZ : int = 5
let private HANDLE_PLANE_XZ : int = 6

// Geometry constants shared with GizmoPass.cpp. Picking uses the same numbers as the mesh.
let private AXIS_HIT_RADIUS : float = 0.085f
let private RING_RADIUS : float = 0.85f
let private RING_HIT_RADIUS : float = 0.065f
let private PLANE_HIT_LOW : float = 0.0f
let private PLANE_HIT_HIGH : float = 0.44f
let private CENTER_HIT_RADIUS : float = 0.12f

// A gizmo unit is this many screen pixels long when distance scaling is on; otherwise the
// gizmo keeps one fixed world size.
let private GIZMO_PIXELS : float = 90.0f
// Fallback size, used only when distance scaling is switched off: compact enough that the
// handles do not cover the object. There is no minimum: with scaling on, layout is purely
// proportional to depth, so flying close shrinks the gizmo together with its handles.
let private GIZMO_WORLD_SIZE : float = 0.7f
let private MOVE_SNAP : float = 0.5f
let private ROTATE_SNAP_DEG : float = 15.0f
let private SCALE_SNAP : float = 0.05f

var gizmo_mode : GizmoMode = GizmoMode.Translate
var gizmo_space : GizmoSpace = GizmoSpace.World
//! true: screen-constant size (an axis is about 120 px at any distance);
//! false: a compact fixed world size (GIZMO_WORLD_SIZE, 0.4 unit).
var gizmo_scale_with_distance : bool = true

var private g_draw : GizmoDrawState
var private g_drag : GizmoDrag
var private g_hover : int = -1
var private g_viewport : float2 = float2(600.0f, 600.0f)

[export]
def GetGizmoDrawState : GizmoDrawState ? {
    return unsafe(addr(g_draw))
}

//! The picking/selection pass must not select the object behind a handle that started a drag.
[export]
def IsGizmoDragging : bool {
    return g_drag.active
}

def private AxisUnit(axis : int) : float3 {
    if (axis == 0) {
        return float3(1.0f, 0.0f, 0.0f)
    } elif (axis == 1) {
        return float3(0.0f, 1.0f, 0.0f)
    }
    return float3(0.0f, 0.0f, 1.0f)
}

//! Axis direction of a handle: flipped to the side facing the camera, the same way
//! GizmoPass renders arrows, cubes and plane squares. Drag math does not care about the sign:
//! it works with the full line parameter.
def private AxisSign(axis : int; camera_dir : float3) : float {
    if (dot(camera_dir, AxisUnit(axis)) < 0.0f) {
        return -1.0f
    }
    return 1.0f
}

def private PlaneNormalAxis(handle_id : int) : int {
    if (handle_id == HANDLE_PLANE_XY) {
        return 2
    } elif (handle_id == HANDLE_PLANE_YZ) {
        return 0
    }
    return 1
}

def private PlaneUAxis(handle_id : int) : int {
    if (handle_id == HANDLE_PLANE_XY) {
        return 0
    } elif (handle_id == HANDLE_PLANE_YZ) {
        return 1
    }
    return 0
}

def private PlaneVAxis(handle_id : int) : int {
    if (handle_id == HANDLE_PLANE_XY) {
        return 1
    } elif (handle_id == HANDLE_PLANE_YZ) {
        return 2
    }
    return 2
}

def private IsCtrlDown() : bool {
    return is_down(Key.LCtrl) || is_down(Key.RCtrl)
}

def private GizmoBasis(target : GizmoTarget; mode : GizmoMode; space : GizmoSpace) : float4 {
    // Scale is always local: scaling happens along the object's own axes.
    if (mode == GizmoMode.Scale || space == GizmoSpace.Local) {
        return target.rotation
    }
    return float4(0.0f, 0.0f, 0.0f, 1.0f)
}

def private GetGizmoCamera(var camera : GizmoCamera&) : bool {
    var found = false
    query() $[REQUIRE(editorCameraTag)] (let _world : WorldTransform, let camera_component : Camera) {
        camera.position = _world.position
        camera.forward = quat_mul_vec(_world.rotation, float3(0.0f, 1.0f, 0.0f))
        camera.right = quat_mul_vec(_world.rotation, float3(1.0f, 0.0f, 0.0f))
        camera.up = quat_mul_vec(_world.rotation, float3(0.0f, 0.0f, 1.0f))
        camera.fov = camera_component.fov
        found = true
    }
    return found
}

def private RayFromCamera(camera : GizmoCamera; mouse : float2; viewport : float2) : GizmoRay {
    let size = float2(max(viewport.x, 1.0f), max(viewport.y, 1.0f))
    let ndc_x = mouse.x / size.x * 2.0f - 1.0f
    let ndc_y = 1.0f - mouse.y / size.y * 2.0f
    let half_tan = tan(radians(camera.fov) * 0.5f)

    let horizontal = camera.right * (ndc_x * half_tan * size.x / size.y)
    let vertical = camera.up * (ndc_y * half_tan)
    let direction = camera.forward + horizontal + vertical
    return GizmoRay(origin = camera.position, direction = normalize(direction))
}

def public GizmoRayFromViewport(mouse : float2; viewport : float2) : GizmoRay {
    var camera : GizmoCamera
    if (GetGizmoCamera(camera)) {
        return RayFromCamera(camera, mouse, viewport)
    }
    return GizmoRay(origin = float3(0.0f, 0.0f, 0.0f), direction = float3(0.0f, 1.0f, 0.0f))
}

def private GizmoSizeFor(camera : GizmoCamera; position : float3; viewport : float2) : float {
    if (viewport.x < 1.0f || viewport.y < 1.0f) {
        return 0.0f
    }
    // Depth along the camera forward: at the screen edge the euclidean distance would make
    // the gizmo grow, while what matters for a constant pixel size is the plane distance.
    let depth = dot(position - camera.position, camera.forward)
    if (depth <= 0.05f) {
        // The pivot is at or behind the camera plane: a perspective gizmo would be garbage.
        return 0.0f
    }
    if (!gizmo_scale_with_distance) {
        return GIZMO_WORLD_SIZE
    }
    return 2.0f * depth * tan(radians(camera.fov) * 0.5f) * (GIZMO_PIXELS / viewport.y)
}

def private ToLocalRay(ray : GizmoRay; target : GizmoTarget; basis : float4; size : float) : GizmoRay {
    let inverse = quat_conjugate(basis)
    let local_origin = quat_mul_vec(inverse, ray.origin - target.position) / size
    let local_direction = normalize(quat_mul_vec(inverse, ray.direction))
    return GizmoRay(origin = local_origin, direction = local_direction)
}

def private RayPlane(ray : GizmoRay; point : float3; normal : float3; var t : float&) : bool {
    let denominator = dot(ray.direction, normal)
    if (abs(denominator) < 1e-6f) {
        return false
    }
    let distance = dot(point - ray.origin, normal) / denominator
    if (distance <= 0.0f) {
        return false
    }
    t = distance
    return true
}

def private RaySphere(ray : GizmoRay; center : float3; radius : float; var t : float&) : bool {
    let offset = ray.origin - center
    let b = dot(offset, ray.direction)
    let c = dot(offset, offset) - radius * radius
    let discriminant = b * b - c
    if (discriminant < 0.0f) {
        return false
    }
    let root = sqrt(discriminant)
    var distance = -b - root
    if (distance <= 0.0f) {
        distance = -b + root
    }
    if (distance <= 0.0f) {
        return false
    }
    t = distance
    return true
}

//! Closest approach of the ray to the segment [a, b]. Returns false when the closest point
//! is behind the ray origin.
def private RaySegment(ray : GizmoRay; a : float3; b : float3; var t : float&; var distance : float&;
                       var point : float3&) : bool {
    let u = b - a
    let w0 = ray.origin - a
    let aa = dot(ray.direction, ray.direction)
    let bb = dot(ray.direction, u)
    let cc = dot(u, u)
    let dd = dot(ray.direction, w0)
    let ee = dot(u, w0)
    let denominator = aa * cc - bb * bb

    var s = 0.0f
    if (abs(denominator) > 1e-6f) {
        s = (aa * ee - bb * dd) / denominator
    }
    s = clamp(s, 0.0f, 1.0f)

    let closest = a + u * s
    let ray_t = dot(closest - ray.origin, ray.direction)
    if (ray_t <= 0.0f) {
        return false
    }
    t = ray_t
    distance = length(ray.origin + ray.direction * ray_t - closest)
    point = closest
    return true
}

//! Parameter of the closest point on an infinite line through `origin` along `direction`.
def private RayLineParameter(ray : GizmoRay; origin : float3; direction : float3; var value : float&) : bool {
    let w0 = ray.origin - origin
    let b = dot(ray.direction, direction)
    let d = dot(ray.direction, w0)
    let e = dot(direction, w0)
    let denominator = 1.0f - b * b
    if (abs(denominator) < 1e-5f) {
        return false
    }
    value = (e - b * d) / denominator
    return true
}

def private RingBasis(axis : int; var u : float3&; var v : float3&) {
    if (axis == 0) {
        u = AxisUnit(1)
        v = AxisUnit(2)
    } elif (axis == 1) {
        u = AxisUnit(2)
        v = AxisUnit(0)
    } else {
        u = AxisUnit(0)
        v = AxisUnit(1)
    }
}

//! Nearest point on the ring polyline to the ray. Works for a ring seen edge-on, where the
//! ray-plane intersection does not exist; the drag angle is then driven by the nearest point
//! on the visible line, which is exactly what the user is aiming at.
def private ClosestRingPoint(ray : GizmoRay; axis : int; var point : float3&; var distance : float&) : bool {
    var u = float3(0.0f, 0.0f, 0.0f)
    var v = float3(0.0f, 0.0f, 0.0f)
    RingBasis(axis, u, v)

    var previous = u * RING_RADIUS
    var best = 1e30f
    var best_t = 1e30f
    var found = false
    for (step in range(1, 65)) {
        let angle = 2.0f * PI * float(step) / 64.0f
        let current = (u * cos(angle) + v * sin(angle)) * RING_RADIUS
        var ray_t = 0.0f
        var segment_distance = 0.0f
        var segment_point = float3(0.0f, 0.0f, 0.0f)
        if (RaySegment(ray, previous, current, ray_t, segment_distance, segment_point)) {
            // An edge-on ray crosses the circle twice at distance zero; the point in front is
            // the one the user sees, so ties are broken by the distance along the ray.
            let closer = segment_distance < best - 1e-5f
            let same_but_nearer = abs(segment_distance - best) <= 1e-5f && ray_t < best_t
            if (closer || same_but_nearer) {
                best = segment_distance
                best_t = ray_t
                point = segment_point
                found = true
            }
        }
        previous = current
    }
    distance = best
    return found
}

//! The square is drawn in the camera-facing quadrant of its plane (GizmoPass flips it every
//! frame), so picking flips it by the same rule and the clickable area stays under the quad.
def private PlanePointInHandle(point : float3; handle_id : int; camera_dir : float3) : bool {
    let u_axis = AxisUnit(PlaneUAxis(handle_id))
    let v_axis = AxisUnit(PlaneVAxis(handle_id))
    var u = dot(point, u_axis)
    var v = dot(point, v_axis)
    if (dot(camera_dir, u_axis) < 0.0f) {
        u = -u
    }
    if (dot(camera_dir, v_axis) < 0.0f) {
        v = -v
    }
    return u >= PLANE_HIT_LOW && u <= PLANE_HIT_HIGH && v >= PLANE_HIT_LOW && v <= PLANE_HIT_HIGH
}

//! Ray cast against the exact handle shapes, in gizmo-local space. Returns the nearest
//! handle id, or -1. A `t` comparison is enough because the gizmo is an overlay and the
//! handles do not occlude each other in a meaningful way.
def public GizmoHitTest(target : GizmoTarget; mode : GizmoMode; space : GizmoSpace; size : float;
                        ray : GizmoRay) : int {
    if (size <= 0.0f) {
        return -1
    }
    let basis = GizmoBasis(target, mode, space)
    let local = ToLocalRay(ray, target, basis, size)
    let origin = float3(0.0f, 0.0f, 0.0f)

    var best = -1
    var best_t = 1e30f

    if (mode == GizmoMode.Rotate) {
        // The ring is tested as a polyline instead of a ray-plane hit: a ring seen edge-on
        // projects to a line, and a plane test is exactly the case that fails there. 64
        // segments cost nothing on hover and stay accurate at any camera angle.
        for (axis in range(3)) {
            var u = float3(0.0f, 0.0f, 0.0f)
            var v = float3(0.0f, 0.0f, 0.0f)
            RingBasis(axis, u, v)

            var previous = u * RING_RADIUS
            for (step in range(1, 65)) {
                let angle = 2.0f * PI * float(step) / 64.0f
                let current = (u * cos(angle) + v * sin(angle)) * RING_RADIUS
                var ray_t = 0.0f
                var distance = 0.0f
                var segment_point = float3(0.0f, 0.0f, 0.0f)
                if (RaySegment(local, previous, current, ray_t, distance, segment_point)) {
                    if (distance <= RING_HIT_RADIUS && ray_t < best_t) {
                        best = axis
                        best_t = ray_t
                    }
                }
                previous = current
            }
        }
        return best
    }

    // Direction from the gizmo to the camera: the local ray origin is the camera position in
    // gizmo units. It must match GizmoPass, which flips handles toward the camera.
    var camera_dir = -local.direction
    if (length(local.origin) > 0.0001f) {
        camera_dir = normalize(local.origin)
    }

    // The center is a uniform-scale handle only; Translate has no free camera-plane move.
    var t = 0.0f
    if (mode == GizmoMode.Scale && RaySphere(local, origin, CENTER_HIT_RADIUS, t) && t < best_t) {
        best = HANDLE_CENTER
        best_t = t
    }

    for (axis in range(3)) {
        let direction = AxisUnit(axis) * AxisSign(axis, camera_dir)
        var ray_t = 0.0f
        var distance = 0.0f
        var segment_point = float3(0.0f, 0.0f, 0.0f)
        if (RaySegment(local, direction * 0.04f, direction * 1.0f, ray_t, distance, segment_point)) {
            if (distance <= AXIS_HIT_RADIUS && ray_t < best_t) {
                best = axis
                best_t = ray_t
            }
        }
    }

    if (mode == GizmoMode.Translate) {
        for (plane_handle in range(HANDLE_PLANE_XY, HANDLE_PLANE_XZ + 1)) {
            var plane_t = 0.0f
            if (RayPlane(local, origin, AxisUnit(PlaneNormalAxis(plane_handle)), plane_t)) {
                let point = local.origin + local.direction * plane_t
                if (PlanePointInHandle(point, plane_handle, camera_dir) && plane_t < best_t) {
                    best = plane_handle
                    best_t = plane_t
                }
            }
        }
    }

    return best
}

def private SelectedTarget(var target : GizmoTarget&) : bool {
    if (selected_eid == INVALID_ENTITY_ID || !is_alive(selected_eid)) {
        return false
    }
    var found = false
    query(selected_eid) $(let transform : Transform; let _world : WorldTransform) {
        target = GizmoTarget(position = _world.position, rotation = _world.rotation, scale = transform.scale)
        found = true
    }
    return found
}

def public GizmoBeginDrag(target : GizmoTarget; mode : GizmoMode; space : GizmoSpace; size : float; ray : GizmoRay;
                          handle_id : int) : bool {
    g_drag = GizmoDrag()
    g_drag.handle = handle_id
    g_drag.mode = mode
    g_drag.space = space
    g_drag.target = target
    g_drag.basis = GizmoBasis(target, mode, space)
    g_drag.size = size
    g_drag.start_scale = target.scale

    let local = ToLocalRay(ray, target, g_drag.basis, size)
    let origin = float3(0.0f, 0.0f, 0.0f)

    if (mode == GizmoMode.Rotate) {
        var point = float3(0.0f, 0.0f, 0.0f)
        var distance = 0.0f
        if (!ClosestRingPoint(local, handle_id, point, distance)) {
            return false
        }
        if (distance > RING_HIT_RADIUS) {
            return false
        }
        g_drag.start_point = point
        g_drag.plane_normal = AxisUnit(handle_id)
        g_drag.active = true
        return true
    }

    if (mode == GizmoMode.Translate) {
        if (handle_id >= 0 && handle_id <= 2) {
            var value = 0.0f
            if (!RayLineParameter(local, origin, AxisUnit(handle_id), value)) {
                return false
            }
            g_drag.start_value = value
            g_drag.active = true
            return true
        }
        // Only the 4..6 plane handles reach this branch.
        g_drag.plane_normal = AxisUnit(PlaneNormalAxis(handle_id))
        var t = 0.0f
        if (!RayPlane(local, origin, g_drag.plane_normal, t)) {
            return false
        }
        g_drag.start_point = local.origin + local.direction * t
        g_drag.active = true
        return true
    }

    // Scale.
    if (handle_id >= 0 && handle_id <= 2) {
        var value = 0.0f
        if (!RayLineParameter(local, origin, AxisUnit(handle_id), value)) {
            return false
        }
        if (abs(value) < 0.05f) {
            return false
        }
        g_drag.start_value = value
        g_drag.active = true
        return true
    }
    // Uniform scale: the center cube is dragged on the plane facing the click ray.
    g_drag.plane_normal = normalize(local.direction) * -1.0f
    var t = 0.0f
    if (!RayPlane(local, origin, g_drag.plane_normal, t)) {
        return false
    }
    g_drag.start_point = local.origin + local.direction * t
    g_drag.start_value = length(g_drag.start_point)
    if (g_drag.start_value < 0.05f) {
        return false
    }
    g_drag.active = true
    return true
}

def private SnapFloat(value : float; step : float) : float {
    if (step <= 0.0f) {
        return value
    }
    return round(value / step) * step
}

def private SnapVector(value : float3; step : float) : float3 {
    return float3(SnapFloat(value.x, step), SnapFloat(value.y, step), SnapFloat(value.z, step))
}

def private ClampScale(value : float3) : float3 {
    let minimum = 0.01f
    return float3(max(value.x, minimum), max(value.y, minimum), max(value.z, minimum))
}

//! Total delta from the drag start, not an incremental step: the caller applies it to the
//! target captured in `GizmoBeginDrag`, so round-off cannot accumulate while dragging.
def public GizmoUpdateDrag(ray : GizmoRay; snap : bool) : GizmoDelta {
    var delta = GizmoDelta()
    if (!g_drag.active) {
        return delta
    }

    let local = ToLocalRay(ray, g_drag.target, g_drag.basis, g_drag.size)
    let origin = float3(0.0f, 0.0f, 0.0f)

    if (g_drag.mode == GizmoMode.Rotate) {
        var point = float3(0.0f, 0.0f, 0.0f)
        var distance = 0.0f
        if (!ClosestRingPoint(local, g_drag.handle, point, distance)) {
            return delta
        }
        let a = normalize(g_drag.start_point)
        let b = normalize(point)
        var angle = atan2(dot(cross(a, b), g_drag.plane_normal), dot(a, b))
        if (snap) {
            angle = radians(SnapFloat(degrees(angle), ROTATE_SNAP_DEG))
        }
        let world_axis = normalize(quat_mul_vec(g_drag.basis, g_drag.plane_normal))
        delta.rotation = quat_from_unit_vec_ang(world_axis, angle)
        delta.changed = true
        return delta
    }

    if (g_drag.mode == GizmoMode.Translate) {
        if (g_drag.handle >= 0 && g_drag.handle <= 2) {
            var value = 0.0f
            if (!RayLineParameter(local, origin, AxisUnit(g_drag.handle), value)) {
                return delta
            }
            var distance = value - g_drag.start_value
            if (snap) {
                distance = SnapFloat(distance * g_drag.size, MOVE_SNAP) / g_drag.size
            }
            let local_delta = AxisUnit(g_drag.handle) * distance
            delta.translation = quat_mul_vec(g_drag.basis, local_delta * g_drag.size)
            delta.changed = true
            return delta
        }

        var t = 0.0f
        if (!RayPlane(local, origin, g_drag.plane_normal, t)) {
            return delta
        }
        let current = local.origin + local.direction * t
        var world_delta = quat_mul_vec(g_drag.basis, (current - g_drag.start_point) * g_drag.size)
        if (snap) {
            world_delta = SnapVector(world_delta, MOVE_SNAP)
        }
        delta.translation = world_delta
        delta.changed = true
        return delta
    }

    // Scale. Every branch produces the final local scale, then turns it into a multiplier so
    // the caller (including a future collider editor) applies it uniformly.
    var new_scale = g_drag.start_scale

    if (g_drag.handle >= 0 && g_drag.handle <= 2) {
        var value = 0.0f
        if (!RayLineParameter(local, origin, AxisUnit(g_drag.handle), value)) {
            return delta
        }
        if (abs(g_drag.start_value) < 0.05f) {
            return delta
        }
        let factor = value / g_drag.start_value
        if (g_drag.handle == 0) {
            new_scale.x = g_drag.start_scale.x * factor
        } elif (g_drag.handle == 1) {
            new_scale.y = g_drag.start_scale.y * factor
        } else {
            new_scale.z = g_drag.start_scale.z * factor
        }
    } else {
        var t = 0.0f
        if (!RayPlane(local, origin, g_drag.plane_normal, t)) {
            return delta
        }
        let current = local.origin + local.direction * t
        let start_length = g_drag.start_value
        if (start_length < 0.05f) {
            return delta
        }
        // The center cube scales every axis by the same factor.
        let factor = length(current) / start_length
        new_scale = g_drag.start_scale * factor
    }

    if (snap) {
        new_scale = SnapVector(new_scale, SCALE_SNAP)
    }
    new_scale = ClampScale(new_scale)

    if (g_drag.start_scale.x > 1e-5f) {
        delta.scale.x = new_scale.x / g_drag.start_scale.x
    }
    if (g_drag.start_scale.y > 1e-5f) {
        delta.scale.y = new_scale.y / g_drag.start_scale.y
    }
    if (g_drag.start_scale.z > 1e-5f) {
        delta.scale.z = new_scale.z / g_drag.start_scale.z
    }
    delta.changed = true
    return delta
}

def public GizmoEndDrag() {
    g_drag.active = false
    g_drag.handle = -1
}

//! Drops the drag and the hover highlight. A placement tool calls it every frame it owns the
//! viewport, so an armed tool never leaves a grabbed handle behind.
def public CancelGizmoInteraction() {
    GizmoEndDrag()
    g_hover = -1
}

// ---------------------------------------------------------------------------
// Applying a delta to the selected entity.
// ---------------------------------------------------------------------------

def private ParentWorld(eid : EntityId; var parent_world : WorldTransform&) : bool {
    var parent_eid = INVALID_ENTITY_ID
    query(eid) $(relationship : Relationship) {
        parent_eid = relationship.parent
    }
    if (parent_eid == INVALID_ENTITY_ID) {
        return false
    }
    var has_parent = false
    query(parent_eid) $(_world : WorldTransform) {
        parent_world = _world
        has_parent = true
    }
    return has_parent
}

def private ApplyDeltaToSelection(delta : GizmoDelta) {
    if (selected_eid == INVALID_ENTITY_ID || !is_alive(selected_eid)) {
        return
    }

    var local = Transform()
    var has_transform = false
    query(selected_eid) $(transform : Transform) {
        local = transform
        has_transform = true
    }
    if (!has_transform) {
        return
    }

    var parent_transform = WorldTransform()
    let has_parent = ParentWorld(selected_eid, parent_transform)

    let start = g_drag.target
    if (g_drag.mode == GizmoMode.Translate) {
        let world_position = start.position + delta.translation
        if (has_parent) {
            let offset = world_position - parent_transform.position
            local.position = quat_mul_vec(quat_conjugate(parent_transform.rotation), offset / parent_transform.scale)
        } else {
            local.position = world_position
        }
    } elif (g_drag.mode == GizmoMode.Rotate) {
        let world_rotation = normalize(quat_mul(delta.rotation, start.rotation))
        if (has_parent) {
            local.rotation = normalize(quat_mul(quat_conjugate(parent_transform.rotation), world_rotation))
        } else {
            local.rotation = world_rotation
        }
    } else {
        local.scale = ClampScale(start.scale * delta.scale)
    }

    update_entity(selected_eid) @(eid, cmp) {
        cmp |> set("transform", local)
    }
    commit()

    // The object is rendered from `_world` in the same frame the gizmo is refreshed from it,
    // so the subtree must not wait for the next EngineUpdate tick.
    RefreshWorldSubtree(selected_eid)
}

// ---------------------------------------------------------------------------
// Frame state and input.
// ---------------------------------------------------------------------------

//! Fills the state GizmoPass draws. Called from EditorUpdate, so the gizmo follows selection
//! changes even when the viewport is not hovered.
[profile]
def public UpdateGizmoDrawState() {
    g_draw = GizmoDrawState()
    g_draw.mode = int(gizmo_mode)
    g_draw.hover = g_hover
    g_draw.active = g_drag.active ? g_drag.handle : -1
    g_draw.visible = 0

    var target : GizmoTarget
    if (!SelectedTarget(target)) {
        return
    }
    var camera : GizmoCamera
    if (!GetGizmoCamera(camera)) {
        return
    }
    let size = GizmoSizeFor(camera, target.position, g_viewport)
    if (size <= 0.0f) {
        return
    }

    g_draw.position = target.position
    g_draw.rotation = GizmoBasis(target, gizmo_mode, gizmo_space)
    g_draw.size = size
    g_draw.visible = 1
}

//! Called from the scene viewport once per ImGui frame. `mouse` is viewport-local.
[profile]
def public UpdateGizmoInteraction(mouse : float2; viewport : float2; hovered : bool; clicked : bool;
                                  down : bool) {
    if (g_drag.active) {
        // Apply the release frame's position too: otherwise the last mouse movement before
        // the button comes up would be dropped.
        var camera : GizmoCamera
        if (GetGizmoCamera(camera)) {
            let ray = RayFromCamera(camera, mouse, viewport)
            let delta = GizmoUpdateDrag(ray, IsCtrlDown())
            if (delta.changed) {
                ApplyDeltaToSelection(delta)
            }
        }
        if (!down) {
            GizmoEndDrag()
        }
        return
    }

    g_hover = -1
    if (!hovered) {
        return
    }

    var target : GizmoTarget
    if (!SelectedTarget(target)) {
        return
    }
    var camera : GizmoCamera
    if (!GetGizmoCamera(camera)) {
        return
    }
    let size = GizmoSizeFor(camera, target.position, viewport)
    if (size <= 0.0f) {
        return
    }

    let ray = RayFromCamera(camera, mouse, viewport)
    let hit_handle = GizmoHitTest(target, gizmo_mode, gizmo_space, size, ray)
    g_hover = hit_handle
    if (clicked && hit_handle != -1) {
        if (!GizmoBeginDrag(target, gizmo_mode, gizmo_space, size, ray, hit_handle)) {
            g_hover = -1
        }
    }
}

// ---------------------------------------------------------------------------
// Editor integration.
// ---------------------------------------------------------------------------

def public SetGizmoViewport(viewport : float2) {
    g_viewport = viewport
}

def public SetGizmoMode(mode : GizmoMode) {
    gizmo_mode = mode
    GizmoEndDrag()
    g_hover = -1
}

def public SetGizmoSpace(space : GizmoSpace) {
    gizmo_space = space
    GizmoEndDrag()
    g_hover = -1
}

def public SetGizmoScaleWithDistance(value : bool) {
    if (gizmo_scale_with_distance == value) {
        return
    }
    gizmo_scale_with_distance = value
    GizmoEndDrag()
    g_hover = -1
}

def public ToggleGizmoScaleWithDistance() {
    SetGizmoScaleWithDistance(!gizmo_scale_with_distance)
}

def public ToggleGizmoSpace() {
    SetGizmoSpace(gizmo_space == GizmoSpace.World ? GizmoSpace.Local : GizmoSpace.World)
}

//! W/E/R/X/V are read directly instead of going through EditorHotkeys: the generic
//! framework is fine for future actions, but the gizmo must not depend on its registration
//! and invoke path. Text input and the right-button camera flight win over mode switching.
[profile]
def public ProcessGizmoHotkeys() {
    if (IsGameViewportFocused() || IsEditorTextInputActive()) {
        return
    }
    if (is_mouse_down(Mouse.Right)) {
        return
    }

    if (pressed(Key.W)) {
        SetGizmoMode(GizmoMode.Translate)
    } elif (pressed(Key.E)) {
        SetGizmoMode(GizmoMode.Rotate)
    } elif (pressed(Key.R)) {
        SetGizmoMode(GizmoMode.Scale)
    }

    if (pressed(Key.X)) {
        ToggleGizmoSpace()
    }
    if (pressed(Key.V)) {
        ToggleGizmoScaleWithDistance()
    }
}
