// 陽炎。熱で空気が揺らぐように、上へ流れるノイズで画面を細かく揺らす
#include "PostFxCommon.hlsli"

cbuffer HeatHazeParams : register(b0)
{
    POSTFX_FRAME_HEADER
    float gStrength;  // 揺らぎの強さ（ピクセル）
    float gScale;     // 揺らぎの細かさ
    float gSpeed;     // 立ち上る速さ
    float gCoverage;  // 下からどこまで掛けるか（1で画面全体）
};

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    int2 pixel = int2(id.xy);
    POSTFX_EARLY_OUT(pixel)

    float2 uv = PixelToUv(pixel);
    float2 p = AspectUv(uv) * gScale;
    p.y += gTime * gSpeed;
    float2 n = float2(Fbm(p, 3), Fbm(p + float2(5.2f, 1.3f), 3)) - 0.5f;

    // 画面の下ほど強く（地面からの熱）。coverage=1 なら一様
    float fromBottom = 1.0f - uv.y;
    float mask = (gCoverage >= 0.999f) ? 1.0f : 1.0f - smoothstep(gCoverage * 0.6f, max(gCoverage, 0.001f), fromBottom);
    float2 offset = n * gStrength * mask / float2(gTextureSize);
    gOutput[pixel] = SampleSourceMirror(uv + offset);
}
