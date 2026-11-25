options gen2
options relaxed_assign = false

module GameBundles shared public

//! One bundle per screen. The path is the screen's own address: the screen reads asset ids
//! with `Bundle::Get(GameBundles::kHud, "logo")`, and the game loads the same path with
//! `LoaderRequest::AddBundle` before showing that screen. `kUi` is the shared UI bundle
//! (font and UI material) loaded once at boot.
//!
//! Paths are relative to `game/`: `data/...`, extension explicit. A `.bundle` file owns the
//! name -> guid table, so a guid appears in exactly one authored place.

let public kUi : string = "data/bundles/ui.bundle"
let public kHud : string = "data/bundles/hud.bundle"

//! Bundles the editor preloads so every previewed screen has its assets. Add a screen's bundle
//! here when the screen gets one.
def public All() : array<string> {
    var out : array<string>
    out |> push(kUi)
    out |> push(kHud)
    return <- out
}
