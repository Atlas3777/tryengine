options gen2
options relaxed_assign = false

module GameState shared public

//! State shared by the HUD, the pause menu and the shop, plus the labels they show. Keeping it
//! out of the screens lets each screen live in its own file without importing another screen.

require math
require trygame/GameScreens
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

def public PhaseLabel() : string {
    if (GameTime::IsGamePaused()) {
        return "Paused"
    }
    if (GameScreens::Has("shop")) {
        return "Shop open"
    }
    return "Running"
}

//! Pause is the clock flag plus its menu layer: the layer follows the flag, so nothing can pause
//! the clock without showing the menu, and a resume closes it.
def public TogglePause() {
    GameTime::ToggleGamePaused()
    if (GameTime::IsGamePaused()) {
        GameScreens::Push("pause")
    } else {
        GameScreens::Close("pause")
    }
}
