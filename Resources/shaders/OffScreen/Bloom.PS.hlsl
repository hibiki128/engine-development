#include "FullScreen.hlsli"

cbuffer BloomParams : register(b0)
{
    float bloomThreshold;
    float bloomIntensity;
    float2 texelSize;
};

Texture2D<float4> gTexture : register(t0);
SamplerState gSampler : register(s0);

// 輝度計算（BT.709）
float Luminance(float3 color)
{
    return dot(color, float3(0.2126f, 0.7152f, 0.0722f));
}

static const float kGaussKernel[5] =
{
    0.0625f, 0.25f, 0.375f, 0.25f, 0.0625f
};

float3 SampleBright(float2 uv)
{
    float3 color = gTexture.Sample(gSampler, uv).rgb;
    float lum = Luminance(color);
    return (lum > bloomThreshold) ? color : float3(0.0f, 0.0f, 0.0f);
}

float4 main(VertexShaderOutput input) : SV_TARGET
{
    float2 uv = input.texcoord;
    
    float3 sceneColor = gTexture.Sample(gSampler, uv).rgb;
    
    float3 bloom = float3(0.0f, 0.0f, 0.0f);

    [unroll]
    for (int y = -2; y <= 2; ++y)
    {
        [unroll]
        for (int x = -2; x <= 2; ++x)
        {
            float2 offset = float2((float) x, (float) y) * texelSize;
            float3 bright = SampleBright(uv + offset);
            bloom += bright * kGaussKernel[x + 2] * kGaussKernel[y + 2];
        }
    }
    
    float3 finalColor = sceneColor + bloom * bloomIntensity;
    
    return float4(finalColor, 1.0f);
}
