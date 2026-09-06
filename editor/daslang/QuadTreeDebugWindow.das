options gen2
options _allow_imgui_legacy = true

module QuadTreeDebugWindow shared

require imgui
require imgui/imgui_widgets_builtin
require imgui/imgui_containers_builtin
require imgui/imgui_layout_builtin
require imgui/imgui_id_builtin
require imgui/imgui_boost_runtime
require daslib/decs
require daslib/decs_boost
require math

require tryengine/QuadTree

var public g_observerPos : float2 = float2(2000.0f, 2000.0f)
var public g_observerAngle : float = 0.0f
var public g_tree : QuadTree

def public init_quadtree_debug() {
    var cfg : QuadTreeConfig
    cfg.rootSize = 4000.0f
    cfg.rootCenter = float2(2000.0f, 2000.0f)
    cfg.minChunkSize = 250.0f
    cfg.splitMultiplier = 1.3f
    cfg.mergeMultiplier = 1.5f
    cfg.fovAngle = PI / 3
    cfg.viewDistance = 5000.0f

    g_tree <- create_quadtree(cfg)
}

def private draw_node_recursive(
                                tree : QuadTree&;
                                nodeIdx : int;
                                canvasPos : float2;
                                viewSize : float;
                                worldMin, worldSize : float2;
                                observerPos : float2;
                                observerAngle : float
                                ) {
    if (nodeIdx < 0 || nodeIdx >= length(tree.nodes)) { return }

    assume node = tree.nodes[nodeIdx]

    if (node.state == NodeState.Split) {
        for (childIdx in node.children) {
            draw_node_recursive(tree, childIdx, canvasPos, viewSize, worldMin, worldSize, observerPos, observerAngle)
        }
        return
    }

    // Проекция координат прямоугольника узла в ImGui Screen Space
    let minWorld = node.center - float2(node.halfSize)
    let maxWorld = node.center + float2(node.halfSize)

    let minTx = (minWorld.x - worldMin.x) / worldSize.x
    let minTy = 1.0f - (minWorld.y - worldMin.y) / worldSize.y
    let minP = canvasPos + float2(minTx * viewSize, minTy * viewSize)

    let maxTx = (maxWorld.x - worldMin.x) / worldSize.x
    let maxTy = 1.0f - (maxWorld.y - worldMin.y) / worldSize.y
    let maxP = canvasPos + float2(maxTx * viewSize, maxTy * viewSize)

    let rectMin = float2(min(minP.x, maxP.x), min(minP.y, maxP.y))
    let rectMax = float2(max(minP.x, maxP.x), max(minP.y, maxP.y))

    // Проверка попадания в Frustum
    let inFrustum = is_node_in_frustum(
        node.center, node.halfSize,
        observerPos, observerAngle,
        tree.config.fovAngle, tree.config.viewDistance
    )

    var strokeCol = uint(0xFF444444)
    var fillCol = uint(0x00000000)

    if (inFrustum) {
        if (node.depth == 0) { strokeCol = uint(0xFF00AAFF); }
        elif (node.depth == 1) { strokeCol = uint(0xFF00FFAA); }
        elif (node.depth == 2) { strokeCol = uint(0xFFFFFF00); }
        else { strokeCol = uint(0xFF00FFFF); }

        fillCol = (strokeCol & 0x00FFFFFF) | 0x20000000
    }

        if (fillCol != 0u) {
            *GetWindowDrawList() |> AddRectFilled(rectMin, rectMax, fillCol, 0.0f, ImDrawFlags.None)
        }
        *GetWindowDrawList() |> AddRect(rectMin, rectMax, strokeCol, 0.0f, ImDrawFlags.None, inFrustum ? 1.5f : 1.0f)

        // Отображаем тег сущности из DECS для активных узлов
        if (inFrustum && (rectMax.x - rectMin.x) > 30.0f) {
            let textPos = rectMin + float2(4.0f, 4.0f)
            var labell = "D{node.depth}"

            if (length(node.ownedEntities) > 0) {
                let firstEid = node.ownedEntities[0]
                query(firstEid) $(tag : string) {
                    labell = "{tag}"
                }
            }

            *GetWindowDrawList() |> AddText(textPos, uint(0xEEFFFFFF), labell)
        }
}

def public DrawQuadTreeDebug() {
    window(QUAD, (text = "QuadTree Visualizer", closable = false, flags = ImGuiWindowFlags.None)) {
        // Управление: WASD — Перемещение, Q/E — Поворот
        if (IsWindowFocused()) {
            let speed = 20.0f
            let rotSpeed = 0.05f

            if (IsKeyDown(ImGuiKey.W)) { g_observerPos.y += speed }
            if (IsKeyDown(ImGuiKey.S)) { g_observerPos.y -= speed }
            if (IsKeyDown(ImGuiKey.A)) { g_observerPos.x -= speed }
            if (IsKeyDown(ImGuiKey.D)) { g_observerPos.x += speed }
            if (IsKeyDown(ImGuiKey.Q)) { g_observerAngle += rotSpeed }
            if (IsKeyDown(ImGuiKey.E)) { g_observerAngle -= rotSpeed }
        }

        update_quadtree(g_tree, g_observerPos)

        text("Управление: WASD — Движение | Q/E — Вращение взгляда")
        text("Позиция: X={g_observerPos.x:.0f}, Y={g_observerPos.y:.0f} | Угол: {g_observerAngle:.2f} rad")
        separator()

        let canvasPos = GetCursorScreenPos()
        let canvasSize = GetContentRegionAvail()
        if (canvasSize.x < 50.0f || canvasSize.y < 50.0f) { return }

        let viewSize = min(canvasSize.x, canvasSize.y)

        let rootCenter = g_tree.config.rootCenter
        let rootHalf = g_tree.config.rootSize * 0.5f
        let worldMin = rootCenter - float2(rootHalf)
        let worldMax = rootCenter + float2(rootHalf)
        let worldSize = worldMax - worldMin

        let worldToScreen = $(p : float2) : float2 {
            let tx = (p.x - worldMin.x) / worldSize.x
            let ty = 1.0f - (p.y - worldMin.y) / worldSize.y
            return canvasPos + float2(tx * viewSize, ty * viewSize)
        }

            // Фон
            *GetWindowDrawList() |> AddRectFilled(canvasPos, canvasPos + float2(viewSize), uint(0xFF09090B), 0.0f, ImDrawFlags.None)

            // Дерево и узлы
            draw_node_recursive(g_tree, 0, canvasPos, viewSize, worldMin, worldSize, g_observerPos, g_observerAngle)

            // Отрисовка конуса Frustum через перевод мировых точек в экранные
            let obsScr = worldToScreen(g_observerPos)
            let halfFov = g_tree.config.fovAngle * 0.5f
            let leftA = g_observerAngle - halfFov
            let rightA = g_observerAngle + halfFov

            let viewDist = g_tree.config.viewDistance
            let pLeftWorld = g_observerPos + float2(cos(leftA), sin(leftA)) * viewDist
            let pRightWorld = g_observerPos + float2(cos(rightA), sin(rightA)) * viewDist

            let pLeftScr = worldToScreen(pLeftWorld)
            let pRightScr = worldToScreen(pRightWorld)

            *GetWindowDrawList() |> AddLine(obsScr, pLeftScr, uint(0xAA00FFCC), 1.5f)
            *GetWindowDrawList() |> AddLine(obsScr, pRightScr, uint(0xAA00FFCC), 1.5f)
            *GetWindowDrawList() |> AddLine(pLeftScr, pRightScr, uint(0x6600FFCC), 1.0f)

            // Игрок
            *GetWindowDrawList() |> AddCircleFilled(obsScr, 5.0f, uint(0xFF00FFCC), 16)
    }
}