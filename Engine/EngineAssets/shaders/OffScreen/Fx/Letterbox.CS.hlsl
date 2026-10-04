// 黒帯（レターボックス）。映画の画面比率になるよう上下（または左右）に帯を出す。
// 「出し具合」を 0→1 へ動かすと、会話シーンやムービーに入るときの帯がせり出す演出になる
#include "PostFxCommon.hlsli"

cbuffer LetterboxParams : register(b0)
{
    POSTFX_FRAME_HEADER
    float3 gColor;     // 帯の色
    float gAmount;     // 出し具合（0〜1）
    float gAspect;     // 目標の画面比率（横/縦。2.35 でシネスコ）
    float gSoftness;   // 帯の境目のぼかし（ピクセル）
    float gOpacity;    // 帯の濃さ
    float gPadding;
};

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    int2 pixel = int2(id.xy);
    POSTFX_EARLY_OUT(pixel)

    float4 source = LoadSource(pixel);
    float screenAspect = AspectRatio();
    float target = max(gAspect, 0.1f);
    float2 p = float2(pixel) + 0.5f;
    float bar;
    float distanceToEdge;
    float softness = max(gSoftness, 0.5f);
    if (target >= screenAspect)
    {
        // 横長にする: 上下に帯
        float barHeight = (1.0f - screenAspect / target) * 0.5f * gTextureSize.y * saturate(gAmount);
        distanceToEdge = min(p.y, gTextureSize.y - p.y);
        bar = 1.0f - smoothstep(barHeight - softness, barHeight + softness, distanceToEdge);
        bar *= step(0.5f, barHeight);
    }
    else
    {
        // 縦長にする: 左右に帯
        float barWidth = (1.0f - target / screenAspect) * 0.5f * gTextureSize.x * saturate(gAmount);
        distanceToEdge = min(p.x, gTextureSize.x - p.x);
        bar = 1.0f - smoothstep(barWidth - softness, barWidth + softness, distanceToEdge);
        bar *= step(0.5f, barWidth);
    }
    gOutput[pixel] = float4(lerp(source.rgb, gColor, bar * gOpacity), source.a);
}
