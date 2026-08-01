options gen2

require math
require daslib/decs
require tryengine/Types
require tryeditor/Hierarchy

[export]
def start(){
    let player = create_entity() @(eid, cmp) {
        cmp.tag := "player1"
        apply_decs_template(cmp, Relationship())
        apply_decs_template(cmp, Transform(
            position = float3(0, 0, 0),
            scale = float3(1, 1, 1),
            rotation = float4(0, 0, 0, 1)
        ))
    }

    let player2 = create_entity() @(eid, cmp) {
        cmp.tag := "player2"
        apply_decs_template(cmp, Transform(
            position = float3(0, 100, 0),
            scale = float3(33, 33, 33),
            rotation = float4(0, 0, 0, 1)
        ))
        cmp.hp := 100
    }

    // commit()
    // print("создан игрок \n")

      // Этап 1: Создаём сущности в deferred-очереди
    let room_eid = create_entity() @(eid, cmp) {
        cmp.tag := "My Room"
        apply_decs_template(cmp, Transform(
            position = float3(0.0f, 0.0f, 0.0f),
            rotation = float4(0.0f, 0.0f, 0.0f, 1.0f),
            scale = float3(1.0f, 1.0f, 1.0f)
        ))
    }

    let table_eid = create_entity() @(eid, cmp) {
        cmp.tag := "Table"
        apply_decs_template(cmp, Transform(
            position = float3(1.0f, 0.0f, 1.0f),
            rotation = float4(0.0f, 0.0f, 0.0f, 1.0f),
            scale = float3(1.0f, 1.0f, 1.0f)
        ))
    }

    let chair_eid = create_entity() @(eid, cmp) {
        cmp.tag := "Chair"
        apply_decs_template(cmp, Transform(
            position = float3(2.0f, 0.0f, 1.0f),
            rotation = float4(0.0f, 0.0f, 0.0f, 1.0f),
            scale = float3(1.0f, 1.0f, 1.0f)
        ))
    }

    let laptop_eid = create_entity() @(eid, cmp) {
        cmp.tag := "Laptop"
        apply_decs_template(cmp, Transform(
            position = float3(1.0f, 1.0f, 1.0f),
            rotation = float4(0.0f, 0.0f, 0.0f, 1.0f),
            scale = float3(0.5f, 0.5f, 0.5f)
        ))
    }

    let empty_eid = create_entity() @(eid, cmp) {
        cmp.tag := "Empty Entity"
    }

    // Коммитим создание, чтобы получить настоящие EntityId
    commit()

    // Этап 2: Связываем созданные сущности в иерархию
    // Ожидаемое дерево:
    // ├── My Room (Root)
    // │    ├── Table (First Child)
    // │    │    └── Laptop (Child of Table)
    // │    └── Chair (Sibling of Table)
    // └── Empty Entity (Root, без потомков)

    // 1. Настраиваем Root (My Room) — его первый ребёнок Table
    update_entity(room_eid) @(eid, cmp) {
        apply_decs_template(cmp, Relationship(
            parent = INVALID_ENTITY_ID,
            first_child = table_eid,
            prev = INVALID_ENTITY_ID,
            next = INVALID_ENTITY_ID
        ))
    }

    // 2. Настраиваем Table — его родитель Room, первый ребёнок Laptop, следующий сосед Chair
    update_entity(table_eid) @(eid, cmp) {
        apply_decs_template(cmp, Relationship(
            parent = room_eid,
            first_child = laptop_eid,
            prev = INVALID_ENTITY_ID,
            next = chair_eid
        ))
    }

    // 3. Настраиваем Chair — его родитель Room, предыдущий сосед Table, детей нет
    update_entity(chair_eid) @(eid, cmp) {
        apply_decs_template(cmp, Relationship(
            parent = room_eid,
            first_child = INVALID_ENTITY_ID,
            prev = table_eid,
            next = INVALID_ENTITY_ID
        ))
    }

    // 4. Настраиваем Laptop — его родитель Table, детей и соседей нет
    update_entity(laptop_eid) @(eid, cmp) {
        apply_decs_template(cmp, Relationship(
            parent = table_eid,
            first_child = INVALID_ENTITY_ID,
            prev = INVALID_ENTITY_ID,
            next = INVALID_ENTITY_ID
        ))
    }

    // 5. Настраиваем Empty Entity — это отдельный корень без связей
    update_entity(empty_eid) @(eid, cmp) {
        apply_decs_template(cmp, Relationship(
            parent = INVALID_ENTITY_ID,
            first_child = INVALID_ENTITY_ID,
            prev = INVALID_ENTITY_ID,
            next = INVALID_ENTITY_ID
        ))
    }
     update_entity(player) @(eid, cmp) {
        apply_decs_template(cmp, Relationship(
            parent = INVALID_ENTITY_ID,
            first_child = INVALID_ENTITY_ID,
            prev = INVALID_ENTITY_ID,
            next = INVALID_ENTITY_ID
        ))
    }

        update_entity(player2) @(eid, cmp) {
        apply_decs_template(cmp, Relationship(
            parent = INVALID_ENTITY_ID,
            first_child = INVALID_ENTITY_ID,
            prev = INVALID_ENTITY_ID,
            next = INVALID_ENTITY_ID
        ))
    }


    // Финальный коммит связей в ECS
    commit()
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
    }
}