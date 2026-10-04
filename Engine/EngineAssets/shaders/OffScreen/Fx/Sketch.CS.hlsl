// 鉛筆画風。輪郭の線と、暗さに応じて重ねる斜線（ハッチング）で描き直す
#include "PostFxCommon.hlsli"

cbuffer SketchParams : register(b0)
{
    POSTFX_FRAME_HEADER
    float3 gInkColor;     // 線の色
    float gEdgeStrength;  // 輪郭線の濃さ
    float3 gPaperColor;   // 紙の色
    float gHatchSpacing;  // 斜線の間隔（ピクセル）
    float gHatchStrength; // 斜線の濃さ
    float gColorAmount;   // 元の色を残す量（0で白黒の鉛筆画）
    float gWobble;        // 線の揺れ
    float gPadding;
};

float Hatch(float2 p, float angle, float spacing, float threshold, float darkness)
{
    // 線は暗い所ほど太くなる
    float2 r = Rotate2D(p, angle);
    float v = abs(frac(r.y / spacing) - 0.5f) * 2.0f;
    float width = saturate((threshold - darkness) * -4.0f + 0.4f);
    return (darkness > threshold) ? (1.0f - smoothstep(width * 0.5f, width * 0.5f + 0.25f, v)) : 0.0f;
}

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    int2 pixel = int2(id.xy);
    POSTFX_EARLY_OUT(pixel)

    float2 uv = PixelToUv(pixel);
    float2 texel = 1.0f / float2(gTextureSize);
    float4 source = LoadSource(pixel);
    float l = DisplayLuminance(source.rgb);
    float darkness = 1.0f - l;

    // 輪郭（ソーベル）
    float gx = 0.0f;
    float gy = 0.0f;
    const float kx[9] = {-1, 0, 1, -2, 0, 2, -1, 0, 1};
    const float ky[9] = {-1, -2, -1, 0, 0, 0, 1, 2, 1};
    [unroll]
    for (int i = 0; i < 9; ++i)
    {
        float2 offset = float2(i % 3 - 1, i / 3 - 1) * texel * 1.2f;
        float s = DisplayLuminance(SampleSource(uv + offset).rgb);
        gx += s * kx[i];
        gy += s * ky[i];
    }
    float edge = saturate(length(float2(gx, gy)) * 1.5f) * gEdgeStrength;

    // 手描きらしく線を少し揺らす
    float2 p = float2(pixel) + (float2(ValueNoise(uv * 40.0f), ValueNoise(uv * 40.0f + 7.0f)) - 0.5f) * gWobble * 6.0f;
    float spacing = max(gHatchSpacing, 2.0f);
    float hatch = 0.0f;
    hatch = max(hatch, Hatch(p, 0.785f, spacing, 0.35f, darkness));
    hatch = max(hatch, Hatch(p, -0.785f, spacing, 0.55f, darkness));
    hatch = max(hatch, Hatch(p, 0.0f, spacing * 0.7f, 0.75f, darkness));
    hatch *= gHatchStrength;

    float ink = saturate(max(edge, hatch));
    float3 paper = lerp(gPaperColor, EncodeDisplay(source.rgb), gColorAmount);
    float3 color = lerp(paper, gInkColor, ink);
    gOutput[pixel] = float4(DecodeDisplay(color), source.a);
}
