options gen2

module QuadTree shared

require daslib/decs
require daslib/decs_boost
require math
require tryengine/Types

// ===========================================================================
// Конфигурация дерева
// ===========================================================================
[safe_when_uninitialized]
struct public QuadTreeConfig {
    rootSize : float = 4000.0f
    rootCenter : float2 = float2(2000.0f, 2000.0f)
    minChunkSize : float = 250.0f

    splitMultiplier : float = 1.3f
    mergeMultiplier : float = 1.5f

    fovAngle : float = PI / 3
    viewDistance : float = 2200.0f

    maxDepth : int
}

def public init_config(var cfg : QuadTreeConfig&) {
    var currentSize = cfg.rootSize
    cfg.maxDepth = 0
    while (currentSize > cfg.minChunkSize + 0.001f) {
        currentSize *= 0.5f
        cfg.maxDepth++
    }
}

enum public NodeState {
    Loaded  // Активный лист ландшафта (содержит сущность DECS)
    Split   // Разбит на 4 дочерних узла
}

struct public QuadNode {
    depth : int
    center : float2
    halfSize : float
    state : NodeState
    parent : int
    children : array<int>
    ownedEntities : array<EntityId>
}

struct public QuadTree {
    config : QuadTreeConfig
    nodes : array<QuadNode>
}

// ---------------------------------------------------------------------------
// Создание и удаление сущностей DECS
// ---------------------------------------------------------------------------
def public load_node_entity(var tree : QuadTree&; nodeIdx : int) {
    if (length(tree.nodes[nodeIdx].ownedEntities) > 0) { return }

    let depth = tree.nodes[nodeIdx].depth
    let center = tree.nodes[nodeIdx].center
    let halfSize = tree.nodes[nodeIdx].halfSize

    let eid = create_entity() @(e, cmp) {
        cmp |> set("tag", "QuadChunk_D{depth}_{nodeIdx}")
        apply_decs_template(cmp, Transform(
            position = float3(center.x, center.y, 0.0f),
            rotation = float4(0.0f, 0.0f, 0.0f, 1.0f),
            scale = float3(halfSize, halfSize, 1.0f)
        ))
        apply_decs_template(cmp, Relationship())
    }

    tree.nodes[nodeIdx].ownedEntities |> push(eid)
}

def public unload_node_entity(var tree : QuadTree&; nodeIdx : int) {
    for (eid in tree.nodes[nodeIdx].ownedEntities) {
        delete_entity(eid)
    }
    tree.nodes[nodeIdx].ownedEntities |> clear()
}

// ---------------------------------------------------------------------------
// Проверка попадания узла в Frustum
// ---------------------------------------------------------------------------
def public is_node_in_frustum(center : float2; halfSize : float; eye : float2; angle : float; fov : float; viewDist : float) : bool {
    // 1. Если игрок находится внутри узла — узел всегда успешен
    if (abs(eye.x - center.x) <= halfSize && abs(eye.y - center.y) <= halfSize) {
        return true
    }

    let offsets = fixed_array<float2>(
        float2(-halfSize, -halfSize),
        float2(halfSize, -halfSize),
        float2(-halfSize, halfSize),
        float2(halfSize, halfSize),
        float2(0.0f, 0.0f)
    )

    let halfFov = fov * 0.5f
    for (off in offsets) {
        let p = center + off
        let d = p - eye
        let dist = length(d)

        if (dist <= viewDist) {
            var angleToCorner = atan2(d.y, d.x) - angle
            angleToCorner = atan2(sin(angleToCorner), cos(angleToCorner)) // Нормализация [-PI, PI]

            if (abs(angleToCorner) <= halfFov * 1.15f) {
                return true
            }
        }
    }
    return false
}

// ---------------------------------------------------------------------------
// Создание дерева
// ---------------------------------------------------------------------------
def public create_quadtree(var config : QuadTreeConfig&) : QuadTree {
    init_config(config)

    var tree : QuadTree

    // Корень всегда создан и загружен по умолчанию
    tree.nodes |> emplace(QuadNode(
        depth = 0,
        center = config.rootCenter,
        halfSize = config.rootSize * 0.5f,
        state = NodeState.Loaded,
        parent = -1
    ))
    tree.config <- config

    load_node_entity(tree, 0)
    return <- tree
}

// ---------------------------------------------------------------------------
// Разбиение / Схлопывание
// ---------------------------------------------------------------------------
def public split_node(var tree : QuadTree&; nodeIdx : int) {
    if (tree.nodes[nodeIdx].state == NodeState.Split) { return }

    // Выгружаем сущность родителя, так как он больше не является листом
    unload_node_entity(tree, nodeIdx)

    let parentCenter = tree.nodes[nodeIdx].center
    let parentHalf = tree.nodes[nodeIdx].halfSize
    let childHalf = parentHalf * 0.5f
    let childDepth = tree.nodes[nodeIdx].depth + 1

    tree.nodes[nodeIdx].state = NodeState.Split

    for (dx in range(2)) {
        for (dy in range(2)) {
            let cx = parentCenter.x + (dx == 0 ? -childHalf : childHalf)
            let cy = parentCenter.y + (dy == 0 ? -childHalf : childHalf)

            let childIdx = length(tree.nodes)
            tree.nodes |> emplace(QuadNode(
                depth = childDepth,
                center = float2(cx, cy),
                halfSize = childHalf,
                state = NodeState.Loaded,
                parent = nodeIdx
            ))
            tree.nodes[nodeIdx].children |> push(childIdx)

            // Загружаем сущность для нового дочернего узла
            load_node_entity(tree, childIdx)
        }
    }
}

def public merge_node(var tree : QuadTree&; nodeIdx : int) {
    if (tree.nodes[nodeIdx].state != NodeState.Split) { return }

    // Копируем индексы детей для избежания блокировки массива при рекурсивных вызовах
    var childIndices := tree.nodes[nodeIdx].children// array<int>

    for (childIdx in childIndices) {
        if (tree.nodes[childIdx].state == NodeState.Split) {
            merge_node(tree, childIdx)
        }
        // Выгружаем сущности удаляемых детей
        unload_node_entity(tree, childIdx)
    }

    tree.nodes[nodeIdx].children |> clear()
    tree.nodes[nodeIdx].state = NodeState.Loaded

    // Восстанавливаем сущность родителя
    load_node_entity(tree, nodeIdx)
}

// ---------------------------------------------------------------------------
// Обновление структуры дерева
// ---------------------------------------------------------------------------
def public update_quadtree(var tree : QuadTree&; observerPos : float2) {
    update_node(tree, 0, observerPos)
}

def private update_node(var tree : QuadTree&; nodeIdx : int; observerPos : float2) {
    let dist = length(observerPos - tree.nodes[nodeIdx].center)
    let depth = tree.nodes[nodeIdx].depth
    let canGoDeeper = depth < tree.config.maxDepth

    // Размер узла и дистанции рассчитываются на лету из halfSize
    let nodeSize = tree.nodes[nodeIdx].halfSize * 2.0f
    let splitDist = nodeSize * tree.config.splitMultiplier
    let mergeDist = nodeSize * tree.config.mergeMultiplier

    if (canGoDeeper && dist < splitDist) {
        if (tree.nodes[nodeIdx].state == NodeState.Loaded) {
            split_node(tree, nodeIdx)
        }

        var childIndices := tree.nodes[nodeIdx].children
        for (childIdx in childIndices) {
            update_node(tree, childIdx, observerPos)
        }
    } else {
        if (tree.nodes[nodeIdx].state == NodeState.Split && dist > mergeDist) {
            merge_node(tree, nodeIdx)
        }
    }
}