#include"FullScreen.hlsli"

Texture2D<float4> gTexture : register(t0);
SamplerState gSampler : register(s0);

struct PixelShaderOutput
{
    float4 color : SV_TARGET0;
};

cbuffer RadialBlurSetting : register(b0)
{
    float2 kCenter;
    float kBlurWidth;
}

PixelShaderOutput main(VertexShaderOutput input)
{
    const int kNumSamples = 10;
    float2 direction = input.texcoord - kCenter;
    float3 outputColor = float3(0.0f, 0.0f, 0.0f);
    for (int sampleIndex = 0; sampleIndex < kNumSamples; ++sampleIndex)
    {
        float2 texcoord = input.texcoord + direction * kBlurWidth * float(sampleIndex);
        outputColor.rgb += gTexture.Sample(gSampler, texcoord).rgb;
    }
    outputColor.rgb *= rcp(kNumSamples);
    
    PixelShaderOutput output;
    output.color.rgb = outputColor;
    output.color.a = 1.0f;
    return output;
}
