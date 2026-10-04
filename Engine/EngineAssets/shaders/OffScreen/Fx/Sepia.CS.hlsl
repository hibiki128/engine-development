// セピア調。明るさで「暗部の色 → 明部の色」へ塗り直し、古い写真のような単色にする
#include "PostFxCommon.hlsli"

cbuffer SepiaParams : register(b0)
{
    POSTFX_FRAME_HEADER
    float3 gShadowColor;    // 暗い所の色
    float gStrength;        // 元の色との混ぜ具合
    float3 gHighlightColor; // 明るい所の色
    float gContrast;        // コントラスト
};

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    int2 pixel = int2(id.xy);
    POSTFX_EARLY_OUT(pixel)

    float4 source = LoadSource(pixel);
    float l = DisplayLuminance(source.rgb);
    l = saturate((l - 0.5f) * gContrast + 0.5f);
    float3 toned = DecodeDisplay(lerp(gShadowColor, gHighlightColor, l));
    gOutput[pixel] = float4(lerp(source.rgb, toned, gStrength), source.a);
}
