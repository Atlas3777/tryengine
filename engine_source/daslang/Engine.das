options gen2
options relaxed_assign = false
options gc = true
options persistent_heap
options intern_strings

module Engine shared public

require tryengine/AsyncTaskManager
require tryengine/Input
require tryengine/Profiling
require daslib/just_in_time

// A zone around the tick: tick_async_tasks finishes hot-reload work for scripts, and one
// frame in a long run can cost hundreds of milliseconds. Without a name that time hangs on
// the frame root and reads as "no idea where it went".
[export, profile]
def EngineUpdate() {
    tick_async_tasks()
}

[export]
def EngineStart() {
    SetInputSource()
}
