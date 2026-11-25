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
    Check(empty(GameState::TakeSceneRequest()), "nothing is pending before a request")

    GameState::RequestScene("content/first.scene")
    Check(GameState::TakeSceneRequest() == "content/first.scene", "the request delivers the scene path")
    Check(empty(GameState::TakeSceneRequest()), "a taken request is not delivered twice")

    // A second click replaces a request the game has not drained yet.
    GameState::RequestScene("content/first.scene")
    GameState::RequestScene("content/second.scene")
    Check(GameState::TakeSceneRequest() == "content/second.scene", "a newer request replaces the pending one")

    print("-- {g_failures} check(s) failed --\n")
    return g_failures == 0 ? 0 : 1
}
