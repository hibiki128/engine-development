#include "Line3d.hlsli"

float4 main(VSOutput input) : SV_TARGET
{
    // 入力された色をそのまま返す
    return input.color;
}
