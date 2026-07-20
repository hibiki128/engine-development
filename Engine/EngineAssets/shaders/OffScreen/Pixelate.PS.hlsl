#include"FullScreen.hlsli"

Texture2D<float4> gTexture : register(t0);
SamplerState gSampler : register(s0);

cbuffer PixelatedBuffer : register(b0)
{
    float gBlockSize;
    float gCenterX;
    float gCenterY;
};

struct PixelShaderOutput
{
    float4 color : SV_TARGET0;
};

PixelShaderOutput main(VertexShaderOutput input)
{
    PixelShaderOutput output;
    
    float2 center = float2(gCenterX, gCenterY);
    
    // 中心を基準にしてブロック座標を計算
    float2 relativeCoord = input.texcoord - center;
    float2 blockCoord = floor(relativeCoord / gBlockSize);
    float2 blockCenter = center + (blockCoord + 0.5) * gBlockSize;
    
    output.color = gTexture.Sample(gSampler, blockCenter);
    
    return output;
}