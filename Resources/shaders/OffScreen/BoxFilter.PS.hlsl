#include "FullScreen.hlsli"


// 3x3カーネル用
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

static const float kKernel3x3[3][3] =
{
    { 1.0f / 9.0f, 1.0f / 9.0f, 1.0f / 9.0f },
    { 1.0f / 9.0f, 1.0f / 9.0f, 1.0f / 9.0f },
    { 1.0f / 9.0f, 1.0f / 9.0f, 1.0f / 9.0f },
};

// 5x5カーネル用
static const float kKernel5x5[5][5] =
{
    { 1.0f / 25.0f, 1.0f / 25.0f, 1.0f / 25.0f, 1.0f / 25.0f, 1.0f / 25.0f },
    { 1.0f / 25.0f, 1.0f / 25.0f, 1.0f / 25.0f, 1.0f / 25.0f, 1.0f / 25.0f },
    { 1.0f / 25.0f, 1.0f / 25.0f, 1.0f / 25.0f, 1.0f / 25.0f, 1.0f / 25.0f },
    { 1.0f / 25.0f, 1.0f / 25.0f, 1.0f / 25.0f, 1.0f / 25.0f, 1.0f / 25.0f },
    { 1.0f / 25.0f, 1.0f / 25.0f, 1.0f / 25.0f, 1.0f / 25.0f, 1.0f / 25.0f },
};

static const float kKernel7x7[7][7] =
{
    { 1.0f / 49.0f, 1.0f / 49.0f, 1.0f / 49.0f, 1.0f / 49.0f, 1.0f / 49.0f, 1.0f / 49.0f, 1.0f / 49.0f },
    { 1.0f / 49.0f, 1.0f / 49.0f, 1.0f / 49.0f, 1.0f / 49.0f, 1.0f / 49.0f, 1.0f / 49.0f, 1.0f / 49.0f },
    { 1.0f / 49.0f, 1.0f / 49.0f, 1.0f / 49.0f, 1.0f / 49.0f, 1.0f / 49.0f, 1.0f / 49.0f, 1.0f / 49.0f },
    { 1.0f / 49.0f, 1.0f / 49.0f, 1.0f / 49.0f, 1.0f / 49.0f, 1.0f / 49.0f, 1.0f / 49.0f, 1.0f / 49.0f },
    { 1.0f / 49.0f, 1.0f / 49.0f, 1.0f / 49.0f, 1.0f / 49.0f, 1.0f / 49.0f, 1.0f / 49.0f, 1.0f / 49.0f },
    { 1.0f / 49.0f, 1.0f / 49.0f, 1.0f / 49.0f, 1.0f / 49.0f, 1.0f / 49.0f, 1.0f / 49.0f, 1.0f / 49.0f },
    { 1.0f / 49.0f, 1.0f / 49.0f, 1.0f / 49.0f, 1.0f / 49.0f, 1.0f / 49.0f, 1.0f / 49.0f, 1.0f / 49.0f },
};


cbuffer KernelSettings : register(b0)
{
    int kernelSize; // カーネルのサイズ（3, 5, 7 のいずれか）
};

Texture2D<float4> gTexture : register(t0);
SamplerState gSampler : register(s0);

struct PixelShaderOutput
{
    float4 color : SV_TARGET0;
};

PixelShaderOutput main(VertexShaderOutput input)
{
    uint width, height;
    gTexture.GetDimensions(width, height);
    float2 uvStepSize = float2(1.0f / width, 1.0f / height);

    PixelShaderOutput output;
    output.color.rgb = float3(0.0f, 0.0f, 0.0f);
    output.color.a = 1.0f;

    if (kernelSize == 3)
    {
        for (int x = 0; x <= 3; ++x)
        {
            for (int y = 0; y <= 3; ++y)
            {
            // テクスチャ座標を計算
                float2 texcoord = input.texcoord + kIndex3x3[x][y] * uvStepSize;

            // テクスチャの値をサンプリング
                float3 fetchColor = gTexture.Sample(gSampler, texcoord).rgb;

            // カーネル値を掛けて加算
                output.color.rgb += fetchColor * kKernel3x3[x][y];
            }
        }
    }
    else if (kernelSize == 5)
    {
        for (int x = 0; x <= 5; ++x)
        {
            for (int y = 0; y <= 5; ++y)
            {
            // テクスチャ座標を計算
                float2 texcoord = input.texcoord + kIndex5x5[x][y] * uvStepSize;

            // テクスチャの値をサンプリング
                float3 fetchColor = gTexture.Sample(gSampler, texcoord).rgb;

            // カーネル値を掛けて加算
                output.color.rgb += fetchColor * kKernel5x5[x][y];
            }
        }
    }
    else if (kernelSize == 7)
    {
        for (int x = 0; x <= 6; ++x)
        {
            for (int y = 0; y <= 6; ++y)
            {
            // テクスチャ座標を計算
                float2 texcoord = input.texcoord + kIndex7x7[x][y] * uvStepSize;

            // テクスチャの値をサンプリング
                float3 fetchColor = gTexture.Sample(gSampler, texcoord).rgb;

            // カーネル値を掛けて加算
                output.color.rgb += fetchColor * kKernel7x7[x][y];
            }
        }
    }

    return output;
}
