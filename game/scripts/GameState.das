options gen2
options relaxed_assign = false

module GameState shared public

//! State shared by the HUD, the pause menu, the shop and the main menu, plus the labels they
//! show. Keeping it out of the screens lets each screen live in its own file without importing
//! another screen.
//!
//! It also carries the one message a screen sends to the game: the scene the player picked.
//! The main menu cannot import the game module (the game imports the UI, so that would be a
//! cycle), and it must not load anything itself — it asks for a scene path, and `Game` drains
//! that request in its own frame.

require math
// The live host only exists in the development hosts (the editor, the CLI gate and
// the probes). The game pak is compiled without it, so these modules stay clean of
// live: the guarded require is skipped and @live is just an unused annotation.
require ?live_host live_host
require ?live_host live/live_vars
require trygame/GameTime

struct public GameUiState {
    hp : float = 100.0f
    hp_max : float = 100.0f
    volume : float = 0.6f
    gold : int = 50
    clicks : int = 0
    damage_taken : int = 0
}

//! HUD values (hp, gold, clicks) are gameplay state, not a scene value: they live through a
//! hot reload of a running scene with `@live`, while the DECS world is restored by the
//! editor's reload snapshot.
var public @live g_state = GameUiState()

// The scene a screen asked for, "" when there is none. This is the whole hand-off: no title,
// no flags — the screen already has its label, and what to load is a path.
var private g_requested_scene : string

//! Asks the game to load `scene`. The game takes the request on its next frame; until then the
//! screen can show that a load is running by asking `Loader::IsActive()`.
def public RequestScene(scene : string) {
    g_requested_scene = scene
}

//! Takes the requested scene path; "" when there was none.
def public TakeSceneRequest() : string {
    let scene = g_requested_scene
    g_requested_scene = ""
    return scene
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
