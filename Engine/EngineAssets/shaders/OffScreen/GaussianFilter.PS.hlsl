#include "FullScreen.hlsli"

static const float PI = 3.14159265f;

// ガウス関数の定義
float gauss(float x, float y, float sigma)
{
    float exponent = -(x * x + y * y) / (2.0f * sigma * sigma);
    float denominator = 2.0f * PI * sigma * sigma;
    return exp(exponent) / denominator;
}

// 3x3カーネルのインデックス
static const float2 kIndex3x3[3][3] =
{
    { { -1.0f, -1.0f }, { 0.0f, -1.0f }, { 1.0f, -1.0f } },
    { { -1.0f, 0.0f }, { 0.0f, 0.0f }, { 1.0f, 0.0f } },
    { { -1.0f, 1.0f }, { 0.0f, 1.0f }, { 1.0f, 1.0f } },
};

// 5x5カーネル用
static const float2 kIndex5x5[5][5] =
{
    { { -2.0f, -2.0f }, { -1.0f, -2.0f }, { 0.0f, -2.0f }, { 1.0f, -2.0f }, { 2.0f, -2.0f } },
    { { -2.0f, -1.0f }, { -1.0f, -1.0f }, { 0.0f, -1.0f }, { 1.0f, -1.0f }, { 2.0f, -1.0f } },
    { { -2.0f, 0.0f }, { -1.0f, 0.0f }, { 0.0f, 0.0f }, { 1.0f, 0.0f }, { 2.0f, 0.0f } },
    { { -2.0f, 1.0f }, { -1.0f, 1.0f }, { 0.0f, 1.0f }, { 1.0f, 1.0f }, { 2.0f, 1.0f } },
    { { -2.0f, 2.0f }, { -1.0f, 2.0f }, { 0.0f, 2.0f }, { 1.0f, 2.0f }, { 2.0f, 2.0f } },
};

// 7x7カーネル用
static const float2 kIndex7x7[7][7] =
{
    { { -3.0f, -3.0f }, { -2.0f, -3.0f }, { -1.0f, -3.0f }, { 0.0f, -3.0f }, { 1.0f, -3.0f }, { 2.0f, -3.0f }, { 3.0f, -3.0f } },
    { { -3.0f, -2.0f }, { -2.0f, -2.0f }, { -1.0f, -2.0f }, { 0.0f, -2.0f }, { 1.0f, -2.0f }, { 2.0f, -2.0f }, { 3.0f, -2.0f } },
    { { -3.0f, -1.0f }, { -2.0f, -1.0f }, { -1.0f, -1.0f }, { 0.0f, -1.0f }, { 1.0f, -1.0f }, { 2.0f, -1.0f }, { 3.0f, -1.0f } },
    { { -3.0f, 0.0f }, { -2.0f, 0.0f }, { -1.0f, 0.0f }, { 0.0f, 0.0f }, { 1.0f, 0.0f }, { 2.0f, 0.0f }, { 3.0f, 0.0f } },
    { { -3.0f, 1.0f }, { -2.0f, 1.0f }, { -1.0f, 1.0f }, { 0.0f, 1.0f }, { 1.0f, 1.0f }, { 2.0f, 1.0f }, { 3.0f, 1.0f } },
    { { -3.0f, 2.0f }, { -2.0f, 2.0f }, { -1.0f, 2.0f }, { 0.0f, 2.0f }, { 1.0f, 2.0f }, { 2.0f, 2.0f }, { 3.0f, 2.0f } },
    { { -3.0f, 3.0f }, { -2.0f, 3.0f }, { -1.0f, 3.0f }, { 0.0f, 3.0f }, { 1.0f, 3.0f }, { 2.0f, 3.0f }, { 3.0f, 3.0f } },
};

// カーネルのサイズとsigmaを動的に変更するためのCBuffer
cbuffer GaussianParams : register(b0)
{
    int kernelSize; // カーネルのサイズ（3, 5, 7 のいずれか）
    float sigma; // ガウス関数のsigma
}

// ピクセルシェーダーの出力構造体
struct PixelShaderOutput
{
    float4 color : SV_TARGET0;
};

Texture2D<float4> gTexture : register(t0);
SamplerState gSampler : register(s0);

// メイン関数
PixelShaderOutput main(VertexShaderOutput input)
{
    PixelShaderOutput output;
    float weight = 0.0f;
    float kernel3x3[3][3];
    float kernel5x5[5][5];
    float kernel7x7[7][7];
    uint width, height;
    gTexture.GetDimensions(width, height);
    float2 uvStepSize = float2(1.0f / width, 1.0f / height);
    
    if (kernelSize == 3)
    {
    // 3x3カーネルをガウス関数を使って計算
        for (int x = 0; x < 3; ++x)
        {
            for (int y = 0; y < 3; ++y)
            {
                kernel3x3[x][y] = gauss(kIndex3x3[x][y].x, kIndex3x3[x][y].y, sigma);
                weight += kernel3x3[x][y];
            }
        }
    
    // 畳み込みを行う
        output.color.rgb = float3(0.0f, 0.0f, 0.0f);
        output.color.a = 1.0f;
        for (int a = 0; a < 3; ++a)
        {
            for (int b = 0; b < 3; ++b)
            {
                float2 texcoord = clamp(input.texcoord + kIndex3x3[a][b] * uvStepSize, 0.0f, 1.0f);
                float3 fetchColor = gTexture.Sample(gSampler, texcoord).rgb;
                output.color.rgb += fetchColor * kernel3x3[a][b];
            }
        }
    }
    else if (kernelSize == 5)
    {
        // 5x5カーネルをガウス関数を使って計算
        for (int x = 0; x < 5; ++x)
        {
            for (int y = 0; y < 5; ++y)
            {
                kernel5x5[x][y] = gauss(kIndex5x5[x][y].x, kIndex5x5[x][y].y, sigma);
                weight += kernel5x5[x][y];
            }
        }
    
    // 畳み込みを行う
        output.color.rgb = float3(0.0f, 0.0f, 0.0f);
        output.color.a = 1.0f;
        for (int a = 0; a < 5; ++a)
        {
            for (int b = 0; b < 5; ++b)
            {
                float2 texcoord = clamp(input.texcoord + kIndex5x5[a][b] * uvStepSize, 0.0f, 1.0f);
                float3 fetchColor = gTexture.Sample(gSampler, texcoord).rgb;
                output.color.rgb += fetchColor * kernel5x5[a][b];
            }
        }
    }
    else if (kernelSize == 7)
    {
        // 3x3カーネルをガウス関数を使って計算
        for (int x = 0; x < 7; ++x)
        {
            for (int y = 0; y < 7; ++y)
            {
                kernel7x7[x][y] = gauss(kIndex7x7[x][y].x, kIndex7x7[x][y].y, sigma);
                weight += kernel7x7[x][y];
            }
        }
    
    // 畳み込みを行う
        output.color.rgb = float3(0.0f, 0.0f, 0.0f);
        output.color.a = 1.0f;
        for (int a = 0; a < 7; ++a)
        {
            for (int b = 0; b < 7; ++b)
            {
                float2 texcoord = clamp(input.texcoord + kIndex7x7[a][b] * uvStepSize, 0.0f, 1.0f);
                float3 fetchColor = gTexture.Sample(gSampler, texcoord).rgb;
                output.color.rgb += fetchColor * kernel7x7[a][b];
            }
        }
    }
    
    
    // 正規化
    if (weight > 0.0f)
    {
        output.color.rgb /= weight;
    }

    return output;
}
