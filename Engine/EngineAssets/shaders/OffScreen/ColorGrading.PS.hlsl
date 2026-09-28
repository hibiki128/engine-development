#include "FullScreen.hlsli"

// カラーグレーディング。画面全体の色味をまとめて作り込む。
// 暗い所・中間・明るい所を別々に持ち上げ下げできるので、
// 「夕方っぽく」「寒々しく」といった空気感を後から足せる。

Texture2D<float4> gTexture : register(t0);
SamplerState gSampler : register(s0);

struct ColorGradingParameters
{
    float4 lift;  // 暗い所へ足す色（rgb） / a=全体の持ち上げ
    float4 gamma; // 中間の明るさの色（rgb） / a=全体のガンマ
    float4 gain;  // 明るい所へ掛ける色（rgb） / a=全体のゲイン

    float temperature; // 色温度 (-1=青い/寒い, +1=オレンジ/暖かい)
    float tint;        // 色合い (-1=緑, +1=マゼンタ)
    float saturation;  // 彩度 (1.0 で素通し)
    float contrast;    // コントラスト (1.0 で素通し)
};
ConstantBuffer<ColorGradingParameters> gGrading : register(b0);

struct PixelShaderOutput
{
    float4 color : SV_TARGET0;
};

float Luminance(float3 color)
{
    return dot(color, float3(0.2126f, 0.7152f, 0.0722f));
}

PixelShaderOutput main(VertexShaderOutput input)
{
    PixelShaderOutput output;
    float3 color = gTexture.Sample(gSampler, input.texcoord).rgb;

    // --- 色温度と色合い ---
    // 青-オレンジ、緑-マゼンタの2軸で、フィルムの色補正フィルタのように寄せる
    const float3 warm = float3(1.0f, 0.72f, 0.45f);
    const float3 cool = float3(0.45f, 0.68f, 1.0f);
    const float3 temperatureFilter = lerp(cool, warm, saturate(gGrading.temperature * 0.5f + 0.5f));
    // 平均を 1 に戻して、色を寄せても明るさが変わらないようにする
    color *= temperatureFilter / max(Luminance(temperatureFilter), 0.0001f);

    const float3 tintFilter = float3(1.0f + gGrading.tint * 0.2f, 1.0f - gGrading.tint * 0.2f,
                                     1.0f + gGrading.tint * 0.2f);
    color *= tintFilter;

    // --- リフト / ガンマ / ゲイン ---
    // 暗部・中間・明部をそれぞれ別に動かす、色調整の基本の3つ
    const float3 lift = gGrading.lift.rgb + gGrading.lift.a;
    const float3 gain = gGrading.gain.rgb * gGrading.gain.a;
    color = color * gain + lift;

    const float3 gammaValue = max(gGrading.gamma.rgb * gGrading.gamma.a, 0.0001f);
    color = pow(max(color, 0.0f), 1.0f / gammaValue);

    // --- 彩度とコントラスト ---
    const float luminance = Luminance(color);
    color = lerp(luminance.xxx, color, max(gGrading.saturation, 0.0f));
    color = (color - 0.5f) * max(gGrading.contrast, 0.0f) + 0.5f;

    output.color = float4(max(color, 0.0f), 1.0f);
    return output;
}
