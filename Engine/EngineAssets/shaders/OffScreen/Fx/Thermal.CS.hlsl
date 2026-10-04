// サーモグラフィ。明るさを温度に見立てて、暗い=冷たい（青）→ 明るい=熱い（黄・白）の色へ置き換える
#include "PostFxCommon.hlsli"

cbuffer ThermalParams : register(b0)
{
    POSTFX_FRAME_HEADER
    float gStrength; // 元の色との混ぜ具合
    float gContrast; // 温度差の強調
    float gNoise;    // センサーのざらつき
    int gPalette;    // 0=アイアン / 1=レインボー / 2=白熱（白黒）
};

float3 IronPalette(float t)
{
    // 黒 → 紺 → 紫 → 赤 → 橙 → 黄 → 白
    const float3 colors[7] = {
        float3(0.0f, 0.0f, 0.0f), float3(0.1f, 0.0f, 0.4f), float3(0.5f, 0.0f, 0.6f),
        float3(0.9f, 0.1f, 0.2f), float3(1.0f, 0.5f, 0.0f), float3(1.0f, 0.9f, 0.1f), float3(1.0f, 1.0f, 1.0f)};
    float x = saturate(t) * 6.0f;
    int i = min((int)x, 5);
    return lerp(colors[i], colors[i + 1], x - i);
}

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    int2 pixel = int2(id.xy);
    POSTFX_EARLY_OUT(pixel)

    float4 source = LoadSource(pixel);
    float heat = DisplayLuminance(source.rgb);
    heat = saturate((heat - 0.5f) * gContrast + 0.5f);
    heat = saturate(heat + (Hash12(float2(pixel) + frac(gTime * 13.0f) * 100.0f) - 0.5f) * gNoise);

    float3 mapped;
    if (gPalette == 0)
    {
        mapped = IronPalette(heat);
    }
    else if (gPalette == 1)
    {
        mapped = HsvToRgb(float3((1.0f - heat) * 0.7f, 1.0f, 0.4f + heat * 0.6f));
    }
    else
    {
        mapped = heat.xxx;
    }
    gOutput[pixel] = float4(lerp(source.rgb, DecodeDisplay(mapped), gStrength), source.a);
}
