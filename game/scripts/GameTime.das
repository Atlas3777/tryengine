options gen2
options relaxed_assign = false

module GameTime shared public

require live/live_vars
require live_host
require tryengine/Time public

struct GameClock : ClockTag {}
struct GameWorldClock : ClockTag {}
struct GameUIClock : ClockTag {}

//! The tags are `@live`: they address slots in the restored clock array, and the game clocks
//! must keep their pause/scale/total state across a reload of a running scene.
var @live game_clock       = GameClock()
var @live game_world_clock = GameWorldClock()
var @live game_ui_clock    = GameUIClock()


def RegisterGameTime() {
    TimeAttach(game_clock)
    TimeRegister(game_world_clock, game_clock)
    TimeRegister(game_ui_clock, game_clock)
}

//! The game clocks are wired on first use as well as in GameStart: the game screens read the
//! pause state, and a host that re-created the script context calls this to put the tags back
//! without knowing anything else. TimeInit registers the engine root clock before the first
//! UI frame, so the parent exists by then. Idempotent; a headless probe without the root clock
//! leaves the tags unregistered instead of asserting.
def public EnsureGameTime() {
    if (root_clock.slot_ < 0) {
        return
    }
    // Each tag is skipped by TimeRegister when it already has a slot, so this also repairs a
    // partially restored set (for example a stale @live entry) instead of returning early.
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