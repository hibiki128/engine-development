// アナモルフィックの光の筋。明るい所から横一直線に光が伸びる（映画のレンズ・SF の光源）
#include "PostFxCommon.hlsli"

cbuffer AnamorphicStreakParams : register(b0)
{
    POSTFX_FRAME_HEADER
    float gThreshold; // これより明るい所だけが伸びる（HDRの明るさ）
    float gIntensity; // 筋の明るさ
    float gLength;    // 筋の長さ（ピクセル）
    int gSamples;     // 片側の分割数
    float3 gTint;     // 筋の色（青みが定番）
    float gFalloff;   // 先へ行くほど弱まる度合い
    float gAngle;     // 筋の向き（度。0で横）
    float3 gPadding;
};

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    int2 pixel = int2(id.xy);
    POSTFX_EARLY_OUT(pixel)

    float2 uv = PixelToUv(pixel);
    float4 source = LoadSource(pixel);
    float2 dir = float2(cos(radians(gAngle)), sin(radians(gAngle))) / float2(gTextureSize);
    int samples = clamp(gSamples, 4, 64);
    float stepLength = gLength / samples;

    float3 sum = 0.0f;
    float total = 0.0f;
    for (int i = -samples; i <= samples; ++i)
    {
        float t = abs(float(i)) / samples;
        float weight = exp(-t * gFalloff);
        float3 c = SampleSource(uv + dir * (float(i) * stepLength)).rgb;
        sum += max(c - gThreshold, 0.0f) * weight;
        total += weight;
    }
    float3 streak = sum / max(total, 1e-4f) * gIntensity * gTint;
    gOutput[pixel] = float4(source.rgb + streak, source.a);
}
