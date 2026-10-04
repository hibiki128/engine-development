// 方向ブラー。決めた向きへ一直線にぼかす（高速移動・ダッシュの表現）
#include "PostFxCommon.hlsli"

cbuffer DirectionalBlurParams : register(b0)
{
    POSTFX_FRAME_HEADER
    float gAngle;    // 向き（度）
    float gLength;   // ぼかしの長さ（ピクセル）
    int gSamples;    // 分割数
    float gCenterMask; // 画面の中心をぼかさない量（0で全体）
};

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    int2 pixel = int2(id.xy);
    POSTFX_EARLY_OUT(pixel)

    float2 uv = PixelToUv(pixel);
    float2 dir = float2(cos(radians(gAngle)), sin(radians(gAngle))) * gLength / float2(gTextureSize);
    float2 p = (uv - 0.5f) * float2(AspectRatio(), 1.0f);
    dir *= lerp(1.0f, saturate(length(p) * 2.0f), gCenterMask);

    int samples = clamp(gSamples, 2, 48);
    float4 sum = 0.0f;
    for (int i = 0; i < samples; ++i)
    {
        float t = float(i) / float(samples - 1) - 0.5f;
        sum += SampleSource(uv + dir * t);
    }
    gOutput[pixel] = sum / samples;
}
