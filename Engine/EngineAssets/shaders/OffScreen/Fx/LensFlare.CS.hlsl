// レンズフレア。明るい所の像が、画面の中心をはさんだ反対側に玉（ゴースト）や輪（ハロー）になって写る
#include "PostFxCommon.hlsli"

cbuffer LensFlareParams : register(b0)
{
    POSTFX_FRAME_HEADER
    float gThreshold;     // これより明るい所だけがフレアになる（HDRの明るさ）
    float gIntensity;     // ゴーストの明るさ
    int gGhostCount;      // ゴーストの数
    float gGhostSpacing;  // ゴーストの間隔
    float gHaloRadius;    // ハローの半径
    float gHaloIntensity; // ハローの明るさ
    float gChroma;        // 色のにじみ
    float gPadding;
    float3 gTint;         // 全体の色
    float gPadding2;
};

float3 Bright(float2 uv)
{
    // 少しぼかしてから明るい所だけを取る（細かいちらつきを抑える）
    float2 texel = 3.0f / float2(gTextureSize);
    float3 c = SampleSource(uv).rgb * 0.4f;
    c += SampleSource(uv + float2(texel.x, 0)).rgb * 0.15f;
    c += SampleSource(uv - float2(texel.x, 0)).rgb * 0.15f;
    c += SampleSource(uv + float2(0, texel.y)).rgb * 0.15f;
    c += SampleSource(uv - float2(0, texel.y)).rgb * 0.15f;
    return max(c - gThreshold, 0.0f);
}

float3 BrightChroma(float2 uv, float2 dir)
{
    float2 offset = dir * gChroma;
    return float3(Bright(uv + offset).r, Bright(uv).g, Bright(uv - offset).b);
}

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    int2 pixel = int2(id.xy);
    POSTFX_EARLY_OUT(pixel)

    float2 uv = PixelToUv(pixel);
    float4 source = LoadSource(pixel);
    float2 flipped = 1.0f - uv;
    float2 ghostVector = (0.5f - flipped) * gGhostSpacing;
    float2 dir = normalize(ghostVector + 1e-5f);

    float3 flare = 0.0f;
    int count = clamp(gGhostCount, 0, 8);
    for (int i = 0; i < count; ++i)
    {
        float2 offset = frac(flipped + ghostVector * float(i));
        // 画面の中心に近いゴーストほど強く、端では消える
        float weight = pow(saturate(1.0f - length(0.5f - offset) / 0.7071f), 8.0f);
        // ゴーストごとに色を少し変える
        float3 hue = HsvToRgb(float3(frac(i * 0.17f + 0.55f), 0.35f, 1.0f));
        flare += BrightChroma(offset, dir) * weight * hue;
    }

    // ハロー: 中心のまわりの輪
    float2 haloVector = dir * gHaloRadius;
    float2 haloUv = frac(flipped + haloVector);
    float2 aspectCenter = (haloUv - 0.5f) * float2(AspectRatio(), 1.0f);
    float haloWeight = pow(saturate(1.0f - abs(length(aspectCenter) - gHaloRadius) * 4.0f), 4.0f);
    flare += BrightChroma(haloUv, dir) * haloWeight * gHaloIntensity;

    gOutput[pixel] = float4(source.rgb + flare * gIntensity * gTint, source.a);
}
