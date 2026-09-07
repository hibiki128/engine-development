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

    // gBlockSize はピクセル単位なので、まずUV空間の大きさへ直す。
    // （UV空間の値として扱うと 1.0 を超えた時点で画面全体が1ブロックになり、
    //   単色で塗り潰されたようにしか見えなくなる）
    uint width, height;
    gTexture.GetDimensions(width, height);
    float2 blockSizeUV = max(gBlockSize, 1.0f) / float2(width, height);

    float2 center = float2(gCenterX, gCenterY);

    // 中心を基準にしてブロック座標を計算
    float2 relativeCoord = input.texcoord - center;
    float2 blockCoord = floor(relativeCoord / blockSizeUV);
    float2 blockCenter = center + (blockCoord + 0.5) * blockSizeUV;

    output.color = gTexture.Sample(gSampler, blockCenter);

    return output;
}
