#include "FullScreen.hlsli"

// ポストエフェクトのチェーン出口。
// HDR（リニア、1.0 を超えうる）を露出とトーンマップで 0〜1 の見える範囲へ収める。
// ここを通った後の値が最終結果テクスチャに入り、その上に UI が重なる。

Texture2D<float4> gTexture : register(t0);
SamplerState gSampler : register(s0);

struct ToneMapParameters
{
    float exposure;   // 露出。2倍で1段明るい
    int   mode;       // 0=なし(クランプのみ) 1=Reinhard 2=ACES 3=Uncharted2
    float contrast;   // コントラスト (1.0 で素通し)
    float saturation; // 彩度 (1.0 で素通し)

    float whitePoint; // これ以上の明るさを白とみなす値（Reinhard/Uncharted2 用）
    float3 colorFilter; // 画面全体に掛ける色（1,1,1 で素通し）
};
ConstantBuffer<ToneMapParameters> gToneMap : register(b0);

/// ACES のフィルミックな曲線を近似したもの。
/// 明るい側がゆるやかに白へ寄るので、強い光がべたっと白飛びしにくい
float3 ACESFilmic(float3 color)
{
    const float a = 2.51f;
    const float b = 0.03f;
    const float c = 2.43f;
    const float d = 0.59f;
    const float e = 0.14f;
    return saturate((color * (a * color + b)) / (color * (c * color + d) + e));
}

/// Uncharted2（Hable）の曲線。暗部を持ち上げすぎず、コントラストが強めに出る
float3 Uncharted2Curve(float3 x)
{
    const float A = 0.15f;
    const float B = 0.50f;
    const float C = 0.10f;
    const float D = 0.20f;
    const float E = 0.02f;
    const float F = 0.30f;
    return ((x * (A * x + C * B) + D * E) / (x * (A * x + B) + D * F)) - E / F;
}

/// 明るさ（輝度）。彩度の調整で「色を抜いた状態」の基準に使う
float Luminance(float3 color)
{
    return dot(color, float3(0.2126f, 0.7152f, 0.0722f));
}

struct PixelShaderOutput
{
    float4 color : SV_TARGET0;
};

PixelShaderOutput main(VertexShaderOutput input)
{
    PixelShaderOutput output;

    float3 color = gTexture.Sample(gSampler, input.texcoord).rgb;

    // 露出。ここで明るさを決めてからカーブへ通す
    color *= max(gToneMap.exposure, 0.0f);

    // 色被り（全体の色味）
    color *= gToneMap.colorFilter;

    const float white = max(gToneMap.whitePoint, 0.01f);

    if (gToneMap.mode == 1)
    {
        // Reinhard。白飛びしにくいが全体に眠くなりやすい
        const float3 scaled = color * (1.0f + color / (white * white));
        color = scaled / (1.0f + color);
    }
    else if (gToneMap.mode == 2)
    {
        color = ACESFilmic(color);
    }
    else if (gToneMap.mode == 3)
    {
        const float3 curved = Uncharted2Curve(color * 2.0f);
        const float3 whiteScale = 1.0f / Uncharted2Curve(white.xxx);
        color = saturate(curved * whiteScale);
    }
    else
    {
        // なし。従来どおり 1.0 で頭打ちにするだけ
        color = saturate(color);
    }

    // 彩度とコントラストは、見える範囲へ収めた後に整えるほうが破綻しない
    const float luminance = Luminance(color);
    color = lerp(luminance.xxx, color, max(gToneMap.saturation, 0.0f));
    color = saturate((color - 0.5f) * max(gToneMap.contrast, 0.0f) + 0.5f);

    output.color = float4(color, 1.0f);
    return output;
}
