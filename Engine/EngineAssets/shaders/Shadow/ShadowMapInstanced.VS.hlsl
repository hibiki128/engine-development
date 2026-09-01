// =============================================
// シャドウマップ描画のインスタンシング版
//   Object3dInstanced.VS と同じインスタンスバッファ（同じ構造体）を読み、
//   LightWVP だけを使って深度を書く。
//   ルートシグネチャは通常のシャドウマップ用と共有（末尾に足したルートSRV t0 を使う）。
// =============================================

struct ObjectInstanceData
{
    float4x4 WVP;
    float4x4 World;
    float4x4 WorldInverseTranspose;
    float4x4 LightWVP;
    float4 color;
};

struct VertexInput
{
    float4 position : POSITION0;
    float2 texcoord : TEXCOORD0;
    float3 normal   : NORMAL0;
};

StructuredBuffer<ObjectInstanceData> gInstances : register(t0);

float4 main(VertexInput input, uint instanceId : SV_InstanceID) : SV_POSITION
{
    return mul(input.position, gInstances[instanceId].LightWVP);
}
