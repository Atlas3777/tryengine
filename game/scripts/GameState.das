options gen2
options relaxed_assign = false

module GameState shared public

//! State shared by the HUD, the pause menu, the shop and the main menu, plus the labels they
//! show. Keeping it out of the screens lets each screen live in its own file without importing
//! another screen.
//!
//! It also carries the one message a screen sends to the game: the scene the player picked.
//! The main menu cannot import the game module (the game imports the UI, so that would be a
//! cycle), and it must not load anything itself — it fills a `SceneRequest`, and `Game` drains
//! it in its own frame.

require math
require trygame/GameTime

struct public GameUiState {
    hp : float = 100.0f
    hp_max : float = 100.0f
    volume : float = 0.6f
    gold : int = 50
    clicks : int = 0
    damage_taken : int = 0
}

var public g_state = GameUiState()

//! A scene the UI asked for: what to load and what to spawn after it. The main menu owns the
//! list of entries; the game only knows how to load one.
struct public SceneRequest {
    title        : string
    scene        : string
    prefab       : string
    physics_demo : bool
}

var private g_scene_request : SceneRequest
var private g_scene_pending = false

//! Hands the game the scene the player picked. The request is not a load: the game starts it
//! on its next frame, and `Loader::IsActive()` is how a screen shows that a load is running.
def public RequestScene(request : SceneRequest) {
    g_scene_request = SceneRequest(title = request.title, scene = request.scene,
                                   prefab = request.prefab, physics_demo = request.physics_demo)
    g_scene_pending = true
}

//! Takes the pending request; `scene` is "" when there was none.
def public TakeSceneRequest() : SceneRequest {
    if (!g_scene_pending) {
        return SceneRequest()
    }
    g_scene_pending = false
    var request = g_scene_request
    g_scene_request = SceneRequest()
    return <- request
}

// Mutation helpers keep the arithmetic here: a screen must not read and write the shared state
// through two module hops (a cross-module field is a reference, and `min` takes values).
def public AddClick() {
    g_state.clicks += 1
}

def public Heal(amount : float) {
    g_state.hp = min(g_state.hp_max, g_state.hp + amount)
}

def public TakeDamage(amount : float) {
    g_state.damage_taken += 1
    g_state.hp = max(0.0f, g_state.hp - amount)
}

def public BuyHealth(cost : int; amount : float) {
    if (g_state.gold >= cost) {
        g_state.gold -= cost
        g_state.hp = min(g_state.hp_max, g_state.hp + amount)
    }
}

def public HpText() : string {
    return "{int(g_state.hp)} / {int(g_state.hp_max)}"
}

def public PauseLabel() : string {
    if (GameTime::IsGamePaused()) {
        return "Paused"
    }
    return "Running"
}

//! Pause is only the clock flag. The layer that shows the menu is owned by the HUD, which
//! pushes and removes it when the flag changes; a state module must not import screens.
def public TogglePause() {
    GameTime::ToggleGamePaused()
}
