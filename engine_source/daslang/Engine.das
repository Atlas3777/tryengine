options gen2
options relaxed_assign = false
options gc = true
options persistent_heap
options intern_strings

module Engine shared public

require tryengine/AsyncTaskManager
require tryengine/Input
require daslib/just_in_time

[export]
def EngineUpdate() {
    tick_async_tasks()
}

[export]
def EngineStart() {
    SetInputSource()
}
