options gen2

module DebugRender shared public

struct DebugLine{
    start : float3
    color : uint8[4]
    end : float3
    _padding : uint
}

var DebugLines : array<DebugLine>

enum Color {
    Red,
    Blue,
    Yellow,
    Black,
    White,
    Green,
    Grey
}

def GetColor(color : Color) : uint8[4]{
    if (color == Color.Red){
        return fixed_array(255u8, 0u8, 0u8, 255u8)
    } elif (color == Color.Blue){
        return fixed_array(0u8, 0u8, 255u8, 255u8)
    } elif (color == Color.Yellow) {
        return fixed_array(255u8, 255u8, 0u8, 255u8)
    } elif (color == Color.Green) {
        return fixed_array(0u8, 255u8, 0u8, 255u8)
    } elif (color == Color.Grey) {
        return fixed_array(100u8, 100u8, 100u8, 255u8)
    }
    return fixed_array(255u8, 255u8, 255u8, 255u8);
}

def AddLine(start, end : float3; color : Color){
    DebugLines |> emplace(DebugLine(start = start, end = end, color = GetColor(color)))
}

[export]
def GetDebugLines : array<DebugLine> ? {
    return unsafe(addr(DebugLines));
}

[export]
def ClearDebugLines{
    DebugLines.clear()
}
