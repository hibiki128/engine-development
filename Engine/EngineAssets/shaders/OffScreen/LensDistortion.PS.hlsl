#include "FullScreen.hlsli"

// レンズ歪み。広角レンズのように画面を樽型／糸巻き型へ曲げる。
// あわせて周辺減光（画面の端を暗く）も掛けられる。

Texture2D<float4> gTexture : register(t0);
SamplerState gSampler : register(s0);

struct LensDistortionParameters
{
    float distortion;   // 歪みの量（+で樽型＝中央が膨らむ / -で糸巻き型）
    float cubic;        // 端だけを強く曲げる量
    float scale;        // 拡大率。歪みで外周に隙間ができるときに詰める
    float vignette;     // 周辺減光の強さ (0 で無効)
};
ConstantBuffer<LensDistortionParameters> gLens : register(b0);

struct PixelShaderOutput
{
    float4 color : SV_TARGET0;
};

PixelShaderOutput main(VertexShaderOutput input)
{
    PixelShaderOutput output;

    // 中心を原点にした -1〜1 の座標で考えると、歪みは「中心からの距離の関数」になる
    float2 centered = (input.texcoord - 0.5f) * 2.0f;
    const float radiusSq = dot(centered, centered);

    // 距離の2乗と4乗で曲げる。これがレンズ歪みの一般的な近似
    const float factor = 1.0f + gLens.distortion * radiusSq + gLens.cubic * radiusSq * radiusSq;
    centered *= factor;
    centered /= max(gLens.scale, 0.01f);

    const float2 uv = centered * 0.5f + 0.5f;

    // 曲げた結果、元の絵の外を参照してしまう部分は黒で埋める
    if (uv.x < 0.0f || uv.x > 1.0f || uv.y < 0.0f || uv.y > 1.0f)
    {
        output.color = float4(0.0f, 0.0f, 0.0f, 1.0f);
        return output;
    }

    float3 color = gTexture.Sample(gSampler, uv).rgb;

    // 周辺減光。中心からの距離でなだらかに暗くする
    if (gLens.vignette > 0.0f)
    {
        const float darkening = 1.0f - saturate(radiusSq * 0.5f) * saturate(gLens.vignette);
        color *= darkening;
    }

    output.color = float4(color, 1.0f);
    return output;
}
