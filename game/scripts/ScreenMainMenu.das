options gen2
options relaxed_assign = false

module ScreenMainMenu shared public

//! The main menu: one button per scene plus Exit. It is the initial screen; a scene load
//! replaces it with the HUD.

require tryengine/Input
require tryengine/Ui
require trygame/GameFlow
require trygame/UiTheme

// The menu is the place to edit the scene list: `GameFlow` only knows how to load an entry.
// `new_scene.scene` is the scene this repository ships; the player entity lives in it, so it
// is the entry that actually starts the game.
let private kNewScene = SceneEntry(title = "New Scene", scene = "content/new_scene.scene", prefab = "",
                                   physics_demo = false)

def private LoadNewScene() {
    GameFlow::LoadScene(kNewScene)
}

// Exit is the same quit flag as closing the window: the standalone game ends, the editor closes
// too. In Edit-mode preview the action is suppressed, like every other screen action.
def private ExitGame() {
    RequestQuit()
}

def private BuildMainMenu() {
    Ui::Backdrop("##menu_backdrop", UiTheme::kPauseCurtain) {
        Ui::Panel("##menu", UiTheme::kPausePanel, UiLayout(anchor = float2(0.5f, 0.5f), spacing = 12.0f)) {
            Ui::Label("##title", "Main Menu", UiTheme::kTitle)
            Ui::Label("##subtitle", "choose a scene", UiTheme::kMuted)
            Ui::Button(UiTheme::kPrimaryButton, "##scene_new", kNewScene.title, @@LoadNewScene)
            if (GameFlow::IsLoading()) {
                Ui::Label("##loading", "Loading...", UiTheme::kMuted)
            }
            Ui::Button(UiTheme::kQuietButton, "##exit", "Exit", @@ExitGame)
        }
    }
}

// The registry lives in the script context, so it is filled when the module initializes — the
// editor's Screens panel can list the screens before Play has ever started.
[init]
def private RegisterScreen() {
    Ui::RegisterScreen("main_menu", @@BuildMainMenu)
}
