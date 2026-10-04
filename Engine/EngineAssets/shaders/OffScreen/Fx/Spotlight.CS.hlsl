// スポットライト。決めた点のまわりだけを明るく残し、外を暗く落とす（懐中電灯・注目・ホラーの表現）
#include "PostFxCommon.hlsli"

cbuffer SpotlightParams : register(b0)
{
    POSTFX_FRAME_HEADER
    float2 gCenter;     // 光の中心（UV）
    float gRadius;      // 光の半径（画面の高さ=1）
    float gSoftness;    // ふちのぼかし
    float3 gDarkColor;  // 暗い所の色
    float gDarkness;    // 外の暗さ
    float gPulse;       // 半径の揺れ（ゆらめき）
    float gPulseSpeed;  // 揺れの速さ
    float gAspectX;     // 楕円にする（横の伸び）
    float gPadding;
};

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    int2 pixel = int2(id.xy);
    POSTFX_EARLY_OUT(pixel)

    float2 uv = PixelToUv(pixel);
    float4 source = LoadSource(pixel);
    float2 p = (uv - gCenter) * float2(AspectRatio() / max(gAspectX, 0.1f), 1.0f);
    float flicker = (ValueNoise(float2(gTime * gPulseSpeed, 0.5f)) - 0.5f) * 2.0f;
    float radius = max(gRadius * (1.0f + flicker * gPulse), 0.001f);
    float light = 1.0f - smoothstep(radius * (1.0f - gSoftness), radius, length(p));
    float3 dark = lerp(source.rgb, gDarkColor, gDarkness);
    gOutput[pixel] = float4(lerp(dark, source.rgb, light), source.a);
}
