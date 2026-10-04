// シャープ（アンシャープマスク）。周りとの差を強めて輪郭をくっきりさせる
#include "PostFxCommon.hlsli"

cbuffer SharpenParams : register(b0)
{
    POSTFX_FRAME_HEADER
    float gStrength; // 強さ
    float gRadius;   // 見る範囲（ピクセル）
    float gClamp;    // 一度に変えてよい量の上限（輪郭の白飛び・黒つぶれを防ぐ）
    float gPadding;
};

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    int2 pixel = int2(id.xy);
    POSTFX_EARLY_OUT(pixel)

    float2 uv = PixelToUv(pixel);
    float2 texel = gRadius / float2(gTextureSize);
    float4 center = LoadSource(pixel);
    float3 blur = SampleSource(uv + float2(texel.x, 0.0f)).rgb + SampleSource(uv - float2(texel.x, 0.0f)).rgb +
                  SampleSource(uv + float2(0.0f, texel.y)).rgb + SampleSource(uv - float2(0.0f, texel.y)).rgb;
    blur *= 0.25f;

    // 見た目の空間で差を取って、明るい所だけ過剰に効かないようにする
    float3 c = EncodeDisplay(center.rgb);
    float3 detail = clamp(c - EncodeDisplay(blur), -gClamp, gClamp);
    gOutput[pixel] = float4(DecodeDisplay(saturate(c + detail * gStrength)), center.a);
}
