options gen2
options relaxed_assign = false

module Time shared public

//! Hierarchical time.
//!
//! A clock is a node of a tree. Its delta is its parent's delta multiplied by its own
//! scale, so a slowed or paused parent carries its whole subtree with it and a child
//! never needs to know about its children. All clocks live in one dense global array;
//! a node only stores its parent's index, so one pass per frame updates everything.
//!
//! A tag is an empty structure inheriting ClockTag. Its `slot_` is the index in that
//! array; the accessors below resolve the type at the call site, so nothing is looked up
//! by name. Because the slot lives in the tag value and not in the type, one module can
//! own several clocks of the same kind (a per-cutscene or per-boss slowdown, say).
//!
//! Ownership: the engine registers the root clock (TimeInit, called from EngineStart) and
//! nothing else. The game and the editor attach their own subtrees from their own Start
//! functions, so neither has to know about the other.

let k_no_slot = -1


struct Clock {
    parent_ : int = k_no_slot
    scale_ : float = 1.0f
    is_paused_ : bool
    dt_ : float
    total_ : double
}

//! Base of every clock tag. `slot_` is the clock's index in the global array.
struct ClockTag {
    slot_ : int = k_no_slot
}

struct RootClock : ClockTag {}

var clocks : array<Clock>


//! Upper bound for a single frame. A stall (shader compile, asset import, a breakpoint)
//! must not become one giant integration step for everything downstream.
let max_frame_dt = 0.25f

var root_clock = RootClock()

var last_tick_ns : int64 = -1

def private AppendClock(parent : int; scale : float) : int {
    clocks |> push(Clock(parent_ = parent, scale_ = scale))
    return length(clocks) - 1
}

//! Registers the root clock. Called once from EngineStart; later calls do nothing.
def TimeInit() {
    if (root_clock.slot_ != k_no_slot) {
        return
    }
    root_clock.slot_ = AppendClock(k_no_slot, 1.0f)
}

//! Registers `tag` as a child of `parent`. The parent must exist already: that keeps the
//! array topologically sorted, which is why a single pass is enough to update the tree.
//! Idempotent, because the editor restarts the game clocks on every Play.
def TimeRegister(var tag : auto; var parent : auto; scale : float = 1.0f) {
    if (tag.slot_ != k_no_slot) {
        return
    }
    assert(parent.slot_ != k_no_slot, "time: parent clock is not registered")
    tag.slot_ = AppendClock(parent.slot_, scale)
}

//! Attaches `tag` directly to the root clock: unscaled frame time that never pauses.
def TimeAttach(var tag : auto; scale : float = 1.0f) {
    TimeRegister(tag, root_clock, scale)
}

def private Advance(dt : float; force : bool) {
    for (clock in clocks) {
        let parent = clock.parent_
        let parent_dt = parent < 0 ? dt : clocks[parent].dt_
        clock.dt_ = clock.is_paused_ && !force ? 0.0f : parent_dt * clock.scale_
        clock.total_ += double(clock.dt_)
    }
}

//! One frame of wall time for the whole tree; EngineUpdate calls this before any system.
def TimeTick() {
    let now = get_time_nsec(0l)
    var dt = 0.0f
    if (last_tick_ns >= 0l) {
        dt = float(double(now - last_tick_ns) * 1.0e-9d)
    }
    last_tick_ns = now
    if (dt < 0.0f) {
        dt = 0.0f
    }
    if (dt > max_frame_dt) {
        dt = max_frame_dt
    }
    Advance(dt, false)
}

//! Advances the tree by a synthetic frame, ignoring both the wall clock and every pause
//! flag. This is what the editor's "Step frame" button needs.
def TimeStepOnce(dt : float) {
    var step = dt
    if (step < 0.0f) {
        step = 0.0f
    }
    if (step > max_frame_dt) {
        step = max_frame_dt
    }
    Advance(step, true)
    // Re-seed, so the next real tick does not also add the wall time of this frame.
    last_tick_ns = get_time_nsec(0l)
}

//! Frame delta of this clock, already scaled by its ancestors and zeroed by a pause.
def TimeDelta(var tag : auto) : float {
    assert(tag.slot_ != k_no_slot, "time: clock is not registered")
    return clocks[tag.slot_].dt_
}

def TimeTotal(var tag : auto) : double {
    assert(tag.slot_ != k_no_slot, "time: clock is not registered")
    return clocks[tag.slot_].total_
}

def TimeScale(var tag : auto) : float {
    assert(tag.slot_ != k_no_slot, "time: clock is not registered")
    return clocks[tag.slot_].scale_
}

def TimeSetScale(var tag : auto; scale : float) {
    assert(tag.slot_ != k_no_slot, "time: clock is not registered")
    clocks[tag.slot_].scale_ = scale
}

def TimeIsPaused(var tag : auto) : bool {
    assert(tag.slot_ != k_no_slot, "time: clock is not registered")
    return clocks[tag.slot_].is_paused_
}

//! A pause is a flag, not `scale = 0`: the scale is a rate the caller wants back exactly
//! when the pause ends, and a paused clock must be tellable from a motionless one.
def TimeSetPaused(var tag : auto; paused : bool) {
    assert(tag.slot_ != k_no_slot, "time: clock is not registered")
    clocks[tag.slot_].is_paused_ = paused
}

//! True when this clock moved this frame. Gate a system that integrates time with this
//! instead of letting it do the work on a zero delta.
def TimeIsAdvancing(var tag : auto) : bool => TimeDelta(tag) > 0.0f

//! Zeroes the delta and the accumulated time of `tag` and of every clock below it, and
//! clears their pause flags. Play mode calls this so that a new session starts from zero
//! instead of continuing the previous one.
def TimeReset(var tag : auto) {
    assert(tag.slot_ != k_no_slot, "time: clock is not registered")
    let first = tag.slot_
    for (i in range(first, length(clocks))) {
        var below = i == first
        var j = clocks[i].parent_
        while (!below && j >= first) {
            below = j == first
            j = clocks[j].parent_
        }
        if (!below) {
            continue
        }
        clocks[i].dt_ = 0.0f
        clocks[i].total_ = 0.0d
        clocks[i].is_paused_ = false
    }
}
