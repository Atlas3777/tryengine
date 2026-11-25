options gen2
options relaxed_assign = false

module HotReload shared public

//! Editor hot reload: the one piece of state a serializer cannot carry.
//!
//! The editor asks for a reload through RequestScriptReload() (button / F5). The actual
//! recompilation runs in the C++ frame loop after EditorUpdate returns, because a context
//! cannot be replaced while one of its functions is still on the stack.
//!
//! Everything that can be serialized is restored by the engine without code here:
//! * module globals marked `@live` (the clocks and their tags, the active scene, the Play
//!   snapshot, editor flags, player look, HUD state, physics attachment) carry themselves;
//! * `tryengine/LiveEcs` puts the DECS world and the model pointers back.
//!
//! What is left is the game's screen stack: a `ScreenRef` is a function pointer plus an
//! `AssetMap` handle, and neither survives a new script generation. This module captures the
//! two flags needed to decide which screen belongs on top before the swap and rebuilds the
//! stack after it. Both sides go through the same `tryLive` store as `@live`.

require TryEditor
require tryeditor/Editor
require tryeditor/EditorHotkeys
require trygame/Game
require trygame/GameTime
require trygame/GameUi
require trygame/UiTheme
require tryengine/Input
require tryengine/Live
require tryengine/Physics
require tryengine/Ui
require daslib/archive

//! The part of the session the coordinator has to read while the old context is still alive.
struct private ReloadUiState {
    is_playing : bool
    game_ready : bool
}

let private kReloadUiKey : string = "__tryeditor_reload_ui"

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
def private SaveReloadUiState() {
    var state : ReloadUiState
    state.is_playing = is_playing
    state.game_ready = Game::IsReady()
    let data <- mem_archive_save(state)
    live_save(kReloadUiKey, data)
}

[after_reload]
def private RestoreReloadUiState() {
    var data : array<uint8>
    if (!live_load(kReloadUiKey, data) || length(data) == 0) {
        print("[RELOAD] no saved session; the game UI starts from the menu\n")
        return
    }

    var state : ReloadUiState
    if (!mem_archive_load(data, state, canfail = true)) {
        print("[RELOAD] saved session cannot be read; the game UI starts from the menu\n")
        return
    }

    if (!state.is_playing) {
        // Edit mode declares no game UI; the next Play calls GameStart and starts from the menu.
        return
    }
    if (!state.game_ready) {
        // Play was requested but the loader had not finished: there is no world to resume, so
        // start the game flow again on the new context.
        GameStart()
        return
    }

    // The engine restored the clocks and the physics attachment through `@live`; these calls
    // are idempotent fallbacks for a store written by a build that predates them.
    GameTime::EnsureGameTime()
    Physics::Attach(game_world_clock)
    // The theme is game policy, not engine state: re-install it before the screens resolve
    // their font and material keys.
    Ui::SetTheme(UiTheme::kTheme)
    // The screen stack and its asset maps are gone; rebuild them through the loader. The
    // world, the scene and the HUD values are already back.
    GameUi::Restore(true)
}
