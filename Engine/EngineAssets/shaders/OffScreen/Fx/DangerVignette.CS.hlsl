// ピンチ演出（体力が少ないとき）。画面の縁が脈打つように赤く染まり、色が抜ける
#include "PostFxCommon.hlsli"

cbuffer DangerVignetteParams : register(b0)
{
    POSTFX_FRAME_HEADER
    float3 gColor;      // 縁の色
    float gIntensity;   // 強さ（体力に合わせて 0〜1 で動かす）
    float gPulseSpeed;  // 脈の速さ（回/秒）
    float gPulseAmount; // 脈の大きさ
    float gRadius;      // 縁の広さ
    float gDesaturate;  // 色の抜け具合
    float gVeins;       // 縁のむら（血管のような模様）
    float3 gPadding;
};

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    int2 pixel = int2(id.xy);
    POSTFX_EARLY_OUT(pixel)

    float2 uv = PixelToUv(pixel);
    float4 source = LoadSource(pixel);

    // 心臓の鼓動のように「ドクン、ドクン」と2回ずつ脈打つ
    float beat = frac(gTime * gPulseSpeed);
    float pulse = exp(-beat * 12.0f) + exp(-abs(beat - 0.22f) * 14.0f) * 0.6f;
    float strength = saturate(gIntensity) * (1.0f + pulse * gPulseAmount);

    float2 p = (uv - 0.5f) * float2(AspectRatio(), 1.0f);
    float d = length(p) / length(float2(AspectRatio(), 1.0f) * 0.5f);
    float veins = (Fbm(uv * float2(AspectRatio(), 1.0f) * 6.0f + gTime * 0.05f, 3) - 0.5f) * gVeins;
    float edge = smoothstep(1.0f - gRadius, 1.0f, d + veins) * strength;

    float3 display = EncodeDisplay(source.rgb);
    float gray = Luminance(display);
    display = lerp(display, gray.xxx, saturate(gDesaturate * gIntensity));
    display = lerp(display, gColor, saturate(edge));
    gOutput[pixel] = float4(DecodeDisplay(display), source.a);
}
