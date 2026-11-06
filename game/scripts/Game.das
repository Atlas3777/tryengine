
options gen2
options rtti = true

module Game shared public

require daslib/decs
require tryengine/Types
require tryengine/Input
require tryResources
require tryengine/Engine
require tryengine/Render
require tryengine/QuadTree
require math

let mesh_id_1 = 3081795527557720915ul
let mat_id_1 = 17036954862819815981ul

// === конвенция движка ===
let right_raw = float3(1.0f, 0.0f, 0.0f)   // локальная X (вправо)
let front_raw = float3(0.0f, 1.0f, 0.0f)   // локальная Y (вперёд)
let up_raw    = float3(0.0f, 0.0f, 1.0f)   // локальная Z (вверх)
let world_up  = float3(0.0f, 0.0f, 1.0f)

// === параметры орбиты ===
var  orbit_angle  = 0.0f
let  orbit_center = float3(0.0f, 7.5f, 0.0f)   // центр группы мешей в плоскости XY
let  orbit_radius = 20.0f
let  orbit_height = 10.0f
let  orbit_speed  = 0.008f

[export]
def TestTestTest(){
    print("Ура, работает\n")
}

[export]
def GameStart(){
    print("Game Start\n")
    create_entity() @(eid, cmp){
        cmp.tag := "Camera"
        apply_decs_template(cmp, Transform(
            position = float3(0.0f, -20.0f, 10.0f),
            rotation = float4(0f, 0f, 0f, 1f),
            scale = float3(1.0f, 1.0f, 1.0f)
        ))
        apply_decs_template(cmp, Camera(
            fov = 35.0f,
            near_plane = 0.01f,
            far_plane = 500.0f
        ))
        apply_decs_template(cmp, Relationship())
    }

    commit()

    print("request mesh GAME\n")
    RequestLoadMesh(mesh_id_1)
    print("request mat GAME\n")
    RequestLoadMaterial(mat_id_1)

    SpawnGame()
}

[export]
def GameUpdate() {

    // ================== ОРБИТА КАМЕРЫ ==================
    orbit_angle += orbit_speed

    // позиция камеры: крутимся в плоскости XY, поднимаемся по Z
    let eye = float3(
        orbit_center.x + orbit_radius * cos(orbit_angle),
        orbit_center.y + orbit_radius * sin(orbit_angle),
        orbit_center.z + orbit_height
    )

    // целевой базис камеры
    let f = normalize(orbit_center - eye)
    let r = normalize(cross(f, world_up))   // горизонтальный right
    let u = cross(r, f)                     // скорректированный up

    // кватернион: сначала совмещаем forward, потом докручиваем вокруг f,
    // чтобы выровнять up
    let q1 = quat_from_unit_arc(front_raw, f)
    let up_after = quat_mul_vec(q1, up_raw)

    let cos_roll = dot(up_after, u)
    let sin_roll = dot(cross(up_after, u), f)
    let roll = atan2(sin_roll, cos_roll)

    let q2 = quat_from_unit_vec_ang(f, roll)
    let q  = quat_mul(q2, q1)

    query()$(var t : Transform, var cam : Camera) {
        t.position = eye
        t.rotation = q
    }

    // ================== ввод игрока (без изменений) ==================
    query()$(var t : Transform, var inputState : float3){
        inputState = float3(0, 0, 0)
        if (is_down(Key.I)) { inputState.y =  1.0f }
        if (is_down(Key.K)) { inputState.y = -1.0f }
        if (is_down(Key.J)) { inputState.x = -1.0f }
        if (is_down(Key.L)) { inputState.x =  1.0f }
    }

    query()$(var t : Transform, inputState : float3, speed : float){
        t.position += inputState * 0.08 * speed;
    }
}

def SpawnGame() {
    print("Spawn Game\n")

    let test_eid = create_entity() @(eid, cmp) {
        cmp.tag := "teeest"
        apply_decs_template(cmp, Transform(
            position = float3(0.0f, 0.0f, 0.0f),
            rotation = float4(0.0f, 0.0f, 0.0f, 1.0f),
            scale = float3(1.0f, 1.0f, 1.0f)
        ))
        cmp.inputState := float3(0, 0, 0)
        cmp.speed := float(3)

        cmp.meshGuid := mesh_id_1
        cmp.materialGuid := mat_id_1
        apply_decs_template(cmp, Relationship())
    }

    create_mesh_entity_game("Right 1", float3(5.0f, 5.0f, 0.0f))
    create_mesh_entity_game("Right 2", float3(5.0f, 10.0f, 0.0f))
    create_mesh_entity_game("Left 1",  float3(-5.0f, 5.0f, 0.0f))
    create_mesh_entity_game("Left 2",  float3(-5.0f, 10.0f, 0.0f))

    spawn_light("Light Red",    float3(5.0f,  5.0f, 4.0f), float3(1.00f, 0.45f, 0.40f), 2.5f, 28.0f)
    spawn_light("Light Green",  float3(-5.0f, 5.0f, 4.0f), float3(0.40f, 1.00f, 0.50f), 2.5f, 28.0f)
    spawn_light("Light Blue",   float3(0.0f, 10.0f, 6.0f), float3(0.45f, 0.55f, 1.00f), 3.0f, 32.0f)
    spawn_light("Light Yellow", float3(0.0f,  0.0f, 4.0f), float3(1.00f, 0.92f, 0.45f), 2.5f, 28.0f)

    commit()
}

def create_mesh_entity_game(tag : string, pos : float3) {
    return create_entity() @(eid, cmp) {
        cmp.tag := tag
        apply_decs_template(cmp, Transform(
            position = pos,
            rotation = float4(0.0f, 0.0f, 0.0f, 1.0f),
            scale = float3(1.0f, 1.0f, 1.0f)
        ))
        cmp.meshGuid := mesh_id_1
        cmp.materialGuid := mat_id_1
        apply_decs_template(cmp, Relationship())
    }
}

def spawn_light(tag : string, pos : float3, color : float3, intensity : float, radius : float) {
    create_entity() @(eid, cmp) {
        cmp.tag := tag
        apply_decs_template(cmp, Transform(
            position = pos,
            rotation = float4(0.0f, 0.0f, 0.0f, 1.0f),
            scale = float3(1.0f, 1.0f, 1.0f)
        ))
        apply_decs_template(cmp, PointLight(
            color = color,
            intensity = intensity,
            radius = radius
        ))
        apply_decs_template(cmp, Relationship())
    }
}