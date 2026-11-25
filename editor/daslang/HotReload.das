options gen2
options relaxed_assign = false

module HotReload shared public

//! Script hot reload: snapshot before the old context dies, restore after the new one starts.
//!
//! The editor asks for a reload through RequestScriptReload() (button / F5). The actual
//! recompilation runs in the C++ frame loop after EditorUpdate returns, because a context
//! cannot be replaced while one of its functions is still on the stack. ScriptSystem calls
//! the annotated hooks in order: [before_reload] in the old context, then the host entry
//! points on the new context, then [after_reload] there.
//!
//! The snapshot stores the world (DECS heap), the open scene plus its resource references,
//! the editor selection, play mode with its saved editor snapshot, and the game state. UI
//! widget state is intentionally not stored: imgui keeps its own docking/layout state and
//! the panel globals start from their defaults.

require TryEditor
require tryeditor/Editor
require tryeditor/EditorHotkeys
require tryeditor/Hierarchy
require tryeditor/PlayMode
require trygame/Game
require tryengine/EcsMacro
require tryengine/Input
require tryengine/Model
require tryengine/Scene
require tryengine/Time
require daslib/archive
require daslib/decs
require daslib/decs_boost

struct HotReloadState {
    decs : array<uint8>
    scene : SceneState
    selected_eid : EntityId
    is_playing : bool
    is_paused : bool
    play : PlayReloadState
    // The game's one flag: a scene is applied and gameplay runs.
    game_ready : bool
}

//! Asks the C++ frame loop to recompile the scripts. Safe to call from a widget or a hotkey:
//! it only raises a flag, the context is swapped after the current EditorUpdate returns.
def public ReloadScripts() {
    if (!RequestScriptReload()) {
        print("[RELOAD] script reload request was rejected\n")
    }
}

[init]
def RegisterReloadHotkey() {
    RegisterHotkey(Key.F5, @@ReloadScripts, "Reload scripts", system = true)
}

[before_reload]
def SaveHotReloadState() {
    var state : HotReloadState
    state.decs <- mem_archive_save(decsState)
    state.scene <- Scene::CaptureState()
    state.selected_eid = selected_eid
    state.is_playing = is_playing
    state.is_paused = is_paused
    state.play <- PlayMode::CaptureReloadState()
    state.game_ready = Game::CaptureReloadState()

    let data <- mem_archive_save(state)
    ScriptReloadStoreBytes(data)
}

[after_reload]
def RestoreHotReloadState() {
    var data : array<uint8>
    if (!ScriptReloadLoadBytes(data)) {
        print("[RELOAD] no saved script state; starting from the default scene\n")
        return
    }

    var state : HotReloadState
    if (!mem_archive_load(data, state, canfail = true)) {
        print("[RELOAD] saved script state cannot be read; starting from the default scene\n")
        return
    }

    // Flags first: PlayMode/Game restore below branches on them.
    is_playing = state.is_playing
    is_paused = state.is_paused

    // ApplyState re-requests the scene resources through the new context; the DECS heap is
    // restored separately, exactly like EndPlay does.
    Scene::ApplyState(state.scene)
    decs::restart()
    if (!mem_archive_load(state.decs, decsState, canfail = true)) {
        print("[RELOAD] DECS heap could not be restored\n")
        return
    }
    EcsApplyRegistry()
    // Same reason as PlayMode::EndPlay: the archive does not carry the mesh/material raw
    // pointers, so they are resolved again from the authored `model`.
    Model::ReapplyPointers()

    selected_eid = state.selected_eid
    PlayMode::ApplyReloadState(state.play)
    Game::ApplyReloadState(state.game_ready)

    if (is_playing && !state.game_ready) {
        // Play was requested but the loader had not finished: there is no world to resume,
        // so start the game flow again on the new context.
        GameStart()
    } elif (is_playing) {
        TimeSetPaused(game_clock, is_paused)
    }
}
