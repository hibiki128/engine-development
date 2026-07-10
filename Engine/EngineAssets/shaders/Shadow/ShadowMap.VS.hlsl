struct TransformationMatrix
{
    float4x4 WVP;
    float4x4 World;
    float4x4 WorldInverseTranspose;
    float4x4 LightWVP;
};

struct VertexInput
{
    float4 position : POSITION0;
    float2 texcoord : TEXCOORD0;
    float3 normal   : NORMAL0;
};

ConstantBuffer<TransformationMatrix> gTransformationMatrix : register(b0);

float4 main(VertexInput input) : SV_POSITION
{
    return mul(input.position, gTransformationMatrix.LightWVP);
}
