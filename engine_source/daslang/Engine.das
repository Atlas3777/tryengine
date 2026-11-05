options gen2

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
