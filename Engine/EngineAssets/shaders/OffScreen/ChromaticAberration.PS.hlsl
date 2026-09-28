#include "FullScreen.hlsli"

// 色収差。レンズの端で赤・緑・青がわずかにずれる現象を真似る。
// 画面の端ほど強く出すと、安物のレンズ越しに見ているような生々しさが出る。

Texture2D<float4> gTexture : register(t0);
SamplerState gSampler : register(s0);

struct ChromaticAberrationParameters
{
    float strength;    // ずれの大きさ
    float falloff;     // 中心からの距離の効き方（大きいほど端だけに出る）
    int   sampleCount; // 分割数。多いほどなめらかだが重い
    float pad;
};
ConstantBuffer<ChromaticAberrationParameters> gAberration : register(b0);

struct PixelShaderOutput
{
    float4 color : SV_TARGET0;
};

PixelShaderOutput main(VertexShaderOutput input)
{
    PixelShaderOutput output;

    const float2 center = float2(0.5f, 0.5f);
    const float2 toCenter = input.texcoord - center;
    // 端ほど強くする。falloff を上げると中心付近はほぼ素通しになる
    const float distanceFactor = pow(saturate(length(toCenter) * 2.0f), max(gAberration.falloff, 0.01f));
    const float2 offset = toCenter * gAberration.strength * distanceFactor;

    const int count = clamp(gAberration.sampleCount, 1, 8);
    float3 sum = 0.0f;
    float3 weightSum = 0.0f;

    // 赤→緑→青へ少しずつずらして読み、間を埋めるように足し合わせる。
    // 3点だけだと境目が目立つので、分割して虹のように繋ぐ
    for (int i = 0; i < count; ++i)
    {
        const float t = (count == 1) ? 0.5f : (float)i / (float)(count - 1);
        const float2 uv = input.texcoord - offset * (t * 2.0f - 1.0f);
        const float3 sampled = gTexture.Sample(gSampler, uv).rgb;

        // t=0 で赤寄り、t=1 で青寄りの重みにする
        const float3 weight = float3(saturate(1.0f - t * 2.0f) + 0.0001f,
                                     saturate(1.0f - abs(t - 0.5f) * 2.0f) + 0.0001f,
                                     saturate(t * 2.0f - 1.0f) + 0.0001f);
        sum += sampled * weight;
        weightSum += weight;
    }

    output.color = float4(sum / max(weightSum, 0.0001f), 1.0f);
    return output;
}
