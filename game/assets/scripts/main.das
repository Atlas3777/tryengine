options gen2

require math
require daslib/decs
require tryengine/Types
require tryeditor/Hierarchy

[export]
def start(){
    let player = create_entity() @(eid, cmp) {
        cmp.name := "player1"
        apply_decs_template(cmp, Relationship())
        apply_decs_template(cmp, Transform(
            position = float3(0, 0, 0),
            scale = float3(1, 1, 1),
            rotation = float4(0, 0, 0, 1)
        ))
    }

    let player2 = create_entity() @(eid, cmp) {
        cmp.name := "player2"
        apply_decs_template(cmp, Transform(
            position = float3(0, 100, 0),
            scale = float3(33, 33, 33),
            rotation = float4(0, 0, 0, 1)
        ))
        cmp.hp := 100
    }

    commit()
    print("создан игрок \n")
}

var seconds = 0.0
var last_seconds = 0.0

[export]
def update(dt : float) {
    seconds += dt
    if (seconds - last_seconds >= 1.0) {
        last_seconds = seconds
        print("Seconds: {int(seconds)}\n")

        query() $(var t : Transform) {
            t.position += float3(0, 1, 1)
            //print("Position: {t.position}\n")
        }

        query() $(eid : EntityId; var r : Types::Transform){
            print("{eid}\n")
            print("{r.position}\n")
            print("{r.rotation}\n")
            print("{r.scale}\n")
            print("=============\n")
        }
        var transform = Transform()
        transform.position = float3(9, 6, 3)
        transform.rotation = quat_from_euler(55f, 55f, 55f)
        transform.scale = float3(1f, 1f, 1f)


        DebugPrint()
    }
}