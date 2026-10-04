// 渦巻き。中心のまわりを、中心ほど大きくねじる
#include "PostFxCommon.hlsli"

cbuffer SwirlParams : register(b0)
{
    POSTFX_FRAME_HEADER
    float2 gCenter;  // 中心（UV）
    float gRadius;   // ねじる範囲（画面の高さ=1）
    float gAngle;    // 中心でのねじれ（度）
    float gSpeed;    // ねじれを回し続ける速さ（度/秒）
    float3 gPadding;
};

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    int2 pixel = int2(id.xy);
    POSTFX_EARLY_OUT(pixel)

    float2 uv = PixelToUv(pixel);
    float aspect = AspectRatio();
    float2 p = (uv - gCenter) * float2(aspect, 1.0f);
    float d = length(p);
    float radius = max(gRadius, 1e-4f);
    if (d < radius)
    {
        float t = 1.0f - d / radius;
        float angle = radians(gAngle + gSpeed * gTime) * t * t;
        p = Rotate2D(p, angle);
    }
    float2 sampleUv = p / float2(aspect, 1.0f) + gCenter;
    gOutput[pixel] = SampleSourceMirror(sampleUv);
}
