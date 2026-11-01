options gen2

module Engine shared public

require tryengine/AsyncTaskManager
require tryengine/Input

[export]
def EngineUpdate() {
    tick_async_tasks()
}

[export]
def EngineStart() {
    update_input()
}
