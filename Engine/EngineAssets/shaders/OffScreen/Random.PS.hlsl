#include"FullScreen.hlsli"

struct Time
{
    float time;
};

Texture2D<float4> gTexture : register(t0);
SamplerState gSampler : register(s0);
ConstantBuffer<Time> gTime : register(b0);

struct PixelShaderOutput
{
    float4 color : SV_TARGET0;
};

float rand2dTo1d(float2 uv)
{
    // 大きめの定数を使って混ぜる
    float2 k = float2(12.9898, 78.233);

    // dotしてsinしてfractでランダムっぽくする
    float f = dot(uv, k);
    return frac(sin(f) * 43758.5453);
}

PixelShaderOutput main(VertexShaderOutput input)
{
    PixelShaderOutput output;
    float random = rand2dTo1d(input.texcoord * gTime.time);
    output.color = gTexture.Sample(gSampler, input.texcoord) * float4(random, random, random, 1.0f);
    return output;
}
