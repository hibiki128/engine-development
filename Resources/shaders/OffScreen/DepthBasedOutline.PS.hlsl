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

static const float kPrewittHorizontalKernel3x3[3][3] =
{
    { -1.0f / 6.0f, 0.0f, 1.0f / 6.0f },
    { -1.0f / 6.0f, 0.0f, 1.0f / 6.0f },
    { -1.0f / 6.0f, 0.0f, 1.0f / 6.0f },
};

static const float kPrewittVerticalKernel3x3[3][3] =
{
    { -1.0f / 6.0f, -1.0f / 6.0f, -1.0f / 6.0f },
    { 0.0f, 0.0f, 0.0f },
    { 1.0f / 6.0f, 1.0f / 6.0f, 1.0f / 6.0f },
};

// 5x5 用 Prewitt Horizontal カーネル
static const float kPrewittHorizontalKernel5x5[5][5] =
{
    { -1.0f / 12.0f, -1.0f / 6.0f, 0.0f, 1.0f / 6.0f, 1.0f / 12.0f },
    { -1.0f / 12.0f, -1.0f / 6.0f, 0.0f, 1.0f / 6.0f, 1.0f / 12.0f },
    { -1.0f / 12.0f, -1.0f / 6.0f, 0.0f, 1.0f / 6.0f, 1.0f / 12.0f },
    { -1.0f / 12.0f, -1.0f / 6.0f, 0.0f, 1.0f / 6.0f, 1.0f / 12.0f },
    { -1.0f / 12.0f, -1.0f / 6.0f, 0.0f, 1.0f / 6.0f, 1.0f / 12.0f }
};

// 5x5 用 Prewitt Vertical カーネル
static const float kPrewittVerticalKernel5x5[5][5] =
{
    { -1.0f / 12.0f, -1.0f / 12.0f, -1.0f / 12.0f, -1.0f / 12.0f, -1.0f / 12.0f },
    { -1.0f / 6.0f, -1.0f / 6.0f, -1.0f / 6.0f, -1.0f / 6.0f, -1.0f / 6.0f },
    { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f },
    { 1.0f / 6.0f, 1.0f / 6.0f, 1.0f / 6.0f, 1.0f / 6.0f, 1.0f / 6.0f },
    { 1.0f / 12.0f, 1.0f / 12.0f, 1.0f / 12.0f, 1.0f / 12.0f, 1.0f / 12.0f }
};

// 7x7 用 Prewitt Horizontal カーネル
static const float kPrewittHorizontalKernel7x7[7][7] =
{
    { -1.0f / 24.0f, -1.0f / 18.0f, -1.0f / 12.0f, 0.0f, 1.0f / 12.0f, 1.0f / 18.0f, 1.0f / 24.0f },
    { -1.0f / 24.0f, -1.0f / 18.0f, -1.0f / 12.0f, 0.0f, 1.0f / 12.0f, 1.0f / 18.0f, 1.0f / 24.0f },
    { -1.0f / 24.0f, -1.0f / 18.0f, -1.0f / 12.0f, 0.0f, 1.0f / 12.0f, 1.0f / 18.0f, 1.0f / 24.0f },
    { -1.0f / 24.0f, -1.0f / 18.0f, -1.0f / 12.0f, 0.0f, 1.0f / 12.0f, 1.0f / 18.0f, 1.0f / 24.0f },
    { -1.0f / 24.0f, -1.0f / 18.0f, -1.0f / 12.0f, 0.0f, 1.0f / 12.0f, 1.0f / 18.0f, 1.0f / 24.0f },
    { -1.0f / 24.0f, -1.0f / 18.0f, -1.0f / 12.0f, 0.0f, 1.0f / 12.0f, 1.0f / 18.0f, 1.0f / 24.0f },
    { -1.0f / 24.0f, -1.0f / 18.0f, -1.0f / 12.0f, 0.0f, 1.0f / 12.0f, 1.0f / 18.0f, 1.0f / 24.0f }
};

// 7x7 用 Prewitt Vertical カーネル
static const float kPrewittVerticalKernel7x7[7][7] =
{
    { -1.0f / 24.0f, -1.0f / 24.0f, -1.0f / 24.0f, -1.0f / 24.0f, -1.0f / 24.0f, -1.0f / 24.0f, -1.0f / 24.0f },
    { -1.0f / 18.0f, -1.0f / 18.0f, -1.0f / 18.0f, -1.0f / 18.0f, -1.0f / 18.0f, -1.0f / 18.0f, -1.0f / 18.0f },
    { -1.0f / 12.0f, -1.0f / 12.0f, -1.0f / 12.0f, -1.0f / 12.0f, -1.0f / 12.0f, -1.0f / 12.0f, -1.0f / 12.0f },
    { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f },
    { 1.0f / 12.0f, 1.0f / 12.0f, 1.0f / 12.0f, 1.0f / 12.0f, 1.0f / 12.0f, 1.0f / 12.0f, 1.0f / 12.0f },
    { 1.0f / 18.0f, 1.0f / 18.0f, 1.0f / 18.0f, 1.0f / 18.0f, 1.0f / 18.0f, 1.0f / 18.0f, 1.0f / 18.0f },
    { 1.0f / 24.0f, 1.0f / 24.0f, 1.0f / 24.0f, 1.0f / 24.0f, 1.0f / 24.0f, 1.0f / 24.0f, 1.0f / 24.0f }
};


float Luminance(float3 v)
{
    return dot(v, float3(0.2125f, 0.7154f, 0.0721f));
}

struct Material
{
    float4x4 projectionInverse;
    int kernelSize;
};

ConstantBuffer<Material> gMaterial : register(b0);
Texture2D<float4> gTexture : register(t0);
Texture2D<float> gDepthTexture : register(t1);
SamplerState gSampler : register(s0);
SamplerState gSamplerPoint : register(s1);

struct PixelShaderOutput
{
    float4 color : SV_TARGET0;
};

PixelShaderOutput main(VertexShaderOutput input)
{
    uint width, height;
    gTexture.GetDimensions(width, height);
    float2 uvStepSize = float2(1.0f / width, 1.0f / height);
    float2 difference = float2(0.0f, 0.0f);
    if (gMaterial.kernelSize == 3)
    {
        for (int x = 0; x < 3; ++x)
        {
            for (int y = 0; y < 3; ++y)
            {
                float2 texcoord = input.texcoord + kIndex3x3[x][y] * uvStepSize;
                float ndcDepth = gDepthTexture.Sample(gSamplerPoint, texcoord);
                float4 viewSpace = mul(float4(0.0f, 0.0f, ndcDepth, 1.0f), gMaterial.projectionInverse);
                float viewZ = viewSpace.z * rcp(viewSpace.w);
                difference.x += viewZ * kPrewittHorizontalKernel3x3[x][y];
                difference.y += viewZ * kPrewittVerticalKernel3x3[x][y];
            }
        }
    }
    else if (gMaterial.kernelSize == 5)
    {
        for (int x = 0; x < 5; ++x)
        {
            for (int y = 0; y < 5; ++y)
            {
                float2 texcoord = input.texcoord + kIndex5x5[x][y] * uvStepSize;
                float ndcDepth = gDepthTexture.Sample(gSamplerPoint, texcoord);
                float4 viewSpace = mul(float4(0.0f, 0.0f, ndcDepth, 1.0f), gMaterial.projectionInverse);
                float viewZ = viewSpace.z * rcp(viewSpace.w);
                difference.x += viewZ * kPrewittHorizontalKernel5x5[x][y];
                difference.y += viewZ * kPrewittVerticalKernel5x5[x][y];
            }
        }
    }
    else if (gMaterial.kernelSize == 7)
    {
        for (int x = 0; x < 7; ++x)
        {
            for (int y = 0; y < 7; ++y)
            {
                float2 texcoord = input.texcoord + kIndex7x7[x][y] * uvStepSize;
                float ndcDepth = gDepthTexture.Sample(gSamplerPoint, texcoord);
                float4 viewSpace = mul(float4(0.0f, 0.0f, ndcDepth, 1.0f), gMaterial.projectionInverse);
                float viewZ = viewSpace.z * rcp(viewSpace.w);
                difference.x += viewZ * kPrewittHorizontalKernel7x7[x][y];
                difference.y += viewZ * kPrewittVerticalKernel7x7[x][y];
            }
        }
    }
    
    float weight = length(difference);
    weight = saturate(weight / 6.0f);
    
    PixelShaderOutput output;
    output.color.rgb = (1.0f - weight) * gTexture.Sample(gSampler, input.texcoord).rgb;
    output.color.a = 1.0f;
    
    return output;
}

