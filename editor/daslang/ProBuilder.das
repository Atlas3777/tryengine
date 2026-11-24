options gen2
options relaxed_assign = false

module ProBuilder shared public

//! ProBuilder-like box tool for the scene viewport.
//!
//! A box is an ordinary entity: a unit-cube mesh, the grid material
//! (`data/materials/grid.mat`, one-metre cells on the faces), a `meshScale` component that
//! holds the box dimensions and, when "Add box collider" is on, a `boxCollider` sized to the
//! same dimensions. The dimensions live in `meshScale`, not in `Transform.scale`: the
//! transform stays the object pose (position/rotation/scale), so moving, rotating or scaling
//! a box with the gizmo does not rewrite its authored size and children are not affected by
//! a fake scale.
//!
//! Placement starts at the surface under the cursor. A CPU raycast against the local AABB of
//! every visible mesh gives the contact point and its outward normal; the Z=0 editor ground
//! plane is the fallback when the ray hits no mesh. The placement frame is built at the
//! click: local X/Y span the surface, local Z is along its normal (height on a floor, wall
//! thickness on a wall). A click places the "Default size" in the grid cell containing the
//! contact point, a drag covers every surface cell under the cursor. `Vertical walls` forces
//! a wall on a horizontal surface, facing the camera.
//!
//! While armed the tool owns the viewport click: the gizmo is suspended and
//! `PickingPass` never sees the click, so placing a box cannot also select or grab something.
//!
//! Raycast caveat: the engine keeps no CPU triangle data for meshes, so the hit uses the
//! mesh artifact's local AABB. For a box it is exact; for a complex imported mesh the contact
//! point can be off inside the bounding box.

require math
require daslib/math_boost
require daslib/decs
require daslib/decs_boost
require tryengine/Assets
require tryengine/Input
require tryengine/Model
require tryengine/Physics
require tryengine/Profiling
require tryengine/TransformSystem
require tryengine/Types
require tryResources
require tryeditor/DebugRender
require tryeditor/EditorInput
require tryeditor/Gizmo
require tryeditor/Hierarchy
require tryeditor/SceneViewPort
require tryeditor/UndoRedo
require TryEditor
require imgui
require imgui/imgui_widgets_builtin
require imgui/imgui_containers_builtin
require imgui/imgui_boost_v2

// A unit box centred at the origin (24 vertices, 36 indices, MeshBinary v4). Positions are
// in [-0.5, 0.5], so `meshScale.scale` is exactly the box size in metres.
let private UNIT_BOX_PATH : string = "data/meshes/unit_box.mesh"
let private GRID_MATERIAL_PATH : string = "game/data/materials/grid.mat"

// The tag also marks the entity as a ProBuilder box. It is a saved component, so the
// collider sync keeps working after the scene is written and loaded again.
let private BOX_TAG : string = "PB Box"
let private DRAG_PIXELS : float = 4.0f
let private MIN_SIZE : float = 0.01f

// Placement frame: an orthonormal basis and the plane the box is built on.
// `plane_w = dot(contact_point, n)`; the box extends from that plane along `n`.
struct private BoxFrame {
    u        : float3 = float3(1.0f, 0.0f, 0.0f)
    v        : float3 = float3(0.0f, 1.0f, 0.0f)
    n        : float3 = float3(0.0f, 0.0f, 1.0f)
    plane_w  : float
    rotation : float4 = float4(0.0f, 0.0f, 0.0f, 1.0f)
}

struct private BoxSpec {
    center   : float3 = float3(0.0f, 0.0f, 0.0f)
    size     : float3 = float3(1.0f, 1.0f, 1.0f)
    rotation : float4 = float4(0.0f, 0.0f, 0.0f, 1.0f)
}

struct private BoxDraft {
    active      : bool
    frame       : BoxFrame = BoxFrame()
    start_u     : float
    start_v     : float
    current_u   : float
    current_v   : float
    start_mouse : float2
    dragging    : bool
}

struct private SurfaceHit {
    valid  : bool = false
    point  : float3 = float3(0.0f, 0.0f, 0.0f)
    normal : float3 = float3(0.0f, 0.0f, 1.0f)
    entity : EntityId = INVALID_ENTITY_ID
}

var private pb_tool_active : bool = false
var private pb_snap_enabled : bool = true
var private pb_snap_step : float = 1.0f
var private pb_default_size : float3 = float3(1.0f, 1.0f, 1.0f)
var private pb_auto_collider : bool = true
var private pb_keep_active : bool = true
var private pb_vertical : bool = false
var private pb_status : string = ""

var private g_draft : BoxDraft = BoxDraft()
var private g_preview_drawn : bool = false
var private g_pending : BoxSpec = BoxSpec()
var private g_has_pending : bool = false

//! Loads the box assets once, from `EditorStart`. The authoritative trees are built before
//! the scripts load, so the paths already resolve here. The reference taken by `Reserve` is
//! intentionally kept for the lifetime of the editor: entities only hold raw pointers.
//!
//! This must not live in `[init]`: module init runs under the daslang CLI gate too, where the
//! native resource bindings have no `TryengineContext` to resolve against.
def public ProBuilderStart() {
    let mesh_id = Assets::IdOf(UNIT_BOX_PATH)
    if (mesh_id == 0ul) {
        print("[PROBUILDER] mesh '{UNIT_BOX_PATH}' is not registered; check game/data/meshes/unit_box.mesh\n")
        return
    }
    let refs = Model::Resolve(mesh_id, GRID_MATERIAL_PATH)
    if (!refs.ok) {
        print("[PROBUILDER] material '{GRID_MATERIAL_PATH}' is not registered\n")
        return
    }
    // EditorStart also runs after a script hot reload. The reference taken on the first run
    // is still held by the resource cache, so a second `Reserve` would only inflate it.
    if (Model::IsReady(refs)) {
        return
    }
    if (!Model::Reserve(refs)) {
        print("[PROBUILDER] box assets are still loading; placement will wait for them\n")
    }
}

// ---------------------------------------------------------------------------
// Ray / surface helpers.
// ---------------------------------------------------------------------------

def private AxisValue(v : float3; axis : int) : float {
    if (axis == 0) {
        return v.x
    } elif (axis == 1) {
        return v.y
    }
    return v.z
}

//! Ray against an axis-aligned box in the mesh's local space. `hit_sign` is the outward
//! normal of the entered face along `hit_axis`; inside origins are skipped (a placement
//! started inside a mesh has no meaningful contact point).
def private RayAABB(origin : float3; direction : float3; bmin : float3; bmax : float3;
                    var t_hit : float&; var hit_axis : int&; var hit_sign : float&) : bool {
    var t_near = -1.0e30f
    var t_far = 1.0e30f
    var axis_out = -1
    var sign_out = 1.0f

    for (axis in range(3)) {
        let o = AxisValue(origin, axis)
        let d = AxisValue(direction, axis)
        let lo = AxisValue(bmin, axis)
        let hi = AxisValue(bmax, axis)
        if (abs(d) < 1.0e-8f) {
            if (o < lo || o > hi) {
                return false
            }
        } else {
            var near = (lo - o) / d
            var far = (hi - o) / d
            var face_sign = -1.0f
            if (near > far) {
                let saved = near
                near = far
                far = saved
                face_sign = 1.0f
            }
            if (near > t_near) {
                t_near = near
                axis_out = axis
                sign_out = face_sign
            }
            if (far < t_far) {
                t_far = far
            }
            if (t_near > t_far) {
                return false
            }
        }
    }

    if (t_far < 0.0f || axis_out < 0) {
        return false
    }
    t_hit = t_near
    hit_axis = axis_out
    hit_sign = sign_out
    return t_near > 1.0e-4f
}

//! Nearest visible mesh under the ray. The entity transform is inverted analytically, so the
//! ray stays a ray in local space and `t` is the same world parameter for every candidate.
def private RaycastScene(ray : GizmoRay; var hit : SurfaceHit&) {
    var found = SurfaceHit()
    var best_t = 1.0e30f

    query() $(eid : EntityId; _world : Transform; model : ModelRender; mesh : TryMesh?;
              material : TryMat?) {
        // Match the renderer's visibility rule: only entities with both runtime pointers are
        // drawn, so an entity with an unresolved material must not catch the placement ray.
        if (mesh == null || material == null) {
            return
        }
        let mesh_id = model.mesh
        if (mesh_id == 0ul) {
            return
        }
        let mesh_scale = get_component(eid, "meshScale", MeshScale(scale = float3(1.0f, 1.0f, 1.0f))).scale
        let world_scale = _world.scale * mesh_scale
        if (abs(world_scale.x) < 1.0e-4f || abs(world_scale.y) < 1.0e-4f || abs(world_scale.z) < 1.0e-4f) {
            return
        }

        let inv_scale = float3(1.0f / world_scale.x, 1.0f / world_scale.y, 1.0f / world_scale.z)
        let rotation = normalize(_world.rotation)
        let inverse_rotation = quat_conjugate(rotation)
        let delta = ray.origin - _world.position
        let local_origin = quat_mul_vec(inverse_rotation,
                                        float3(delta.x * inv_scale.x, delta.y * inv_scale.y, delta.z * inv_scale.z))
        let local_direction = quat_mul_vec(inverse_rotation,
                                           float3(ray.direction.x * inv_scale.x, ray.direction.y * inv_scale.y,
                                                  ray.direction.z * inv_scale.z))

        var t = 0.0f
        var axis = 0
        var face_sign = 1.0f
        if (!RayAABB(local_origin, local_direction, GetMeshBoundsMin(mesh_id), GetMeshBoundsMax(mesh_id), t, axis,
                     face_sign) ||
            t >= best_t) {
            return
        }

        var local_normal = float3(0.0f, 0.0f, 0.0f)
        if (axis == 0) {
            local_normal.x = face_sign
        } elif (axis == 1) {
            local_normal.y = face_sign
        } else {
            local_normal.z = face_sign
        }
        var normal = normalize(quat_mul_vec(rotation, float3(local_normal.x * inv_scale.x, local_normal.y * inv_scale.y,
                                                            local_normal.z * inv_scale.z)))
        // The renderer only draws front faces; the contact normal must point back at the
        // camera whatever the mesh's handedness or a negative scale did to it.
        if (dot(normal, ray.direction) > 0.0f) {
            normal = -normal
        }

        best_t = t
        found.valid = true
        found.point = ray.origin + ray.direction * t
        found.normal = normal
        found.entity = eid
    }

    hit = found
}

def private GroundHit(ray : GizmoRay; var point : float3&) : bool {
    if (abs(ray.direction.z) < 1e-6f) {
        return false
    }
    let distance = -ray.origin.z / ray.direction.z
    if (distance <= 0.0f) {
        return false
    }
    point = ray.origin + ray.direction * distance
    return true
}

def private RayPlanePoint(ray : GizmoRay; normal : float3; plane_w : float; var point : float3&) : bool {
    let denominator = dot(ray.direction, normal)
    if (abs(denominator) < 1e-6f) {
        return false
    }
    let distance = (plane_w - dot(ray.origin, normal)) / denominator
    if (distance <= 0.0f) {
        return false
    }
    point = ray.origin + ray.direction * distance
    return true
}

// ---------------------------------------------------------------------------
// Placement frame.
// ---------------------------------------------------------------------------

def private NearestHorizontalAxis(direction : float3) : float3 {
    if (abs(direction.x) >= abs(direction.y)) {
        return float3(direction.x >= 0.0f ? 1.0f : -1.0f, 0.0f, 0.0f)
    }
    return float3(0.0f, direction.y >= 0.0f ? 1.0f : -1.0f, 0.0f)
}

//! Quaternion that maps local +X to `u` and local +Z to `n` (the local +Y follows from the
//! right-handed basis). Built as "shortest arc Z -> n", then a twist around n that aligns
//! the rotated X with the requested `u`; no matrix-to-quaternion binding is needed.
def private FrameRotation(u : float3; n : float3) : float4 {
    let cos_angle = clamp(n.z, -1.0f, 1.0f)
    var arc : float4
    if (cos_angle > 0.99999f) {
        arc = float4(0.0f, 0.0f, 0.0f, 1.0f)
    } elif (cos_angle < -0.99999f) {
        arc = quat_from_unit_vec_ang(float3(1.0f, 0.0f, 0.0f), PI)
    } else {
        arc = quat_from_unit_vec_ang(normalize(cross(float3(0.0f, 0.0f, 1.0f), n)), acos(cos_angle))
    }
    let rotated_x = quat_mul_vec(arc, float3(1.0f, 0.0f, 0.0f))
    let twist = atan2(dot(cross(rotated_x, u), n), dot(rotated_x, u))
    return normalize(quat_mul(quat_from_unit_vec_ang(n, twist), arc))
}

//! The surface frame. On a wall-ish surface local Y is world up projected onto the plane; on
//! a floor/ceiling local X is world X projected onto the plane. `Vertical walls` replaces a
//! floor/ceiling normal with the camera-facing horizontal axis so the wall stands on the
//! surface and looks at the user.
def private MakeFrame(hit : SurfaceHit; ray : GizmoRay; vertical : bool) : BoxFrame {
    var n = normalize(hit.normal)
    var u = float3(1.0f, 0.0f, 0.0f)
    var v = float3(0.0f, 1.0f, 0.0f)

    if (vertical && abs(n.z) > 0.7f) {
        var facing = float3(-ray.direction.x, -ray.direction.y, 0.0f)
        if (length(facing) < 1.0e-3f) {
            facing = float3(1.0f, 0.0f, 0.0f)
        }
        n = NearestHorizontalAxis(facing)
        v = float3(0.0f, 0.0f, 1.0f)
        u = normalize(cross(v, n))
    } elif (abs(n.z) >= 0.7f) {
        u = float3(1.0f, 0.0f, 0.0f) - n * n.x
        if (length(u) < 1.0e-4f) {
            u = float3(0.0f, 1.0f, 0.0f) - n * n.y
        }
        u = normalize(u)
        v = normalize(cross(n, u))
    } else {
        v = float3(0.0f, 0.0f, 1.0f) - n * n.z
        if (length(v) < 1.0e-4f) {
            v = float3(1.0f, 0.0f, 0.0f) - n * n.x
        }
        v = normalize(v)
        u = normalize(cross(v, n))
    }

    if (dot(n, ray.direction) > 0.0f) {
        n = -n
        u = -u
    }

    return BoxFrame(u = u, v = v, n = n, plane_w = dot(hit.point, n), rotation = FrameRotation(u, n))
}

// ---------------------------------------------------------------------------
// Draft -> box.
// ---------------------------------------------------------------------------

def private CellFloor(value : float; step : float) : float => floor(value / step) * step

def private MakeSpec(draft : BoxDraft) : BoxSpec {
    let frame = draft.frame
    let thickness = max(pb_default_size.z, MIN_SIZE)
    var origin_u = draft.start_u
    var origin_v = draft.start_v
    var size_u = max(pb_default_size.x, MIN_SIZE)
    var size_v = max(pb_default_size.y, MIN_SIZE)

    if (pb_snap_enabled && pb_snap_step > 0.0f) {
        let step = pb_snap_step
        let start_cell_u = CellFloor(draft.start_u, step)
        let start_cell_v = CellFloor(draft.start_v, step)
        if (draft.dragging) {
            let current_cell_u = CellFloor(draft.current_u, step)
            let current_cell_v = CellFloor(draft.current_v, step)
            origin_u = min(start_cell_u, current_cell_u)
            origin_v = min(start_cell_v, current_cell_v)
            size_u = max(start_cell_u, current_cell_u) + step - origin_u
            size_v = max(start_cell_v, current_cell_v) + step - origin_v
        } else {
            origin_u = start_cell_u
            origin_v = start_cell_v
        }
    } elif (draft.dragging) {
        origin_u = min(draft.start_u, draft.current_u)
        origin_v = min(draft.start_v, draft.current_v)
        size_u = max(abs(draft.current_u - draft.start_u), MIN_SIZE)
        size_v = max(abs(draft.current_v - draft.start_v), MIN_SIZE)
    }

    let center = (frame.u * (origin_u + size_u * 0.5f) + frame.v * (origin_v + size_v * 0.5f) +
                  frame.n * (frame.plane_w + thickness * 0.5f))
    return BoxSpec(center = center, size = float3(size_u, size_v, thickness), rotation = frame.rotation)
}

// ---------------------------------------------------------------------------
// Entity creation and the collider sync.
// ---------------------------------------------------------------------------

//! Creates the box. Returns false while its mesh or material is not resident yet; the
//! caller retries through `g_pending` on the next frame.
def private CreateBox(spec : BoxSpec) : bool {
    let mesh_id = Assets::IdOf(UNIT_BOX_PATH)
    if (mesh_id == 0ul) {
        pb_status = "unit box mesh is not registered: {UNIT_BOX_PATH}"
        return false
    }
    let refs = Model::Resolve(mesh_id, GRID_MATERIAL_PATH)
    if (!refs.ok) {
        pb_status = "grid material is not resolvable: {GRID_MATERIAL_PATH}"
        return false
    }
    // `IsReady` is pure: calling `Reserve` here every frame would leak reference counts.
    if (!Model::IsReady(refs)) {
        pb_status = "waiting for box mesh and grid material..."
        return false
    }

    var mesh_pointer = Model::MeshPointer(refs)
    var material_pointer = Model::MaterialPointer(refs)
    if (mesh_pointer == null || material_pointer == null) {
        pb_status = "box mesh or grid material failed to load"
        return false
    }

    let size = spec.size
    let center = spec.center
    let rotation = spec.rotation
    let add_collider = pb_auto_collider

    let new_eid = create_entity() @(eid : EntityId; var cmp : ComponentMap) {
        cmp.tag := BOX_TAG
        // Transform is the pose only: the authored dimensions live in `meshScale`, so the
        // object can be moved, rotated or scaled without losing the box size.
        ApplyEntityTransform(cmp, center, rotation, float3(1.0f, 1.0f, 1.0f))
        cmp |> set("meshScale", MeshScale(scale = size))
        cmp |> set("model", ModelRender(mesh = mesh_id, material = GRID_MATERIAL_PATH))
        cmp |> set(Model::MESH_POINTER_FIELD, mesh_pointer)
        cmp |> set(Model::MATERIAL_POINTER_FIELD, material_pointer)
        if (add_collider) {
            cmp |> set("boxCollider", BoxCollider(half_extents = size * 0.5f))
        }
        cmp |> set(RELATIONSHIP_FIELD, Relationship())
    }
    commit()

    selected_eid = new_eid
    UndoRedo::RecordCreated(new_eid, "Place box")
    pb_status = "placed {size.x:0.2f} x {size.y:0.2f} x {size.z:0.2f} m box"
    return true
}

def private SameHalfExtents(a : float3; b : float3) : bool => abs(a.x - b.x) <= 1e-4f && abs(a.y - b.y) <= 1e-4f && abs(a.z - b.z) <= 1e-4f

//! Box colliders follow the authored size and the object scale. `size` is the meshScale
//! (identity for boxes created before meshScale existed, whose size was Transform.scale).
def private SyncCollider(eid : EntityId; transform : Transform; boxCollider : BoxCollider; size : float3) : bool {
    let scale = transform.scale
    let half = float3(max(abs(size.x * scale.x), MIN_SIZE) * 0.5f, max(abs(size.y * scale.y), MIN_SIZE) * 0.5f,
                      max(abs(size.z * scale.z), MIN_SIZE) * 0.5f)
    if (SameHalfExtents(boxCollider.half_extents, half)) {
        return false
    }
    update_entity(eid) @(eid, cmp) {
        cmp |> set("boxCollider", BoxCollider(half_extents = half))
    }
    return true
}

//! Keeps box colliders in sync with the authored size. New boxes carry `meshScale`; old
//! boxes (whose size was Transform.scale) are handled by the second query so a scene saved
//! by the previous version does not lose its colliders.
[profile]
def private SyncBoxColliders() {
    var changed = false

    query() $(eid : EntityId; transform : Transform; boxCollider : BoxCollider; meshScale : MeshScale; tag : string) {
        if (tag != BOX_TAG) {
            return
        }
        if (SyncCollider(eid, transform, boxCollider, meshScale.scale)) {
            changed = true
        }
    }
    query() $ [REQUIRE_NOT(meshScale)] (eid : EntityId; transform : Transform; boxCollider : BoxCollider;
                                        tag : string; relationship : Relationship) {
        if (tag != BOX_TAG) {
            return
        }
        if (SyncCollider(eid, transform, boxCollider, float3(1.0f, 1.0f, 1.0f))) {
            changed = true
        }
        // Boxes from the first version stored their size in Transform.scale. Fold that scale
        // into meshScale and reset the transform, so the size and the object pose are
        // separated from now on without changing what the box looks like. Only roots: a
        // child's local scale is not its size relative to the parent.
        if (relationship.parent == INVALID_ENTITY_ID) {
            let authored = transform
            update_entity(eid) @(eid, cmp) {
                cmp |> set("meshScale", MeshScale(scale = authored.scale))
                cmp |> set("transform", Transform(position = authored.position, rotation = authored.rotation,
                                                  scale = float3(1.0f, 1.0f, 1.0f)))
            }
            changed = true
        }
    }

    if (changed) {
        commit()
        // The draw matrix is world scale * meshScale and `_world` is still the old scale in
        // this frame: compose the migrated poses before the opaque pass reads them.
        UpdateWorldTransforms()
    }
}

// ---------------------------------------------------------------------------
// Preview.
// ---------------------------------------------------------------------------

def private PreviewCorner(spec : BoxSpec; x : float; y : float; z : float) : float3 {
    let local = float3(x * spec.size.x * 0.5f, y * spec.size.y * 0.5f, z * spec.size.z * 0.5f)
    return spec.center + quat_mul_vec(spec.rotation, local)
}

def private PreviewEdge(spec : BoxSpec; ax : float; ay : float; az : float; bx : float; by : float; bz : float) {
    AddLine(PreviewCorner(spec, ax, ay, az), PreviewCorner(spec, bx, by, bz), Color.Yellow)
}

//! The box grows from the contact plane along its normal, so its preview is a rotated box,
//! not an axis-aligned one: build all 12 edges in local space and rotate them.
def private DrawPreview(spec : BoxSpec) {
    ClearDebugLines()
    PreviewEdge(spec, -1.0f, -1.0f, -1.0f, 1.0f, -1.0f, -1.0f)
    PreviewEdge(spec, 1.0f, -1.0f, -1.0f, 1.0f, 1.0f, -1.0f)
    PreviewEdge(spec, 1.0f, 1.0f, -1.0f, -1.0f, 1.0f, -1.0f)
    PreviewEdge(spec, -1.0f, 1.0f, -1.0f, -1.0f, -1.0f, -1.0f)
    PreviewEdge(spec, -1.0f, -1.0f, 1.0f, 1.0f, -1.0f, 1.0f)
    PreviewEdge(spec, 1.0f, -1.0f, 1.0f, 1.0f, 1.0f, 1.0f)
    PreviewEdge(spec, 1.0f, 1.0f, 1.0f, -1.0f, 1.0f, 1.0f)
    PreviewEdge(spec, -1.0f, 1.0f, 1.0f, -1.0f, -1.0f, 1.0f)
    PreviewEdge(spec, -1.0f, -1.0f, -1.0f, -1.0f, -1.0f, 1.0f)
    PreviewEdge(spec, 1.0f, -1.0f, -1.0f, 1.0f, -1.0f, 1.0f)
    PreviewEdge(spec, 1.0f, 1.0f, -1.0f, 1.0f, 1.0f, 1.0f)
    PreviewEdge(spec, -1.0f, 1.0f, -1.0f, -1.0f, 1.0f, 1.0f)
    g_preview_drawn = true
}

def private ClearPreview() {
    if (g_preview_drawn) {
        ClearDebugLines()
        g_preview_drawn = false
    }
}

// ---------------------------------------------------------------------------
// Tool state and input.
// ---------------------------------------------------------------------------

def private CancelDraft() {
    g_draft = BoxDraft()
    ClearPreview()
}

def private SetToolActive(value : bool) {
    pb_tool_active = value
    SetPlacementActive(value)
    if (!value) {
        CancelDraft()
    }
}

//! Disarms the tool without touching anything else. `EditorUpdate` calls it as Play starts:
//! level authoring belongs to Edit mode, and a placement inside Play would be undone by the
//! Play snapshot restore anyway.
def public StopProBuilder() {
    // Entering Play drops a release that is still waiting for its assets: the box belongs to
    // the Edit-mode scene, not to the running game.
    g_has_pending = false
    if (pb_tool_active) {
        SetToolActive(false)
    }
}

//! Called from `EditorUpdate`: the B/Escape keys do not go through `EditorHotkeys`, because
//! the tool is a mode of the viewport and must not depend on registration order.
[profile]
def public ProcessProBuilderHotkeys() {
    if (IsGameViewportFocused() || IsEditorTextInputActive()) {
        return
    }
    if (pressed(Key.B) && !is_mouse_down(Mouse.Right)) {
        SetToolActive(!pb_tool_active)
    }
    if (pb_tool_active && pressed(Key.Escape)) {
        g_has_pending = false
        SetToolActive(false)
    }
}

def private FinishDraft() {
    let spec = MakeSpec(g_draft)
    g_draft = BoxDraft()
    ClearPreview()
    if (!CreateBox(spec)) {
        g_pending = spec
        g_has_pending = true
    }
    if (!pb_keep_active) {
        SetToolActive(false)
    }
}

//! Starts a draft at the surface under the cursor. A mesh hit defines the contact point and
//! the placement plane; the Z=0 ground is the fallback when no mesh is hit.
def private BeginDraft(ray : GizmoRay; mouse : float2) : bool {
    var hit = SurfaceHit()
    RaycastScene(ray, hit)
    if (!hit.valid) {
        var ground = float3(0.0f, 0.0f, 0.0f)
        if (GroundHit(ray, ground)) {
            hit = SurfaceHit(valid = true, point = ground, normal = float3(0.0f, 0.0f, 1.0f),
                             entity = INVALID_ENTITY_ID)
        }
    }
    if (!hit.valid) {
        pb_status = "no surface under the cursor"
        return false
    }

    let frame = MakeFrame(hit, ray, pb_vertical)
    let start_u = dot(hit.point, frame.u)
    let start_v = dot(hit.point, frame.v)
    g_draft = BoxDraft(active = true, frame = frame, start_u = start_u, start_v = start_v, current_u = start_u,
                       current_v = start_v, start_mouse = mouse)
    DrawPreview(MakeSpec(g_draft))
    return true
}

//! Called after `DrawSceneViewPort`, once the panel has recorded this frame's viewport mouse.
//! `ConsumeSceneViewportClick` clears the click before `PickingPass` sees it, so placing a
//! box does not also select (or start a gizmo drag on) the object behind the cursor.
[profile]
def public UpdateProBuilderPlacement() {
    SyncBoxColliders()

    if (g_has_pending) {
        if (CreateBox(g_pending)) {
            g_has_pending = false
        }
    }

    if (!pb_tool_active) {
        return
    }

    let viewport = GetEditorViewportSize()
    if (viewport.x < 1.0f || viewport.y < 1.0f) {
        return
    }

    let mouse = GetSceneViewportMouseState()
    let clicked = IsMouseClicked(ImGuiMouseButton.Left)
    let down = IsMouseDown(ImGuiMouseButton.Left)

    if (!MousePointInViewport(mouse.pos, viewport)) {
        // A draft survives the cursor leaving the image, but does not track an off-screen
        // position: the ray has no meaning there. Releasing outside still commits it with
        // the last on-screen footprint.
        if (g_draft.active && !down) {
            FinishDraft()
        } elif (!g_draft.active) {
            ClearPreview()
        }
        return
    }

    // The armed tool owns every left click over the image, including one that lands on a
    // spot with no surface: selection must not fire behind the placement cursor.
    let wants_click = clicked && IsSceneViewportHovered() && !is_mouse_down(Mouse.Right)
    if (wants_click) {
        ConsumeSceneViewportClick()
    }

    let ray = GizmoRayFromViewport(mouse.pos, viewport)

    if (!g_draft.active) {
        if (wants_click) {
            BeginDraft(ray, mouse.pos)
        }
        return
    }

    // The plane is fixed at the click: moving the cursor past the hit surface (or off the
    // screen edge of it) still produces a coherent rectangle.
    var plane_point = float3(0.0f, 0.0f, 0.0f)
    if (RayPlanePoint(ray, g_draft.frame.n, g_draft.frame.plane_w, plane_point)) {
        g_draft.current_u = dot(plane_point, g_draft.frame.u)
        g_draft.current_v = dot(plane_point, g_draft.frame.v)
    }
    g_draft.dragging = length(mouse.pos - g_draft.start_mouse) > DRAG_PIXELS

    if (down) {
        DrawPreview(MakeSpec(g_draft))
        return
    }

    FinishDraft()
}

def private MousePointInViewport(mouse : float2; viewport : float2) : bool => mouse.x >= 0.0f && mouse.y >= 0.0f && mouse.x <= viewport.x && mouse.y <= viewport.y

//! Dimensions of the selected box, false when the selection is not a ProBuilder box. The
//! Inspector still shows `meshScale`; this is the panel-local editor for the same value.
def private SelectedBoxSize(var size : float3&) : bool {
    if (selected_eid == INVALID_ENTITY_ID || !is_alive(selected_eid)) {
        return false
    }
    var found = false
    query(selected_eid) $(transform : Transform; meshScale : MeshScale; tag : string) {
        if (tag == BOX_TAG) {
            size = meshScale.scale
            found = true
        }
    }
    return found
}

// ---------------------------------------------------------------------------
// Panel.
// ---------------------------------------------------------------------------

[profile]
def DrawProBuilder() {
    window(PROBUILDER_WIN, (text = "ProBuilder", closable = false, flags = ImGuiWindowFlags.None)) {
        if (button(PB_TOOL_BTN, (text = pb_tool_active ? "Stop placing (B)" : "Place box (B)"))) {
            SetToolActive(!pb_tool_active)
        }
        same_line(PB_TOOL_ROW)
        text(PB_TOOL_STATE, (text = pb_tool_active ? "armed: click or drag a surface" : "tool off"))
        separator(PB_SEP_1)

        PB_SNAP.value = pb_snap_enabled
        if (checkbox(PB_SNAP, (text = "Snap to grid"))) {
            pb_snap_enabled = PB_SNAP.value
        }
        unsafe {
            edit_drag_scalar(addr(pb_snap_step),
                             (id = "pb_snap_step", text = "Grid step (m)", speed = 0.1f, v_min = 0.01f, v_max = 100.0f))
        }

        unsafe {
            edit_drag_float3(addr(pb_default_size),
                             (id = "pb_default_size", text = "Default size (m)", speed = 0.05f))
        }
        pb_default_size = float3(max(pb_default_size.x, MIN_SIZE), max(pb_default_size.y, MIN_SIZE),
                                 max(pb_default_size.z, MIN_SIZE))
        text(PB_SIZE_HINT, (text = "X/Y span the surface, Z is along its normal (height on floors, thickness on walls)."))

        PB_VERTICAL.value = pb_vertical
        if (checkbox(PB_VERTICAL, (text = "Vertical walls"))) {
            pb_vertical = PB_VERTICAL.value
            CancelDraft()
        }
        text(PB_VERTICAL_HINT, (text = "On a floor, a vertical wall stands on the contact point and faces the camera."))

        PB_AUTO_COLLIDER.value = pb_auto_collider
        if (checkbox(PB_AUTO_COLLIDER, (text = "Add box collider"))) {
            pb_auto_collider = PB_AUTO_COLLIDER.value
        }
        text(PB_COLLIDER_HINT, (text = "The collider follows meshScale and the object scale while the entity keeps the PB Box tag."))

        PB_KEEP.value = pb_keep_active
        if (checkbox(PB_KEEP, (text = "Keep tool armed"))) {
            pb_keep_active = PB_KEEP.value
        }

        separator(PB_SEP_2)
        var selected_size = float3(1.0f, 1.0f, 1.0f)
        if (SelectedBoxSize(selected_size)) {
            var size_changed = false
            unsafe {
                size_changed = edit_drag_float3(addr(selected_size),
                                                (id = "pb_selected_size", text = "Selected box size (m)", speed = 0.05f))
            }
            if (size_changed) {
                selected_size = float3(max(selected_size.x, MIN_SIZE), max(selected_size.y, MIN_SIZE),
                                       max(selected_size.z, MIN_SIZE))
                update_entity(selected_eid) @(eid, cmp) {
                    cmp |> set("meshScale", MeshScale(scale = selected_size))
                }
                commit()
            }
        } else {
            text(PB_NO_SELECTION, (text = "Select a PB Box to edit its size here."))
        }

        separator(PB_SEP_3)
        text(PB_MATERIAL, (text = "Material: {GRID_MATERIAL_PATH}"))
        text(PB_HINT_1, (text = "Click/drag starts at the surface hit by the camera ray; Z=0 ground is the fallback."))
        text(PB_HINT_2, (text = "B toggles the tool, Esc cancels."))
        if (!empty(pb_status)) {
            text(PB_STATUS, (text = pb_status))
        }
    }
}
