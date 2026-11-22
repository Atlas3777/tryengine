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
require tryengine/Physics
require tryengine/Time
require tryengine/TransformSystem
// A zone around the tick: tick_async_tasks finishes hot-reload work for scripts, and one
// frame in a long run can cost hundreds of milliseconds. Without a name that time hangs on
// the frame root and reads as "no idea where it went".
[export, profile]
def EngineUpdate() {
    // The frame's dt is read here and nowhere else; every clock derives from it, so all
    // systems see the same frame and no caller passes dt around.
    TimeTick()
    tick_async_tasks()
    Physics::Update()
    UpdateWorldTransforms()
}

[export]
def EngineStart() {
    TimeInit()
    SetInputSource()
}
