// 網点（漫画のトーン・印刷物）。明るさを点の大きさに置き換える。CMYKの4色刷りにもできる
#include "PostFxCommon.hlsli"

cbuffer HalftoneParams : register(b0)
{
    POSTFX_FRAME_HEADER
    float3 gInkColor;  // インクの色（白黒のとき）
    float gDotSize;    // 網点の間隔（ピクセル）
    float3 gPaperColor; // 紙の色
    float gAngle;      // 網の角度（度）
    int gMode;         // 0=白黒 / 1=CMYK / 2=元の色の点
    float gStrength;   // 元の色との混ぜ具合
    float gSoftness;   // 点のふちのぼかし
    float gPadding;
};

/// 回した格子の中で、マスの中心（画面のピクセル座標）と、中心からの距離（マスの大きさ=1）を求める
float DotCoverage(float2 p, float angle, float cell, out float2 cellCenter)
{
    float2 r = Rotate2D(p, angle);
    float2 cellIndex = floor(r / cell);
    float2 local = frac(r / cell) - 0.5f;
    cellCenter = Rotate2D((cellIndex + 0.5f) * cell, -angle);
    return length(local);
}

float DotMask(float distanceToCenter, float amount, float softness)
{
    // 点の面積がマスの amount 倍になる半径（円が隣と触れる π/4 まで）。
    // それより濃い所は、角が埋まりきる √0.5 まで半径を伸ばす
    float a = saturate(amount);
    float radius = (a < 0.785398f) ? sqrt(a / kPi) : lerp(0.5f, 0.7072f, (a - 0.785398f) / (1.0f - 0.785398f));
    return 1.0f - smoothstep(radius - softness, radius + softness, distanceToCenter);
}

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    int2 pixel = int2(id.xy);
    POSTFX_EARLY_OUT(pixel)

    float2 p = float2(pixel) + 0.5f;
    float cell = max(gDotSize, 2.0f);
    float softness = max(gSoftness, 0.01f) * 0.2f;
    float4 source = LoadSource(pixel);
    float3 result;

    if (gMode == 1)
    {
        // C・M・Y・K をそれぞれ違う角度の網で刷る（モアレを避ける定番の角度）
        const float angles[4] = {15.0f, 75.0f, 0.0f, 45.0f};
        float3 paper = float3(1.0f, 1.0f, 1.0f);
        float3 ink = paper;
        [unroll]
        for (int i = 0; i < 4; ++i)
        {
            float2 center;
            float d = DotCoverage(p, radians(angles[i] + gAngle), cell, center);
            float3 c = EncodeDisplay(SampleSource(center / float2(gTextureSize)).rgb);
            float k = 1.0f - max(c.r, max(c.g, c.b));
            float3 cmy = (1.0f - c - k) / max(1.0f - k, 1e-3f);
            float amount = (i == 0) ? cmy.x : (i == 1) ? cmy.y : (i == 2) ? cmy.z : k;
            float mask = DotMask(d, amount, softness);
            float3 inkColor = (i == 0) ? float3(0, 1, 1) : (i == 1) ? float3(1, 0, 1) : (i == 2) ? float3(1, 1, 0) : float3(0, 0, 0);
            ink *= lerp(float3(1, 1, 1), inkColor, mask);
        }
        result = ink * gPaperColor;
    }
    else
    {
        float2 center;
        float d = DotCoverage(p, radians(gAngle), cell, center);
        float3 c = EncodeDisplay(SampleSource(center / float2(gTextureSize)).rgb);
        if (gMode == 0)
        {
            float darkness = 1.0f - DisplayLuminance(SampleSource(center / float2(gTextureSize)).rgb);
            result = lerp(gPaperColor, gInkColor, DotMask(d, darkness, softness));
        }
        else
        {
            // 元の色の点。点の大きさは明るさで決める
            float brightness = max(c.r, max(c.g, c.b));
            result = lerp(gPaperColor * 0.08f, c / max(brightness, 1e-3f), DotMask(d, brightness, softness));
        }
    }
    gOutput[pixel] = float4(lerp(source.rgb, DecodeDisplay(result), gStrength), source.a);
}
