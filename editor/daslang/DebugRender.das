options gen2

module DebugRender shared public

struct DebugLine{
    start, end : float4
    color : uint8[4]
    _padding : uint3
}

var DebugLines : array<DebugLine>

enum Color {
    Red,
    Blue,
    Yellow,
    Black,
    White
}

def GetColor(color : Color) : uint8[4]{
    if (color == Color.Red)
        return fixed_array(255u8, 0u8, 0u8, 255u8)
    elif (color == Color.Blue)
        return fixed_array(0u8, 0u8, 255u8, 255u8)
    elif (color == Color.Yellow){
        return fixed_array(255u8, 255u8, 0u8, 255u8)
    }

    return fixed_array(255u8, 255u8, 255u8, 255u8);
}

def AddLine(start, end : float3; color : Color){
    var line : DebugLine
    line.start = float4(start.x, start.y, start.z, 0)
    line.end = float4(end.x, end.y, end.z, 0)
    line.color = GetColor(color)

    DebugLines |> emplace(line)
}

[export]
def GetDebugLines : array<DebugLine> ? {
    // DebugLines.clear()
    return unsafe(addr(DebugLines));
}

[export]
def ClearDebugLines{
    DebugLines.clear()
}