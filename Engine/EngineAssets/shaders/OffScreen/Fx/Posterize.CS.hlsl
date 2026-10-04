// ポスタリゼーション。色の段階を減らしてポスターやアニメ塗りのようにする
#include "PostFxCommon.hlsli"

cbuffer PosterizeParams : register(b0)
{
    POSTFX_FRAME_HEADER
    float gLevels;   // 段階の数
    int gMode;       // 0=RGBそれぞれ / 1=明るさだけ（色味は残す）
    float gStrength; // 元の色との混ぜ具合
    float gDither;   // 段差をぼかすディザの量
};

static const float kBayer4[16] = {
    0.0f, 8.0f, 2.0f, 10.0f,
    12.0f, 4.0f, 14.0f, 6.0f,
    3.0f, 11.0f, 1.0f, 9.0f,
    15.0f, 7.0f, 13.0f, 5.0f};

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    int2 pixel = int2(id.xy);
    POSTFX_EARLY_OUT(pixel)

    float4 source = LoadSource(pixel);
    float levels = max(gLevels, 2.0f);
    float threshold = (kBayer4[(pixel.y & 3) * 4 + (pixel.x & 3)] / 16.0f - 0.5f) * gDither / levels;

    float3 display = EncodeDisplay(source.rgb);
    float3 result;
    if (gMode == 0)
    {
        result = floor((display + threshold) * (levels - 1.0f) + 0.5f) / (levels - 1.0f);
    }
    else
    {
        // 明るさだけを段にして、色味（各成分の比）は保つ
        float l = max(max(display.r, display.g), max(display.b, 1e-4f));
        float stepped = floor((l + threshold) * (levels - 1.0f) + 0.5f) / (levels - 1.0f);
        result = display / l * stepped;
    }
    gOutput[pixel] = float4(lerp(source.rgb, DecodeDisplay(saturate(result)), gStrength), source.a);
}
