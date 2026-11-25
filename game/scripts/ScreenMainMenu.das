options gen2
options relaxed_assign = false

module ScreenMainMenu shared public

//! The main menu: one button per scene plus Exit. It is the initial screen; a scene load
//! replaces it with the HUD. `GameUi::Start` shows it after the menu bundle is ready.

require tryengine/AssetMap
require tryengine/Input
require tryengine/Loader
require tryengine/Ui
require trygame/GameState
require trygame/UiTheme

//! Bundle of this screen; the boot request loads the same path (its parent is the shared UI
//! bundle with the fonts).
let public kMenuBundle : string = "data/bundles/main_menu.bundle"

// The menu is the place to edit the scene list: the game only knows how to load one entry.
// `new_scene.scene` is the scene this repository ships; the player entity lives in it, so it
// is the entry that actually starts the game.
let private kNewScene = GameState::SceneRequest(title = "New Scene", scene = "content/new_scene.scene",
                                                prefab = "", physics_demo = false)

def private LoadNewScene() {
    GameState::RequestScene(kNewScene)
}

// Exit is the same quit flag as closing the window: the standalone game ends, the editor closes
// too.
def private ExitGame() {
    RequestQuit()
}

def public Draw(_assets : AssetMap) {
    Ui::Backdrop("##menu_backdrop", UiTheme::kPauseCurtain) {
        Ui::Panel("##menu", UiTheme::kPausePanel, UiLayout(anchor = float2(0.5f, 0.5f), spacing = 12.0f)) {
            Ui::Label("##title", "Main Menu", UiTheme::kTitle)
            Ui::Label("##subtitle", "choose a scene", UiTheme::kMuted)
            Ui::Button(UiTheme::kPrimaryButton, "##scene_new", kNewScene.title, @@LoadNewScene)
            if (Loader::IsActive()) {
                Ui::Label("##loading", "Loading...", UiTheme::kMuted)
            }
            Ui::Button(UiTheme::kQuietButton, "##exit", "Exit", @@ExitGame)
        }
    }
}
