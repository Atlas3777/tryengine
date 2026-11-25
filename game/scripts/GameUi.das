options gen2
options relaxed_assign = false

module GameUi shared public

//! The game's screens and the boot/restore flow.
//!
//! The engine has no screen registry: a screen is a function pointer plus the flat asset map of
//! its bundle (`Ui::ScreenRef`). This module is the manifest that can see every screen; it owns
//! the initial menu request and the hot-reload restore, and it draws the stack. Screens push
//! each other explicitly (the menu loads the HUD, the HUD opens pause/shop).

require tryengine/AssetMap
require tryengine/AsyncTaskManager
require tryengine/Loader
require tryengine/Ui
require tryResources
require trygame/GameScreens
require trygame/ScreenHud
require trygame/ScreenMainMenu
require trygame/ScreenPause
require trygame/ScreenShop
require tryUi

//! Boot: load the menu bundle (its parent is the shared UI bundle with the fonts) and the fixed
//! base UI material, then show the menu with its aliases.
def public Start() {
    if (Loader::IsActive()) {
        Loader::Abort()
    }
    var request <- LoaderRequest()
    request.AddBundle(ScreenMainMenu::kMenuBundle)
    // The base UI material is engine-owned: its path is a C++ constant and its id is the
    // authored path id, so it is added as a ready resource instead of a document.
    request.AddResource("material", AssetPathId(UiMaterialPath()))
    StartTask(Loader::Load(request, @@OnBootLoaded))
}

def private OnBootLoaded(maps : table<string; AssetMap>) {
    GameScreens::Show(Ui::ScreenRef(build = @@ScreenMainMenu::Draw, assets = Loader::MapOf(maps, ScreenMainMenu::kMenuBundle)))
}

//! Hot reload: the world may survive, the screen stack and its asset maps do not. Reload the
//! in-game bundles and rebuild the HUD with fresh aliases.
def public Restore(scene_ready : bool) {
    if (!scene_ready) {
        Start()
        return
    }
    if (Loader::IsActive()) {
        Loader::Abort()
    }
    var request <- LoaderRequest()
    request.AddBundle(ScreenHud::kHudBundle)
    request.AddBundle(ScreenPause::kPauseBundle)
    request.AddBundle(ScreenShop::kShopBundle)
    // The base material is engine-owned and the previous request's hold was dropped by the
    // Abort that preceded this reload: re-request it together with the screen bundles.
    request.AddResource("material", AssetPathId(UiMaterialPath()))
    StartTask(Loader::Load(request, @@OnGameLoaded))
}

def private OnGameLoaded(maps : table<string; AssetMap>) {
    ScreenHud::Show(maps)
}

//! One UI frame: keep the pause layer in sync with the clock, then draw the stack. Call inside
//! the game's `GameUiFrame`, not from gameplay.
def public Frame() {
    if (GameScreens::Has(@@ScreenHud::Draw)) {
        ScreenHud::SyncPause()
    }
    Ui::Frame() {
        GameScreens::Frame()
    }
}
