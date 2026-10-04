options gen2

module AsyncTaskManager shared public

require daslib/coroutines

var g_async_tasks : Coroutines

def StartTask(var task : iterator<bool>) {
    g_async_tasks |> emplace <| task
}

[export]
def tick_async_tasks() {
    var i = length(g_async_tasks) - 1
    while (i >= 0) {
        var is_running = false
        if (!next(g_async_tasks[i], is_running) || !is_running) {
            g_async_tasks |> erase(i)
        }
        i--
    }
}