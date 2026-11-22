options gen2
options relaxed_assign = false

module UiWidgets shared public

//! Reusable pieces of this game's interface: the widgets that repeat across screens.
//!
//! They are plain functions over `tryengine/Ui` primitives. Rules that keep them composable:
//! - the caller passes the id the piece builds under and every child id is local to it, so two
//!   call sites under different parents never collide;
//! - no state of their own: values come in as arguments, changes go out through `action`;
//! - no colours of their own: look comes from `trygame/UiTheme`, so a piece restyles with the
//!   theme instead of with an edit per screen.

require math
require tryengine/Ui
require trygame/UiTheme

//! "Title  value" row — the same pair the HUD and the pause screen both show.
def public StatRow(id : string; title : string; value : string) {
    Ui::Row(id, UiStyle(), UiLayout(spacing = 8.0f)) {
        Ui::Label("##title", title, UiTheme::kMuted)
        Ui::Label("##value", value, UiTheme::kValue)
    }
}

//! Titled meter: a label over a progress bar, e.g. health or stamina.
def public MeterRow(id : string; title : string; value : float; value_max : float; fill : UiStyle;
                    text : string = "") {
    let t = value_max == 0.0f ? 0.0f : clamp(value / value_max, 0.0f, 1.0f)
    Ui::Column(id, UiStyle(), UiLayout(spacing = 4.0f)) {
        Ui::Label("##title", title, UiTheme::kMuted)
        Ui::ProgressBar("##bar", t, text, fill)
    }
}
