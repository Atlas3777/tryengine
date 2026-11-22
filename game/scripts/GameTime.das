options gen2
options relaxed_assign = false

module GameTime shared public

require tryengine/Time public

struct GameClock : ClockTag {}
struct GameWorldClock : ClockTag {}
struct GameUIClock : ClockTag {}

var game_clock       = GameClock()
var game_world_clock = GameWorldClock()
var game_ui_clock    = GameUIClock()


def RegisterGameTime() {
    TimeAttach(game_clock)
    TimeRegister(game_world_clock, game_clock)
    TimeRegister(game_ui_clock, game_clock)
}

//! The game clocks are created on first use, not only in GameStart: the editor's Edit-mode
//! preview draws the game screens without starting the game, and the screens read the pause
//! state. TimeInit registers the engine root clock before the first UI frame, so the parent
//! exists by then. Idempotent; a headless probe without the root clock leaves the tags
//! unregistered instead of asserting.
def public EnsureGameTime() {
    if (game_clock.slot_ >= 0 || root_clock.slot_ < 0) {
        return
    }
    RegisterGameTime()
}

//! Pause state of the gameplay clock. False while the clocks do not exist yet.
def public IsGamePaused() : bool {
    EnsureGameTime()
    return game_world_clock.slot_ >= 0 && TimeIsPaused(game_world_clock)
}

def public ToggleGamePaused() {
    EnsureGameTime()
    if (game_world_clock.slot_ >= 0) {
        TimeTogglePaused(game_world_clock)
    }
}