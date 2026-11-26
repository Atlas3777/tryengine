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
require tryengine/EcsMacro
require live/live_vars
require live_host
require tryengine/Loader
require tryengine/Model
require tryengine/Physics
require tryengine/Scene
require tryengine/Time
require trygame/Game
require trygame/Player
require tryeditor/Editor
require tryeditor/Hierarchy
require tryeditor/UndoRedo

// The DECS heap is the scene: entities, components and the edited values. The scene
// state holds what the heap does not: the open path and the resource references the
// entities render through. While Play is running the heap holds the game world and this
// snapshot holds the editor world; `@live` carries the snapshot across a reload so that
// Stop still restores the pre-Play scene without any reload code here.
var private @live g_saved_decs : array<uint8>
var private @live g_saved_scene : SceneState
var private @live g_saved_selected_eid : EntityId = INVALID_ENTITY_ID

def public BeginPlay() {
    // The editor history describes the authored world; the play snapshot replaces the whole
    // heap and Stop restores it, so entries from before Play would describe nothing.
    UndoRedo::ClearHistory()
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

    // The game may be stopped while `Loader::Load` is still running. Abort releases its
    // references; the task carries its own generation and exits on the next frame.
    Loader::Abort()
    Physics::Reset()
    Player::Reset()

    // Release the play scene's references, install the editor scene again, and then
    // restore the heap. The resource cache keeps the meshes/materials: only the
    // reference counts change, so the restored entities still render through them.
    Scene::ApplyState(g_saved_scene)
    decs::restart()
    mem_archive_load(g_saved_decs, decsState)
    EcsApplyRegistry()
    // The heap archive zeroes raw pointers; without this every scene model is invisible
    // until its Inspector call re-applies the pair.
    Model::ReapplyPointers()

    selected_eid = g_saved_selected_eid
    UndoRedo::ClearHistory()
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
// runs here because EditorUpdate already skipped its own call for this frame. The UI is
// declared here too: while paused it only advances when the user steps a frame.
def public StepPlay() {
    TimeStepOnce(1.0f / 60.0f)
    GameUiFrame()
    GameUpdate()
}
