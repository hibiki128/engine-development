// すりガラス。ピクセルごとにばらばらな位置を読んで、細かく散らしたぼかしにする
#include "PostFxCommon.hlsli"

cbuffer FrostedGlassParams : register(b0)
{
    POSTFX_FRAME_HEADER
    float gAmount;   // 散らす幅（ピクセル）
    float gScale;    // ガラスの凹凸の細かさ
    float gSamples;  // 重ねる回数（多いほどなめらか）
    float gTint;     // 白っぽくする量
};

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    int2 pixel = int2(id.xy);
    POSTFX_EARLY_OUT(pixel)

    float2 uv = PixelToUv(pixel);
    float2 texel = 1.0f / float2(gTextureSize);
    int samples = clamp(int(gSamples), 1, 16);
    float4 sum = 0.0f;
    for (int i = 0; i < samples; ++i)
    {
        // 凹凸（ノイズ）で決まる向き＋ピクセルごとの乱数で散らす
        float2 bump = float2(ValueNoise(uv * gScale * 100.0f + i * 3.1f), ValueNoise(uv * gScale * 100.0f + 17.0f + i * 1.7f)) - 0.5f;
        float2 jitter = Hash22(float2(pixel) + i * 19.19f) - 0.5f;
        sum += SampleSource(uv + (bump + jitter) * gAmount * texel * 2.0f);
    }
    float4 color = sum / samples;
    color.rgb = lerp(color.rgb, color.rgb * 0.85f + 0.15f, gTint);
    gOutput[pixel] = color;
}
