#include"FullScreen.hlsli"

// テクスチャとサンプラー
Texture2D<float4> gTexture : register(t0);
SamplerState gSampler : register(s0);

// ピクセルシェーダーの出力構造体
struct PixelShaderOutput
{
    float4 color : SV_TARGET0;
};

// 二値化白黒のためのパラメータ
cbuffer MonochromeParameter : register(b0)
{
    float threshold; // この明度(輝度)を境に、白(1)か黒(0)のどちらかへ振り分ける
    float3 pad;
}

PixelShaderOutput main(VertexShaderOutput input)
{
    PixelShaderOutput output;
    float4 src = gTexture.Sample(gSampler, input.texcoord);

    // 明度(輝度)を計算する（人間の目の感度に合わせた加重平均）
    float luminance = dot(src.rgb, float3(0.2125f, 0.7154f, 0.0721f));

    // 閾値で完全な白か黒のどちらかへ二値化する（灰色を残さない）
    float bw = luminance >= threshold ? 1.0f : 0.0f;

    output.color = float4(bw, bw, bw, src.a);
    return output;
}
