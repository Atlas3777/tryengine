options gen2
options relaxed_assign = false

module PlayMode shared public

//! Editor play mode.
//!
//! Play is not a separate game loop: it snapshots the editor state and then calls the
//! same `GameStart` the standalone game calls. Stop restores the snapshot, including
//! unsaved scene edits. The only thing that differs between the two callers is the
//! content layer, and that difference lives in Assets/Scene/Prefab, not here.

require daslib/archive
require daslib/decs
require tryengine/Loader
require tryengine/Scene
require tryengine/Time
require trygame/Game
require tryeditor/Editor
require tryeditor/Hierarchy

// The DECS heap is the scene: entities, components and the edited values. The scene
// state holds what the heap does not: the open path and the resource references the
// entities render through.
var private g_saved_decs : array<uint8>
var private g_saved_scene : SceneState
var private g_saved_selected_eid : EntityId = INVALID_ENTITY_ID

def public BeginPlay() {
    g_saved_decs <- mem_archive_save(decsState)
    g_saved_scene <- Scene::CaptureState()
    g_saved_selected_eid = selected_eid

    selected_eid = INVALID_ENTITY_ID

    is_playing = true
    is_paused = false

    GameStart()
    TimeReset(game_clock)
}

def public EndPlay() {
    is_playing = false
    is_paused = false
    TimeSetPaused(game_clock, false)

    // The game may be stopped in the middle of its loading step. Drop the text and asset
    // references it held before the editor scene takes over the resource cache again.
    Loader::Abort()

    // Release the play scene's references, install the editor scene again, and then
    // restore the heap. The resource cache keeps the meshes/materials: only the
    // reference counts change, so the restored entities still render through them.
    Scene::ApplyState(g_saved_scene)
    decs::restart()
    mem_archive_load(g_saved_decs, decsState)

    selected_eid = g_saved_selected_eid
}

def public PausePlay() {
    is_paused = true
    TimeSetPaused(game_clock, true)
}

def public ResumePlay() {
    is_paused = false
    TimeSetPaused(game_clock, false)
}

// One synthetic frame at a fixed step: the step ignores the pause flag, and GameUpdate
// runs here because EditorUpdate already skipped its own call for this frame.
def public StepPlay() {
    TimeStepOnce(1.0f / 60.0f)
    GameUpdate()
}
