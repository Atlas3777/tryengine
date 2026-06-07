options gen2

require daslib/decs
require daslib/decs_boost
require gameq
require live_host
require live/live_vars


[export]
def start(){
    // Для live_vars полезно оборачивать инициализацию:
     if (is_reload()) {
         print("---Старт после перезапуска ---\n")
     }
}

var @live seconds = 0.0
var last_seconds = 0.0

[export]
def update(dt: float){
    if (is_reload()) {
        print("--- update после перезапуска-\n")

    }
    //print("Seconds:  + {seconds} \n")
    seconds += dt
    if (seconds - last_seconds >= 1.0) {
        last_seconds = seconds
        print("Seconds: {int(seconds)} \n")
    }

    // test2()
}