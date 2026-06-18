#include "FullScreen.hlsli"

Texture2D<float4> gTexture : register(t0); // 入力テクスチャ
SamplerState gSampler : register(s0); // サンプラー

// 定数バッファ (解像度、コントラスト、彩度、輝度)
cbuffer Constants : register(b0)
{
    float2 iResolution; // 解像度 (1280, 720)
    float contrast; // コントラスト調整 (デフォルト: 1.05)
    float saturation; // 彩度調整 (デフォルト: 0.68)
    float brightness; // 輝度調整 (デフォルト: 0.13)
}

struct PixelShaderOutput
{
    float4 color : SV_TARGET0; // 出力カラー
};

// グレースケール計算
float greyScale(float3 col)
{
    return dot(col, float3(0.3, 0.59, 0.11));
}

// 彩度調整行列
float3x3 saturationMatrix(float saturation)
{
    float3 luminance = float3(0.3086, 0.6094, 0.0820);
    float oneMinusSat = 1.0 - saturation;

    float3 red = luminance * oneMinusSat;
    red.r += saturation;

    float3 green = luminance * oneMinusSat;
    green.g += saturation;

    float3 blue = luminance * oneMinusSat;
    blue.b += saturation;

    return float3x3(red, green, blue);
}

// Levels調整
void levels(inout float3 col, float3 inleft, float3 inright, float3 outleft, float3 outright)
{
    col = clamp(col, inleft, inright);
    col = (col - inleft) / (inright - inleft);
    col = outleft + col * (outright - outleft);
}

// 輝度調整
void brightnessAdjust(inout float3 color, float b)
{
    color += b;
}

// コントラスト調整
void contrastAdjust(inout float3 color, float c)
{
    float t = 0.5 - c * 0.5;
    color = color * c + t;
}

// Color Burn処理
float3 colorBurn(float3 s, float3 d)
{
    return 1.0 - (1.0 - d) / s;
}

PixelShaderOutput main(VertexShaderOutput input)
{
    PixelShaderOutput output;

    // UV座標の計算
    float2 uv = input.texcoord;

    // テクスチャのサンプリング
    float3 col = gTexture.Sample(gSampler, uv).rgb;

    // グラデーション作成
    float2 coord = (float2(input.texcoord.x * iResolution.x, input.texcoord.y * iResolution.y) * 2.0 - iResolution) / iResolution.y;
    float3 gradient = pow(1.0 - length(coord * 0.4), 0.6) * 1.2;

    // 基本カラー
    float3 grey = float3(184.0 / 255.0, 184.0 / 255.0, 184.0 / 255.0);
    float3 tint = float3(252.0, 243.0, 213.0) / 255.0;

    // 彩度調整
    col = mul(saturationMatrix(saturation), col);
    levels(col, float3(0.0, 0.0, 0.0), float3(1.0, 1.0, 1.0),
                float3(27.0, 0.0, 0.0) / 255.0, float3(1.0, 1.0, 1.0));
    col = pow(col, float3(1.19, 1.19, 1.19));

    // 輝度とコントラスト調整
    brightnessAdjust(col, brightness);
    contrastAdjust(col, contrast);

    // 再度彩度調整
    col = mul(saturationMatrix(0.85), col);
    levels(col, float3(0.0, 0.0, 0.0), float3(235.0 / 255.0, 235.0 / 255.0, 235.0 / 255.0),
                float3(0.0, 0.0, 0.0), float3(1.0, 1.0, 1.0));

    // グラデーションとカラー Burnの適用
    col = lerp(tint * col, col, gradient);
    col = colorBurn(grey, col);

    output.color = float4(col, 1.0f);
    return output;
}
