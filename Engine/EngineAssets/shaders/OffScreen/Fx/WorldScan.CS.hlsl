// ワールドスキャン（ソナー）。ある地点から光の輪が地形を這うように広がっていく。
// 輪の中はうっすら色が付き、地面に格子が浮かぶ（探索・索敵・スキャナーの表現）
#include "PostFxCommon.hlsli"

cbuffer WorldScanParams : register(b0)
{
    POSTFX_FRAME_HEADER
    float4x4 gInverseViewProjection; // NDC → ワールド
    float3 gOrigin;                  // 広がり始める地点
    float gRadius;                   // 今の輪の半径
    float3 gColor;                   // 輪の色
    float gWidth;                    // 輪の太さ
    float gIntensity;                // 輪の明るさ
    float gTrail;                    // 輪の後ろに残る色の長さ
    float gGrid;                     // 格子の濃さ
    float gGridSize;                 // 格子の間隔
    float gFade;                     // 全体の濃さ（広がりきったら消える）
    float3 gPadding;
};

Texture2D<float> gDepth : register(t1);

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    int2 pixel = int2(id.xy);
    POSTFX_EARLY_OUT(pixel)

    float4 source = LoadSource(pixel);
    float depth = gDepth.Load(int3(pixel, 0));
    if (depth >= 1.0f || gFade <= 0.0f)
    {
        gOutput[pixel] = source;
        return;
    }

    float2 uv = PixelToUv(pixel);
    float4 ndc = float4(uv.x * 2.0f - 1.0f, 1.0f - uv.y * 2.0f, depth, 1.0f);
    float4 world = mul(ndc, gInverseViewProjection);
    world /= (abs(world.w) < 1e-6f) ? 1e-6f : world.w;

    float distance = length(world.xyz - gOrigin);
    float behind = gRadius - distance; // 輪の内側（もう通り過ぎた所）ほど大きい
    if (behind < 0.0f)
    {
        gOutput[pixel] = source;
        return;
    }
    float width = max(gWidth, 0.01f);
    // 先頭はくっきり明るく、後ろへ行くほど薄く
    float ring = exp(-behind / width * 3.0f);
    float trail = saturate(1.0f - behind / max(gTrail, 0.01f)) * 0.25f;

    // 地面の格子（ワールドの XZ）
    float2 cell = abs(frac(world.xz / max(gGridSize, 0.01f)) - 0.5f);
    float grid = smoothstep(0.47f, 0.5f, max(cell.x, cell.y)) * gGrid * saturate(trail * 4.0f + ring);

    float3 glow = gColor * (ring * gIntensity + trail + grid) * gFade;
    gOutput[pixel] = float4(source.rgb + glow, source.a);
}
