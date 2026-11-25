options gen2
options relaxed_assign = false

//! The one message a screen sends to the game: the scene the player picked. The main menu
//! cannot import `Game` (the game imports the UI), so the request travels through the shared
//! state and the game drains it on its own frame. Covers: a request is delivered once, an
//! empty request means "nothing pending", and a second request replaces the first.

require trygame/GameState

var g_failures : int

def Check(ok : bool; what : string) {
    if (!ok) {
        g_failures++
        print("FAIL: {what}\n")
    }
}

[export]
def main() : int {
    let first = GameState::SceneRequest(title = "First", scene = "content/first.scene",
                                        prefab = "content/props/sword.prefab", physics_demo = true)

    Check(empty(GameState::TakeSceneRequest().scene), "nothing is pending before a request")

    GameState::RequestScene(first)
    var taken = GameState::TakeSceneRequest()
    Check(taken.scene == "content/first.scene", "the request carries the scene path")
    Check(taken.prefab == "content/props/sword.prefab", "the request carries the prefab")
    Check(taken.physics_demo, "the request carries the physics flag")
    Check(taken.title == "First", "the request carries the label")
    Check(empty(GameState::TakeSceneRequest().scene), "a taken request is not delivered twice")

    // A second click replaces a request the game has not drained yet.
    GameState::RequestScene(first)
    GameState::RequestScene(GameState::SceneRequest(title = "Second", scene = "content/second.scene",
                                                    prefab = "", physics_demo = false))
    var replaced = GameState::TakeSceneRequest()
    Check(replaced.scene == "content/second.scene", "a newer request replaces the pending one")
    Check(!replaced.physics_demo, "the replacement carries its own flags")

    print("-- {g_failures} check(s) failed --\n")
    return g_failures == 0 ? 0 : 1
}
