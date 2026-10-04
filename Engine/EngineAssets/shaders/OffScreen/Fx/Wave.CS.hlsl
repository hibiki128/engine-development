// 画面の揺らぎ。サイン波で画面を横・縦・同心円に揺らす（回想・夢・酔いの表現）
#include "PostFxCommon.hlsli"

cbuffer WaveParams : register(b0)
{
    POSTFX_FRAME_HEADER
    float gAmplitude;  // 揺れ幅（画面の高さ=1）
    float gFrequency;  // 波の数
    float gSpeed;      // 波の進む速さ
    int gDirection;    // 0=横に揺れる / 1=縦に揺れる / 2=両方 / 3=同心円
    float2 gCenter;    // 同心円の中心（UV）
    float gChroma;     // 揺れに合わせて色をずらす量
    float gPadding;
};

float2 Offset(float2 uv)
{
    float phase = gTime * gSpeed;
    float2 offset = 0.0f;
    if (gDirection == 0 || gDirection == 2)
    {
        offset.x += sin(uv.y * gFrequency * kTwoPi + phase) * gAmplitude;
    }
    if (gDirection == 1 || gDirection == 2)
    {
        offset.y += sin(uv.x * gFrequency * kTwoPi + phase * 1.13f) * gAmplitude;
    }
    if (gDirection == 3)
    {
        float2 p = (uv - gCenter) * float2(AspectRatio(), 1.0f);
        float d = length(p);
        offset = (d > 1e-4f) ? (p / d) * sin(d * gFrequency * kTwoPi - phase) * gAmplitude : 0.0f;
        offset.x /= AspectRatio();
    }
    return offset;
}

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    int2 pixel = int2(id.xy);
    POSTFX_EARLY_OUT(pixel)

    float2 uv = PixelToUv(pixel);
    float2 offset = Offset(uv);
    float4 color = SampleSourceMirror(uv + offset);
    if (gChroma > 0.0f)
    {
        color.r = SampleSourceMirror(uv + offset * (1.0f + gChroma)).r;
        color.b = SampleSourceMirror(uv + offset * (1.0f - gChroma)).b;
    }
    gOutput[pixel] = color;
}
