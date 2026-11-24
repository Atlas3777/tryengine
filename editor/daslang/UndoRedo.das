options gen2
options relaxed_assign = false

module UndoRedo shared public

//! Editor undo/redo, entity deletion and the entity clipboard.
//!
//! One history holds three kinds of operation:
//!
//! * `KindEdit` — the component values of a single entity, captured before and after. The
//!   Inspector records one per frame (a widget drag is coalesced while it stays active) and
//!   the gizmo records one per drag, so every value the editor can touch is covered by one
//!   mechanism: adding a component, removing it, and editing a field inside it are all just
//!   "the entity's components changed".
//! * `KindCreate` — an entity subtree that appeared (New Entity, pasted or dropped prefab,
//!   ProBuilder box). Undo deletes it, redo spawns it back.
//! * `KindDelete` — an entity subtree that went away. Undo spawns it back and re-attaches it
//!   to the parent and the sibling slot it had, redo deletes it again.
//!
//! ## Snapshot form
//!
//! A component value is stored as the ConfigFormat text the document format writes, one
//! field per component (`transform: Transform = { ... }`). Scenes and prefabs already round
//! trip through that format, so an undo restores exactly what a save/load would; the text is
//! also the one form that holds a `string` value by value, where a raw byte copy of a
//! component would keep a pointer into the script heap and dangle after a collection.
//! Components the scene does not save (`relationship`, `_world`, `physicsBody`, the
//! mesh/material pointers) are runtime state and are never part of a snapshot: `_world` is
//! recomposed after every restore.
//!
//! ## Entity ids
//!
//! DECS hands out a fresh id on every create, so an entity that was deleted and spawned back
//! by an undo is a *different* `EntityId`. History entries and the selection keep the ids
//! that were current when they were recorded, and `g_alias` maps an old id to the id the
//! subtree was rebuilt with; every entry point resolves its ids through that table. The
//! re-spawned subtree reports its ids in node order, so a whole subtree is remapped at once.

require strings
require math
require imgui
require daslib/decs
require daslib/decs_boost
require daslib/strings_boost
require tryengine/ConfigFormat
require tryengine/ConfigFormatBoost
require tryengine/Content
require tryengine/EcsMacro
require tryengine/Model
require tryengine/Prefab
require tryengine/Serializable
require tryengine/TransformSystem
require tryengine/Types

enum public UndoKind {
    KindEdit
    KindCreate
    KindDelete
}

//! One component value of a snapshot, as ConfigFormat text. `name` is the DECS component
//! name (template fields included: `Camera_fov` is a component of its own).
struct public UndoField {
    name : string
    text : string
}

//! The saved components of one entity.
struct public UndoEntityState {
    eid    : EntityId
    fields : array<UndoField>
}

struct public UndoOperation {
    kind   : UndoKind
    title  : string
    // KindEdit: the same entity before and after the change.
    before : UndoEntityState
    after  : UndoEntityState
    // KindCreate/KindDelete: the subtree as prefab text plus what re-spawning it needs.
    // `eids` is in node order and is rewritten to the new ids after every re-spawn.
    text   : string
    root   : EntityId
    eids   : array<EntityId>
    parent : EntityId
    prev   : EntityId
    next   : EntityId
    local  : Transform = Transform()
}

//! Entries kept before the oldest is dropped.
let private MAX_HISTORY : int = 64
//! Offset of the n-th paste of the same copy, so a copy is visible where it lands.
let private PASTE_OFFSET : float = 0.5f
//! A parent/sibling chain this deep is a cycle in the links, not a tree.
let private MAX_SUBTREE_STEPS : int = 65536

var private g_history : array<UndoOperation>
//! Number of applied operations: `g_history[g_cursor]` is the first redo entry.
var private g_cursor : int = 0
//! Old entity key -> the id the subtree was rebuilt with.
var private g_alias : table<uint64; EntityId>

var private g_suspended : bool = false

// Capture slots of the two edit gestures. The panel slot is captured and compared inside one
// ImGui frame; the gesture slot spans frames (a gizmo drag) and must not be overwritten by
// the panel that also runs every frame.
var private g_panel_before : UndoEntityState
var private g_panel_eid : EntityId = INVALID_ENTITY_ID
var private g_gesture_before : UndoEntityState
var private g_gesture_eid : EntityId = INVALID_ENTITY_ID

// Decoded values of one restore. `update_entity` takes a block, and a block may not capture
// local arrays, so the block reads these module-level buffers instead.
var private g_apply_names : array<string>
var private g_apply_values : array<StructField?>
var private g_apply_remove : array<string>

// Clipboard: prefab text of the copied subtree and the world position of its root.
var private g_clipboard_text : string
var private g_clipboard_origin : float3 = float3(0.0f, 0.0f, 0.0f)
var private g_clipboard_pastes : int = 0

// ---------------------------------------------------------------------------
// Entity ids and the local helpers
// ---------------------------------------------------------------------------

//! Key of an entity: id in the low half, generation in the high half. Both fields matter (a
//! slot is reused with a new generation), and arithmetic is what the JIT can emit: a
//! `reinterpret` of the whole struct is not supported by the LLVM emitter.
def private EntityKey(eid : EntityId) : uint64 {
    return uint64(eid.id) | (uint64(uint(eid.generation)) << 32)
}

//! Maps a recorded id to the id the entity actually has now. A subtree restored from text is
//! re-created with fresh ids, and every entry recorded before the deletion is rewritten the
//! same way.
def public ResolveEid(eid : EntityId) : EntityId {
    if (eid == INVALID_ENTITY_ID) {
        return INVALID_ENTITY_ID
    }
    var current = eid
    var depth = 0
    while (depth < 32) {
        let key = EntityKey(current)
        if (!key_exists(g_alias, key)) {
            break
        }
        let next = g_alias[key]
        if (next == current) {
            break
        }
        current = next
        depth++
    }
    return current
}

def private RegisterAlias(old_eid : EntityId; new_eid : EntityId) {
    if (old_eid == INVALID_ENTITY_ID || new_eid == INVALID_ENTITY_ID) {
        return
    }
    g_alias[EntityKey(old_eid)] = new_eid
}

def private GetEntityLocation(eid : EntityId; var out_arch_idx : int&; var out_index : int64&) : bool {
    if (eid == INVALID_ENTITY_ID || eid.id >= uint(length(decsState.entityLookup))) {
        return false
    }
    let rec = decsState.entityLookup[int(eid.id)]
    if (rec.generation != eid.generation) {
        return false
    }
    if (!key_exists(decsState.archetypeLookup, rec.archetype)) {
        return false
    }
    let found = decsState.archetypeLookup[rec.archetype]
    if (found == 0) {
        return false
    }
    out_arch_idx = found - 1
    out_index = rec.index
    return true
}

def private ReadRelationship(eid : EntityId) : Relationship {
    var rel = Relationship()
    query(eid) $(relationship : Relationship) {
        rel = relationship
    }
    return rel
}

def private WriteRelationship(eid : EntityId; rel : Relationship) {
    update_entity(eid) @(entity : EntityId; var cmp : ComponentMap) {
        cmp |> set(RELATIONSHIP_FIELD, rel)
    }
    commit()
}

def private ReadTransform(eid : EntityId) : Transform {
    var value = Transform()
    query(eid) $(transform : Transform) {
        value = transform
    }
    return value
}

def private HasTransform(eid : EntityId) : bool {
    var found = false
    query(eid) $(transform : Transform) {
        found = true
    }
    return found
}

def private WorldPosition(eid : EntityId) : float3 {
    var position = float3(0.0f, 0.0f, 0.0f)
    query(eid) $(_world : Transform) {
        position = _world.position
    }
    return position
}

def private EntityTag(eid : EntityId) : string {
    var tag = ""
    query(eid) $(value : string) {
        tag = value
    }
    return tag
}

def private ArchetypeHasComponent(arch_idx : int; name : string) : bool {
    if (arch_idx < 0 || arch_idx >= length(decsState.allArchetypes)) {
        return false
    }
    return decs::has(decsState.allArchetypes[arch_idx], name)
}

//! The editor camera is editor state, not scene content: deleting it would leave the
//! viewport without a camera until the scene is reloaded.
def private IsEditorCamera(eid : EntityId) : bool {
    var arch_idx = -1
    var row : int64 = 0l
    if (!GetEntityLocation(eid, arch_idx, row)) {
        return false
    }
    return ArchetypeHasComponent(arch_idx, "editorCameraTag")
}

// Pre-order: the entity, then its children in sibling order. The same order
// `Prefab::BuildPrefabTextFromEntity` writes and `SpawnPrefabTemplate` reports ids in, which
// is what makes the whole-subtree id remap possible.
def private CollectSubtree(eid : EntityId; var order : array<EntityId>; var visited : table<uint; bool>) {
    if (eid == INVALID_ENTITY_ID || !decs::is_alive(eid) || key_exists(visited, eid.id)) {
        return
    }
    visited[eid.id] = true
    order |> push(eid)

    var child = INVALID_ENTITY_ID
    query(eid) $(relationship : Relationship) {
        child = relationship.first_child
    }

    var steps = 0
    while (child != INVALID_ENTITY_ID && !key_exists(visited, child.id) && steps < MAX_SUBTREE_STEPS) {
        if (!decs::is_alive(child)) {
            break
        }
        CollectSubtree(child, order, visited)
        var next = INVALID_ENTITY_ID
        query(child) $(relationship : Relationship) {
            next = relationship.next
        }
        child = next
        steps++
    }
}

//! Detaches one entity from its parent's child list and from its siblings, keeping the rest
//! of the list intact.
def private DetachFromParent(eid : EntityId) {
    let rel = ReadRelationship(eid)
    if (rel.prev != INVALID_ENTITY_ID && decs::is_alive(rel.prev)) {
        var prev_rel = ReadRelationship(rel.prev)
        prev_rel.next = rel.next
        WriteRelationship(rel.prev, prev_rel)
    } elif (rel.parent != INVALID_ENTITY_ID && decs::is_alive(rel.parent)) {
        var parent_rel = ReadRelationship(rel.parent)
        if (parent_rel.first_child == eid) {
            parent_rel.first_child = rel.next
        }
        WriteRelationship(rel.parent, parent_rel)
    }
    if (rel.next != INVALID_ENTITY_ID && decs::is_alive(rel.next)) {
        var next_rel = ReadRelationship(rel.next)
        next_rel.prev = rel.prev
        WriteRelationship(rel.next, next_rel)
    }
}

//! Puts a re-spawned subtree root back where it was: under the same parent, at the same
//! position in the sibling list. A parent or sibling that is gone (or was itself deleted
//! later) simply leaves the entity a root.
def private AttachToRecordedPlace(eid : EntityId; parent : EntityId; prev : EntityId; next : EntityId) {
    let resolved_parent = ResolveEid(parent)
    let resolved_prev = ResolveEid(prev)
    let resolved_next = ResolveEid(next)
    let parent_alive = decs::is_alive(resolved_parent)
    let prev_alive = decs::is_alive(resolved_prev)
    let next_alive = decs::is_alive(resolved_next)

    if (!parent_alive && !prev_alive && !next_alive) {
        return
    }

    var rel = Relationship()
    if (parent_alive) {
        rel.parent = resolved_parent
    }
    if (prev_alive) {
        rel.prev = resolved_prev
    }
    if (next_alive) {
        rel.next = resolved_next
    }
    WriteRelationship(eid, rel)

    if (prev_alive) {
        var prev_rel = ReadRelationship(resolved_prev)
        prev_rel.next = eid
        WriteRelationship(resolved_prev, prev_rel)
    } elif (parent_alive) {
        var parent_rel = ReadRelationship(resolved_parent)
        parent_rel.first_child = eid
        WriteRelationship(resolved_parent, parent_rel)
    }
    if (next_alive) {
        var next_rel = ReadRelationship(resolved_next)
        next_rel.prev = eid
        WriteRelationship(resolved_next, next_rel)
    }
}

// ---------------------------------------------------------------------------
// Snapshot: entity components <-> ConfigFormat text
// ---------------------------------------------------------------------------

def private EncodeComponent(name : string; var value : StructField?) : string {
    value.name = name
    var obj : table<string; StructField?>
    obj[name] = value
    return write_config(CF(obj))
}

def private DecodeComponent(text : string; name : string) : StructField? {
    var error : string
    var root = read_config(text, error)
    if (root == null || !empty(error) || !(root.value is _object)) {
        print("[UNDO] cannot read the stored value of component '{name}': {error}\n")
        return null
    }
    assume obj = root.value as _object
    return obj?[name] ?? null
}

def private IndexOfField(state : UndoEntityState; name : string) : int {
    for (i in range(length(state.fields))) {
        if (state.fields[i].name == name) {
            return i
        }
    }
    return -1
}

//! The saved components of one entity. Reads the archetype row directly: the values are
//! written in place by the Inspector widgets, so there is no other party to ask.
def public CaptureEntity(eid : EntityId) : UndoEntityState {
    var state : UndoEntityState
    state.eid = eid

    var arch_idx = -1
    var row : int64 = 0l
    if (!GetEntityLocation(eid, arch_idx, row)) {
        return <- state
    }

    for (comp in decsState.allArchetypes[arch_idx].components) {
        // `eid` is structural, pointers are runtime references, and `EcsShouldSave` is the
        // same declaration the scene format asks.
        if (comp.name == "eid" || comp.info.basicType == Type.tPointer || !EcsShouldSave(comp.name)) {
            continue
        }
        var value = DecsComponentToCF(comp, row)
        if (value == null) {
            continue
        }
        var field : UndoField
        field.name = comp.name
        field.text = EncodeComponent(comp.name, value)
        state.fields |> push(field)
    }
    return <- state
}

def public SameEntityState(a : UndoEntityState; b : UndoEntityState) : bool {
    if (length(a.fields) != length(b.fields)) {
        return false
    }
    for (field in a.fields) {
        let index = IndexOfField(b, field.name)
        if (index < 0) {
            return false
        }
        if (b.fields[index].text != field.text) {
            return false
        }
    }
    return true
}

def private ComponentTitles(before : UndoEntityState; after : UndoEntityState) : string {
    var added = ""
    var removed = ""
    var changed = ""
    for (field in after.fields) {
        let index = IndexOfField(before, field.name)
        if (index < 0) {
            added = field.name
        } elif (before.fields[index].text != field.text) {
            changed = field.name
        }
    }
    for (field in before.fields) {
        if (IndexOfField(after, field.name) < 0) {
            removed = field.name
        }
    }
    if (!empty(added)) {
        return "Add {added}"
    }
    if (!empty(removed)) {
        return "Remove {removed}"
    }
    if (!empty(changed)) {
        return "Change {changed}"
    }
    return "Edit"
}

//! Writes the snapshot back: components it does not name are removed (an undo of "Add
//! component"), the rest are set. Ids of listed components are updated as well, so the
//! entity's row can move to another archetype.
def public RestoreEntityState(state : UndoEntityState) : EntityId {
    let eid = ResolveEid(state.eid)
    if (!decs::is_alive(eid)) {
        return INVALID_ENTITY_ID
    }

    g_apply_names |> clear()
    g_apply_values |> clear()
    g_apply_remove |> clear()

    var arch_idx = -1
    var row : int64 = 0l
    if (GetEntityLocation(eid, arch_idx, row)) {
        for (comp in decsState.allArchetypes[arch_idx].components) {
            if (comp.name == "eid" || comp.info.basicType == Type.tPointer || !EcsShouldSave(comp.name)) {
                continue
            }
            if (IndexOfField(state, comp.name) < 0) {
                g_apply_remove |> push(comp.name)
            }
        }
    }

    for (field in state.fields) {
        var value = DecodeComponent(field.text, field.name)
        if (value == null) {
            continue
        }
        g_apply_names |> push(field.name)
        g_apply_values |> push(value)
    }

    update_entity(eid) @(entity : EntityId; var cmp : ComponentMap) {
        for (name in g_apply_remove) {
            decs::remove(cmp, name)
        }
        for (i in range(length(g_apply_names))) {
            let value = g_apply_values[i]
            ApplyDecsConfigField(cmp, g_apply_names[i], value, value != null ? value.typeName : "")
        }
    }
    commit()

    // `_world` is derived from the authored transforms and the runtime pointers are resolved
    // from the authored `model`, so neither is stored in the snapshot.
    UpdateWorldTransforms()
    Model::ApplyToEntity(eid, false)
    return eid
}

// ---------------------------------------------------------------------------
// History
// ---------------------------------------------------------------------------

def public SetSuspended(value : bool) {
    if (g_suspended == value) {
        return
    }
    g_suspended = value
    g_panel_eid = INVALID_ENTITY_ID
    g_gesture_eid = INVALID_ENTITY_ID
}

def public IsSuspended() : bool => g_suspended

def private CanRecord() : bool => !g_suspended

def private ResetState(var state : UndoEntityState) {
    state.eid = INVALID_ENTITY_ID
    state.fields |> clear()
}

def public ClearHistory() {
    g_history |> clear()
    g_cursor = 0
    g_alias |> clear()
    g_panel_eid = INVALID_ENTITY_ID
    g_gesture_eid = INVALID_ENTITY_ID
    ResetState(g_panel_before)
    ResetState(g_gesture_before)
    g_clipboard_text = ""
    g_clipboard_pastes = 0
}

def public CanUndo() : bool => !g_suspended && g_cursor > 0

def public CanRedo() : bool => !g_suspended && g_cursor < length(g_history)

def public UndoTitle() : string => CanUndo() ? g_history[g_cursor - 1].title : ""

def public RedoTitle() : string => CanRedo() ? g_history[g_cursor].title : ""

def public HistorySize() : int => length(g_history)

// A snapshot owns arrays, so it is a move-only value in daslang: every hand-over below is a
// move (`<-`), and nothing is copied.
def private PushOperation(var op : UndoOperation) {
    // A new operation after an undo drops the redo tail.
    if (g_cursor < length(g_history)) {
        resize(g_history, g_cursor)
    }
    if (length(g_history) >= MAX_HISTORY) {
        g_history |> erase(0)
        if (g_cursor > 0) {
            g_cursor--
        }
    }
    g_history |> emplace(op)
    g_cursor = length(g_history)
}

//! Pushes one "the components of this entity changed" step. Identical snapshots are not an
//! operation at all: the Inspector captures every frame, and only a real change is recorded.
//! `coalesce` merges the step into the previous one while a widget stays active, so dragging
//! a slider or typing in a field is one undo step, not one per frame. Both states are moved
//! out of the caller.
def public PushEdit(var before : UndoEntityState; var after : UndoEntityState; coalesce : bool) : bool {
    if (!CanRecord()) {
        return false
    }
    if (SameEntityState(before, after)) {
        return false
    }

    let eid = ResolveEid(before.eid)
    if (coalesce && g_cursor > 0 && g_cursor == length(g_history)) {
        unsafe {
            var top & = g_history[g_cursor - 1]
            if (top.kind == UndoKind.KindEdit && top.after.eid == eid && before.eid == eid) {
                top.title = ComponentTitles(top.before, after)
                top.after <- after
                return true
            }
        }
    }

    var op <- UndoOperation()
    op.kind = UndoKind.KindEdit
    op.title = ComponentTitles(before, after)
    op.before <- before
    op.after <- after
    op.before.eid = eid
    op.after.eid = eid
    PushOperation(op)
    return true
}

// ---------------------------------------------------------------------------
// Edit gestures
// ---------------------------------------------------------------------------

//! Starts recording the components of one entity for a change that spans frames (a gizmo
//! drag). The earlier capture is replaced.
def public BeginEdit(eid : EntityId) {
    g_gesture_eid = INVALID_ENTITY_ID
    if (!CanRecord()) {
        return
    }
    let target = ResolveEid(eid)
    if (!decs::is_alive(target)) {
        return
    }
    g_gesture_before <- CaptureEntity(target)
    g_gesture_eid = target
}

//! Ends a gesture: records one step when anything changed.
def public EndEdit() {
    if (g_gesture_eid == INVALID_ENTITY_ID) {
        return
    }
    let target = g_gesture_eid
    g_gesture_eid = INVALID_ENTITY_ID
    var after <- CaptureEntity(target)
    PushEdit(g_gesture_before, after, false)
    ResetState(g_gesture_before)
}

def public CancelEdit() {
    g_gesture_eid = INVALID_ENTITY_ID
    ResetState(g_gesture_before)
}

//! Starts recording the components of one entity for a whole ImGui panel. The Inspector draws
//! every component a widget can write, so one capture around the panel body covers adding,
//! removing and editing a value, including the material picker.
def public BeginPanelEdit(eid : EntityId) {
    g_panel_eid = INVALID_ENTITY_ID
    if (!CanRecord()) {
        return
    }
    let target = ResolveEid(eid)
    if (!decs::is_alive(target)) {
        return
    }
    g_panel_before <- CaptureEntity(target)
    g_panel_eid = target
}

//! Ends a panel capture. Must be called from inside the ImGui frame: the coalescing rule asks
//! whether a widget is still being dragged or typed into.
def public EndPanelEdit() {
    if (g_panel_eid == INVALID_ENTITY_ID) {
        return
    }
    let target = g_panel_eid
    g_panel_eid = INVALID_ENTITY_ID
    let active = IsAnyItemActive()
    var after <- CaptureEntity(target)
    PushEdit(g_panel_before, after, active)
    ResetState(g_panel_before)
}

// ---------------------------------------------------------------------------
// Entity create and delete
// ---------------------------------------------------------------------------

def private RemoveSubtree(root : EntityId) {
    let target = ResolveEid(root)
    if (!decs::is_alive(target)) {
        return
    }
    var order : array<EntityId>
    var visited : table<uint; bool>
    CollectSubtree(target, order, visited)

    DetachFromParent(target)

    for (eid in order) {
        decs::delete_entity(eid)
    }
    commit()
}

//! Records an entity subtree that was just created. The subtree becomes prefab text, which
//! is what redo spawns again; the entity keeps its id until it is deleted by an undo.
def public RecordCreated(eid : EntityId; title : string) {
    if (!CanRecord()) {
        return
    }
    let target = ResolveEid(eid)
    if (!decs::is_alive(target)) {
        return
    }

    var text = BuildPrefabTextFromEntity(target, title)
    if (empty(text)) {
        return
    }

    var op <- UndoOperation()
    op.kind = UndoKind.KindCreate
    op.title = title
    op.text = text
    op.root = target
    op.local = ReadTransform(target)
    var order : array<EntityId>
    var visited : table<uint; bool>
    CollectSubtree(target, order, visited)
    op.eids <- order
    let rel = ReadRelationship(target)
    op.parent = rel.parent
    op.prev = rel.prev
    op.next = rel.next
    PushOperation(op)
}

//! Deletes the entity and its subtree as one undoable operation. Returns false when there is
//! nothing to delete or the entity is editor state.
def public DeleteSubtree(eid : EntityId) : bool {
    if (!CanRecord()) {
        return false
    }
    let target = ResolveEid(eid)
    if (!decs::is_alive(target)) {
        return false
    }
    if (IsEditorCamera(target)) {
        print("[UNDO] the editor camera cannot be deleted\n")
        return false
    }

    var order : array<EntityId>
    var visited : table<uint; bool>
    CollectSubtree(target, order, visited)

    let tag = EntityTag(target)
    let title = empty(tag) ? "Delete entity" : "Delete '{tag}'"
    var text = BuildPrefabTextFromEntity(target, title)
    if (empty(text)) {
        print("[UNDO] entity {target.id:d} has no state to restore; not deleted\n")
        return false
    }

    var op <- UndoOperation()
    op.kind = UndoKind.KindDelete
    op.title = title
    op.text = text
    op.root = target
    op.local = ReadTransform(target)
    op.eids <- order
    let rel = ReadRelationship(target)
    op.parent = rel.parent
    op.prev = rel.prev
    op.next = rel.next
    PushOperation(op)

    RemoveSubtree(target)
    return true
}

//! Re-creates an operation's subtree from its text, remaps the recorded ids to the new ones
//! and puts the root back under the parent and into the sibling slot it had.
def private SpawnOperationSubtree(var op : UndoOperation) : EntityId {
    var document = Content::ParseFor(DocumentKind.Prefab, "undo.prefab", op.text)
    if (document == null) {
        return INVALID_ENTITY_ID
    }
    var data = CompilePrefab(document)
    if (data == null) {
        return INVALID_ENTITY_ID
    }

    // The compiled template stays a local: the strings it bakes into the components are kept
    // alive by the archetype itself (EngineCollectGarbage roots string components), which is
    // what lets the template be dropped right after the spawn.
    var ids : array<EntityId>
    let root = SpawnPrefabTemplateIds(data, float3(0.0f, 0.0f, 0.0f), ids)
    if (root == INVALID_ENTITY_ID) {
        print("[UNDO] the stored entity could not be spawned back\n")
        return INVALID_ENTITY_ID
    }

    // The text stores the root position rebased to zero, so the authored local transform is
    // written back before the links are restored.
    if (HasTransform(root)) {
        let authored = op.local
        update_entity(root) @(entity : EntityId; var cmp : ComponentMap) {
            cmp |> set(TRANSFORM_FIELD, authored)
        }
        commit()
    }

    AttachToRecordedPlace(root, op.parent, op.prev, op.next)

    // Recorded ids -> ids this spawn got, so entries older than the deletion still resolve.
    for (i in range(length(ids))) {
        if (i < length(op.eids)) {
            RegisterAlias(op.eids[i], ids[i])
        }
    }
    op.eids <- ids
    op.root = root

    UpdateWorldTransforms()
    return root
}

// ---------------------------------------------------------------------------
// Undo / redo
// ---------------------------------------------------------------------------

//! Reverts the last operation and returns the entity the selection should move to, or
//! INVALID_ENTITY_ID when nothing (or an entity that no longer exists) is affected.
def public Undo() : EntityId {
    if (!CanUndo()) {
        return INVALID_ENTITY_ID
    }
    g_cursor--
    unsafe {
        var op & = g_history[g_cursor]
        if (op.kind == UndoKind.KindEdit) {
            return RestoreEntityState(op.before)
        }
        if (op.kind == UndoKind.KindCreate) {
            RemoveSubtree(op.root)
            return INVALID_ENTITY_ID
        }
        return SpawnOperationSubtree(op)
    }
}

//! Re-applies the next operation and returns the entity to select.
def public Redo() : EntityId {
    if (!CanRedo()) {
        return INVALID_ENTITY_ID
    }
    var focus = INVALID_ENTITY_ID
    unsafe {
        var op & = g_history[g_cursor]
        if (op.kind == UndoKind.KindEdit) {
            focus = RestoreEntityState(op.after)
        } elif (op.kind == UndoKind.KindCreate || op.kind == UndoKind.KindDelete) {
            if (op.kind == UndoKind.KindDelete) {
                RemoveSubtree(op.root)
            } else {
                focus = SpawnOperationSubtree(op)
            }
        }
    }
    g_cursor++
    return focus
}

// ---------------------------------------------------------------------------
// Clipboard
// ---------------------------------------------------------------------------

def public HasClipboard() : bool => !empty(g_clipboard_text)

//! Copies the entity subtree to the clipboard. The clipboard is prefab text, so it is the
//! same document format a saved prefab uses, including nested children.
def public CopyEntity(eid : EntityId) : bool {
    let target = ResolveEid(eid)
    if (!decs::is_alive(target)) {
        return false
    }
    var text = BuildPrefabTextFromEntity(target, EntityTag(target))
    if (empty(text)) {
        return false
    }
    g_clipboard_text = text
    g_clipboard_origin = WorldPosition(target)
    g_clipboard_pastes = 0
    return true
}

//! Spawns a copy of the clipboard at the copied position and records it as a creation, so one
//! Ctrl+Z removes the paste. Returns the new root, or INVALID_ENTITY_ID.
def public PasteClipboard() : EntityId {
    if (!CanRecord() || empty(g_clipboard_text)) {
        return INVALID_ENTITY_ID
    }

    var document = Content::ParseFor(DocumentKind.Prefab, "clipboard.prefab", g_clipboard_text)
    if (document == null) {
        return INVALID_ENTITY_ID
    }
    var data = CompilePrefab(document)
    if (data == null) {
        return INVALID_ENTITY_ID
    }

    // Every paste is shifted a little, so a copy is visible instead of hiding inside its
    // source, and the n-th paste of one copy does not land on the previous one.
    let origin = g_clipboard_origin + float3(PASTE_OFFSET * float(g_clipboard_pastes + 1), 0.0f,
                                             PASTE_OFFSET * float(g_clipboard_pastes + 1))
    g_clipboard_pastes++

    let root = SpawnPrefabTemplate(data, origin)
    if (root == INVALID_ENTITY_ID) {
        return INVALID_ENTITY_ID
    }

    UpdateWorldTransforms()
    RecordCreated(root, "Paste")
    return root
}
