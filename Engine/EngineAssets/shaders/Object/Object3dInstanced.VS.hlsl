#include "object3d.hlsli"

// =============================================
// Object3d インスタンシング描画用の頂点シェーダ
//
//   同じモデル（同じ頂点/インデックスバッファ）を参照するオブジェクトを1回の
//   DrawIndexedInstanced でまとめて描くためのバリアント。
//   1オブジェクト＝1定数バッファだった変換行列を StructuredBuffer に置き換え、
//   SV_InstanceID で自分のぶんを引く。
//
//   ※ ルートシグネチャは通常描画(Standard)と共有し、末尾に足したルートSRV(t4)だけを使う。
//     b0(変換行列CB) は読まないので、バインドされていてもされていなくてもよい。
//   ※ ピクセルシェーダは通常描画と同じものを使う。個体ごとの色は instanceColor で渡す
//     （マテリアルCBの色は白にしておくこと。詳細は Object3dInstancing.cpp）。
// =============================================

struct ObjectInstanceData
{
    float4x4 WVP;
    float4x4 World;
    float4x4 WorldInverseTranspose;
    float4x4 LightWVP;
    float4 color; // 個体ごとの色（マテリアル色に乗算される）
};

struct VertexShaderInput
{
    float4 position : POSITION0;
    float2 texcoord : TEXCOORD0;
    float3 normal : NORMAL0;
};

StructuredBuffer<ObjectInstanceData> gInstances : register(t4);

VertexShaderOutput main(VertexShaderInput input, uint instanceId : SV_InstanceID)
{
    VertexShaderOutput output;
    ObjectInstanceData instance = gInstances[instanceId];

    output.position = mul(input.position, instance.WVP);
    output.texcoord = input.texcoord;
    output.normal = normalize(mul(input.normal, (float3x3) instance.WorldInverseTranspose));
    output.worldPosition = mul(input.position, instance.World).xyz;
    output.shadowCoord = mul(input.position, instance.LightWVP);
    output.instanceColor = instance.color;
    return output;
}
