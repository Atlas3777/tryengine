options gen2
options relaxed_assign = false

module ScreenHud shared public

//! HUD: the bottom gameplay layer. Widgets in three corners show how anchors work: the panel is
//! pinned to the top-left, the phase label to the top-right, the counter to the bottom-left.
//!
//! The screen owns the asset maps of the layers it can open: the scene request loads the HUD,
//! pause and shop bundles together, and `Show` keeps their handles so a pause/shop layer is
//! pushed with a ready map instead of looking a path up in the cache.

require tryengine/AssetMap
require tryengine/Loader
require tryengine/Ui
require trygame/GameScreens
require trygame/GameState
require trygame/GameTime
require trygame/ScreenPause
require trygame/ScreenShop
require trygame/UiTheme
require trygame/UiWidgets

//! Bundle of this screen; the scene request loads the same path.
let public kHudBundle : string = "data/bundles/hud.bundle"

// A pulse has to be requested while the node is declared: click callbacks run after the tree is
// built, and their id scope is already gone. The button sets the flag, the HUD consumes it.
var private g_pulse : bool = false
//! Alias maps of the HUD and of the layers it can push (handles from the scene request).
var private g_assets : AssetMap = AssetMap()
var private g_pause_assets : AssetMap = AssetMap()
var private g_shop_assets : AssetMap = AssetMap()

def private PhaseLabel() : string {
    if (GameTime::IsGamePaused()) {
        return "Paused"
    }
    if (GameScreens::Has(@@ScreenShop::Draw)) {
        return "Shop open"
    }
    return "Running"
}

//! Pushes or removes the pause layer to match the clock flag. The flag is flipped by the pause
//! key in gameplay and by the HUD button; the layer follows it, so there is exactly one place
//! that decides when the menu is on screen.
def private SetPauseLayer(paused : bool) {
    let open = GameScreens::Has(@@ScreenPause::Draw)
    if (paused && !open) {
        GameScreens::Push(Ui::ScreenRef(build = @@ScreenPause::Draw, assets = g_pause_assets))
    } elif (!paused && open) {
        GameScreens::Remove(@@ScreenPause::Draw)
    }
}

def private OnFire() {
    GameState::AddClick()
    GameState::Heal(4.0f)
}

def private OnDamage() {
    GameState::TakeDamage(20.0f)
    g_pulse = true
}

def private OnOpenShop() {
    GameScreens::Push(Ui::ScreenRef(build = @@ScreenShop::Draw, assets = g_shop_assets))
}

def private OnTogglePause() {
    GameState::TogglePause()
    SetPauseLayer(GameTime::IsGamePaused())
}

def public Draw(assets : AssetMap) {
    Ui::Panel("##hud", UiTheme::kHudPanel, UiLayout(anchor = float2(0.0f, 0.0f), offset = float2(24.0f, 24.0f), spacing = 8.0f)) {
        // A texture asset drawn directly, without a material file: the screen's asset map
        // resolves the screen-local name to the asset id, the size comes from the layout.
        Ui::Row("##brand", UiStyle(), UiLayout(spacing = 10.0f)) {
            Ui::ImageTexture("##logo", assets.Texture("logo"), float2(24.0f, 24.0f))
            Ui::Column("##brand_text", UiStyle(), UiLayout(spacing = 2.0f)) {
                Ui::Label("##title", "tryengine UI", UiTheme::kTitle)
                Ui::Label("##subtitle", "hud screen", UiTheme::kMuted)
            }
        }
        // Composite widgets: the health meter and the volume row also appear on the pause screen.
        UiWidgets::MeterRow("##hp", "HP", GameState::g_state.hp, GameState::g_state.hp_max, UiTheme::kHpBar,
                            GameState::HpText())
        Ui::Row("##buttons", UiStyle(), UiLayout(spacing = 8.0f)) {
            Ui::Button(UiTheme::kPrimaryButton, "##fire", "Fire", @@OnFire)
            Ui::Button(UiTheme::kDangerButton, "##damage", "Take damage", @@OnDamage)
            Ui::Button(UiTheme::kQuietButton, "##shop", "Shop", @@OnOpenShop)
        }
        Ui::Row("##settings", UiStyle(), UiLayout(spacing = 8.0f)) {
            Ui::Button(UiTheme::kPrimaryButton, "##pause button", "TogglePause", @@OnTogglePause)
            Ui::Label("##paused_label", GameState::PauseLabel(), UiTheme::kMuted)
        }
        UiWidgets::StatRow("##volume_row", "Volume", "{int(GameState::g_state.volume * 100.0f)}%")
        Ui::Slider("##volume", GameState::g_state.volume, 0.0f, 1.0f)
    }

    // anchor.x = 1 pins the widget's right edge to the parent's right edge, so the offset is
    // negative to leave a margin. anchor.y = 0 keeps it at the top.
    Ui::Label("##phase", PhaseLabel(), UiTheme::kMuted,
              UiLayout(anchor = float2(1.0f, 0.0f), offset = float2(-24.0f, 24.0f)))

    // anchor.y = 1 pins the bottom edge; the negative Y offset lifts the label off it.
    Ui::Label("##counter", "Fire {GameState::g_state.clicks} | Damage {GameState::g_state.damage_taken}",
              UiTheme::kMuted, UiLayout(anchor = float2(0.0f, 1.0f), offset = float2(24.0f, -40.0f)))

    if (g_pulse) {
        g_pulse = false
        Ui::Pulse("##hud")
    }
}

def public Show(maps : table<string; AssetMap>) {
    g_assets = Loader::MapOf(maps, kHudBundle)
    g_pause_assets = Loader::MapOf(maps, ScreenPause::kPauseBundle)
    g_shop_assets = Loader::MapOf(maps, ScreenShop::kShopBundle)
    GameScreens::Show(Ui::ScreenRef(build = @@ScreenHud::Draw, assets = g_assets))
}

//! Reconciles the pause layer with the clock flag for a pause pressed outside the HUD button
//! (the Escape key is handled in gameplay).
def public SyncPause() {
    if (!GameScreens::Has(@@ScreenHud::Draw)) {
        return
    }
    SetPauseLayer(GameTime::IsGamePaused())
}
