#include "Line3d.hlsli"

struct PSInput
{
    float4 pos : SV_POSITION;
    float4 color : COLOR0;
};

float4 main(PSInput input) : SV_TARGET
{
    // 入力された色をそのまま返す
    return input.color;
}
