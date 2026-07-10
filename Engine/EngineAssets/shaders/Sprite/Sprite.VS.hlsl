#include"Sprite.hlsli"

struct TransformationMatrix
{
    float4x4 WVP;
    float4x4 World;
};

struct VertexShaderInput
{
    float4 position : POSITION0;
    float2 texcoord : TEXCOORD0;
    uint instanceID : SV_InstanceID;
};

StructuredBuffer<TransformationMatrix> gTransformationMatrices : register(t0);

VertexShaderOutput main(VertexShaderInput input)
{
    VertexShaderOutput output;
    output.position = mul(input.position, gTransformationMatrices[input.instanceID].WVP);
    output.texcoord = input.texcoord;
    return output;
}