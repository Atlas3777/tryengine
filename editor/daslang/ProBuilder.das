options gen2
options relaxed_assign = false

module ProBuilder shared public

//! ProBuilder-like box tool for the scene viewport.
//!
//! A box is an ordinary entity: a unit-cube mesh scaled to its size, the grid material
//! (`data/materials/grid.mat`, which draws one-metre cells on the faces) and, when "Add box
//! collider" is on, a `boxCollider` sized to the same scale. Placement happens on the Z=0
//! ground plane the editor grid and the asset drop already use:
//!
//! * click — a box of "Default size" whose minimum corner is the clicked grid cell;
//! * drag — the footprint covers every grid cell the cursor crossed, the height is
//!   "Default size" Z.
//!
//! Both are snapped to the grid step when "Snap to grid" is on. The tool consumes the click
//! before `PickingPass` reads it and suspends gizmo interaction while it is armed, so a click
//! never both places a box and selects/grabs something behind it.
//!
//! The collider follows the entity scale: a box scaled with the gizmo or the Inspector gets
//! its half extents rewritten from `Transform.scale / 2`. The marker is the tag `PB Box`,
//! which is saved with the scene, so this also works after a reload.

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
require tryeditor/DebugRender
require tryeditor/EditorInput
require tryeditor/Gizmo
require tryeditor/Hierarchy
require tryeditor/SceneViewPort
require TryEditor
require imgui
require imgui/imgui_widgets_builtin
require imgui/imgui_containers_builtin
require imgui/imgui_boost_v2

// A unit box centred at the origin (24 vertices, 36 indices, MeshBinary v4). Positions are
// in [-0.5, 0.5], so an entity scale is exactly the box size and a collider half extent is
// scale / 2.
let private UNIT_BOX_PATH : string = "data/meshes/unit_box.mesh"
let private GRID_MATERIAL_PATH : string = "game/data/materials/grid.mat"

// The tag doubles as the "this is a ProBuilder box" marker. It is a saved component, so the
// collider sync keeps working after the scene is written and loaded again.
let private BOX_TAG : string = "PB Box"
let private DRAG_PIXELS : float = 4.0f

struct private BoxSpec {
    center : float3 = float3(0.0f, 0.0f, 0.0f)
    size   : float3 = float3(1.0f, 1.0f, 1.0f)
}

struct private BoxDraft {
    active      : bool
    start       : float3
    current     : float3
    start_mouse : float2
    dragging    : bool
}

var private pb_tool_active : bool = false
var private pb_snap_enabled : bool = true
var private pb_snap_step : float = 1.0f
var private pb_default_size : float3 = float3(1.0f, 1.0f, 1.0f)
var private pb_auto_collider : bool = true
var private pb_keep_active : bool = true
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
// Geometry helpers.
// ---------------------------------------------------------------------------

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

def private CellFloor(value : float; step : float) : float => floor(value / step) * step

def private MakeSpec(draft : BoxDraft) : BoxSpec {
    let height = max(pb_default_size.z, 0.01f)
    let default_x = max(pb_default_size.x, 0.01f)
    let default_y = max(pb_default_size.y, 0.01f)

    var spec = BoxSpec()
    spec.size.z = height

    if (pb_snap_enabled && pb_snap_step > 0.0f) {
        let step = pb_snap_step
        let start_x = CellFloor(draft.start.x, step)
        let start_y = CellFloor(draft.start.y, step)
        let end_x = CellFloor(draft.current.x, step)
        let end_y = CellFloor(draft.current.y, step)
        let min_x = min(start_x, end_x)
        let min_y = min(start_y, end_y)

        if (draft.dragging) {
            spec.size.x = max(start_x, end_x) + step - min_x
            spec.size.y = max(start_y, end_y) + step - min_y
        } else {
            spec.size.x = default_x
            spec.size.y = default_y
        }
        spec.center = float3(min_x + spec.size.x * 0.5f, min_y + spec.size.y * 0.5f, height * 0.5f)
    } else {
        if (draft.dragging) {
            spec.size.x = max(abs(draft.current.x - draft.start.x), 0.01f)
            spec.size.y = max(abs(draft.current.y - draft.start.y), 0.01f)
            spec.center = float3(min(draft.start.x, draft.current.x) + spec.size.x * 0.5f,
                                 min(draft.start.y, draft.current.y) + spec.size.y * 0.5f,
                                 height * 0.5f)
        } else {
            spec.size.x = default_x
            spec.size.y = default_y
            spec.center = float3(draft.start.x + default_x * 0.5f, draft.start.y + default_y * 0.5f, height * 0.5f)
        }
    }
    return <- spec
}

def private AddPreviewEdge(a : float3; b : float3) {
    AddLine(a, b, Color.Yellow)
}

def private DrawPreview(spec : BoxSpec) {
    ClearDebugLines()

    let half = spec.size * 0.5f
    let min_corner = spec.center - half
    let max_corner = spec.center + half
    let p000 = float3(min_corner.x, min_corner.y, min_corner.z)
    let p100 = float3(max_corner.x, min_corner.y, min_corner.z)
    let p110 = float3(max_corner.x, max_corner.y, min_corner.z)
    let p010 = float3(min_corner.x, max_corner.y, min_corner.z)
    let p001 = float3(min_corner.x, min_corner.y, max_corner.z)
    let p101 = float3(max_corner.x, min_corner.y, max_corner.z)
    let p111 = float3(max_corner.x, max_corner.y, max_corner.z)
    let p011 = float3(min_corner.x, max_corner.y, max_corner.z)

    AddPreviewEdge(p000, p100)
    AddPreviewEdge(p100, p110)
    AddPreviewEdge(p110, p010)
    AddPreviewEdge(p010, p000)
    AddPreviewEdge(p001, p101)
    AddPreviewEdge(p101, p111)
    AddPreviewEdge(p111, p011)
    AddPreviewEdge(p011, p001)
    AddPreviewEdge(p000, p001)
    AddPreviewEdge(p100, p101)
    AddPreviewEdge(p110, p111)
    AddPreviewEdge(p010, p011)

    g_preview_drawn = true
}

def private ClearPreview() {
    if (g_preview_drawn) {
        ClearDebugLines()
        g_preview_drawn = false
    }
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
    let add_collider = pb_auto_collider

    let new_eid = create_entity() @(eid : EntityId; var cmp : ComponentMap) {
        cmp.tag := BOX_TAG
        ApplyEntityTransform(cmp, center, float4(0.0f, 0.0f, 0.0f, 1.0f), size)
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
    pb_status = "placed {size.x:0.2f} x {size.y:0.2f} x {size.z:0.2f} m box"
    return true
}

def private SameHalfExtents(a : float3; b : float3) : bool => abs(a.x - b.x) <= 1e-4f && abs(a.y - b.y) <= 1e-4f && abs(a.z - b.z) <= 1e-4f

//! Keeps the box collider in sync with the entity scale. `update_entity` copies the row, so
//! it runs only for boxes whose extents actually changed (gizmo drag, Inspector edit).
[profile]
def private SyncBoxColliders() {
    var changed = false
    query() $(eid : EntityId; transform : Transform; boxCollider : BoxCollider; tag : string) {
        if (tag != BOX_TAG) {
            return
        }
        let half = float3(max(abs(transform.scale.x), 0.01f) * 0.5f,
                          max(abs(transform.scale.y), 0.01f) * 0.5f,
                          max(abs(transform.scale.z), 0.01f) * 0.5f)
        if (SameHalfExtents(boxCollider.half_extents, half)) {
            return
        }
        update_entity(eid) @(eid, cmp) {
            cmp |> set("boxCollider", BoxCollider(half_extents = half))
        }
        changed = true
    }
    if (changed) {
        commit()
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
    // spot with no ground hit: selection must not fire behind the placement cursor.
    let wants_click = clicked && IsSceneViewportHovered() && !is_mouse_down(Mouse.Right)
    if (wants_click) {
        ConsumeSceneViewportClick()
    }

    let ray = GizmoRayFromViewport(mouse.pos, viewport)
    var ground = float3(0.0f, 0.0f, 0.0f)
    if (!GroundHit(ray, ground)) {
        if (g_draft.active && !down) {
            FinishDraft()
        }
        return
    }

    if (!g_draft.active) {
        if (wants_click) {
            g_draft = BoxDraft(active = true, start = ground, current = ground, start_mouse = mouse.pos)
            DrawPreview(MakeSpec(g_draft))
        }
        return
    }

    g_draft.current = ground
    g_draft.dragging = length(mouse.pos - g_draft.start_mouse) > DRAG_PIXELS

    if (down) {
        DrawPreview(MakeSpec(g_draft))
        return
    }

    FinishDraft()
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

def private MousePointInViewport(mouse : float2; viewport : float2) : bool => mouse.x >= 0.0f && mouse.y >= 0.0f && mouse.x <= viewport.x && mouse.y <= viewport.y

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
        text(PB_TOOL_STATE, (text = pb_tool_active ? "armed: click or drag in SceneView" : "tool off"))
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
        pb_default_size = float3(max(pb_default_size.x, 0.01f), max(pb_default_size.y, 0.01f),
                                 max(pb_default_size.z, 0.01f))

        PB_AUTO_COLLIDER.value = pb_auto_collider
        if (checkbox(PB_AUTO_COLLIDER, (text = "Add box collider"))) {
            pb_auto_collider = PB_AUTO_COLLIDER.value
        }
        text(PB_COLLIDER_HINT, (text = "The collider follows the box scale while the entity keeps the PB Box tag."))

        PB_KEEP.value = pb_keep_active
        if (checkbox(PB_KEEP, (text = "Keep tool armed"))) {
            pb_keep_active = PB_KEEP.value
        }

        separator(PB_SEP_2)
        text(PB_MATERIAL, (text = "Material: {GRID_MATERIAL_PATH}"))
        text(PB_HINT_1, (text = "Click: one box of the default size; drag: grid-cell footprint."))
        text(PB_HINT_2, (text = "B toggles the tool, Esc cancels. Boxes sit on the Z=0 ground plane."))
        if (!empty(pb_status)) {
            text(PB_STATUS, (text = pb_status))
        }
    }
}
