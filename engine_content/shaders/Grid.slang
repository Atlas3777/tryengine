// Бесконечная сетка редактора. Полноэкранный треугольник: луч каждого пикселя
// пересекается с мировой плоскостью Z=0, поэтому у сетки нет ни размера, ни шва между
// тайлами, и ей не нужна ни одна вершина в сцене.
//
// Считается по мотивам 3D Graphics Rendering Cookbook (chapter05/GridCalculation.h):
// минорная клетка выбирается по LOD, линии антиалиасятся footprint'ом пикселя, а
// непрозрачность гаснет с расстоянием от камеры по плоскости.
//
// Глубина точки пересечения уходит в SV_Depth: геометрия, отрисованная раньше обычным
// opaque-проходом, перекрывает сетку тем же depth-тестом. discard не используется:
// устройство не включает shaderDemoteToHelperInvocation, а пиксель без попадания и так
// отдаёт нулевую альфу.

// Fullscreen editor grid, not a mesh material.
public static const uint MESH_ATTRIBUTES = 0xFFFFFFFEu;

struct GridCamera
{
    float4x4 view;
    float4x4 proj;
    float4x4 viewInverse;
    float4x4 projInverse;
    float3   position;
    float    _pad;
};

struct GridPushConstants
{
    GridCamera* camera;
    float2 screenSize;
    float  cellSize;               // базовая минорная клетка в мировых юнитах
    float  minPixelsBetweenCells;  // порог LOD: минимум пикселей между линиями
    float  gridSize;               // радиус, на котором сетка гаснет вокруг камеры
    float  _pad;
};

[vk::push_constant] GridPushConstants pc;

struct VSOutput
{
    float4 position : SV_Position;
};

struct FSOutput
{
    float4 color : SV_Target;
    float  depth : SV_Depth;
};

// Цвета линий: тонкие и толстые отличаются и цветом, и непрозрачностью — толстый уровень
// это клетка на порядок крупнее, её видно дальше.
static const float3 GRID_THIN_COLOR = float3(0.55f, 0.57f, 0.62f);
static const float3 GRID_THICK_COLOR = float3(0.70f, 0.72f, 0.78f);
static const float3 AXIS_X_COLOR = float3(0.75f, 0.32f, 0.32f);
static const float3 AXIS_Y_COLOR = float3(0.36f, 0.70f, 0.40f);

static const float GRID_THIN_ALPHA = 0.35f;
static const float GRID_THICK_ALPHA = 0.70f;
static const float AXIS_ALPHA = 0.90f;

// Ширина антиалиасящейся линии в пикселях.
static const float LINE_WIDTH_PX = 4.0f;

[shader("vertex")]
VSOutput vsMain(uint vertexID : SV_VertexID)
{
    VSOutput output;
    // Один треугольник на весь экран: (0,0) -> (-1,-1), (2,0) -> (3,-1), (0,2) -> (-1,3).
    float2 corner = float2((vertexID << 1) & 2, vertexID & 2);
    output.position = float4(corner * 2.0 - 1.0, 0.0, 1.0);
    return output;
}

// Сигнал сетки с клеткой cell: 1 на линии, 0 в остальных местах. lineDudv — footprint
// пикселя, растянутый на ширину линии; деление на него даёт рампу ровно в один пиксель.
float GridSignal(float2 uv, float2 lineDudv, float cell)
{
    float2 cellUv = uv - cell * floor(uv / cell);
    float2 line = saturate(cellUv / lineDudv);
    float2 centered = 1.0 - abs(line * 2.0 - 1.0);
    return max(centered.x, centered.y);
}

[shader("fragment")]
FSOutput fsMain(float4 position : SV_Position)
{
    FSOutput output;

    float2 ndc = position.xy / pc.screenSize * 2.0 - 1.0;

    // Дальняя точка луча: клип -> view -> world обратными матрицами камеры.
    float4 farView = mul(float4(ndc, 1.0, 1.0), pc.camera->projInverse);
    float4 farWorld = mul(farView / farView.w, pc.camera->viewInverse);

    float3 origin = pc.camera->position;
    float3 direction = normalize(farWorld.xyz / farWorld.w - origin);

    // Пересечение с плоскостью Z=0. Луч, параллельный плоскости или уходящий от неё,
    // не рисует пиксель, но точку для сетки всё равно получает: ddx/ddy должны считаться
    // для всех пикселей, иначе на границе горизонта производные не определены.
    float denominator = direction.z;
    bool parallel = abs(denominator) < 1e-6;
    float distance = parallel ? 0.0 : -origin.z / denominator;
    bool hitsPlane = !parallel && distance > 0.0;

    float3 hit = origin + direction * (hitsPlane ? distance : 1.0e4);
    float2 uv = hit.xy;

    // dudv — сколько мировых юнитов приходится на пиксель по каждой оси. Это и мера
    // антиалиасинга, и вход LOD: чем мельче сетка в пикселях, тем крупнее берётся клетка.
    float2 dudv = float2(length(float2(ddx(uv.x), ddy(uv.x))), length(float2(ddx(uv.y), ddy(uv.y))));
    dudv = clamp(dudv, 1e-5f, 1e4f);

    // Обычная глубина плоскости: по ней сетку перекрывает геометрия сцены.
    float4 clip = mul(mul(float4(hit, 1.0), pc.camera->view), pc.camera->proj);
    float depth = clip.z / clip.w;

    // LOD: как только между линиями остаётся меньше minPixelsBetweenCells пикселей,
    // минорная клетка вырастает на порядок. Дробная часть уровня — плавный переход.
    float lodLevel = max(0.0f, log10(length(dudv) * pc.minPixelsBetweenCells / pc.cellSize) + 1.0f);
    float lodFade = frac(lodLevel);
    float lod0 = pc.cellSize * pow(10.0f, floor(lodLevel));
    float lod1 = lod0 * 10.0f;
    float lod2 = lod1 * 10.0f;

    // Линия шириной до LINE_WIDTH_PX пикселей: footprint раздувается на её ширину, а
    // координата сдвигается на полширины, чтобы линия легла центром на границу клетки.
    float2 lineDudv = dudv * LINE_WIDTH_PX;
    float2 lineUv = uv + lineDudv * 0.5f;

    float lod0a = GridSignal(lineUv, lineDudv, lod0);
    float lod1a = GridSignal(lineUv, lineDudv, lod1);
    float lod2a = GridSignal(lineUv, lineDudv, lod2);

    // Цвет уровня: далёкие крупные клетки — «толстые» линии, ближние — тонкие.
    float3 lineColor = GRID_THIN_COLOR;
    float lineAlpha = lod0a * (1.0f - lodFade) * GRID_THIN_ALPHA;
    if (lod1a > 0.0f) {
        lineColor = lerp(GRID_THICK_COLOR, GRID_THIN_COLOR, lodFade);
        lineAlpha = lod1a * lerp(GRID_THICK_ALPHA, GRID_THIN_ALPHA, lodFade);
    }
    if (lod2a > 0.0f) {
        lineColor = GRID_THICK_COLOR;
        lineAlpha = lod2a * GRID_THICK_ALPHA;
    }

    // Falloff вокруг камеры по плоскости: у горизонта линии сходятся плотнее пикселя,
    // и без него сетка мерцает.
    float falloff = 1.0f - saturate(length(uv - origin.xy) / pc.gridSize);
    lineAlpha *= falloff;

    // Оси — отдельные линии поверх сетки: X — это y = 0, Y — это x = 0. Расстояние до
    // линии в пикселях берётся по перпендикулярной оси footprint'а.
    float2 axisPx = abs(uv) / dudv;
    float axisX = saturate(1.0f - axisPx.y) * falloff * AXIS_ALPHA;
    float axisY = saturate(1.0f - axisPx.x) * falloff * AXIS_ALPHA;

    // Слои складываются premultiplied over: сетка, поверх неё оси.
    float3 color = lineColor * lineAlpha;
    float alpha = lineAlpha;
    color = AXIS_X_COLOR * axisX + color * (1.0f - axisX);
    alpha = axisX + alpha * (1.0f - axisX);
    color = AXIS_Y_COLOR * axisY + color * (1.0f - axisY);
    alpha = axisY + alpha * (1.0f - axisY);

    // Пиксель без пересечения с плоскостью или за пределами near/far не рисуется. Альфа 0
    // не меняет цвет сцены, а глубина 1.0 гарантированно проходит depth-тест.
    bool visible = hitsPlane && depth >= 0.0f && depth <= 1.0f;
    output.color = visible ? float4(color, alpha) : float4(0.0f, 0.0f, 0.0f, 0.0f);
    output.depth = visible ? depth : 1.0f;
    return output;
}
