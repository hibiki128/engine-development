#include "FullScreen.hlsli"

// フィルムグレイン。フィルム写真のような細かいざらつきを乗せる。
// CG特有のつるっとした均一さが薄れて、実写寄りの質感になる。

Texture2D<float4> gTexture : register(t0);
SamplerState gSampler : register(s0);

struct FilmGrainParameters
{
    float intensity;    // ざらつきの強さ
    float grainSize;    // 粒の大きさ（大きいほど粗い）
    float time;         // 時間。毎フレーム変えると粒が動く
    float luminanceKeep; // 明るい所でざらつきを弱める度合い (0〜1)
};
ConstantBuffer<FilmGrainParameters> gGrain : register(b0);

struct PixelShaderOutput
{
    float4 color : SV_TARGET0;
};

/// 座標から擬似乱数を作る。同じ座標なら同じ値になるので、時間を混ぜて動かす
float Hash(float2 position)
{
    return frac(sin(dot(position, float2(12.9898f, 78.233f))) * 43758.5453f);
}

PixelShaderOutput main(VertexShaderOutput input)
{
    PixelShaderOutput output;
    const float3 color = gTexture.Sample(gSampler, input.texcoord).rgb;

    uint width = 0;
    uint height = 0;
    gTexture.GetDimensions(width, height);

    // 粒の大きさは「何ピクセルを1粒とみなすか」で決める
    const float size = max(gGrain.grainSize, 0.5f);
    const float2 grainCoord = floor(input.texcoord * float2(width, height) / size);

    // -0.5〜0.5 のざらつき。時間をずらして混ぜることで毎フレーム違う模様になる
    const float noise = Hash(grainCoord + frac(gGrain.time) * 137.0f) - 0.5f;

    // 明るい所ほどざらつきを弱める。実際のフィルムも暗部のほうが粒が目立つ
    const float luminance = dot(color, float3(0.2126f, 0.7152f, 0.0722f));
    const float mask = lerp(1.0f, 1.0f - luminance, saturate(gGrain.luminanceKeep));

    output.color = float4(max(color + noise * gGrain.intensity * mask, 0.0f), 1.0f);
    return output;
}
