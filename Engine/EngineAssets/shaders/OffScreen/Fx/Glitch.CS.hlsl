// グリッチ（デジタルノイズ）。ときどき画面がブロック状にずれ、RGBが分かれ、色が化ける
#include "PostFxCommon.hlsli"

cbuffer GlitchParams : register(b0)
{
    POSTFX_FRAME_HEADER
    float gIntensity;  // 全体の強さ
    float gBlockSize;  // ずれるブロックの高さ（ピクセル）
    float gSpeed;      // 乱れが切り替わる速さ（回/秒）
    float gRgbSplit;   // RGBの分かれ幅（ピクセル）
    float gLineNoise;  // 横線のずれ
    float gColorDrop;  // 色が化けるブロックの割合
    float gFrequency;  // 乱れが起きる頻度（0〜1。1で常に）
    float gPadding;
};

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    int2 pixel = int2(id.xy);
    POSTFX_EARLY_OUT(pixel)

    float2 uv = PixelToUv(pixel);
    float step = floor(gTime * max(gSpeed, 0.1f));
    // 乱れはときどき起きる（起きている間の強さもばらつく）
    float burst = (Hash11(step * 1.7f) < gFrequency) ? lerp(0.3f, 1.0f, Hash11(step + 9.1f)) : 0.0f;
    float strength = gIntensity * burst;

    float2 texel = 1.0f / float2(gTextureSize);
    float blockHeight = max(gBlockSize, 2.0f);
    float2 block = float2(floor(pixel.x / (blockHeight * 6.0f)), floor(pixel.y / blockHeight));
    float blockRandom = Hash12(block + step * 13.1f);
    float rowRandom = Hash11(block.y * 7.3f + step);

    float2 offset = 0.0f;
    // 横に帯ごとずれる
    if (rowRandom < strength * 0.35f)
    {
        offset.x += (Hash11(block.y + step * 3.3f) - 0.5f) * 0.25f * strength;
    }
    // 細かい横線のずれ
    float line2 = Hash11(floor(pixel.y / 2.0f) + step * 5.7f);
    if (line2 < gLineNoise * strength * 0.2f)
    {
        offset.x += (Hash11(pixel.y + step) - 0.5f) * 0.05f;
    }

    float split = gRgbSplit * strength * texel.x * (0.5f + blockRandom);
    float3 color;
    color.r = SampleSource(uv + offset + float2(split, 0)).r;
    color.g = SampleSource(uv + offset).g;
    color.b = SampleSource(uv + offset - float2(split, 0)).b;

    // 色が化けるブロック（反転・チャンネル入れ替え・明るさが飛ぶ）
    if (blockRandom < gColorDrop * strength * 0.3f)
    {
        float kind = Hash12(block + 41.0f + step);
        if (kind < 0.33f)
            color = 1.0f - saturate(color);
        else if (kind < 0.66f)
            color = color.gbr;
        else
            color = color * 3.0f;
    }
    gOutput[pixel] = float4(color, 1.0f);
}
