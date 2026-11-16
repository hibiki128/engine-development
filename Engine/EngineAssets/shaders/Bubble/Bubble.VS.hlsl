#include "Bubble.hlsli"

// 画面の大きさ
struct BubbleConstants
{
    float2 screenSize;
    float2 padding;
};

struct VertexShaderInput
{
    float2 position : POSITION0;
    float2 texcoord : TEXCOORD0;
    float4 color : COLOR0;
};

ConstantBuffer<BubbleConstants> gConstants : register(b0);

VertexShaderOutput main(VertexShaderInput input)
{
    VertexShaderOutput output;
    // 画面上の px から正規化デバイス座標へ変換
    float2 ndc = input.position / gConstants.screenSize * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f);
    output.position = float4(ndc, 0.0f, 1.0f);
    output.texcoord = input.texcoord;
    output.color = input.color;
    return output;
}
