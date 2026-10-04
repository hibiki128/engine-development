// 油絵風（桑原フィルタ）。周りを4つの区画に分け、いちばん色のばらつきが小さい区画の平均で塗る。
// 輪郭は残ったまま平らな所が筆で塗ったような面になる
#include "PostFxCommon.hlsli"

cbuffer KuwaharaParams : register(b0)
{
    POSTFX_FRAME_HEADER
    int gRadius;       // 区画の大きさ（ピクセル）
    float gStrength;   // 元の色との混ぜ具合
    float gSaturation; // 彩度の倍率（油絵具らしく少し鮮やかに）
    float gPadding;
};

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    int2 pixel = int2(id.xy);
    POSTFX_EARLY_OUT(pixel)

    int radius = clamp(gRadius, 1, 8);
    float3 mean[4] = {float3(0, 0, 0), float3(0, 0, 0), float3(0, 0, 0), float3(0, 0, 0)};
    float3 sq[4] = {float3(0, 0, 0), float3(0, 0, 0), float3(0, 0, 0), float3(0, 0, 0)};
    const int2 signs[4] = {int2(-1, -1), int2(1, -1), int2(-1, 1), int2(1, 1)};
    float count = float((radius + 1) * (radius + 1));

    [unroll]
    for (int k = 0; k < 4; ++k)
    {
        for (int y = 0; y <= radius; ++y)
        {
            for (int x = 0; x <= radius; ++x)
            {
                float3 c = EncodeDisplay(LoadSource(pixel + int2(x, y) * signs[k]).rgb);
                mean[k] += c;
                sq[k] += c * c;
            }
        }
    }

    float3 best = mean[0] / count;
    float bestVariance = 1e9f;
    [unroll]
    for (int i = 0; i < 4; ++i)
    {
        float3 m = mean[i] / count;
        float3 v = abs(sq[i] / count - m * m);
        float variance = v.r + v.g + v.b;
        if (variance < bestVariance)
        {
            bestVariance = variance;
            best = m;
        }
    }

    float3 hsv = RgbToHsv(saturate(best));
    hsv.y = saturate(hsv.y * gSaturation);
    float4 source = LoadSource(pixel);
    gOutput[pixel] = float4(lerp(source.rgb, DecodeDisplay(HsvToRgb(hsv)), gStrength), source.a);
}
