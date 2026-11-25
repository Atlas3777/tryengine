options gen2
options relaxed_assign = false

module ScreenShop shared public

//! Shop: a second layer over the HUD, on its own ids and with its own backdrop. Shows that two
//! screens can be on screen at once without either knowing about the other. The HUD pushes it
//! with the shop asset map; Close pops it.

require tryengine/AssetMap
require tryengine/Ui
require trygame/GameScreens
require trygame/GameState
require trygame/UiTheme
require trygame/UiWidgets

//! Bundle of this screen; the scene request loads the same path.
let public kShopBundle : string = "data/bundles/shop.bundle"

def private OnBuyHealth() {
    GameState::BuyHealth(10, 25.0f)
}

def private OnCloseShop() {
    GameScreens::Pop()
}

def public Draw(_assets : AssetMap) {
    Ui::Backdrop("##shop", UiTheme::kPauseCurtain) {
        Ui::Panel("##cart", UiTheme::kPausePanel, UiLayout(anchor = float2(0.5f, 0.5f), spacing = 12.0f)) {
            Ui::Label("##title", "Shop", UiTheme::kTitle)
            UiWidgets::StatRow("##gold_row", "Gold", "{GameState::g_state.gold}")
            Ui::Row("##actions", UiStyle(), UiLayout(spacing = 8.0f)) {
                Ui::Button(UiTheme::kPrimaryButton, "##buy", "Buy health", @@OnBuyHealth)
                Ui::Button(UiTheme::kQuietButton, "##close", "Close", @@OnCloseShop)
            }
        }
    }
}
