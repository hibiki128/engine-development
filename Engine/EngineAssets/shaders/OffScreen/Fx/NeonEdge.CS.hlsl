// ネオン輪郭。色の変わり目を光る線にして、まわりを暗く落とす（サイバー・電脳空間の表現）
#include "PostFxCommon.hlsli"

cbuffer NeonEdgeParams : register(b0)
{
    POSTFX_FRAME_HEADER
    float3 gColor;      // 線の色（単色のとき）
    float gThreshold;   // 線にする差の大きさ
    float gGlow;        // 光のにじむ広さ（ピクセル）
    float gIntensity;   // 線の明るさ
    float gHueSpeed;    // 虹色を流す速さ
    int gColorMode;     // 0=元の色 / 1=虹色 / 2=単色
    float gBackground;  // 元の絵を残す量
    float3 gPadding;
};

float EdgeAt(float2 uv)
{
    float2 texel = 1.0f / float2(gTextureSize);
    float l = DisplayLuminance(SampleSource(uv - float2(texel.x, 0)).rgb);
    float r = DisplayLuminance(SampleSource(uv + float2(texel.x, 0)).rgb);
    float t = DisplayLuminance(SampleSource(uv - float2(0, texel.y)).rgb);
    float b = DisplayLuminance(SampleSource(uv + float2(0, texel.y)).rgb);
    float g = length(float2(r - l, b - t));
    return smoothstep(gThreshold, gThreshold * 2.0f + 0.02f, g);
}

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    int2 pixel = int2(id.xy);
    POSTFX_EARLY_OUT(pixel)

    float2 uv = PixelToUv(pixel);
    float4 source = LoadSource(pixel);
    float edge = EdgeAt(uv);

    // 周りの輪郭を拾ってにじませる（2重の輪で8方向ずつ）
    float glow = 0.0f;
    float2 texel = gGlow / float2(gTextureSize);
    [unroll]
    for (int i = 0; i < 8; ++i)
    {
        float a = i * 0.785398f;
        float2 dir = float2(cos(a), sin(a));
        glow += EdgeAt(uv + dir * texel) * 0.6f;
        glow += EdgeAt(uv + dir * texel * 0.45f);
    }
    glow /= 12.8f;

    float3 color;
    if (gColorMode == 0)
    {
        float3 display = EncodeDisplay(source.rgb);
        float3 hsv = RgbToHsv(display);
        color = HsvToRgb(float3(hsv.x, saturate(hsv.y * 1.5f + 0.3f), 1.0f));
    }
    else if (gColorMode == 1)
    {
        color = HsvToRgb(float3(frac(uv.x * 0.5f + uv.y * 0.3f + gTime * gHueSpeed), 0.85f, 1.0f));
    }
    else
    {
        color = gColor;
    }
    float3 neon = color * (edge + glow) * gIntensity;
    gOutput[pixel] = float4(source.rgb * gBackground + neon, source.a);
}
